/* The upload server (see upload.h). Requests are handled one at a time on one thread: PUT
 * /upload/<path> stores a file (written as <path>.upload, renamed when complete), POST /done marks
 * the end of a folder, GET /status returns the console's notes for the page, GET / the page. */
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "upload.h"
#include "upload_page.h"

#define HEAD_LIMIT 8192
#define CHUNK (256 * 1024)
#define MAX_ZIPS 4
#define MAX_NOTES 32
#define NOTE_TEXT 160
#define RECEIVE_TIMEOUT_S 1
#define SEND_TIMEOUT_S 5
#define IDLE_LIMIT_S 30
#define SOCKET_BUFFER (1024 * 1024)
#define MAX_WAITING 8
#define WAITING_LIMIT_S 15
#define POLL_MS 250

int sceKernelUsleep(unsigned microseconds);

/* The game folder's own folders: only files below them are stored. */
static const char *const game_folders[] = { "Data", "ObjData", "Scenarios", "Tracks", "Landscapes" };

typedef struct
{
    int id, ok;
    char text[NOTE_TEXT];
} note_t;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t server;
static int listener = -1, running, port;
static volatile int stopping;
static char folder[UPLOAD_PATH];
static char address[64];
static upload_progress_t progress;
static char zips[MAX_ZIPS][UPLOAD_PATH];
static int zip_count;
static note_t notes[MAX_NOTES];
static int note_count, next_note = 1;

/* Only the server thread touches these. */
static char head[HEAD_LIMIT + 1];
static uint8_t chunk[CHUNK];

static int send_all(int fd, const void *data, size_t size)
{
    const uint8_t *at = data;
    while (size > 0)
    {
        ssize_t sent = send(fd, at, size, 0);
        if (sent <= 0)
            return -1;
        at += sent;
        size -= (size_t)sent;
    }
    return 0;
}

static void respond(int fd, const char *status, const char *type, const void *body, size_t length)
{
    char top[256];
    int n = snprintf(top, sizeof(top),
                     "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %lu\r\n"
                     "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
                     status, type, (unsigned long)length);
    if (send_all(fd, top, (size_t)n) == 0 && length > 0)
        send_all(fd, body, length);
}

static void respond_text(int fd, const char *status, const char *text)
{
    respond(fd, status, "text/plain; charset=utf-8", text, strlen(text));
}

/* recv that gives up when the server stops or the browser stays quiet for too long. */
static ssize_t receive(int fd, void *buffer, size_t size)
{
    for (int idle = 0;;)
    {
        ssize_t got = recv(fd, buffer, size, 0);
        if (got >= 0)
            return got;
        if ((errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) || stopping ||
            ++idle >= IDLE_LIMIT_S / RECEIVE_TIMEOUT_S)
            return -1;
    }
}

/* Reads the request head into head[]; returns its length including the blank line, with the
 * number of bytes read so far (head plus the start of the body) in *used. */
static int read_head(int fd, int *used)
{
    int length = 0;
    while (length < HEAD_LIMIT)
    {
        ssize_t got = receive(fd, head + length, (size_t)(HEAD_LIMIT - length));
        if (got <= 0)
            return -1;
        length += (int)got;
        head[length] = '\0';
        char *end = strstr(head, "\r\n\r\n");
        if (end != NULL)
        {
            *used = length;
            return (int)(end - head) + 4;
        }
    }
    return -1;
}

static const char *header_value(const char *name)
{
    size_t length = strlen(name);
    for (const char *line = strstr(head, "\r\n"); line != NULL && line[2] != '\r'; line = strstr(line, "\r\n"))
    {
        line += 2;
        if (strncasecmp(line, name, length) == 0 && line[length] == ':')
        {
            const char *value = line + length + 1;
            while (*value == ' ' || *value == '\t')
                value++;
            return value;
        }
    }
    return NULL;
}

static int hex(int c)
{
    return isdigit(c) ? c - '0' : isxdigit(c) ? tolower(c) - 'a' + 10 : -1;
}

