/* The first-run screen that receives the RollerCoaster Tycoon 2 game files from a browser (see
 * upload.c). It draws with SDL and its own fonts (tools/make-font.py), since OpenRCT2 can draw
 * nothing until the game's files are there. It returns once Data/g1.dat exists and the sender
 * has finished, or a ZIP file has been unpacked. */
#include <SDL.h>
#include <errno.h>
#include <fcntl.h>
#include <qrcodegen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zip.h>

#include "font.h"
#include "ps5.h"
#include "upload.h"

#define SCREEN_WIDTH 1920
#define SCREEN_HEIGHT 1080
#define MARGIN 160
#define TEXT_WIDTH 1000
#define QR_LEFT 1260
#define QR_MODULE 9
#define NOTES_SHOWN 6
#define ZIP_ENTRIES_PER_FRAME 16

typedef struct
{
    SDL_Texture *texture;
    const short (*glyphs)[4];
    int height;
} font_t;

static const SDL_Color white = { 0xe8, 0xee, 0xf2, 0xff };
static const SDL_Color muted = { 0x93, 0xa4, 0xb1, 0xff };
static const SDL_Color accent = { 0xf2, 0xb2, 0x2b, 0xff };
static const SDL_Color good = { 0x58, 0xc2, 0x6e, 0xff };
static const SDL_Color bad = { 0xe0, 0x5a, 0x4f, 0xff };

static char notes[NOTES_SHOWN][160];
static SDL_Color note_colours[NOTES_SHOWN];
static int note_count;

/* Shows a line on the screen and on the page. */
static void note(const char *text, int ok)
{
    printf("import: %s\n", text);
    upload_note(text, ok);
    if (note_count == NOTES_SHOWN)
    {
        memmove(notes, notes + 1, sizeof(notes[0]) * (NOTES_SHOWN - 1));
        memmove(note_colours, note_colours + 1, sizeof(note_colours[0]) * (NOTES_SHOWN - 1));
        note_count--;
    }
    snprintf(notes[note_count], sizeof(notes[0]), "%s", text);
    note_colours[note_count++] = ok ? good : bad;
}

static font_t load_font(SDL_Renderer *renderer, const unsigned char *pixels, int width, int height,
                        const short (*glyphs)[4])
{
    font_t font = { NULL, glyphs, height };
    Uint32 *argb = malloc((size_t)width * height * sizeof(Uint32));
    if (argb == NULL)
        return font;
    for (int i = 0; i < width * height; i++)
        argb[i] = (Uint32)pixels[i] << 24 | 0xffffff;
    font.texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, width, height);
    if (font.texture != NULL)
    {
        SDL_UpdateTexture(font.texture, NULL, argb, width * (int)sizeof(Uint32));
        SDL_SetTextureBlendMode(font.texture, SDL_BLENDMODE_BLEND);
    }
    free(argb);
    return font;
}

static const short *glyph(const font_t *font, char c)
{
    return font->glyphs[(c >= ' ' && c <= '~' ? c : '?') - ' '];
}

static int text_width(const font_t *font, const char *text, size_t length)
{
    int width = 0;
    for (size_t i = 0; i < length; i++)
        width += glyph(font, text[i])[2];
    return width;
}

static void draw_text(SDL_Renderer *renderer, const font_t *font, int x, int y, const char *text, SDL_Color colour)
{
    SDL_SetTextureColorMod(font->texture, colour.r, colour.g, colour.b);
    for (; *text != '\0'; text++)
    {
        const short *g = glyph(font, *text);
        SDL_Rect source = { g[0], 0, g[1], font->height };
        SDL_Rect target = { x + g[3], y, g[1], font->height };
        SDL_RenderCopy(renderer, font->texture, &source, &target);
        x += g[2];
    }
}

/* Draws text wrapped at width; returns the height it took. */
static int draw_wrapped(SDL_Renderer *renderer, const font_t *font, int x, int y, int width, const char *text,
                        SDL_Color colour)
{
    int top = y;
    char line[256];
    while (*text != '\0')
    {
        size_t length = 0, fits = 0;
        while (text[length] != '\0' && length < sizeof(line) - 1)
        {
            size_t word = length;
            while (text[word] == ' ')
                word++;
            while (text[word] != '\0' && text[word] != ' ')
                word++;
            if (text_width(font, text, word) > width && fits > 0)
                break;
            length = fits = word;
        }
        if (fits == 0)
            fits = length;
        memcpy(line, text, fits);
        line[fits] = '\0';
        draw_text(renderer, font, x, y, line, colour);
        y += font->height + 6;
        text += fits;
        while (*text == ' ')
            text++;
    }
    return y - top;
}

