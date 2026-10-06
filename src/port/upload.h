/* A small HTTP server for sending the RollerCoaster Tycoon 2 game folder from a browser on the same
 * network (adapted from the DOOM port's). It serves the page in upload.html, stores each file the
 * page sends below the game folder's own folders, and hands ZIP files of the folder and GOG
 * installers to the caller. It runs only between upload_start and upload_stop. */
#ifndef PS5_UPLOAD_H
#define PS5_UPLOAD_H

#include <stddef.h>

/* The first port tried. A title's sandbox refuses some ports (8666 and 50000 on FW 13.00) and
 * others may be taken, so the next few are tried after it. */
#define UPLOAD_PORT 9666
#define UPLOAD_PORT_TRIES 8
#define UPLOAD_PATH 512

typedef struct
{
    int receiving;             /* a file is arriving */
    char name[UPLOAD_PATH];    /* its path below the game folder */
    long long done, total;     /* bytes of it */
    int files;                 /* files stored */
    long long bytes;           /* bytes stored */
    int batches;               /* times the page said it sent everything it was given */
    int installer_files;       /* .exe and .bin files of a GOG installer stored */
} upload_progress_t;

/* Starts listening and storing below game_folder; 0 on success. The address is "" when the
 * console has no network address. */
int upload_start(const char *game_folder);
void upload_stop(void);
const char *upload_address(void);

void upload_progress(upload_progress_t *progress);

/* Hands over the oldest ZIP file received: 1 when there was one, with its path. */
int upload_take_zip(char *path, size_t size);

/* Hands over the GOG installer's .exe (its .bin files are stored next to it) once the page has
 * finished sending, and again after more of its files arrive: 1 when it is ready, with its path. */
int upload_take_setup(char *path, size_t size);

/* Deletes the installer's files. */
void upload_remove_installer(void);

/* Turns a path inside the game folder, as a browser or a ZIP file names it ("RCT2/Data/g1.dat"),
 * into the path to store it under ("Data/g1.dat"): only files below the game's own folders are
 * kept, and each part must be a plain name. 0 on success. */
int upload_game_path(char *out, size_t size, const char *path);

/* Adds a line to the log the page shows, for what the console did. */
void upload_note(const char *text, int ok);

#endif
