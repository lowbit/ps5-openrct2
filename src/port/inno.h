/* Unpacks files from an Inno Setup installer, the kind GOG sells RollerCoaster Tycoon 2 as: the
 * setup .exe holds the list of files and where each is kept, the .bin files beside it (or the .exe
 * itself) hold the contents. Covers the Unicode installers of Inno Setup 5.5 to 6.4, and the GOG
 * Galaxy ones that keep each file in deflated parts. */
#ifndef PS5_INNO_H
#define PS5_INNO_H

#include <stddef.h>

typedef struct
{
    /* Turns a path inside the installation folder ("Data/g1.dat") into the path to write it under,
     * below the target folder; nonzero leaves the file out. */
    int (*target)(char *out, size_t size, const char *path);
    /* Called now and then with the bytes written of all the files to write; nonzero stops. */
    int (*progress)(long long done, long long total, void *user);
    void *user;
} inno_callbacks_t;

#define INNO_MISSING_PART 1

/* Unpacks the files target() takes from the installer whose .exe is setup into folder. The .bin
 * files are looked for next to the .exe, as <name of the .exe>-1.bin and so on. Files are written
 * in place (each file created in a title's folder costs tens of milliseconds on the console, a
 * rename several more); one left unfinished by a failure is deleted, the ones finished before it
 * stay. Returns 0 on success, with the number of files written in *files; otherwise a sentence for
 * the player in error, and INNO_MISSING_PART when a .bin file is missing, -1 for anything else. */
int inno_unpack(const char *setup, const char *folder, const inno_callbacks_t *callbacks, int *files, char *error,
                size_t error_size);

#endif
