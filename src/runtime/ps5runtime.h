/* What the PS5 runtime (libps5runtime.a) offers the title's own code. */
#ifndef PS5_RUNTIME_H
#define PS5_RUNTIME_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opens the first of paths that can be written as the log file and returns it; NULL if none. */
const char *ps5_log_open(const char *const *paths, size_t count);

/* The open log file's path, or NULL. */
const char *ps5_log_path(void);

/* Appends text to the log file and the kernel log. */
void ps5_log_write(const char *text, size_t length);

#ifdef __cplusplus
}
#endif

#endif
