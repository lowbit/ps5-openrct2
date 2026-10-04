/* Console test harness. tools/console_test.py writes a plan to /app0/test.cfg and launches the
 * title. Plan lines:
 *   reset            delete the user folder first, for a first-run state
 *   capture N,N,...  frames to capture, counted from the first frame shown
 *   frames N         exit after N frames
 *   serve PORT       serve captures, and the log at the end, over TCP on PORT
 * The runner connects whenever it likes and gets what is queued: for each item a 32-bit name
 * length, the name, a 64-bit size and the data, then a zero name length. Frames are named
 * frame<N>-<width>x<height> and hold raw RGBA rows. */
#include <dirent.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ps5.h"

#define PLAN_PATH "/app0/test.cfg"
#define USER_PATH "/download0/user"
#define MAX_CAPTURES 64
#define MAX_QUEUED 16

int sceKernelUsleep(unsigned microseconds);

extern void (*PS5_FrameCallback)(const void *pixels, int width, int height, int pitch);

typedef struct
{
    char name[48];
    void *data;
    size_t size;
} item_t;

static int captures[MAX_CAPTURES];
static int capture_count;
static int frame_limit;
static int frame;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static item_t queue[MAX_QUEUED];
static int queued;
static int listener = -1;

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

static int send_item(int fd, const item_t *item)
{
    uint32_t name_length = (uint32_t)strlen(item->name);
    uint64_t size = item->size;
    return send_all(fd, &name_length, sizeof(name_length)) || send_all(fd, item->name, name_length) ||
           send_all(fd, &size, sizeof(size)) || send_all(fd, item->data, item->size);
}

static void *serve(void *argument)
{
    (void)argument;
    for (;;)
    {
        int client = accept(listener, NULL, NULL);
        if (client < 0)
            continue;
        pthread_mutex_lock(&lock);
        while (queued > 0 && send_item(client, &queue[0]) == 0)
        {
            free(queue[0].data);
            memmove(queue, queue + 1, (size_t)--queued * sizeof(*queue));
        }
        pthread_mutex_unlock(&lock);
        uint32_t end = 0;
        send_all(client, &end, sizeof(end));
        close(client);
    }
    return NULL;
}

static void start_server(int port)
{
    struct sockaddr_in address;
    pthread_t thread;
    int yes = 1;

    listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0)
    {
        printf("test: no socket\n");
        return;
    }
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons((uint16_t)port);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(listener, (struct sockaddr *)&address, sizeof(address)) != 0 || listen(listener, 1) != 0 ||
        pthread_create(&thread, NULL, serve, NULL) != 0)
    {
        printf("test: cannot serve on port %d\n", port);
        close(listener);
        listener = -1;
        return;
    }
    printf("test: serving on port %d\n", port);
}

/* Queues a copy of data. Takes ownership of copy. */
static void enqueue(const char *name, void *copy, size_t size)
{
    pthread_mutex_lock(&lock);
    if (queued < MAX_QUEUED)
    {
        snprintf(queue[queued].name, sizeof(queue[queued].name), "%s", name);
        queue[queued].data = copy;
        queue[queued].size = size;
        queued++;
        copy = NULL;
    }
    pthread_mutex_unlock(&lock);
    free(copy);
}

/* Waits up to timeout_us for the runner to fetch everything queued. */
static void drain(unsigned timeout_us)
{
    for (unsigned waited = 0; listener >= 0 && waited < timeout_us; waited += 100000)
    {
        pthread_mutex_lock(&lock);
        int pending = queued;
        pthread_mutex_unlock(&lock);
        if (pending == 0)
            return;
        sceKernelUsleep(100000);
    }
}

static void remove_tree(const char *path)
{
    DIR *dir = opendir(path);
    if (dir != NULL)
    {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL)
        {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;
            char child[1024];
            snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
            if (entry->d_type == DT_DIR)
                remove_tree(child);
            else
                unlink(child);
        }
        closedir(dir);
    }
    rmdir(path);
}

void ps5_console_test_finish(void)
{
    if (listener < 0)
        return;
    const char *path = ps5_log_path();
    fflush(stdout);
    fflush(stderr);
    sceKernelUsleep(300000); /* let the log thread write what is in the pipe */
    FILE *file = path != NULL ? fopen(path, "rb") : NULL;
    if (file != NULL)
    {
        fseek(file, 0, SEEK_END);
        long size = ftell(file);
        fseek(file, 0, SEEK_SET);
        void *data = size > 0 ? malloc((size_t)size) : NULL;
        if (data != NULL && fread(data, 1, (size_t)size, file) == (size_t)size)
            enqueue("openrct2.log", data, (size_t)size);
        else
            free(data);
        fclose(file);
    }
    drain(15000000);
}

static int capturing(int number)
{
    for (int i = 0; i < capture_count; i++)
        if (captures[i] == number)
            return 1;
    return 0;
}

static void on_frame(const void *pixels, int width, int height, int pitch)
{
    frame++;
    if (capturing(frame) && listener >= 0)
    {
        size_t row = (size_t)width * 4;
        uint8_t *copy = malloc(row * (size_t)height);
        if (copy != NULL)
        {
            for (int y = 0; y < height; y++)
                memcpy(copy + row * (size_t)y, (const uint8_t *)pixels + (size_t)pitch * (size_t)y, row);
            char name[48];
            snprintf(name, sizeof(name), "frame%04d-%dx%d", frame, width, height);
            enqueue(name, copy, row * (size_t)height);
            printf("test: captured frame %d\n", frame);
        }
    }
    if (frame_limit > 0 && frame >= frame_limit)
    {
        printf("test: %d frames shown, exiting\n", frame);
        ps5_console_test_finish();
        _exit(0);
    }
}

void ps5_console_test_start(void)
{
    FILE *plan = fopen(PLAN_PATH, "r");
    if (plan == NULL)
        return;
    char line[512];
    int port = 0;
    while (fgets(line, sizeof(line), plan) != NULL)
    {
        line[strcspn(line, "\r\n")] = '\0';
        if (strcmp(line, "reset") == 0)
        {
            remove_tree(USER_PATH);
            printf("test: removed %s\n", USER_PATH);
        }
        else if (strncmp(line, "capture ", 8) == 0)
        {
            for (const char *at = line + 8; *at != '\0' && capture_count < MAX_CAPTURES;)
            {
                captures[capture_count++] = atoi(at);
                at = strchr(at, ',');
                if (at == NULL)
                    break;
                at++;
            }
        }
        else if (strncmp(line, "frames ", 7) == 0)
            frame_limit = atoi(line + 7);
        else if (strncmp(line, "serve ", 6) == 0)
            port = atoi(line + 6);
    }
    fclose(plan);
    printf("test: plan with %d captures, frame limit %d\n", capture_count, frame_limit);
    if (port > 0)
        start_server(port);
    PS5_FrameCallback = on_frame;
}
