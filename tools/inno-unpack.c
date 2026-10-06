/* Unpacks RollerCoaster Tycoon 2's game folders from a GOG installer on the PC with the console's
 * reader (src/port/inno.c), for tools/test-inno.sh.
 *
 *     inno-unpack SETUP.exe FOLDER */
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "inno.h"

static const char *const game_folders[] = { "Data", "ObjData", "Scenarios", "Tracks", "Landscapes" };

static int target(char *out, size_t size, const char *path)
{
    for (size_t i = 0; i < sizeof(game_folders) / sizeof(game_folders[0]); i++)
    {
        const size_t length = strlen(game_folders[i]);
        if (strncasecmp(path, game_folders[i], length) == 0 && path[length] == '/')
        {
            snprintf(out, size, "%s%s", game_folders[i], path + length);
            return 0;
        }
    }
    return -1;
}

static int progress(long long done, long long total, void *user)
{
    int *shown = user;
    const int percent = total > 0 ? (int)(done * 100 / total) : 100;
    if (percent >= *shown + 10)
    {
        printf("%d%% of %lld MB\n", percent, total >> 20);
        fflush(stdout);
        *shown = percent;
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        fprintf(stderr, "usage: inno-unpack SETUP.exe FOLDER\n");
        return 2;
    }
    int shown = 0, files;
    char error[256];
    const inno_callbacks_t callbacks = { target, progress, &shown };
    if (inno_unpack(argv[1], argv[2], &callbacks, &files, error, sizeof(error)) != 0)
    {
        printf("failed after %d files: %s\n", files, error);
        return 1;
    }
    printf("unpacked %d files\n", files);
    return 0;
}
