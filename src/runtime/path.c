/* realpath that works in the sandbox. The libc's fails with EPERM for every path, which also
 * breaks libc++'s canonical, weakly_canonical and relative (OpenRCT2's file scanner names files
 * through relative). This one resolves the path component by component: ".", ".." and repeated
 * slashes are folded, symbolic links are followed with readlink, and lstat checks that each
 * component exists (stat where the sandbox refuses lstat, which then cannot see links).
 * Also the file calls whose only implementation is in libkernel_sys, which a game process does
 * not load: openat, unlinkat, readlink and pathconf. */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_LINKS 32

/* Drops the last component of out (out_length bytes, "" for the root). */
static size_t parent(char *out, size_t out_length)
{
    while (out_length > 0 && out[out_length - 1] != '/')
        out_length--;
    if (out_length > 0)
        out_length--;
    out[out_length] = '\0';
    return out_length;
}

char *realpath(const char *path, char *resolved)
{
    char out[PATH_MAX];
    char pending[PATH_MAX];
    size_t out_length = 0;
    int links = 0;

    if (path == NULL)
    {
        errno = EINVAL;
        return NULL;
    }
    if (*path == '\0')
    {
        errno = ENOENT;
        return NULL;
    }
    if (path[0] != '/')
    {
        if (getcwd(out, sizeof(out)) == NULL)
            return NULL;
        out_length = strlen(out);
        if (out_length == 1)
            out_length = 0;
    }
    out[out_length] = '\0';
    if (strlen(path) >= sizeof(pending))
    {
        errno = ENAMETOOLONG;
        return NULL;
    }
    strcpy(pending, path);

    const char *at = pending;
    while (*at != '\0')
    {
        while (*at == '/')
            at++;
        size_t length = strcspn(at, "/");
        if (length == 0)
            break;
        if (length == 1 && at[0] == '.')
        {
            at += length;
            continue;
        }
        if (length == 2 && at[0] == '.' && at[1] == '.')
        {
            out_length = parent(out, out_length);
            at += length;
            continue;
        }
        if (out_length + 1 + length >= sizeof(out))
        {
            errno = ENAMETOOLONG;
            return NULL;
        }
        out[out_length++] = '/';
        memcpy(out + out_length, at, length);
        out_length += length;
        out[out_length] = '\0';
        at += length;

        struct stat info;
        if (lstat(out, &info) != 0 && (errno != EPERM || stat(out, &info) != 0))
            return NULL;
        if (S_ISLNK(info.st_mode))
        {
            char target[PATH_MAX];
            char rest[PATH_MAX];
            if (++links > MAX_LINKS)
            {
                errno = ELOOP;
                return NULL;
            }
            ssize_t target_length = readlink(out, target, sizeof(target) - 1);
            if (target_length < 0)
                return NULL;
            target[target_length] = '\0';
            if ((size_t)target_length + strlen(at) + 1 >= sizeof(rest))
            {
                errno = ENAMETOOLONG;
                return NULL;
            }
            strcpy(rest, target);
            strcat(rest, at);
            strcpy(pending, rest);
            at = pending;
            if (target[0] == '/')
                out_length = 0;
            else
                out_length = parent(out, out_length);
            out[out_length] = '\0';
        }
        else if (*at == '/' && !S_ISDIR(info.st_mode))
        {
            errno = ENOTDIR;
            return NULL;
        }
    }

    if (out_length == 0)
        strcpy(out, "/");
    if (resolved == NULL)
        return strdup(out);
    strcpy(resolved, out);
    return resolved;
}

/* ---- calls only modules a game process does not load provide ------------------------------- */

int sceKernelOpen(const char *path, int flags, int mode);

/* Folder descriptors opened through openat, with their paths, so that later *at calls relative to
   them can be turned into plain calls. libc++'s remove_all walks folders this way. */
#define MAX_FOLDERS 64

static struct
{
    int fd;
    char path[PATH_MAX];
} folders[MAX_FOLDERS];
static pthread_mutex_t folders_lock = PTHREAD_MUTEX_INITIALIZER;

static void remember_folder(int fd, const char *path)
{
    pthread_mutex_lock(&folders_lock);
    int slot = 0;
    for (int i = 0; i < MAX_FOLDERS; i++)
    {
        if (folders[i].fd == fd || folders[i].path[0] == '\0')
        {
            slot = i;
            break;
        }
    }
    folders[slot].fd = fd;
    snprintf(folders[slot].path, sizeof(folders[slot].path), "%s", path);
    pthread_mutex_unlock(&folders_lock);
}

static int resolve_at(int fd, const char *name, char *out, size_t size)
{
    if (name[0] == '/' || fd == AT_FDCWD)
    {
        snprintf(out, size, "%s", name);
        return 0;
    }
    pthread_mutex_lock(&folders_lock);
    for (int i = 0; i < MAX_FOLDERS; i++)
    {
        if (folders[i].fd == fd && folders[i].path[0] != '\0')
        {
            snprintf(out, size, "%s/%s", folders[i].path, name);
            pthread_mutex_unlock(&folders_lock);
            return 0;
        }
    }
    pthread_mutex_unlock(&folders_lock);
    errno = EBADF;
    return -1;
}

int openat(int fd, const char *name, int flags, ...)
{
    char path[PATH_MAX];
    int mode = 0;
    if (flags & O_CREAT)
    {
        va_list args;
        va_start(args, flags);
        mode = va_arg(args, int);
        va_end(args);
    }
    if (resolve_at(fd, name, path, sizeof(path)) != 0)
        return -1;
    if (!(flags & O_DIRECTORY))
        return open(path, flags, mode);
    /* The sandbox lets folders be opened only through sceKernelOpen (see dirent.c). */
    int result = sceKernelOpen(path, flags, mode);
    if (result < 0)
    {
        errno = result & 0xffff;
        return -1;
    }
    remember_folder(result, path);
    return result;
}

int unlinkat(int fd, const char *name, int flags)
{
    char path[PATH_MAX];
    if (resolve_at(fd, name, path, sizeof(path)) != 0)
        return -1;
    return (flags & AT_REMOVEDIR) ? rmdir(path) : unlink(path);
}

/* The sandbox shows no symbolic links (lstat is refused), so nothing is one. */
ssize_t readlink(const char *path, char *buffer, size_t size)
{
    (void)buffer;
    (void)size;
    struct stat info;
    if (stat(path, &info) == 0)
        errno = EINVAL;
    return -1;
}

long pathconf(const char *path, int name)
{
    (void)path;
    switch (name)
    {
    case _PC_NAME_MAX:
        return NAME_MAX;
    case _PC_PATH_MAX:
        return PATH_MAX;
    default:
        errno = EINVAL;
        return -1;
    }
}
