/* The Inno Setup reader (see inno.h). The format follows innoextract's reading of it
 * (https://github.com/dscharrer/innoextract: src/loader, src/setup and src/stream), cut down to what
 * a Unicode installer of data version 5.5.0 or later holds. */
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <lzma.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

#include "inno.h"

#define VERSION(a, b, c, d) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))
#define OLDEST VERSION(5, 5, 0, 0)
#define NEWEST VERSION(6, 4, 255, 255)
/* The offset table is a resource of the .exe, before any data appended to it. */
#define LOADER_SCAN (16 * 1024 * 1024)
#define BUFFER (256 * 1024)
#define PATH_SIZE 1024
#define WINDOWS_VERSIONS 20

enum
{
    STORED,
    ZLIB,
    BZIP2,
    LZMA1,
    LZMA2
};

typedef struct
{
    uint32_t first_slice, chunk_offset;
    uint64_t file_offset, file_size, chunk_size;
    uint8_t sha1[20];
    int has_sha1, filtered, encrypted, compressed;
} data_entry_t;

/* A file to write, made of one part, or of several in a GOG Galaxy installer. */
typedef struct
{
    char path[PATH_SIZE]; /* below the target folder */
    uint32_t parts, written;
    int galaxy; /* each part deflated by GOG, to inflate */
    int fd;     /* while it is being written */
} output_t;

/* A part of a file: its data entry, and the entry's place for sorting. */
typedef struct
{
    uint32_t data, output, part, slice, chunk;
    uint64_t offset;
} piece_t;

typedef struct
{
    const char *setup, *folder;
    const inno_callbacks_t *callbacks;
    char *error;
    size_t error_size;
    uint32_t version, slices_per_disk, data_offset;
    int compression;
    data_entry_t *data;
    uint32_t data_count;
    output_t *outputs;
    size_t output_count;
    piece_t *pieces;
    size_t piece_count;
    long long done, total;
    int files, missing_part;
    char made[PATH_SIZE * 2]; /* the folder the previous file went in */
} inno_t;

static int fail(inno_t *inno, const char *format, ...)
{
    if (inno->error[0] == '\0')
    {
        va_list arguments;
        va_start(arguments, format);
        vsnprintf(inno->error, inno->error_size, format, arguments);
        va_end(arguments);
    }
    return -1;
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int read_at(int fd, uint64_t offset, void *buffer, size_t size)
{
    if (lseek(fd, (off_t)offset, SEEK_SET) < 0)
        return -1;
    for (uint8_t *at = buffer; size > 0;)
    {
        ssize_t got = read(fd, at, size);
        if (got <= 0)
            return -1;
        at += got;
        size -= (size_t)got;
    }
    return 0;
}

/* SHA-1, for the files' checksums. */

typedef struct
{
    uint32_t h[5];
    uint64_t length;
    uint8_t block[64];
    size_t used;
} sha1_t;

static uint32_t rotate(uint32_t x, int n)
{
    return x << n | x >> (32 - n);
}

static void sha1_block(sha1_t *s, const uint8_t *p)
{
    uint32_t w[80];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 80; i++)
        w[i] = rotate(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4];
    for (int i = 0; i < 80; i++)
    {
        uint32_t f, k;
        if (i < 20)
            f = (b & c) | (~b & d), k = 0x5a827999;
        else if (i < 40)
            f = b ^ c ^ d, k = 0x6ed9eba1;
        else if (i < 60)
            f = (b & c) | (b & d) | (c & d), k = 0x8f1bbcdc;
        else
            f = b ^ c ^ d, k = 0xca62c1d6;
        uint32_t t = rotate(a, 5) + f + e + k + w[i];
        e = d, d = c, c = rotate(b, 30), b = a, a = t;
    }
    s->h[0] += a, s->h[1] += b, s->h[2] += c, s->h[3] += d, s->h[4] += e;
}

static void sha1_start(sha1_t *s)
{
    static const uint32_t start[5] = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0 };
    memcpy(s->h, start, sizeof(start));
    s->length = 0;
    s->used = 0;
}

static void sha1_add(sha1_t *s, const uint8_t *p, size_t n)
{
    s->length += n;
    while (n > 0)
    {
        if (s->used == 0 && n >= 64)
        {
            sha1_block(s, p);
            p += 64, n -= 64;
            continue;
        }
        size_t take = 64 - s->used < n ? 64 - s->used : n;
        memcpy(s->block + s->used, p, take);
        s->used += take, p += take, n -= take;
        if (s->used == 64)
        {
            sha1_block(s, s->block);
            s->used = 0;
        }
    }
}

static void sha1_end(sha1_t *s, uint8_t out[20])
{
    const uint64_t bits = s->length * 8;
    uint8_t padding[72] = { 0x80 }, size[8];
    sha1_add(s, padding, 1 + (s->used < 56 ? 55 - s->used : 119 - s->used));
    for (int i = 0; i < 8; i++)
        size[i] = (uint8_t)(bits >> (56 - 8 * i));
    sha1_add(s, size, 8);
    for (int i = 0; i < 20; i++)
        out[i] = (uint8_t)(s->h[i / 4] >> (24 - 8 * (i % 4)));
}

/* The setup headers. */

typedef struct
{
    const uint8_t *at, *end;
    int short_read;
} reader_t;

static uint64_t number(reader_t *r, int bytes)
{
    uint64_t value = 0;
    if (r->end - r->at < bytes)
    {
        r->short_read = 1;
        r->at = r->end;
        return 0;
    }
    for (int i = 0; i < bytes; i++)
        value |= (uint64_t)r->at[i] << (8 * i);
    r->at += bytes;
    return value;
}

static void skip(reader_t *r, uint64_t bytes)
{
    if ((uint64_t)(r->end - r->at) < bytes)
    {
        r->short_read = 1;
        r->at = r->end;
        return;
    }
    r->at += bytes;
}

/* A string is its length in bytes, then the bytes: UTF-16LE in a Unicode installer. */
static const uint8_t *string(reader_t *r, uint32_t *length)
{
    *length = (uint32_t)number(r, 4);
    const uint8_t *start = r->at;
    skip(r, *length);
    return r->short_read ? NULL : start;
}

