// Entry point of the PS5 title: runs OpenRCT2 with the console's data locations.
//   /app0/data         OpenRCT2's own data (languages, objects, title sequences, sound packs)
//   /app0/rct2         the player's RollerCoaster Tycoon 2 folder (Data, ObjData, Scenarios...)
//   /download0/user    config, saves and caches
#include "ps5.h"
#include "ps5runtime.h"

#include <SDL.h>
#include <cstdio>
#include <openrct2-ui/Ui.h>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace
{
    constexpr const char* kUserPath = "/download0/user";
    constexpr const char* kGamePath = "/app0/rct2";
    constexpr const char* kConfigPath = "/download0/user/config.ini";

    // On first run: full screen at the TV's resolution, the interface drawn at twice the size
    // (OpenRCT2 would otherwise infer the scale from the display's DPI on its first start), and a
    // frame for every refresh of a 60 Hz TV rather than one per 40 Hz game tick.
    void WriteDefaultConfig()
    {
        struct stat info{};
        if (stat(kConfigPath, &info) == 0)
            return;
        mkdir(kUserPath, 0755);
        if (auto* file = std::fopen(kConfigPath, "w"))
        {
            std::fputs(
                "[general]\n"
                "fullscreen_mode = 2\n"
                "window_scale = 2.000000\n"
                "infer_display_dpi = false\n"
                "uncap_fps = true\n",
                file);
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
    const char* gamePath = ps5_console_test_game_path() != nullptr ? ps5_console_test_game_path() : kGamePath;
    if (!ps5_game_data_present(gamePath))
        ps5_import_game_data(gamePath);
    const std::string gameArgument = std::string("--rct2-data-path=") + gamePath;

    std::vector<const char*> argv = {
        "openrct2",
        "--openrct2-data-path=/app0/data",
        gameArgument.c_str(),
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
