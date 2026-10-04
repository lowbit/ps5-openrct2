/* Folder listing that works in the sandbox. A native title may not call opendir (the libc's
 * version fails with EPERM), but it may open a folder with sceKernelOpen(O_DIRECTORY) and read
 * its records with sceKernelGetdents, which have the layout of struct dirent. These replace the
 * opendir family and scandir for the title's code, its libraries and libc++'s filesystem. */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int sceKernelOpen(const char *path, int flags, int mode);
int sceKernelClose(int fd);
int sceKernelGetdents(int fd, char *buffer, int size);
long sceKernelLseek(int fd, long offset, int whence);

struct _dirdesc
{
    int fd;
    int length;
    int position;
    char buffer[65536];
};

/* Kernel calls return SCE_KERNEL_ERROR_* codes: 0x80020000 plus the errno value. */
static int kernel_errno(int result)
{
    return result & 0xffff;
}

DIR *fdopendir(int fd)
{
    DIR *dir = calloc(1, sizeof(*dir));
    if (dir == NULL)
        return NULL;
    dir->fd = fd;
    return dir;
}

DIR *opendir(const char *path)
{
    int fd = sceKernelOpen(path, O_RDONLY | O_DIRECTORY, 0);
    if (fd < 0)
    {
        errno = kernel_errno(fd);
        return NULL;
    }
    DIR *dir = fdopendir(fd);
    if (dir == NULL)
        sceKernelClose(fd);
    return dir;
}

struct dirent *readdir(DIR *dir)
{
    for (;;)
    {
        if (dir->position >= dir->length)
        {
            int length = sceKernelGetdents(dir->fd, dir->buffer, sizeof(dir->buffer));
            if (length < 0)
            {
                errno = kernel_errno(length);
                return NULL;
            }
            if (length == 0)
                return NULL;
            dir->length = length;
            dir->position = 0;
        }
        struct dirent *entry = (struct dirent *)(dir->buffer + dir->position);
        if (entry->d_reclen == 0)
        {
            dir->position = dir->length;
            continue;
        }
        dir->position += entry->d_reclen;
        if (entry->d_namlen != 0) /* /app0 (nullfs) reports every inode as 0 */
            return entry;
    }
}

int readdir_r(DIR *dir, struct dirent *entry, struct dirent **result)
{
    int saved = errno;
    errno = 0;
    struct dirent *next = readdir(dir);
    if (next == NULL && errno != 0)
    {
        *result = NULL;
        return errno;
    }
    errno = saved;
    if (next != NULL)
        memcpy(entry, next, next->d_reclen < sizeof(*entry) ? next->d_reclen : sizeof(*entry));
    *result = next != NULL ? entry : NULL;
    return 0;
}

void rewinddir(DIR *dir)
{
    sceKernelLseek(dir->fd, 0, SEEK_SET);
    dir->length = 0;
    dir->position = 0;
}

int dirfd(DIR *dir)
{
    return dir->fd;
}

int closedir(DIR *dir)
{
    int result = sceKernelClose(dir->fd);
    free(dir);
    return result < 0 ? -1 : 0;
}

int fdclosedir(DIR *dir)
{
    int fd = dir->fd;
    free(dir);
    return fd;
}

int alphasort(const struct dirent **a, const struct dirent **b)
{
    return strcmp((*a)->d_name, (*b)->d_name);
}

int scandir(const char *path, struct dirent ***list, int (*select)(const struct dirent *),
            int (*compare)(const struct dirent **, const struct dirent **))
{
    DIR *dir = opendir(path);
    if (dir == NULL)
        return -1;
    struct dirent **entries = NULL;
    size_t count = 0, capacity = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (select != NULL && !select(entry))
            continue;
        if (count == capacity)
        {
            capacity = capacity != 0 ? capacity * 2 : 32;
            struct dirent **grown = realloc(entries, capacity * sizeof(*entries));
            if (grown == NULL)
                goto fail;
            entries = grown;
        }
        entries[count] = malloc(entry->d_reclen);
        if (entries[count] == NULL)
            goto fail;
        memcpy(entries[count++], entry, entry->d_reclen);
    }
    closedir(dir);
    if (compare != NULL)
        qsort(entries, count, sizeof(*entries), (int (*)(const void *, const void *))compare);
    *list = entries;
    return (int)count;

fail:
    while (count > 0)
        free(entries[--count]);
    free(entries);
    closedir(dir);
    errno = ENOMEM;
    return -1;
}
