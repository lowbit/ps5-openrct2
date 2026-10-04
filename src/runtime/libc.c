/* C library functions the native title's libc (libSceLibcInternal) lacks but libc++ and the
 * game's libraries call. The title only ever runs in the "C" locale, so each *_l variant
 * forwards to its plain counterpart. A few POSIX calls exist only in the WebKit POSIX module,
 * which a game process does not load; they are implemented here. */
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <limits.h>
#include <locale.h>
#include <pthread.h>
#include <pwd.h>
#include <runetype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>
#include <xlocale.h>

int _Getmbcurmax(void);

/* ---- locale ---------------------------------------------------------------------------------- */

_RuneLocale *__runes_for_locale(locale_t locale, int *limit)
{
    (void)locale;
    *limit = 256;
    return (_RuneLocale *)&_DefaultRuneLocale;
}

int ___mb_cur_max_l(locale_t locale)
{
    (void)locale;
    return _Getmbcurmax();
}

int freelocale(locale_t locale)
{
    (void)locale;
    return 0;
}

double strtod_l(const char *string, char **end, locale_t locale)
{
    (void)locale;
    return strtod(string, end);
}

float strtof_l(const char *string, char **end, locale_t locale)
{
    (void)locale;
    return strtof(string, end);
}

long double strtold_l(const char *string, char **end, locale_t locale)
{
    (void)locale;
    return strtold(string, end);
}

long long strtoll_l(const char *string, char **end, int base, locale_t locale)
{
    (void)locale;
    return strtoll(string, end, base);
}

unsigned long long strtoull_l(const char *string, char **end, int base, locale_t locale)
{
    (void)locale;
    return strtoull(string, end, base);
}

size_t strftime_l(char *out, size_t size, const char *format, const struct tm *time, locale_t locale)
{
    (void)locale;
    return strftime(out, size, format, time);
}

int snprintf_l(char *out, size_t size, locale_t locale, const char *format, ...)
{
    (void)locale;
    va_list args;
    va_start(args, format);
    int result = vsnprintf(out, size, format, args);
    va_end(args);
    return result;
}

int asprintf_l(char **out, locale_t locale, const char *format, ...)
{
    (void)locale;
    va_list args;
    va_start(args, format);
    int result = vasprintf(out, format, args);
    va_end(args);
    return result;
}

int sscanf_l(const char *string, locale_t locale, const char *format, ...)
{
    (void)locale;
    va_list args;
    va_start(args, format);
    int result = vsscanf(string, format, args);
    va_end(args);
    return result;
}

size_t mbrlen_l(const char *string, size_t size, mbstate_t *state, locale_t locale)
{
    (void)locale;
    return mbrlen(string, size, state);
}

size_t mbrtowc_l(wchar_t *out, const char *string, size_t size, mbstate_t *state, locale_t locale)
{
    (void)locale;
    return mbrtowc(out, string, size, state);
}

int mbtowc_l(wchar_t *out, const char *string, size_t size, locale_t locale)
{
    (void)locale;
    return mbtowc(out, string, size);
}

size_t wcrtomb_l(char *out, wchar_t wide, mbstate_t *state, locale_t locale)
{
    (void)locale;
    return wcrtomb(out, wide, state);
}

size_t mbsrtowcs_l(wchar_t *out, const char **string, size_t size, mbstate_t *state, locale_t locale)
{
    (void)locale;
    return mbsrtowcs(out, string, size, state);
}

/* mbsrtowcs limited to the first `count` bytes of the source. */
size_t mbsnrtowcs(wchar_t *out, const char **string, size_t count, size_t size, mbstate_t *state)
{
    const char *at = *string;
    size_t written = 0;
    while (out == NULL || written < size)
    {
        wchar_t wide;
        size_t length = mbrtowc(&wide, at, count, state);
        if (length == (size_t)-1)
        {
            if (out != NULL)
                *string = at;
            return (size_t)-1;
        }
        if (length == (size_t)-2)
            break;
        if (length == 0)
        {
            if (out != NULL)
            {
                out[written] = L'\0';
                *string = NULL;
            }
            return written;
        }
        if (out != NULL)
            out[written] = wide;
        written++;
        at += length;
        count -= length;
    }
    if (out != NULL)
        *string = at;
    return written;
}

