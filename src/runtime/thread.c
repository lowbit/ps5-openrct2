/* Threads get at least THREAD_STACK_MIN bytes of stack. The console's default may be much less
 * than the 2 MiB or more desktop code is written for (OpenRCT2 draws on worker threads). The
 * linker sends the title's pthread_create calls here (tools/ps5-link wraps it). */
#include <pthread.h>

#define THREAD_STACK_MIN ((size_t)2 << 20)

int __real_pthread_create(pthread_t *thread, const pthread_attr_t *attributes, void *(*start)(void *),
                          void *argument);

int __wrap_pthread_create(pthread_t *thread, const pthread_attr_t *attributes, void *(*start)(void *),
                          void *argument)
{
    pthread_attr_t own;
    pthread_attr_t *used = (pthread_attr_t *)attributes;
    if (used == NULL)
    {
        pthread_attr_init(&own);
        used = &own;
    }
    size_t size = 0;
    if (pthread_attr_getstacksize(used, &size) == 0 && size < THREAD_STACK_MIN)
        pthread_attr_setstacksize(used, THREAD_STACK_MIN);
    int result = __real_pthread_create(thread, used, start, argument);
    if (used == &own)
        pthread_attr_destroy(&own);
    return result;
}