/* Decodes %XX escapes; the request target carries the path URL-encoded. */
static int url_decode(char *out, size_t size, const char *in)
{
    size_t n = 0;
    for (; *in != '\0' && *in != '?'; in++)
    {
        int c = (unsigned char)*in;
        if (c == '%' && hex(in[1]) >= 0 && hex(in[2]) >= 0)
        {
            c = hex(in[1]) * 16 + hex(in[2]);
            in += 2;
        }
        if (n + 1 >= size || c == 0)
            return -1;
        out[n++] = (char)c;
    }
    out[n] = '\0';
    return 0;
}

static int plain_name(const char *name, size_t length)
{
    if (length == 0 || length > 128 || (length <= 2 && name[0] == '.' && (length == 1 || name[1] == '.')))
        return 0;
    for (size_t i = 0; i < length; i++)
    {
        unsigned char c = (unsigned char)name[i];
        if (c < ' ' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            return 0;
    }
    return 1;
}

int upload_game_path(char *out, size_t size, const char *path)
{
    const char *start = NULL;
    const char *game_folder = NULL;

    /* Find the first part that is one of the game's folders, wherever the sender's folder was. */
    for (const char *part = path; *part != '\0' && start == NULL;)
    {
        size_t length = strcspn(part, "/");
        for (size_t i = 0; i < sizeof(game_folders) / sizeof(game_folders[0]); i++)
        {
            if (strlen(game_folders[i]) == length && strncasecmp(part, game_folders[i], length) == 0)
            {
                start = part + length;
                game_folder = game_folders[i];
                break;
            }
        }
        part += length;
        while (*part == '/')
            part++;
    }
    if (start == NULL || *start != '/')
        return -1;

    int n = snprintf(out, size, "%s", game_folder);
    for (const char *part = start; *part != '\0';)
    {
        while (*part == '/')
            part++;
        size_t length = strcspn(part, "/");
        if (length == 0)
            break;
        if (!plain_name(part, length) || (size_t)n + 1 + length >= size)
            return -1;
        out[n++] = '/';
        memcpy(out + n, part, length);
        n += (int)length;
        out[n] = '\0';
        part += length;
    }
    return strchr(out, '/') != NULL ? 0 : -1;
}

static int write_all(int fd, const uint8_t *data, size_t size)
{
    while (size > 0)
    {
        ssize_t written = write(fd, data, size);
        if (written <= 0)
            return -1;
        data += written;
        size -= (size_t)written;
    }
    return 0;
}

/* Creates the folders of path below the game folder. */
static void make_folders(const char *path)
{
    char built[UPLOAD_PATH * 2];
    int n = snprintf(built, sizeof(built), "%s", folder);
    for (const char *at = path; (at = strchr(at, '/')) != NULL; at++)
    {
        snprintf(built + n, sizeof(built) - (size_t)n, "/%.*s", (int)(at - path), path);
        mkdir(built, 0755);
    }
}

static void set_receiving(int receiving, const char *name, long long done, long long total)
{
    pthread_mutex_lock(&lock);
    progress.receiving = receiving;
    snprintf(progress.name, sizeof(progress.name), "%s", name);
    progress.done = done;
    progress.total = total;
    pthread_mutex_unlock(&lock);
}

static void put_file(int fd, const char *target, int head_length, int used)
{
    char sent_path[UPLOAD_PATH], path[UPLOAD_PATH], part[UPLOAD_PATH * 2 + 16], final[UPLOAD_PATH * 2];
    const char *value = header_value("Content-Length");
    long long length = value != NULL ? strtoll(value, NULL, 10) : -1, done = 0;
    int out, lost = 0, error = 0, zip = 0;

    if (url_decode(sent_path, sizeof(sent_path), target) != 0)
    {
        respond_text(fd, "400 Bad Request", "The file name cannot be read.");
        return;
    }
    const char *dot = strrchr(sent_path, '.');
    if (upload_game_path(path, sizeof(path), sent_path) != 0)
    {
        if (dot == NULL || strcasecmp(dot, ".zip") != 0 || strchr(sent_path, '/') != NULL)
        {
            respond_text(fd, "403 Forbidden", "Not part of the game folder: left out.");
            return;
        }
        zip = 1;
        snprintf(path, sizeof(path), "import-%d.zip", progress.files + zip_count);
    }
    if (length < 0)
    {
        respond_text(fd, "411 Length Required", "The upload has no length.");
        return;
    }
    pthread_mutex_lock(&lock);
    int full = zip && zip_count == MAX_ZIPS;
    pthread_mutex_unlock(&lock);
    if (full)
    {
        respond_text(fd, "503 Service Unavailable", "The console is still unpacking earlier ZIP files.");
        return;
    }

    make_folders(path);
    snprintf(final, sizeof(final), "%s/%s", folder, path);
    snprintf(part, sizeof(part), "%s.upload", final);
    if ((out = open(part, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0)
    {
        printf("upload: cannot write %s (error %d)\n", part, errno);
        respond_text(fd, "500 Internal Server Error", "The console cannot write the file.");
        return;
    }
    set_receiving(1, path, 0, length);

    if (used > head_length)
    {
        long long extra = used - head_length < length ? used - head_length : length;
        if (write_all(out, (const uint8_t *)head + head_length, (size_t)extra) != 0)
            error = errno != 0 ? errno : EIO;
        done = extra;
    }
    /* Whole chunks are written: many small writes to the title folder are slow. */
    while (!error && !lost && done < length)
    {
        size_t wanted = length - done < CHUNK ? (size_t)(length - done) : CHUNK, filled = 0;
        while (filled < wanted)
        {
            ssize_t got = receive(fd, chunk + filled, wanted - filled);
            if (got <= 0)
            {
                lost = 1;
                break;
            }
            filled += (size_t)got;
        }
        if (write_all(out, chunk, filled) != 0)
            error = errno != 0 ? errno : EIO;
        done += (long long)filled;
        set_receiving(1, path, done, length);
    }
    close(out);
    set_receiving(0, "", 0, 0);

    if (lost || error || rename(part, final) != 0)
    {
        unlink(part);
        printf("upload: %s stopped at %lld of %lld bytes (error %d)\n", path, done, length, error);
        if (error == ENOSPC)
            respond_text(fd, "507 Insufficient Storage", "Not enough space on the console.");
        else
            respond_text(fd, "500 Internal Server Error", "Writing on the console failed.");
        return;
    }
    pthread_mutex_lock(&lock);
    if (zip)
        snprintf(zips[zip_count++], UPLOAD_PATH, "%s", final);
    else
    {
        progress.files++;
        progress.bytes += length;
    }
    pthread_mutex_unlock(&lock);
    respond_text(fd, "200 OK", zip ? "Received; the console unpacks it." : "Received.");
}

static void send_status(int fd, const char *query)
{
    static char json[MAX_NOTES * (NOTE_TEXT + 40) + 64];
    const char *after_text = strstr(query, "after=");
    int after = after_text != NULL ? atoi(after_text + 6) : 0, first = 1;

    pthread_mutex_lock(&lock);
    int n = snprintf(json, sizeof(json), "{\"files\":%d,\"notes\":[", progress.files);
    for (int i = 0; i < note_count; i++)
    {
        if (notes[i].id > after)
        {
            n += snprintf(json + n, sizeof(json) - (size_t)n, "%s{\"id\":%d,\"ok\":%s,\"text\":\"%s\"}",
                          first ? "" : ",", notes[i].id, notes[i].ok ? "true" : "false", notes[i].text);
            first = 0;
        }
    }
    pthread_mutex_unlock(&lock);
    n += snprintf(json + n, sizeof(json) - (size_t)n, "]}");
    respond(fd, "200 OK", "application/json", json, (size_t)n);
}

static void handle(int fd)
{
    char method[8], target[UPLOAD_PATH * 3 + 16];
    int used, head_length = read_head(fd, &used);
    size_t i = 0, j = 0;

    if (head_length < 0)
        return;
    for (; head[i] != '\0' && head[i] != ' ' && i < sizeof(method) - 1; i++)
        method[i] = head[i];
    method[i] = '\0';
    while (head[i] == ' ')
        i++;
    while (head[i] != '\0' && head[i] != ' ' && head[i] != '\r' && j < sizeof(target) - 1)
        target[j++] = head[i++];
    target[j] = '\0';

    if (strcmp(method, "GET") == 0 && (strcmp(target, "/") == 0 || strcmp(target, "/index.html") == 0))
        respond(fd, "200 OK", "text/html; charset=utf-8", upload_page, sizeof(upload_page));
    else if (strcmp(method, "GET") == 0 && strncmp(target, "/status", 7) == 0)
        send_status(fd, target + 7);
    else if (strcmp(method, "PUT") == 0 && strncmp(target, "/upload/", 8) == 0)
        put_file(fd, target + 8, head_length, used);
    else if (strcmp(method, "POST") == 0 && strcmp(target, "/done") == 0)
    {
        pthread_mutex_lock(&lock);
        progress.batches++;
        pthread_mutex_unlock(&lock);
        respond_text(fd, "200 OK", "Done.");
    }
    else
        respond_text(fd, "404 Not Found", "Not found.");
}

/* Browsers open connections before they need them and may leave them quiet, so the server keeps
 * several waiting and serves whichever sends a request first, one request at a time; waiting for
 * the next request on a single connection blocked everyone else for up to IDLE_LIMIT_S. */
static void *serve(void *argument)
{
    struct timeval receive_timeout = { RECEIVE_TIMEOUT_S, 0 }, send_timeout = { SEND_TIMEOUT_S, 0 };
    int yes = 1, waiting[MAX_WAITING], count = 0;
    time_t since[MAX_WAITING];

    (void)argument;
    while (!stopping)
    {
        struct pollfd fds[1 + MAX_WAITING];
        fds[0].fd = listener;
        fds[0].events = POLLIN;
        for (int i = 0; i < count; i++)
        {
            fds[1 + i].fd = waiting[i];
            fds[1 + i].events = POLLIN;
        }
        if (poll(fds, (nfds_t)(1 + count), POLL_MS) < 0)
        {
            sceKernelUsleep(100000);
            continue;
        }

        /* Connections that sent something (or closed) are served and closed, quiet ones dropped
         * after WAITING_LIMIT_S. */
        const time_t now = time(NULL);
        int kept = 0;
        for (int i = 0; i < count; i++)
        {
            const int ready = fds[1 + i].revents != 0;
            if (ready && !stopping)
                handle(waiting[i]);
            if (ready || stopping || now - since[i] > WAITING_LIMIT_S)
            {
                close(waiting[i]);
                continue;
            }
            waiting[kept] = waiting[i];
            since[kept++] = since[i];
        }
        count = kept;

        if ((fds[0].revents & POLLIN) && !stopping)
        {
            int client = accept(listener, NULL, NULL);
            if (client < 0)
                continue;
            if (count == MAX_WAITING)
            {
                close(waiting[0]);
                memmove(waiting, waiting + 1, (size_t)--count * sizeof(waiting[0]));
                memmove(since, since + 1, (size_t)count * sizeof(since[0]));
            }
            setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &receive_timeout, sizeof(receive_timeout));
            setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &send_timeout, sizeof(send_timeout));
            /* A reply is two sends (head, body). With Nagle the body waits until the sender
             * acknowledges the head, which Windows delays by up to 200 ms per request. */
            setsockopt(client, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
            waiting[count] = client;
            since[count++] = now;
        }
    }
    for (int i = 0; i < count; i++)
        close(waiting[i]);
    return NULL;
}

/* Connecting a UDP socket sends nothing; it only picks the route, and with it the local address. */
static void find_address(void)
{
    struct sockaddr_in remote, local;
    socklen_t size = sizeof(local);
    int fd = socket(AF_INET, SOCK_DGRAM, 0);

    address[0] = '\0';
    if (fd < 0)
        return;
    memset(&remote, 0, sizeof(remote));
    remote.sin_family = AF_INET;
    remote.sin_port = htons(53);
    remote.sin_addr.s_addr = htonl(0x08080808);
    if (connect(fd, (struct sockaddr *)&remote, sizeof(remote)) == 0 &&
        getsockname(fd, (struct sockaddr *)&local, &size) == 0)
    {
        uint32_t ip = ntohl(local.sin_addr.s_addr);
        if (ip != 0)
            snprintf(address, sizeof(address), "http://%u.%u.%u.%u:%d/", ip >> 24, (ip >> 16) & 255,
                     (ip >> 8) & 255, ip & 255, port);
    }
    close(fd);
}

/* Removes partial files a crash or a power loss left behind. */
static void remove_leftovers(const char *path)
{
    DIR *dir = opendir(path);
    if (dir == NULL)
        return;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        char child[UPLOAD_PATH * 2];
        size_t length = strlen(entry->d_name);
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
        if (entry->d_type == DT_DIR)
            remove_leftovers(child);
        else if (length > 7 && strcmp(entry->d_name + length - 7, ".upload") == 0)
            unlink(child);
    }
    closedir(dir);
}

static int listen_on(int candidate)
{
    struct sockaddr_in local;
    int fd = socket(AF_INET, SOCK_STREAM, 0), yes = 1, buffer = SOCKET_BUFFER;

    if (fd < 0)
        return -1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    /* Accepted connections inherit it: a large window keeps a fast sender going. */
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &buffer, sizeof(buffer));
    memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_port = htons((uint16_t)candidate);
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, (struct sockaddr *)&local, sizeof(local)) != 0 || listen(fd, 4) != 0)
    {
        printf("upload: port %d refused (error %d)\n", candidate, errno);
        close(fd);
        return -1;
    }
    return fd;
}