static void skip_strings(reader_t *r, int count)
{
    for (uint32_t length; count > 0; count--)
        string(r, &length);
}

/* A set of flags takes a byte per 8 flags, except that a 3-byte set takes 4. */
static void skip_flags(reader_t *r, int count)
{
    const int bytes = (count + 7) / 8;
    skip(r, bytes == 3 ? 4 : (uint64_t)bytes);
}

static int utf8_from_utf16(char *out, size_t size, const uint8_t *in, uint32_t bytes)
{
    size_t n = 0;
    for (uint32_t i = 0; i + 1 < bytes; i += 2)
    {
        uint32_t c = (uint32_t)in[i] | (uint32_t)in[i + 1] << 8;
        if (c >= 0xd800 && c < 0xdc00 && i + 3 < bytes)
        {
            const uint32_t low = (uint32_t)in[i + 2] | (uint32_t)in[i + 3] << 8;
            if (low >= 0xdc00 && low < 0xe000)
            {
                c = 0x10000 + ((c - 0xd800) << 10) + (low - 0xdc00);
                i += 2;
            }
        }
        uint8_t encoded[4];
        size_t length;
        if (c < 0x80)
            encoded[0] = (uint8_t)c, length = 1;
        else if (c < 0x800)
            encoded[0] = (uint8_t)(0xc0 | c >> 6), encoded[1] = (uint8_t)(0x80 | (c & 0x3f)), length = 2;
        else if (c < 0x10000)
            encoded[0] = (uint8_t)(0xe0 | c >> 12), encoded[1] = (uint8_t)(0x80 | (c >> 6 & 0x3f)),
            encoded[2] = (uint8_t)(0x80 | (c & 0x3f)), length = 3;
        else
            encoded[0] = (uint8_t)(0xf0 | c >> 18), encoded[1] = (uint8_t)(0x80 | (c >> 12 & 0x3f)),
            encoded[2] = (uint8_t)(0x80 | (c >> 6 & 0x3f)), encoded[3] = (uint8_t)(0x80 | (c & 0x3f)), length = 4;
        if (c == 0 || n + length >= size)
            return -1;
        memcpy(out + n, encoded, length);
        n += length;
    }
    out[n] = '\0';
    return 0;
}

/* "Inno Setup Setup Data (5.6.2) (u)": the data version, and whether strings are UTF-16. */
static int read_version(const uint8_t id[64], uint32_t *version, int *unicode)
{
    static const char prefix[] = "Inno Setup Setup Data (";
    char text[65];
    memcpy(text, id, 64);
    text[64] = '\0';
    if (strncmp(text, prefix, sizeof(prefix) - 1) != 0)
        return -1;
    const char *at = text + sizeof(prefix) - 1;
    unsigned long parts[4] = { 0 };
    int count = 0;
    while (count < 4 && isdigit((unsigned char)*at))
    {
        char *end;
        parts[count++] = strtoul(at, &end, 10);
        at = end;
        if (*at != '.')
            break;
        at++;
    }
    if (count < 3 || *at != ')')
        return -1;
    for (int i = 0; i < 4; i++)
        if (parts[i] > 255)
            return -1;
    *version = VERSION(parts[0], parts[1], parts[2], parts[3]);
    *unicode = strstr(at, "(u)") != NULL || strstr(at, "(U)") != NULL || *version >= VERSION(6, 3, 0, 0);
    return 0;
}

/* The offset table the setup loader finds its headers with: a resource of the .exe since 5.1.5. */
static int find_headers(inno_t *inno, int fd, uint32_t *header_offset)
{
    static const uint8_t magics[2][12] = {
        { 'r', 'D', 'l', 'P', 't', 'S', 0xcd, 0xe6, 0xd7, '{', 0x0b, '*' },
        { 'n', 'S', '5', 'W', '7', 'd', 'T', 0x83, 0xaa, 0x1b, 0x0f, 'j' },
    };
    struct stat info;
    if (fstat(fd, &info) != 0)
        return fail(inno, "The installer cannot be read");
    const size_t size = info.st_size < LOADER_SCAN ? (size_t)info.st_size : LOADER_SCAN;
    uint8_t *exe = malloc(size);
    if (exe == NULL || read_at(fd, 0, exe, size) != 0)
    {
        free(exe);
        return fail(inno, "The installer cannot be read");
    }
    int found = 0;
    /* magic, revision, total size, setup offset and size, its CRC32, header offset, data offset, CRC32 */
    for (size_t at = 0; !found && at + 44 <= size; at++)
    {
        if (exe[at] != 'r' && exe[at] != 'n')
            continue;
        for (int m = 0; m < 2 && !found; m++)
        {
            if (memcmp(exe + at, magics[m], 12) != 0 || crc32(0, exe + at, 40) != le32(exe + at + 40))
                continue;
            *header_offset = le32(exe + at + 32);
            inno->data_offset = le32(exe + at + 36);
            found = 1;
        }
    }
    free(exe);
    return found ? 0 : fail(inno, "This is not an installer OpenRCT2 can read: send the setup .exe from GOG");
}

/* A block of the setup headers: its size and whether it is compressed (CRC32-checked), then the data
 * in pieces of 4096 bytes, each after its CRC32, LZMA-compressed as a whole. */