static void draw_qr(SDL_Renderer *renderer, const char *text, int x, int y)
{
    uint8_t code[qrcodegen_BUFFER_LEN_MAX], buffer[qrcodegen_BUFFER_LEN_MAX];
    if (!qrcodegen_encodeText(text, buffer, code, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN,
                              qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true))
        return;
    int size = qrcodegen_getSize(code);
    SDL_Rect quiet = { x, y, (size + 8) * QR_MODULE, (size + 8) * QR_MODULE };
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderFillRect(renderer, &quiet);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    for (int row = 0; row < size; row++)
    {
        for (int column = 0; column < size; column++)
        {
            if (qrcodegen_getModule(code, column, row))
            {
                SDL_Rect module = { x + (column + 4) * QR_MODULE, y + (row + 4) * QR_MODULE, QR_MODULE, QR_MODULE };
                SDL_RenderFillRect(renderer, &module);
            }
        }
    }
}

static void make_parent_folders(char *path)
{
    for (char *at = strchr(path + 1, '/'); at != NULL; at = strchr(at + 1, '/'))
    {
        *at = '\0';
        mkdir(path, 0755);
        *at = '/';
    }
}

typedef struct
{
    zip_t *archive;
    char path[UPLOAD_PATH];
    zip_int64_t next, count;
    int files;
} unpacking_t;

/* Unpacks a few more entries of the ZIP file being unpacked; 1 when it has finished. */
static int unpack_some(unpacking_t *unpacking, const char *game_folder)
{
    static char buffer[256 * 1024];
    for (int done = 0; done < ZIP_ENTRIES_PER_FRAME && unpacking->next < unpacking->count; done++, unpacking->next++)
    {
        const char *name = zip_get_name(unpacking->archive, (zip_uint64_t)unpacking->next, 0);
        char inside[UPLOAD_PATH], target[UPLOAD_PATH * 2];
        size_t length = name != NULL ? strlen(name) : 0;
        if (length == 0 || name[length - 1] == '/' || upload_game_path(inside, sizeof(inside), name) != 0)
            continue;
        snprintf(target, sizeof(target), "%s/%s", game_folder, inside);
        make_parent_folders(target);
        zip_file_t *in = zip_fopen_index(unpacking->archive, (zip_uint64_t)unpacking->next, 0);
        int out = in != NULL ? open(target, O_WRONLY | O_CREAT | O_TRUNC, 0644) : -1;
        zip_int64_t got;
        int failed = out < 0;
        while (!failed && (got = zip_fread(in, buffer, sizeof(buffer))) > 0)
            failed = write(out, buffer, (size_t)got) != got;
        if (out >= 0)
            close(out);
        if (in != NULL)
            zip_fclose(in);
        if (failed)
        {
            char text[160];
            snprintf(text, sizeof(text), "Could not unpack %s", inside);
            note(text, 0);
            unlink(target);
        }
        else
            unpacking->files++;
    }
    if (unpacking->next < unpacking->count)
        return 0;
    char text[160];
    snprintf(text, sizeof(text), "Unpacked %d game files from the ZIP file", unpacking->files);
    note(text, unpacking->files > 0);
    zip_close(unpacking->archive);
    unlink(unpacking->path);
    unpacking->archive = NULL;
    return 1;
}

int ps5_game_data_present(const char *game_folder)
{
    char path[UPLOAD_PATH];
    struct stat info;
    snprintf(path, sizeof(path), "%s/Data/g1.dat", game_folder);
    return stat(path, &info) == 0 && S_ISREG(info.st_mode);
}

