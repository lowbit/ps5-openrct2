// Entry point of the PS5 title: runs OpenRCT2 with the console's data locations.
//   /app0/data         OpenRCT2's own data (languages, objects, title sequences, sound packs)
//   /app0/rct2         the player's RollerCoaster Tycoon 2 folder (Data, ObjData, Scenarios...)
//   /download0/user    config, saves and caches
#include "ps5.h"
#include "ps5runtime.h"

#include <SDL.h>
#include <cstdio>
#include <openrct2-ui/Ui.h>
#include <sys/stat.h>
#include <vector>

namespace
{
    constexpr const char* kUserPath = "/download0/user";
    constexpr const char* kConfigPath = "/download0/user/config.ini";

    // On first run: full screen at the TV's resolution, with the interface drawn at twice the size
    // (OpenRCT2 would otherwise infer the scale from the display's DPI on its first start).
    void WriteDefaultConfig()
    {
        struct stat info{};
        if (stat(kConfigPath, &info) == 0)
            return;
        mkdir(kUserPath, 0755);
        if (auto* file = std::fopen(kConfigPath, "w"))
        {
            std::fputs("[general]\nfullscreen_mode = 2\nwindow_scale = 2.000000\ninfer_display_dpi = false\n", file);
            std::fclose(file);
        }
    }
} // namespace

int main()
{
    // The title folder, where the PC can read the log even after a crash, unless it is read-only.
    const char* logPaths[] = { "/app0/openrct2.log", "/download0/openrct2.log" };
    ps5_log_open(logPaths, 2);
    ps5_console_test_start();
    WriteDefaultConfig();
    // The PS5 SDL has only the software renderer, and OpenRCT2 asks for an accelerated one.
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");

    std::vector<const char*> argv = {
        "openrct2",
        "--openrct2-data-path=/app0/data",
        "--rct2-data-path=/app0/rct2",
        "--user-data-path=/download0/user",
    };
    const char* extra[8];
    argv.insert(argv.end(), extra, extra + ps5_console_test_arguments(extra, 8));
    int count = static_cast<int>(argv.size());
    argv.push_back(nullptr);
    int result = NormalisedMain(count, argv.data());
    std::printf("openrct2 returned %d\n", result);
    ps5_console_test_finish();
    return result;
}
