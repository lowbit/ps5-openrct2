/* realpath that works in the sandbox. The libc's fails with EPERM for every path, which also
 * breaks libc++'s canonical, weakly_canonical and relative (OpenRCT2's file scanner names files
 * through relative). This one resolves the path component by component: ".", ".." and repeated
 * slashes are folded, symbolic links are followed with readlink, and lstat checks that each
 * component exists (stat where the sandbox refuses lstat, which then cannot see links). */
#include <errno.h>
#include <limits.h>
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