void ps5_import_game_data(const char *game_folder)
{
    if (SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0)
    {
        printf("import: no video: %s\n", SDL_GetError());
        return;
    }
    SDL_Window *window = SDL_CreateWindow("OpenRCT2", 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_FULLSCREEN_DESKTOP);
    SDL_Renderer *renderer = window != NULL ? SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC) : NULL;
    if (renderer == NULL)
    {
        printf("import: no renderer: %s\n", SDL_GetError());
        return;
    }
    // No pointer here, though going full screen put it at 0, 0, where it would show; the game
    // gets it back.
    SDL_ShowCursor(SDL_DISABLE);
    font_t body = load_font(renderer, font_body_pixels, font_body_width, font_body_height, font_body_glyphs);
    font_t title = load_font(renderer, font_title_pixels, font_title_width, font_title_height, font_title_glyphs);

    if (upload_start(game_folder) != 0)
        note("The console cannot receive files: no free network port", 0);
    const char *address = upload_address();
    unpacking_t unpacking = { 0 };
    Uint32 finished_at = 0;
    int batches_seen = 0;

    for (;;)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_CONTROLLERDEVICEADDED)
                SDL_GameControllerOpen(event.cdevice.which);
            else if (event.type == SDL_CONTROLLERBUTTONDOWN && event.cbutton.button == SDL_CONTROLLER_BUTTON_B)
                exit(0);
        }

        upload_progress_t progress;
        upload_progress(&progress);
        if (unpacking.archive != NULL)
            unpack_some(&unpacking, game_folder);
        else if (upload_take_zip(unpacking.path, sizeof(unpacking.path)))
        {
            int error = 0;
            unpacking.archive = zip_open(unpacking.path, ZIP_RDONLY, &error);
            unpacking.next = 0;
            unpacking.files = 0;
            unpacking.count = unpacking.archive != NULL ? zip_get_num_entries(unpacking.archive, 0) : 0;
            if (unpacking.archive == NULL)
            {
                note("The ZIP file cannot be read", 0);
                unlink(unpacking.path);
            }
        }
        /* A send ends with the page saying so (after a ZIP file, the unpacking ends it). */
        int present = ps5_game_data_present(game_folder);
        if (progress.batches != batches_seen && unpacking.archive == NULL)
        {
            char text[160];
            batches_seen = progress.batches;
            snprintf(text, sizeof(text), "Received %d game files", progress.files);
            if (progress.files > 0)
                note(text, 1);
            if (!present)
                note("Data/g1.dat is missing: send the whole game folder", 0);
        }
        if (present && batches_seen > 0 && unpacking.archive == NULL && finished_at == 0)
        {
            note("The game files are complete. Starting OpenRCT2.", 1);
            finished_at = SDL_GetTicks();
        }

        SDL_SetRenderDrawColor(renderer, 0x10, 0x18, 0x20, 0xff);
        SDL_RenderClear(renderer);
        int y = 150;
        draw_text(renderer, &title, MARGIN, y, "RollerCoaster Tycoon 2 game files", white);
        y += title.height + 30;
        y += draw_wrapped(renderer, &body, MARGIN, y, TEXT_WIDTH,
                          "OpenRCT2 plays with the game files of your own copy of RollerCoaster Tycoon 2 "
                          "(the Triple Thrill Pack from GOG or Steam). On a PC or phone on the same network, "
                          "open this address and send the game folder, the one with Data, ObjData and "
                          "Scenarios in it, or a ZIP file of it:",
                          muted);
        y += 24;
        draw_text(renderer, &title, MARGIN, y, address[0] != '\0' ? address : "No network connection", accent);
        y += title.height + 40;

        char line[UPLOAD_PATH + 64];
        if (unpacking.archive != NULL)
            snprintf(line, sizeof(line), "Unpacking the ZIP file: %lld of %lld entries", (long long)unpacking.next,
                     (long long)unpacking.count);
        else if (progress.receiving)
            snprintf(line, sizeof(line), "Receiving %s (%lld%%)", progress.name,
                     progress.total > 0 ? progress.done * 100 / progress.total : 0LL);
        else if (progress.files > 0)
            snprintf(line, sizeof(line), "%d files received, %lld MB", progress.files, progress.bytes >> 20);
        else
            snprintf(line, sizeof(line), "Waiting for the game files");
        draw_text(renderer, &body, MARGIN, y, line, white);
        y += body.height + 30;
        for (int i = 0; i < note_count; i++, y += body.height + 6)
            draw_text(renderer, &body, MARGIN, y, notes[i], note_colours[i]);

        draw_text(renderer, &body, MARGIN, SCREEN_HEIGHT - 120, "Circle: close OpenRCT2", muted);
        if (address[0] != '\0')
            draw_qr(renderer, address, QR_LEFT, 180);
        SDL_RenderPresent(renderer);

        if (finished_at != 0 && SDL_GetTicks() - finished_at > 1500)
            break;
    }

    upload_stop();
    SDL_DestroyTexture(body.texture);
    SDL_DestroyTexture(title.texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_ShowCursor(SDL_ENABLE);
}