static int read_block(inno_t *inno, int fd, uint64_t *at, uint8_t **out, size_t *out_size)
{
    static const char damaged[] = "The installer is damaged: download it again";
    uint8_t head[9];
    if (read_at(fd, *at, head, sizeof(head)) != 0 || crc32(0, head + 4, 5) != le32(head))
        return fail(inno, damaged);
    const uint32_t stored = le32(head + 4);
    uint8_t *raw = malloc(stored ? stored : 1);
    if (raw == NULL || read_at(fd, *at + sizeof(head), raw, stored) != 0)
    {
        free(raw);
        return fail(inno, damaged);
    }
    *at += sizeof(head) + stored;

    size_t length = 0;
    for (size_t i = 0; i < stored;)
    {
        const size_t piece = stored - i < 4 ? 0 : stored - i - 4 < 4096 ? stored - i - 4 : 4096;
        if (piece == 0 || crc32(0, raw + i + 4, (uInt)piece) != le32(raw + i))
        {
            free(raw);
            return fail(inno, damaged);
        }
        memmove(raw + length, raw + i + 4, piece);
        length += piece;
        i += 4 + piece;
    }
    if (!head[8])
    {
        *out = raw;
        *out_size = length;
        return 0;
    }

    /* LZMA1: the properties byte and the dictionary size, then a raw stream. */
    lzma_options_lzma options;
    memset(&options, 0, sizeof(options));
    lzma_filter filters[2] = { { LZMA_FILTER_LZMA1, &options }, { LZMA_VLI_UNKNOWN, NULL } };
    lzma_stream stream = LZMA_STREAM_INIT;
    size_t capacity = 1 << 20, produced = 0;
    uint8_t *decoded = length >= 5 && raw[0] < 9 * 5 * 5 ? malloc(capacity) : NULL;
    if (decoded != NULL)
    {
        options.lc = raw[0] % 9, options.lp = raw[0] / 9 % 5, options.pb = raw[0] / 45;
        options.dict_size = le32(raw + 1);
    }
    if (decoded == NULL || lzma_raw_decoder(&stream, filters) != LZMA_OK)
    {
        free(raw);
        free(decoded);
        return fail(inno, damaged);
    }
    stream.next_in = raw + 5;
    stream.avail_in = length - 5;
    lzma_ret result = LZMA_OK;
    while (result == LZMA_OK)
    {
        if (produced == capacity)
        {
            uint8_t *bigger = realloc(decoded, capacity * 2);
            if (bigger == NULL)
                break;
            decoded = bigger;
            capacity *= 2;
        }
        stream.next_out = decoded + produced;
        stream.avail_out = capacity - produced;
        result = lzma_code(&stream, LZMA_RUN);
        const size_t before = produced;
        produced = capacity - stream.avail_out;
        /* Without an end marker, the stream ends where its input does. */
        if (result == LZMA_OK && stream.avail_in == 0 && produced == before)
            result = LZMA_STREAM_END;
    }
    lzma_end(&stream);
    free(raw);
    if (result != LZMA_STREAM_END)
    {
        free(decoded);
        return fail(inno, damaged);
    }
    *out = decoded;
    *out_size = produced;
    return 0;
}

/* The main header: only the counts of the entries after it, the slices per disk and the compression
 * are kept. */
typedef struct
{
    uint32_t languages, messages, permissions, types, components, tasks, directories, files, data;
} counts_t;

static void read_header(inno_t *inno, reader_t *r, counts_t *counts)
{
    const uint32_t v = inno->version;
    /* The application's names, publisher, addresses and version, folders, uninstaller settings
     * and so on, up to the close-applications filter. */
    skip_strings(r, 27);
    if (v >= VERSION(5, 5, 6, 0))
        skip_strings(r, 1); /* setup mutex */
    if (v >= VERSION(5, 6, 1, 0))
        skip_strings(r, 2); /* changes environment and associations */
    if (v >= VERSION(6, 3, 0, 0))
        skip_strings(r, 2); /* architectures allowed and installed in 64-bit mode */
    skip_strings(r, 4);     /* licence, information before and after, compiled code */

    counts->languages = (uint32_t)number(r, 4);
    counts->messages = (uint32_t)number(r, 4);
    counts->permissions = (uint32_t)number(r, 4);
    counts->types = (uint32_t)number(r, 4);
    counts->components = (uint32_t)number(r, 4);
    counts->tasks = (uint32_t)number(r, 4);
    counts->directories = (uint32_t)number(r, 4);
    counts->files = (uint32_t)number(r, 4);
    counts->data = (uint32_t)number(r, 4);
    skip(r, 4 * 7); /* icons, ini, registry, delete, uninstall delete, run, uninstall run */

    skip(r, WINDOWS_VERSIONS);
    if (v < VERSION(6, 4, 0, 1))
        skip(r, 8); /* background colours */
    if (v < VERSION(5, 5, 7, 0))
        skip(r, 4); /* image background colour */
    if (v >= VERSION(6, 0, 0, 0))
        skip(r, 9); /* wizard style and size */
    if (v >= VERSION(5, 5, 7, 0))
        skip(r, 1); /* image alpha format */
    skip(r, v >= VERSION(6, 4, 0, 0) ? 4 + 44 : 20 + 8); /* password hash and salt */
    skip(r, 8);                                          /* extra disk space */
    inno->slices_per_disk = (uint32_t)number(r, 4);
    skip(r, 3); /* uninstall log mode, directory exists warning, privileges required */
    if (v >= VERSION(5, 7, 0, 0))
        skip_flags(r, 2); /* privileges overrides */
    skip(r, 2);           /* language dialog and detection */
    inno->compression = (int)number(r, 1);
    if (v < VERSION(6, 3, 0, 0))
        skip(r, 2); /* architectures */
    skip(r, 2);     /* directory and program group pages */
    skip(r, 8);     /* uninstall display size */

    int options = 45; /* as of 5.6.1 */
    if (v < VERSION(5, 6, 1, 0))
        options += 2; /* changes associations and environment, strings since */
    if (v < VERSION(5, 5, 7, 0))
        options -= 1; /* force close applications */
    if (v >= VERSION(6, 0, 0, 0))
        options += 3; /* application name has constants, previous privileges, wizard resizable */
    if (v >= VERSION(6, 3, 0, 0))
        options += 1; /* uninstall logging */
    if (v >= VERSION(6, 4, 0, 1))
        options -= 5; /* window and background flags */
    skip_flags(r, options);
}