int upload_start(const char *game_folder)
{
    if (running)
        return 0;
    snprintf(folder, sizeof(folder), "%s", game_folder);
    mkdir(folder, 0755);
    remove_leftovers(folder);
    memset(&progress, 0, sizeof(progress));
    zip_count = 0;
    note_count = 0;

    for (port = UPLOAD_PORT; port < UPLOAD_PORT + UPLOAD_PORT_TRIES; port++)
        if ((listener = listen_on(port)) >= 0)
            break;
    if (listener < 0)
        return -1;
    find_address();
    stopping = 0;
    if (pthread_create(&server, NULL, serve, NULL) != 0)
    {
        printf("upload: no server thread\n");
        close(listener);
        listener = -1;
        return -1;
    }
    running = 1;
    printf("upload: serving %s into %s\n", address[0] != '\0' ? address : "without an address", folder);
    return 0;
}

void upload_stop(void)
{
    if (!running)
        return;
    stopping = 1;
    /* The server thread sees it within POLL_MS. */
    pthread_join(server, NULL);
    close(listener);
    listener = -1;
    running = 0;
    printf("upload: stopped\n");
}

const char *upload_address(void)
{
    return address;
}

void upload_progress(upload_progress_t *out)
{
    pthread_mutex_lock(&lock);
    *out = progress;
    pthread_mutex_unlock(&lock);
}

int upload_take_zip(char *path, size_t size)
{
    int taken = 0;
    pthread_mutex_lock(&lock);
    if (zip_count > 0)
    {
        snprintf(path, size, "%s", zips[0]);
        memmove(zips, zips + 1, (size_t)--zip_count * sizeof(zips[0]));
        taken = 1;
    }
    pthread_mutex_unlock(&lock);
    return taken;
}

void upload_note(const char *text, int ok)
{
    pthread_mutex_lock(&lock);
    if (note_count == MAX_NOTES)
        memmove(notes, notes + 1, (size_t)--note_count * sizeof(*notes));
    note_t *note = &notes[note_count++];
    note->id = next_note++;
    note->ok = ok;
    /* Kept safe to place inside a JSON string as it is. */
    int i = 0;
    for (; text[i] != '\0' && i < NOTE_TEXT - 1; i++)
        note->text[i] = text[i] == '"' || text[i] == '\\' ? '\'' : (unsigned char)text[i] < ' ' ? ' ' : text[i];
    note->text[i] = '\0';
    pthread_mutex_unlock(&lock);
}
