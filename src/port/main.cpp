// Entry point of the PS5 title: runs OpenRCT2 with the console's data locations.
//   /app0/data         OpenRCT2's own data (languages, objects, title sequences, sound packs)
//   /app0/rct2         the player's RollerCoaster Tycoon 2 folder (Data, ObjData, Scenarios...)
//   /download0/user    config, saves and caches
#include "ps5.h"

#include <SDL.h>
#include <cstdio>
#include <openrct2-ui/Ui.h>
#include <pthread.h>
#include <sys/stat.h>

namespace
{
    constexpr const char* kUserPath = "/download0/user";
    constexpr const char* kConfigPath = "/download0/user/config.ini";

    // On first run: full screen at the TV's resolution, with the interface drawn at twice the size.
    void WriteDefaultConfig()
    {
        struct stat info{};
        if (stat(kConfigPath, &info) == 0)
            return;
        mkdir(kUserPath, 0755);
        if (auto* file = std::fopen(kConfigPath, "w"))
        {
            std::fputs("[general]\nfullscreen_mode = 2\nwindow_scale = 2.000000\n", file);
            std::fclose(file);
        }
    }

    void LogDefaultThreadStack()
    {
        pthread_attr_t attributes;
        size_t size = 0;
        pthread_attr_init(&attributes);
        pthread_attr_getstacksize(&attributes, &size);
        pthread_attr_destroy(&attributes);
        std::printf("ps5: default thread stack %zu KiB\n", size / 1024);
    }
} // namespace

int main()
{
    ps5_log_start();
    LogDefaultThreadStack();
    ps5_console_test_start();
    WriteDefaultConfig();
    // The PS5 SDL has only the software renderer, and OpenRCT2 asks for an accelerated one.
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");

    const char* argv[] = {
        "openrct2",
        "--openrct2-data-path=/app0/data",
        "--rct2-data-path=/app0/rct2",
        "--user-data-path=/download0/user",
        nullptr,
    };
    int result = NormalisedMain(4, argv);
    std::printf("openrct2 returned %d\n", result);
    ps5_console_test_finish();
    return result;
}