/* The entries before the files, which are only skipped. */
static void skip_entries(reader_t *r, const counts_t *counts)
{
    for (uint32_t i = 0; i < counts->languages && !r->short_read; i++)
    {
        skip_strings(r, 10); /* names, fonts, data, licence and information texts */
        skip(r, 4 + 16 + 1); /* language, font sizes, right to left */
    }
    for (uint32_t i = 0; i < counts->messages && !r->short_read; i++)
    {
        skip_strings(r, 2);
        skip(r, 4); /* language */
    }
    for (uint32_t i = 0; i < counts->permissions && !r->short_read; i++)
        skip_strings(r, 1);
    for (uint32_t i = 0; i < counts->types && !r->short_read; i++)
    {
        skip_strings(r, 4); /* name, description, languages, check */
        skip(r, WINDOWS_VERSIONS);
        skip_flags(r, 1);
        skip(r, 1 + 8); /* type, size */
    }
    for (uint32_t i = 0; i < counts->components && !r->short_read; i++)
    {
        skip_strings(r, 5);                     /* name, description, types, languages, check */
        skip(r, 8 + 4 + 1 + WINDOWS_VERSIONS); /* extra disk space, level, used */
        skip_flags(r, 5);
        skip(r, 8); /* size */
    }
    for (uint32_t i = 0; i < counts->tasks && !r->short_read; i++)
    {
        skip_strings(r, 6); /* name, descriptions, components, languages, check */
        skip(r, 4 + 1 + WINDOWS_VERSIONS);
        skip_flags(r, 5);
    }
    for (uint32_t i = 0; i < counts->directories && !r->short_read; i++)
    {
        skip_strings(r, 1 + 6);                /* name, conditions */
        skip(r, 4 + WINDOWS_VERSIONS + 2);     /* attributes, permission */
        skip_flags(r, 5);
    }
}

/* GOG Galaxy installers keep each file in parts below {tmp}, named after their MD5, and give the
 * file's own name in calls to their install script: before_install('<MD5>', '<path>', <parts>) on
 * the first part's entry, after_install('<MD5>', <size>, <inflated size>) on each part's (both also
 * as name_dependency). Copies the arguments of a call to name in code and returns how many there
 * are, or 0 if code is no such call. */
static int script_call(const char *code, const char *name, char arguments[][PATH_SIZE], int most)
{
    const char *at = code;
    const size_t length = strlen(name);
    while (isspace((unsigned char)*at))
        at++;
    if (strncmp(at, name, length) != 0)
        return 0;
    at += length;
    if (strncmp(at, "_dependency", 11) == 0)
        at += 11;
    while (isspace((unsigned char)*at))
        at++;
    if (*at++ != '(')
        return 0;
    for (int count = 0; count < most;)
    {
        char *out = arguments[count];
        size_t n = 0;
        while (isspace((unsigned char)*at))
            at++;
        if (*at == '\'')
        {
            /* A quoted string, in which '' is a quote. */
            for (at++;; at++)
            {
                if (*at == '\0')
                    return 0;
                if (*at == '\'' && at[1] != '\'')
                {
                    at++;
                    break;
                }
                if (*at == '\'')
                    at++;
                if (n + 1 < PATH_SIZE)
                    out[n++] = *at;
            }
        }
        else
        {
            for (; *at != '\0' && !isspace((unsigned char)*at) && *at != ',' && *at != ')'; at++)
                if (n + 1 < PATH_SIZE)
                    out[n++] = *at;
        }
        out[n] = '\0';
        count++;
        while (isspace((unsigned char)*at))
            at++;
        if (*at == ')')
            return count;
        if (*at++ != ',')
            return 0;
    }
    return most;
}

/* A file installed at path, if target() takes it: paths below {app}, or GOG Galaxy's, which are
 * already inside the installation folder. */
static output_t *add_output(inno_t *inno, const char *path, uint32_t parts, int galaxy)
{
    char inside[PATH_SIZE];
    snprintf(inside, sizeof(inside), "%s", path);
    for (char *at = inside; *at != '\0'; at++)
        if (*at == '\\')
            *at = '/';
    const char *start = inside;
    if (strncasecmp(start, "{app}/", 6) == 0)
        start += 6;
    else if (start[0] == '{')
        return NULL; /* {tmp}, {commonappdata} and the like */
    output_t *output = &inno->outputs[inno->output_count];
    if (inno->callbacks->target(output->path, sizeof(output->path), start) != 0)
        return NULL;
    output->parts = parts;
    output->written = 0;
    output->galaxy = galaxy;
    output->fd = -1;
    inno->output_count++;
    return output;
}

static void add_piece(inno_t *inno, uint32_t data, const output_t *output, uint32_t part)
{
    piece_t *piece = &inno->pieces[inno->piece_count++];
    piece->data = data;
    piece->output = (uint32_t)(output - inno->outputs);
    piece->part = part;
}

/* The file entries: the files target() takes, and their parts. */
static int read_files(inno_t *inno, reader_t *r, uint32_t count)
{
    inno->outputs = calloc(count ? count : 1, sizeof(*inno->outputs));
    inno->pieces = calloc(count ? count : 1, sizeof(*inno->pieces));
    if (inno->outputs == NULL || inno->pieces == NULL)
        return fail(inno, "Not enough memory to read the installer");
    const output_t *galaxy = NULL; /* the GOG Galaxy file whose parts come next */
    uint32_t remaining = 0;
    for (uint32_t i = 0; i < count && !r->short_read; i++)
    {
        const uint8_t *text[3];
        uint32_t lengths[3];
        skip_strings(r, 1);                  /* source */
        text[0] = string(r, &lengths[0]);    /* destination */
        skip_strings(r, 2 + 4);              /* font and assembly names; components, tasks, languages, check */
        text[1] = string(r, &lengths[1]);    /* after install */
        text[2] = string(r, &lengths[2]);    /* before install */
        skip(r, WINDOWS_VERSIONS);
        const uint32_t location = (uint32_t)number(r, 4);
        skip(r, 4 + 8 + 2); /* attributes, external size, permission */
        skip_flags(r, 32);
        skip(r, 1); /* type */

        char destination[PATH_SIZE], after[PATH_SIZE], before[PATH_SIZE], arguments[3][PATH_SIZE];
        if (r->short_read || utf8_from_utf16(destination, sizeof(destination), text[0], lengths[0]) != 0 ||
            utf8_from_utf16(after, sizeof(after), text[1], lengths[1]) != 0 ||
            utf8_from_utf16(before, sizeof(before), text[2], lengths[2]) != 0)
            continue;
        const int started = script_call(before, "before_install", arguments, 3);
        if (started >= 2)
        {
            const long parts = started == 3 ? atol(arguments[2]) : 1;
            remaining = parts > 0 ? (uint32_t)parts : 1;
            galaxy = add_output(inno, arguments[1], remaining, 1);
        }
        if (script_call(after, "after_install", arguments, 3) > 0 && remaining > 0)
        {
            if (galaxy != NULL)
                add_piece(inno, location, galaxy, galaxy->parts - remaining);
            remaining--;
        }
        else
        {
            const output_t *output = add_output(inno, destination, 1, 0);
            if (output != NULL)
                add_piece(inno, location, output, 0);
            remaining = 0;
        }
    }
    return 0;
}