size_t mbsnrtowcs_l(wchar_t *out, const char **string, size_t count, size_t size, mbstate_t *state,
                    locale_t locale)
{
    (void)locale;
    return mbsnrtowcs(out, string, count, size, state);
}

/* wcsrtombs limited to the first `count` wide characters of the source. */
size_t wcsnrtombs(char *out, const wchar_t **string, size_t count, size_t size, mbstate_t *state)
{
    const wchar_t *at = *string;
    size_t written = 0;
    char buffer[MB_LEN_MAX];
    for (; count > 0; count--, at++)
    {
        size_t length = wcrtomb(buffer, *at, state);
        if (length == (size_t)-1)
        {
            if (out != NULL)
                *string = at;
            return (size_t)-1;
        }
        if (out != NULL)
        {
            if (written + length > size)
                break;
            memcpy(out + written, buffer, length);
        }
        if (*at == L'\0')
        {
            if (out != NULL)
                *string = NULL;
            return written + length - 1;
        }
        written += length;
    }
    if (out != NULL)
        *string = at;
    return written;
}

size_t wcsnrtombs_l(char *out, const wchar_t **string, size_t count, size_t size, mbstate_t *state,
                    locale_t locale)
{
    (void)locale;
    return wcsnrtombs(out, string, count, size, state);
}

/* No message catalogues: every message is the built-in default. */
typedef void *nl_catd;

nl_catd catopen(const char *name, int flags)
{
    (void)name;
    (void)flags;
    errno = ENOENT;
    return (nl_catd)-1;
}

char *catgets(nl_catd catalogue, int set, int number, const char *message)
{
    (void)catalogue;
    (void)set;
    (void)number;
    return (char *)message;
}

int catclose(nl_catd catalogue)
{
    (void)catalogue;
    return 0;
}

/* ---- time ------------------------------------------------------------------------------------ */

static pthread_mutex_t time_lock = PTHREAD_MUTEX_INITIALIZER;

static struct tm *copy_time(struct tm *(*convert)(const time_t *), const time_t *time, struct tm *out)
{
    pthread_mutex_lock(&time_lock);
    struct tm *result = convert(time);
    if (result != NULL)
        *out = *result;
    pthread_mutex_unlock(&time_lock);
    return result != NULL ? out : NULL;
}

struct tm *localtime_r(const time_t *time, struct tm *out)
{
    return copy_time(localtime, time, out);
}

struct tm *gmtime_r(const time_t *time, struct tm *out)
{
    return copy_time(gmtime, time, out);
}

/* ---- POSIX calls only the WebKit POSIX module exports ---------------------------------------- */

int isatty(int fd)
{
    (void)fd;
    errno = ENOTTY;
    return 0;
}

uint32_t arc4random(void)
{
    static _Atomic uint64_t state;
    uint64_t value = state;
    if (value == 0)
    {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        value = ((uint64_t)now.tv_sec << 32 ^ (uint64_t)now.tv_nsec ^ (uintptr_t)&now) | 1;
    }
    value ^= value << 13;
    value ^= value >> 7;
    value ^= value << 17;
    state = value;
    return (uint32_t)(value >> 16);
}

void arc4random_buf(void *buffer, size_t size)
{
    unsigned char *at = buffer;
    for (; size >= 4; size -= 4, at += 4)
    {
        uint32_t value = arc4random();
        memcpy(at, &value, 4);
    }
    if (size > 0)
    {
        uint32_t value = arc4random();
        memcpy(at, &value, size);
    }
}

/* The console has no user database. */
struct passwd *getpwuid(uid_t uid)
{
    (void)uid;
    errno = ENOENT;
    return NULL;
}

