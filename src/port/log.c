/* stdout and stderr go through a pipe to a thread that writes them to the log file and to the
 * kernel log (sceKernelDebugOutText wants one line per call). The file lives in the title folder,
 * where the PC can read it even after a crash; /download0 is only a fallback. */
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "ps5.h"

int sceKernelDebugOutText(int channel, const char *text);

static const char *const log_paths[] = { "/app0/openrct2.log", "/download0/openrct2.log" };

static const char *path;
static int log_file = -1;

const char *ps5_log_path(void)
{
    return path;
}

static void write_line(const char *text, size_t length)
{
    char line[512];
    if (length >= sizeof(line))
        length = sizeof(line) - 1;
    memcpy(line, text, length);
    line[length] = '\0';
    sceKernelDebugOutText(0, line);
}

static void *pump(void *argument)
{
    int input = (int)(intptr_t)argument;
    char buffer[4096];
    size_t held = 0;
    for (;;)
    {
        ssize_t length = read(input, buffer + held, sizeof(buffer) - held);
        if (length <= 0)
            return NULL;
        if (log_file >= 0)
            write(log_file, buffer + held, (size_t)length);
        held += (size_t)length;

        size_t start = 0;
        for (size_t i = 0; i < held; i++)
        {
            if (buffer[i] == '\n')
            {
                write_line(buffer + start, i + 1 - start);
                start = i + 1;
            }
        }
        if (start == 0 && held == sizeof(buffer))
        {
            /* No line end in a full buffer: pass it on as it is. */
            write_line(buffer, held);
            start = held;
        }
        memmove(buffer, buffer + start, held - start);
        held -= start;
    }
}

void ps5_log_start(void)
{
    for (size_t i = 0; i < sizeof(log_paths) / sizeof(log_paths[0]) && log_file < 0; i++)
    {
        log_file = open(log_paths[i], O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (log_file >= 0)
            path = log_paths[i];
    }

    int fds[2];
    pthread_t thread;
    if (pipe(fds) != 0)
        return;
    FILE *output = fdopen(fds[1], "w");
    if (output == NULL || pthread_create(&thread, NULL, pump, (void *)(intptr_t)fds[0]) != 0)
        return;
    setvbuf(output, NULL, _IOLBF, 0);
    stdout = output;
    stderr = output;
}