static void read_data_entries(inno_t *inno, reader_t *r)
{
    const uint32_t v = inno->version;
    for (uint32_t i = 0; i < inno->data_count && !r->short_read; i++)
    {
        data_entry_t *d = &inno->data[i];
        d->first_slice = (uint32_t)number(r, 4);
        skip(r, 4); /* last slice */
        d->chunk_offset = (uint32_t)number(r, 4);
        d->file_offset = number(r, 8);
        d->file_size = number(r, 8);
        d->chunk_size = number(r, 8);
        if (v >= VERSION(6, 4, 0, 0))
            skip(r, 32); /* SHA-256, which this reader does not check */
        else
        {
            for (int b = 0; b < 20; b++)
                d->sha1[b] = (uint8_t)number(r, 1);
            d->has_sha1 = 1;
        }
        skip(r, 8 + 8); /* time stamp, file version */
        /* 9 flags, or 11 from 5.5.7 to 6.3, both in 2 bytes: version info valid and not valid,
         * time stamp in UTC, uninstaller, call instruction optimized, touch, chunk encrypted, chunk
         * compressed, solid break, sign, sign once */
        const uint32_t bits = (uint32_t)number(r, 2);
        d->filtered = bits >> 4 & 1;
        d->encrypted = bits >> 6 & 1;
        d->compressed = bits >> 7 & 1;
        if (v >= VERSION(6, 3, 0, 0))
            skip(r, 1); /* sign mode */
    }
}

static int read_headers(inno_t *inno, int fd)
{
    uint32_t header_offset = 0;
    uint8_t id[64];
    int unicode;
    if (find_headers(inno, fd, &header_offset) != 0)
        return -1;
    if (read_at(fd, header_offset, id, sizeof(id)) != 0 || read_version(id, &inno->version, &unicode) != 0)
        return fail(inno, "This is not an installer OpenRCT2 can read: send the setup .exe from GOG");
    if (!unicode || inno->version < OLDEST || inno->version > NEWEST)
        return fail(inno, "This installer was made with Inno Setup %u.%u.%u%s, which OpenRCT2 cannot read",
                    inno->version >> 24, inno->version >> 16 & 255, inno->version >> 8 & 255,
                    unicode ? "" : " (not Unicode)");

    uint64_t at = header_offset + sizeof(id);
    uint8_t *block = NULL;
    size_t size;
    counts_t counts;
    if (read_block(inno, fd, &at, &block, &size) != 0)
        return -1;
    reader_t r = { block, block + size, 0 };
    read_header(inno, &r, &counts);
    skip_entries(&r, &counts);
    int result = r.short_read ? -1 : read_files(inno, &r, counts.files);
    const int short_read = r.short_read;
    free(block);
    if (result != 0 || short_read)
        return fail(inno, "The installer's list of files cannot be read");

    inno->data_count = counts.data;
    inno->data = calloc(counts.data ? counts.data : 1, sizeof(*inno->data));
    if (inno->data == NULL || read_block(inno, fd, &at, &block, &size) != 0)
        return fail(inno, "Not enough memory to read the installer");
    r = (reader_t){ block, block + size, 0 };
    read_data_entries(inno, &r);
    free(block);
    if (r.short_read)
        return fail(inno, "The installer's list of files cannot be read");

    for (size_t i = 0; i < inno->piece_count; i++)
    {
        piece_t *piece = &inno->pieces[i];
        if (piece->data >= inno->data_count)
            return fail(inno, "The installer's list of files cannot be read");
        const data_entry_t *d = &inno->data[piece->data];
        piece->slice = d->first_slice;
        piece->chunk = d->chunk_offset;
        piece->offset = d->file_offset;
        inno->total += (long long)d->file_size;
    }
    return 0;
}

/* The data: .bin slices, chunks and files. */

typedef struct
{
    inno_t *inno;
    int fd, slice;
    uint64_t at, end; /* the read position and the end of the data, in the open file */
} slices_t;

/* Slice n is <setup name>-<n + 1>.bin, or with letters when a disk has several. */
static void slice_name(const inno_t *inno, uint32_t slice, char *out, size_t size)
{
    const char *dot = strrchr(inno->setup, '.');
    const int stem = dot != NULL && strcasecmp(dot, ".exe") == 0 ? (int)(dot - inno->setup) : (int)strlen(inno->setup);
    if (inno->slices_per_disk <= 1)
        snprintf(out, size, "%.*s-%u.bin", stem, inno->setup, slice + 1);
    else
        snprintf(out, size, "%.*s-%u%c.bin", stem, inno->setup, slice / inno->slices_per_disk + 1,
                 'a' + (int)(slice % inno->slices_per_disk));
}

/* The slice under another name next to the .exe, should the .exe have been renamed. */
static int find_slice(const char *wanted, char *out, size_t size)
{
    const char *slash = strrchr(wanted, '/');
    const char *dash = strrchr(wanted, '-');
    if (slash == NULL || dash == NULL || dash < slash)
        return -1;
    char folder[PATH_SIZE];
    snprintf(folder, sizeof(folder), "%.*s", (int)(slash - wanted), wanted);
    const size_t suffix = strlen(dash);
    DIR *dir = opendir(folder);
    if (dir == NULL)
        return -1;
    int found = -1;
    for (struct dirent *entry; found != 0 && (entry = readdir(dir)) != NULL;)
    {
        const size_t length = strlen(entry->d_name);
        if (length > suffix && strcasecmp(entry->d_name + length - suffix, dash) == 0 &&
            snprintf(out, size, "%s/%s", folder, entry->d_name) < (int)size)
            found = 0;
    }
    closedir(dir);
    return found;
}

