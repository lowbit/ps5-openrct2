/* Console test harness. tools/console_test.py writes a plan to /app0/test.cfg and launches the
 * title. Plan lines:
 *   reset            delete the user folder first, for a first-run state
 *   capture N,N,...  frames to capture, counted from the first frame shown
 *   frames N         exit after N frames
 *   serve PORT       serve captures, and the log at the end, over TCP on PORT
 *   list PATH        log the folder's entries (the first 20) and their count
 *   stat PATH        log what stat says about PATH
 *   arg ARGUMENT     pass ARGUMENT to OpenRCT2's command line (e.g. --verbose)
 *   input SCRIPT     controller input (see pad_script.c), frames counted like capture's
 *   point N X Y      put the pointer at window pixel X, Y at frame N
 *   game PATH        use PATH as the game folder (an empty one shows the import screen)
 *   remove PATH      delete PATH first (only below /app0/ or /download0/)
 * The runner connects whenever it likes and gets what is queued: for each item a 32-bit name
 * length, the name, a 64-bit size and the data, then a zero name length. Frames are named
 * frame<N>-<width>x<height> and hold raw RGBA rows. */
#include <SDL.h>
#include <dirent.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "pad_script.h"
#include "ps5.h"
#include "ps5runtime.h"

#define PLAN_PATH "/app0/test.cfg"
#define USER_PATH "/download0/user"
#define MAX_CAPTURES 64
#define MAX_QUEUED 16
#define MAX_ARGUMENTS 8
#define MAX_POINTS 32

int sceKernelUsleep(unsigned microseconds);

extern void (*PS5_FrameCallback)(const void *pixels, int width, int height, int pitch);
extern void (*PS5_PadCallback)(ps5_pad_t *pad);

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
static char arguments[MAX_ARGUMENTS][128];
static int argument_count;
static char game_path[256];
static struct
{
    int frame, x, y;
} points[MAX_POINTS];
static int point_count;

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
    if (dir == NULL && unlink(path) == 0)
        return;
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

static void list_folder(const char *path)
{
    struct dirent **entries;
    int count = scandir(path, &entries, NULL, alphasort);
    if (count < 0)
    {
        printf("test: cannot list %s: errno %d\n", path, errno);
        return;
    }
    printf("test: %s has %d entries\n", path, count);
    for (int i = 0; i < count; i++)
    {
        if (i < 20)
            printf("test:   %s (type %d)\n", entries[i]->d_name, entries[i]->d_type);
        free(entries[i]);
    }
    free(entries);
}

void ps5_console_test_finish(void)
{
    if (listener < 0)
        return;
    const char *path = ps5_log_path();
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

/* Which renderer SDL made for the game's window, and how the window is set up. */
static void log_video_state(void)
{
    SDL_Window *window = SDL_GetWindowFromID(1);
    SDL_Renderer *renderer = window != NULL ? SDL_GetRenderer(window) : NULL;
    SDL_RendererInfo info;
    int width = 0, height = 0;
    if (window != NULL)
        SDL_GetWindowSize(window, &width, &height);
    if (renderer != NULL && SDL_GetRendererInfo(renderer, &info) == 0)
        printf("test: window %dx%d flags %#x, renderer %s flags %#x\n", width, height,
               window != NULL ? SDL_GetWindowFlags(window) : 0, info.name, info.flags);
    else
        printf("test: window %p %dx%d, no renderer (%s)\n", (void *)window, width, height, SDL_GetError());
}

static size_t lit_pixels(const uint8_t *pixels, int width, int height, int pitch)
{
    size_t lit = 0;
    for (int y = 0; y < height; y++)
    {
        const uint32_t *row = (const uint32_t *)(pixels + (size_t)pitch * (size_t)y);
        for (int x = 0; x < width; x++)
            lit += (row[x] & 0x00ffffff) != 0;
    }
    return lit;
}

static void on_frame(const void *pixels, int width, int height, int pitch)
{
    frame++;
    if (frame == 60)
        log_video_state();
    if (frame % 300 == 0)
    {
        static uint32_t last_ticks;
        uint32_t ticks = SDL_GetTicks();
        if (last_ticks != 0)
            printf("test: frame %d, %.1f frames per second\n", frame, 300000.0 / (ticks - last_ticks));
        last_ticks = ticks;
    }
    for (int i = 0; i < point_count; i++)
        if (points[i].frame == frame)
            SDL_WarpMouseInWindow(SDL_GetMouseFocus(), points[i].x, points[i].y);
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
            printf("test: captured frame %d (%zu pixels not black)\n", frame,
                   lit_pixels(pixels, width, height, pitch));
        }
    }
    if (frame_limit > 0 && frame >= frame_limit)
    {
        printf("test: %d frames shown, exiting\n", frame);
        ps5_console_test_finish();
        exit(0);
    }
}

static void on_pad(ps5_pad_t *pad)
{
    pad_script_apply(frame, pad);
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
        else if (strncmp(line, "list ", 5) == 0)
            list_folder(line + 5);
        else if (strncmp(line, "realpath ", 9) == 0)
        {
            char resolved[1024];
            errno = 0;
            const char *result = realpath(line + 9, resolved);
            printf("test: realpath %s = %s (errno %d)\n", line + 9, result != NULL ? result : "NULL", errno);
        }
        else if (strncmp(line, "stat ", 5) == 0)
        {
            struct stat info;
            int result = stat(line + 5, &info);
            printf("test: stat %s = %d (errno %d, size %lld, mode %o)\n", line + 5, result, result != 0 ? errno : 0,
                   result == 0 ? (long long)info.st_size : -1LL, result == 0 ? (unsigned)info.st_mode : 0u);
        }
        else if (strncmp(line, "point ", 6) == 0 && point_count < MAX_POINTS)
        {
            if (sscanf(line + 6, "%d %d %d", &points[point_count].frame, &points[point_count].x,
                       &points[point_count].y) == 3)
                point_count++;
        }
        else if (strncmp(line, "game ", 5) == 0)
            snprintf(game_path, sizeof(game_path), "%s", line + 5);
        else if (strncmp(line, "remove ", 7) == 0 &&
                 (strncmp(line + 7, "/app0/", 6) == 0 || strncmp(line + 7, "/download0/", 11) == 0))
        {
            remove_tree(line + 7);
            printf("test: removed %s\n", line + 7);
        }
        else if (strncmp(line, "input ", 6) == 0)
            pad_script_add(line + 6);
        else if (strncmp(line, "arg ", 4) == 0 && argument_count < MAX_ARGUMENTS)
            snprintf(arguments[argument_count++], sizeof(arguments[0]), "%s", line + 4);
    }
    fclose(plan);
    printf("test: plan with %d captures, frame limit %d\n", capture_count, frame_limit);
    if (port > 0)
        start_server(port);
    PS5_FrameCallback = on_frame;
    PS5_PadCallback = on_pad;
}

int ps5_console_test_arguments(const char **out, int max)
{
    int count = argument_count < max ? argument_count : max;
    for (int i = 0; i < count; i++)
        out[i] = arguments[i];
    return count;
}

const char *ps5_console_test_game_path(void)
{
    return game_path[0] != '\0' ? game_path : NULL;
}
