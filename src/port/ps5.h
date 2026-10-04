/* The PS5 side of the port: logging and the console test harness. */
#ifndef PS5_PORT_H
#define PS5_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Sends stdout and stderr to the log file (/app0/openrct2.log, or /download0 if the title folder
 * is read-only) and to the kernel log, one line per entry. */
void ps5_log_start(void);

/* The log file's path, once ps5_log_start has opened it; NULL before or without one. */
const char *ps5_log_path(void);

/* Reads /app0/test.cfg if it exists and runs its plan (see console_test.c). */
void ps5_console_test_start(void);

/* Called when the game returns from its main loop: sends the log to the test runner. */
void ps5_console_test_finish(void);

#ifdef __cplusplus
}
#endif

#endif