static int open_slice(slices_t *s, uint32_t slice)
{
    inno_t *inno = s->inno;
    if (s->fd >= 0 && s->slice == (int)slice)
        return 0;
    if (s->fd >= 0)
        close(s->fd);
    s->fd = -1;
    struct stat info;

    if (inno->data_offset != 0)
    {
        /* The data follows the headers in the .exe. */
        s->fd = slice == 0 ? open(inno->setup, O_RDONLY) : -1;
        if (s->fd < 0 || fstat(s->fd, &info) != 0)
            return fail(inno, "The installer is damaged: download it again");
        s->slice = 0;
        s->end = (uint64_t)info.st_size;
        s->at = inno->data_offset;
        return 0;
    }

    char path[PATH_SIZE], other[PATH_SIZE];
    slice_name(inno, slice, path, sizeof(path));
    s->fd = open(path, O_RDONLY);
    if (s->fd < 0 && find_slice(path, other, sizeof(other)) == 0)
        s->fd = open(other, O_RDONLY);
    if (s->fd < 0)
    {
        const char *name = strrchr(path, '/');
        inno->missing_part = 1;
        return fail(inno, "The installer's data file %s is missing: send it too", name != NULL ? name + 1 : path);
    }
    /* "idska32" or "idska16" and 0x1a, then the slice's size. */
    uint8_t head[12];
    if (fstat(s->fd, &info) != 0 || read_at(s->fd, 0, head, sizeof(head)) != 0 ||
        memcmp(head, "idska", 5) != 0 || head[7] != 0x1a || le32(head + 8) > (uint64_t)info.st_size ||
        le32(head + 8) < sizeof(head))
        return fail(inno, "The installer's data file %u is damaged: download it again", slice + 1);
    s->slice = (int)slice;
    s->end = le32(head + 8);
    s->at = sizeof(head);
    return 0;
}

static int seek_slice(slices_t *s, uint32_t slice, uint64_t offset)
{
    if (open_slice(s, slice) != 0)
        return -1;
    s->at = offset + s->inno->data_offset;
    if (s->at > s->end || lseek(s->fd, (off_t)s->at, SEEK_SET) < 0)
        return fail(s->inno, "The installer is damaged: download it again");
    return 0;
}

/* Reads on, into the next slice where one ends. */
static int read_slices(slices_t *s, uint8_t *buffer, size_t size)
{
    while (size > 0)
    {
        if (s->at >= s->end)
        {
            if (open_slice(s, (uint32_t)s->slice + 1) != 0 || lseek(s->fd, (off_t)s->at, SEEK_SET) < 0)
                return -1;
            continue;
        }
        const size_t wanted = s->end - s->at < size ? (size_t)(s->end - s->at) : size;
        const ssize_t got = read(s->fd, buffer, wanted);
        if (got <= 0)
            return fail(s->inno, "The installer's data file %d cannot be read", s->slice + 1);
        s->at += (uint64_t)got;
        buffer += got;
        size -= (size_t)got;
    }
    return 0;
}

/* A chunk: "zlb" and 0x1a, then compressed data that holds one file after another. */
typedef struct
{
    int method, ended;
    z_stream zlib;
    lzma_stream lzma;
    uint64_t left; /* compressed bytes not read yet */
    uint8_t in[BUFFER];
    size_t in_at, in_size;
} chunk_t;

static int refill(slices_t *s, chunk_t *c)
{
    if (c->in_at < c->in_size || c->left == 0)
        return 0;
    const size_t size = c->left < BUFFER ? (size_t)c->left : BUFFER;
    if (read_slices(s, c->in, size) != 0)
        return -1;
    c->in_at = 0;
    c->in_size = size;
    c->left -= size;
    return 0;
}

static int take(slices_t *s, chunk_t *c, uint8_t *out, size_t size)
{
    while (size > 0)
    {
        if (refill(s, c) != 0)
            return -1;
        if (c->in_at == c->in_size)
            return fail(s->inno, "The installer is damaged: download it again");
        const size_t n = c->in_size - c->in_at < size ? c->in_size - c->in_at : size;
        memcpy(out, c->in + c->in_at, n);
        c->in_at += n, out += n, size -= n;
    }
    return 0;
}

static int start_chunk(slices_t *s, chunk_t *c, const data_entry_t *d)
{
    inno_t *inno = s->inno;
    uint8_t magic[4];
    memset(c, 0, offsetof(chunk_t, in));
    c->in_at = c->in_size = 0;
    c->lzma = (lzma_stream)LZMA_STREAM_INIT;
    c->method = d->compressed ? inno->compression : STORED;
    if (d->encrypted)
        return fail(inno, "The installer is protected with a password, which OpenRCT2 cannot open");
    if (c->method != STORED && c->method != ZLIB && c->method != LZMA1 && c->method != LZMA2)
        return fail(inno, "The installer is compressed in a way OpenRCT2 cannot unpack (bzip2)");
    if (seek_slice(s, d->first_slice, d->chunk_offset) != 0)
        return -1;
    c->left = 4;
    if (take(s, c, magic, 4) != 0 || memcmp(magic, "zlb\x1a", 4) != 0)
        return fail(inno, "The installer is damaged: download it again");
    c->left = d->chunk_size;

    if (c->method == ZLIB)
    {
        if (inflateInit(&c->zlib) != Z_OK)
            return fail(inno, "Not enough memory to unpack the installer");
        return 0;
    }
    lzma_options_lzma options;
    memset(&options, 0, sizeof(options));
    lzma_filter filters[2] = { { c->method == LZMA1 ? LZMA_FILTER_LZMA1 : LZMA_FILTER_LZMA2, &options },
                               { LZMA_VLI_UNKNOWN, NULL } };
    uint8_t properties[5];
    if (c->method == LZMA1)
    {
        if (take(s, c, properties, 5) != 0 || properties[0] >= 9 * 5 * 5)
            return fail(inno, "The installer is damaged: download it again");
        options.lc = properties[0] % 9, options.lp = properties[0] / 9 % 5, options.pb = properties[0] / 45;
        options.dict_size = le32(properties + 1);
    }
    else if (c->method == LZMA2)
    {
        if (take(s, c, properties, 1) != 0 || properties[0] > 40)
            return fail(inno, "The installer is damaged: download it again");
        options.dict_size = properties[0] == 40 ? UINT32_MAX : (2u | (properties[0] & 1u)) << (properties[0] / 2 + 11);
    }
    if (c->method != STORED && lzma_raw_decoder(&c->lzma, filters) != LZMA_OK)
        return fail(inno, "Not enough memory to unpack the installer");
    return 0;
}

