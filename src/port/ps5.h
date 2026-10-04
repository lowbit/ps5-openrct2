/* The PS5 side of the port: the console test harness. */
#ifndef PS5_PORT_H
#define PS5_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Reads /app0/test.cfg if it exists and runs its plan (see console_test.c). */
void ps5_console_test_start(void);

/* Puts up to max extra command-line arguments from the plan into out; returns how many. */
int ps5_console_test_arguments(const char **out, int max);

/* Called when the game returns from its main loop: sends the log to the test runner. */
void ps5_console_test_finish(void);

#ifdef __cplusplus
}
#endif

#endif
