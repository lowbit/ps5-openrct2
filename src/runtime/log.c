/* The title's console output. The console's printf writes to the libc's own stdout, which ends up
 * nowhere a developer can see, so the linker sends the stdio calls here (tools/ps5-link wraps
 * them): text for stdout and stderr goes to the log file, if one is open, and to the kernel log,
 * one line per sceKernelDebugOutText. Output to any other stream goes on to the libc. */
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ps5runtime.h"

int sceKernelDebugOutText(int channel, const char *text);

int __real_vfprintf(FILE *stream, const char *format, va_list args);
int __real_fputs(const char *text, FILE *stream);
int __real_fputc(int c, FILE *stream);
size_t __real_fwrite(const void *data, size_t size, size_t count, FILE *stream);

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int file = -1;
static const char *path;
static char line[512];
static size_t held;

const char *ps5_log_open(const char *const *paths, size_t count)
{
    pthread_mutex_lock(&lock);
    for (size_t i = 0; i < count && file < 0; i++)
    {
        file = open(paths[i], O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (file >= 0)
            path = paths[i];
    }
    pthread_mutex_unlock(&lock);
    return path;
}

const char *ps5_log_path(void)
{
    return path;
}

void ps5_log_write(const char *text, size_t length)
{
    pthread_mutex_lock(&lock);
    if (file >= 0)
        write(file, text, length);
    for (size_t i = 0; i < length; i++)
    {
        line[held++] = text[i];
        if (text[i] == '\n' || held == sizeof(line) - 1)
        {
            line[held] = '\0';
            sceKernelDebugOutText(0, line);
            held = 0;
        }
    }
    pthread_mutex_unlock(&lock);
}

static int log_format(const char *format, va_list args)
{
    char buffer[2048];
    va_list copy;
    va_copy(copy, args);
    int length = vsnprintf(buffer, sizeof(buffer), format, args);
    if (length >= (int)sizeof(buffer))
    {
        char *large = malloc((size_t)length + 1);
        if (large != NULL)
        {
            vsnprintf(large, (size_t)length + 1, format, copy);
            ps5_log_write(large, (size_t)length);
            free(large);
        }
    }
    else if (length > 0)
        ps5_log_write(buffer, (size_t)length);
    va_end(copy);
    return length;
}

static int is_console(FILE *stream)
{
    return stream == stdout || stream == stderr;
}

int __wrap_vprintf(const char *format, va_list args)
{
    return log_format(format, args);
}

int __wrap_printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int length = log_format(format, args);
    va_end(args);
    return length;
}

int __wrap_vfprintf(FILE *stream, const char *format, va_list args)
{
    return is_console(stream) ? log_format(format, args) : __real_vfprintf(stream, format, args);
}

int __wrap_fprintf(FILE *stream, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int length = __wrap_vfprintf(stream, format, args);
    va_end(args);
    return length;
}

int __wrap_puts(const char *text)
{
    ps5_log_write(text, strlen(text));
    ps5_log_write("\n", 1);
    return 1;
}

int __wrap_putchar(int c)
{
    char character = (char)c;
    ps5_log_write(&character, 1);
    return (unsigned char)c;
}

int __wrap_fputs(const char *text, FILE *stream)
{
    if (!is_console(stream))
        return __real_fputs(text, stream);
    ps5_log_write(text, strlen(text));
    return 1;
}

int __wrap_fputc(int c, FILE *stream)
{
    if (!is_console(stream))
        return __real_fputc(c, stream);
    return __wrap_putchar(c);
}

size_t __wrap_fwrite(const void *data, size_t size, size_t count, FILE *stream)
{
    if (!is_console(stream))
        return __real_fwrite(data, size, count, stream);
    ps5_log_write(data, size * count);
    return count;
}