static void end_chunk(chunk_t *c)
{
    if (c->method == ZLIB)
        inflateEnd(&c->zlib);
    else
        lzma_end(&c->lzma);
}

/* Fills out with the next size bytes of the chunk's files. */
static int read_chunk(slices_t *s, chunk_t *c, uint8_t *out, size_t size)
{
    size_t got = 0;
    while (got < size)
    {
        if (c->ended || refill(s, c) != 0)
            return fail(s->inno, "The installer is damaged: download it again");
        const size_t available = c->in_size - c->in_at;
        size_t produced;
        if (c->method == STORED)
        {
            produced = available < size - got ? available : size - got;
            memcpy(out + got, c->in + c->in_at, produced);
            c->in_at += produced;
        }
        else if (c->method == ZLIB)
        {
            c->zlib.next_in = c->in + c->in_at;
            c->zlib.avail_in = (uInt)available;
            c->zlib.next_out = out + got;
            c->zlib.avail_out = (uInt)(size - got);
            const int result = inflate(&c->zlib, Z_NO_FLUSH);
            c->in_at = c->in_size - c->zlib.avail_in;
            produced = size - got - c->zlib.avail_out;
            if (result == Z_STREAM_END)
                c->ended = 1;
            else if (result != Z_OK && result != Z_BUF_ERROR)
                return fail(s->inno, "The installer is damaged: download it again");
        }
        else
        {
            c->lzma.next_in = c->in + c->in_at;
            c->lzma.avail_in = available;
            c->lzma.next_out = out + got;
            c->lzma.avail_out = size - got;
            const lzma_ret result = lzma_code(&c->lzma, LZMA_RUN);
            c->in_at = c->in_size - c->lzma.avail_in;
            produced = size - got - c->lzma.avail_out;
            if (result == LZMA_STREAM_END)
                c->ended = 1;
            else if (result != LZMA_OK)
                return fail(s->inno, "The installer is damaged: download it again");
        }
        got += produced;
        /* All input used up and nothing more out: the chunk is shorter than its files. */
        if (produced == 0 && c->in_at == c->in_size && c->left == 0)
            c->ended = 1;
    }
    return 0;
}

/* Creates the folders above path, unless they are the previous file's: file system calls are slow
 * in a title's folder on the console, and a game folder has thousands of files. */
static void make_parent_folders(inno_t *inno, char *path)
{
    const char *slash = strrchr(path, '/');
    const size_t length = slash != NULL ? (size_t)(slash - path) : 0;
    if (length == 0 || (strlen(inno->made) == length && strncmp(inno->made, path, length) == 0))
        return;
    for (char *at = strchr(path + 1, '/'); at != NULL; at = strchr(at + 1, '/'))
    {
        *at = '\0';
        mkdir(path, 0755);
        *at = '/';
    }
    snprintf(inno->made, sizeof(inno->made), "%.*s", (int)length, path);
}

static int write_all(int fd, const uint8_t *data, size_t size)
{
    while (size > 0)
    {
        const ssize_t written = write(fd, data, size);
        if (written <= 0)
            return -1;
        data += written;
        size -= (size_t)written;
    }
    return 0;
}

static void target_path(const inno_t *inno, const output_t *output, char *out, size_t size)
{
    snprintf(out, size, "%s/%s", inno->folder, output->path);
}

/* Writes data to the files of pieces [first, last). */
static int write_outputs(inno_t *inno, size_t first, size_t last, const uint8_t *data, size_t size)
{
    for (size_t i = first; i < last; i++)
    {
        const output_t *output = &inno->outputs[inno->pieces[i].output];
        if (write_all(output->fd, data, size) != 0)
        {
            if (errno == ENOSPC)
                return fail(inno, "Not enough space on the console");
            return fail(inno, "Writing %s on the console failed", output->path);
        }
    }
    return 0;
}

/* Inflates a GOG Galaxy part's bytes into its files. */
static int inflate_outputs(inno_t *inno, z_stream *z, int *ended, const uint8_t *in, size_t size, uint8_t *out,
                           size_t first, size_t last)
{
    if (*ended)
        return size > 0 ? fail(inno, "The installer is damaged: download it again") : 0;
    z->next_in = (Bytef *)in;
    z->avail_in = (uInt)size;
    for (;;)
    {
        z->next_out = out;
        z->avail_out = BUFFER;
        const int result = inflate(z, Z_NO_FLUSH);
        const size_t produced = BUFFER - z->avail_out;
        if (result != Z_OK && result != Z_STREAM_END && result != Z_BUF_ERROR)
            return fail(inno, "The installer is damaged: download it again");
        if (produced > 0 && write_outputs(inno, first, last, out, produced) != 0)
            return -1;
        if (result == Z_STREAM_END)
        {
            *ended = 1;
            return z->avail_in == 0 ? 0 : fail(inno, "The installer is damaged: download it again");
        }
        if (z->avail_in == 0 && z->avail_out != 0)
            return 0;
        if (produced == 0)
            return fail(inno, "The installer is damaged: download it again");
    }
}

/* Writes the data entry that pieces [first, last) share into each of their files (inflated for GOG
 * Galaxy parts), and finishes the files whose last part it is. */
