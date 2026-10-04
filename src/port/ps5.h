/* The PS5 side of the port: importing the game files, and the console test harness. */
#ifndef PS5_PORT_H
#define PS5_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Whether game_folder holds RollerCoaster Tycoon 2's files (Data/g1.dat). */
int ps5_game_data_present(const char *game_folder);

/* Shows the screen that receives the game files from a browser into game_folder (import_screen.c)
 * until they are complete. */
void ps5_import_game_data(const char *game_folder);

/* Reads /app0/test.cfg if it exists and runs its plan (see console_test.c). */
void ps5_console_test_start(void);

/* The game folder the plan asks for, or NULL. */
const char *ps5_console_test_game_path(void);

/* Puts up to max extra command-line arguments from the plan into out; returns how many. */
int ps5_console_test_arguments(const char **out, int max);

/* Called when the game returns from its main loop: sends the log to the test runner. */
void ps5_console_test_finish(void);

#ifdef __cplusplus
}
#endif

#endif
