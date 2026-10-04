// Entry point of the PS5 title: runs OpenRCT2 with the console's data locations.
//   /app0/data         OpenRCT2's own data (languages, objects, title sequences, sound packs)
//   /app0/rct2         the player's RollerCoaster Tycoon 2 folder (Data, ObjData, Scenarios...)
//   /download0/user    config, saves and caches
#include <openrct2-ui/Ui.h>

int main()
{
    const char* argv[] = {
        "openrct2",
        "--openrct2-data-path=/app0/data",
        "--rct2-data-path=/app0/rct2",
        "--user-data-path=/download0/user",
        nullptr,
    };
    return NormalisedMain(4, argv);
}