/* Shell wildcard matching: *, ? and [...] with ranges and ! or ^ negation. No flags. */
int fnmatch(const char *pattern, const char *string, int flags)
{
    (void)flags;
    for (;; pattern++, string++)
    {
        switch (*pattern)
        {
        case '\0':
            return *string == '\0' ? 0 : FNM_NOMATCH;
        case '*':
            for (const char *rest = string;; rest++)
            {
                if (fnmatch(pattern + 1, rest, flags) == 0)
                    return 0;
                if (*rest == '\0')
                    return FNM_NOMATCH;
            }
        case '?':
            if (*string == '\0')
                return FNM_NOMATCH;
            break;
        case '[':
        {
            if (*string == '\0')
                return FNM_NOMATCH;
            const char *at = pattern + 1;
            int negate = *at == '!' || *at == '^';
            at += negate;
            int matched = 0;
            do
            {
                char low = *at, high = low;
                if (at[1] == '-' && at[2] != ']' && at[2] != '\0')
                {
                    high = at[2];
                    at += 2;
                }
                matched |= *string >= low && *string <= high;
                at++;
            } while (*at != ']' && *at != '\0');
            if (*at == '\0' || matched == negate)
                return FNM_NOMATCH;
            pattern = at;
            break;
        }
        default:
            if (*pattern != *string)
                return FNM_NOMATCH;
        }
    }
}

int mkstemp(char *path)
{
    static const char letters[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    size_t length = strlen(path);
    if (length < 6 || strcmp(path + length - 6, "XXXXXX") != 0)
    {
        errno = EINVAL;
        return -1;
    }
    for (int attempt = 0; attempt < 100; attempt++)
    {
        for (size_t i = length - 6; i < length; i++)
            path[i] = letters[arc4random() % (sizeof(letters) - 1)];
        int fd = open(path, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0 || errno != EEXIST)
            return fd;
    }
    errno = EEXIST;
    return -1;
}

char *strcasestr(const char *haystack, const char *needle)
{
    size_t length = strlen(needle);
    for (; *haystack != '\0' || length == 0; haystack++)
    {
        if (strncasecmp(haystack, needle, length) == 0)
            return (char *)haystack;
    }
    return NULL;
}

/* ---- C runtime -------------------------------------------------------------------------------- */

/* Destructors of thread_local objects, run in reverse order when their thread exits. libc++abi
 * refers to this weakly, and a PS5 module cannot import a symbol nothing defines. */
struct thread_destructor
{
    void (*destroy)(void *);
    void *object;
    struct thread_destructor *next;
};

static pthread_key_t destructor_key;
static pthread_once_t destructor_once = PTHREAD_ONCE_INIT;

static void run_thread_destructors(void *list)
{
    for (struct thread_destructor *at = list; at != NULL;)
    {
        struct thread_destructor *next = at->next;
        at->destroy(at->object);
        free(at);
        at = next;
    }
}

static void create_destructor_key(void)
{
    pthread_key_create(&destructor_key, run_thread_destructors);
}

int __cxa_thread_atexit_impl(void (*destroy)(void *), void *object, void *dso)
{
    (void)dso;
    pthread_once(&destructor_once, create_destructor_key);
    struct thread_destructor *entry = malloc(sizeof(*entry));
    if (entry == NULL)
        return -1;
    entry->destroy = destroy;
    entry->object = object;
    entry->next = pthread_getspecific(destructor_key);
    pthread_setspecific(destructor_key, entry);
    return 0;
}

__attribute__((noreturn)) void __assert(const char *function, const char *file, int line,
                                        const char *expression)
{
    fprintf(stderr, "assertion failed: %s (%s:%d, %s)\n", expression, file, line, function);
    abort();
}

/* A title ends through the system service: the exit system call gets it killed with SIGSYS.
 * Whatever calls exit, returning from main included, ends here; atexit handlers and static
 * destructors do not run. */
int sceSystemServiceLoadExec(const char *path, char *const argv[]);
int sceKernelUsleep(unsigned microseconds);

__attribute__((noreturn)) void __wrap_exit(int status)
{
    (void)status;
    fflush(NULL);
    sceSystemServiceLoadExec("exit", NULL);
    for (;;)
        sceKernelUsleep(1000000);
}