static int write_pieces(slices_t *s, chunk_t *c, uint64_t *position, size_t first, size_t last, uint8_t *buffer,
                        uint8_t *inflated)
{
    inno_t *inno = s->inno;
    const data_entry_t *d = &inno->data[inno->pieces[first].data];
    const int galaxy = inno->outputs[inno->pieces[first].output].galaxy;
    int result = 0, ended = 0;
    z_stream z;
    memset(&z, 0, sizeof(z));

    if (d->filtered)
        result = fail(inno, "%s is stored in a way OpenRCT2 cannot unpack", inno->outputs[inno->pieces[first].output].path);
    else if (d->file_offset < *position)
        result = fail(inno, "The installer is damaged: download it again");
    for (size_t i = first; result == 0 && i < last; i++)
    {
        output_t *output = &inno->outputs[inno->pieces[i].output];
        char path[PATH_SIZE * 2];
        if (inno->pieces[i].part != output->written || output->galaxy != galaxy)
            result = fail(inno, "The installer keeps %s in an order OpenRCT2 cannot follow", output->path);
        else if (output->fd < 0)
        {
            target_path(inno, output, path, sizeof(path));
            make_parent_folders(inno, path);
            output->fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (output->fd < 0)
                result = fail(inno, "Writing %s on the console failed", output->path);
        }
    }
    if (result == 0 && galaxy && inflateInit(&z) != Z_OK)
        result = fail(inno, "Not enough memory to unpack the installer");

    while (result == 0 && *position < d->file_offset)
    {
        const size_t size = d->file_offset - *position < BUFFER ? (size_t)(d->file_offset - *position) : BUFFER;
        result = read_chunk(s, c, buffer, size);
        *position += size;
    }
    sha1_t sha1;
    sha1_start(&sha1);
    for (uint64_t left = d->file_size; result == 0 && left > 0;)
    {
        const size_t size = left < BUFFER ? (size_t)left : BUFFER;
        if ((result = read_chunk(s, c, buffer, size)) != 0)
            break;
        sha1_add(&sha1, buffer, size);
        result = galaxy ? inflate_outputs(inno, &z, &ended, buffer, size, inflated, first, last)
                        : write_outputs(inno, first, last, buffer, size);
        left -= size;
        *position += size;
        inno->done += (long long)(size * (last - first));
        if (result == 0 && inno->callbacks->progress != NULL &&
            inno->callbacks->progress(inno->done, inno->total, inno->callbacks->user) != 0)
            result = fail(inno, "Stopped");
    }
    if (galaxy)
        inflateEnd(&z);
    uint8_t digest[20];
    sha1_end(&sha1, digest);
    if (result == 0 && ((d->has_sha1 && memcmp(digest, d->sha1, sizeof(digest)) != 0) || (galaxy && !ended)))
        result = fail(inno, "%s came out damaged: download the installer again",
                      inno->outputs[inno->pieces[first].output].path);

    for (size_t i = first; result == 0 && i < last; i++)
    {
        output_t *output = &inno->outputs[inno->pieces[i].output];
        if (++output->written < output->parts)
            continue;
        close(output->fd);
        output->fd = -1;
        inno->files++;
    }
    return result;
}

static int by_place(const void *a, const void *b)
{
    const piece_t *x = a, *y = b;
    if (x->slice != y->slice)
        return x->slice < y->slice ? -1 : 1;
    if (x->chunk != y->chunk)
        return x->chunk < y->chunk ? -1 : 1;
    if (x->offset != y->offset)
        return x->offset < y->offset ? -1 : 1;
    if (x->data != y->data)
        return x->data < y->data ? -1 : 1;
    return x->part < y->part ? -1 : x->part > y->part;
}

/* Each chunk is read once, front to back, writing the pieces in it on the way. */
static int write_files(inno_t *inno)
{
    slices_t slices = { inno, -1, -1, 0, 0 };
    chunk_t *chunk = malloc(sizeof(*chunk));
    uint8_t *buffer = malloc(BUFFER), *inflated = malloc(BUFFER);
    int result = chunk != NULL && buffer != NULL && inflated != NULL
                     ? 0
                     : fail(inno, "Not enough memory to unpack the installer");

    qsort(inno->pieces, inno->piece_count, sizeof(*inno->pieces), by_place);
    for (size_t i = 0; result == 0 && i < inno->piece_count;)
    {
        const piece_t *start = &inno->pieces[i];
        result = start_chunk(&slices, chunk, &inno->data[start->data]);
        uint64_t position = 0;
        while (result == 0 && i < inno->piece_count && inno->pieces[i].slice == start->slice &&
               inno->pieces[i].chunk == start->chunk)
        {
            size_t last = i + 1;
            while (last < inno->piece_count && inno->pieces[last].data == inno->pieces[i].data)
                last++;
            result = write_pieces(&slices, chunk, &position, i, last, buffer, inflated);
            i = last;
        }
        end_chunk(chunk);
    }
    for (size_t i = 0; i < inno->output_count; i++)
    {
        output_t *output = &inno->outputs[i];
        if (result == 0 && output->written < output->parts)
            result = fail(inno, "The installer is damaged: part of %s is missing", output->path);
        if (output->fd >= 0)
        {
            /* Left unfinished by a failure. */
            char path[PATH_SIZE * 2];
            close(output->fd);
            target_path(inno, output, path, sizeof(path));
            unlink(path);
        }
    }
    if (slices.fd >= 0)
        close(slices.fd);
    free(chunk);
    free(buffer);
    free(inflated);
    return result;
}

int inno_unpack(const char *setup, const char *folder, const inno_callbacks_t *callbacks, int *files, char *error,
                size_t error_size)
{
    inno_t inno;
    memset(&inno, 0, sizeof(inno));
    inno.setup = setup;
    inno.folder = folder;
    inno.callbacks = callbacks;
    inno.error = error;
    inno.error_size = error_size;
    error[0] = '\0';
    *files = 0;

    const int fd = open(setup, O_RDONLY);
    int result = fd >= 0 ? read_headers(&inno, fd) : fail(&inno, "The installer cannot be read");
    if (fd >= 0)
        close(fd);
    if (result == 0 && inno.output_count == 0)
        result = fail(&inno, "The installer has none of the game's files: is it RollerCoaster Tycoon 2?");
    if (result == 0)
        result = write_files(&inno);
    *files = inno.files;
    free(inno.data);
    free(inno.outputs);
    free(inno.pieces);
    return result == 0 ? 0 : inno.missing_part ? INNO_MISSING_PART : -1;
}
