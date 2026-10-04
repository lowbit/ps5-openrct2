/* malloc and friends from one large mspace in direct memory. A native title's libc heap lives in
 * flexible memory, which holds only a few hundred MB. Blocks from before the mspace exists, or
 * after it is full, come from the libc heap; free and realloc route by address. The linker sends
 * every call from the title's own code here (tools/ps5-link wraps these symbols). */
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
void *__real_realloc(void *address, size_t size);
void __real_free(void *address);
int __real_posix_memalign(void **address, size_t alignment, size_t size);
size_t __real_malloc_usable_size(void *address);

void *sceLibcMspaceCreate(const char *name, void *base, size_t size, unsigned flags);
void *sceLibcMspaceMalloc(void *mspace, size_t size);
void *sceLibcMspaceCalloc(void *mspace, size_t count, size_t size);
void *sceLibcMspaceRealloc(void *mspace, void *address, size_t size);
void sceLibcMspaceFree(void *mspace, void *address);
int sceLibcMspacePosixMemalign(void *mspace, void **address, size_t alignment, size_t size);
size_t sceLibcMspaceMallocUsableSize(void *address);
int64_t sceKernelGetDirectMemorySize(void);
int sceKernelAllocateDirectMemory(int64_t start, int64_t end, size_t length, size_t alignment,
                                  int type, int64_t *physical);
int sceKernelMapDirectMemory(void **address, size_t length, int protection, int flags,
                             int64_t physical, size_t alignment);

#define HEAP_ALIGNMENT (2u << 20)
#define MEMORY_TYPE_CPU 12
#define PROT_CPU_RW 3

enum { HEAP_NONE, HEAP_STARTING, HEAP_READY, HEAP_FAILED };

static atomic_int state;
static uintptr_t heap_base;
static size_t heap_size;
static void *mspace;

static void heap_start(void)
{
    static const size_t sizes[] = { (size_t)2 << 30, (size_t)1 << 30, (size_t)512 << 20 };

    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
    {
        int64_t physical = 0;
        void *base = NULL;
        if (sceKernelAllocateDirectMemory(0, sceKernelGetDirectMemorySize(), sizes[i], HEAP_ALIGNMENT,
                                          MEMORY_TYPE_CPU, &physical) != 0)
            continue;
        if (sceKernelMapDirectMemory(&base, sizes[i], PROT_CPU_RW, 0, physical, HEAP_ALIGNMENT) != 0)
            continue;
        mspace = sceLibcMspaceCreate("openrct2", base, sizes[i], 0);
        if (mspace != NULL)
        {
            heap_base = (uintptr_t)base;
            heap_size = sizes[i];
            return;
        }
    }
}

static int heap_ready(void)
{
    int expected = HEAP_NONE;
    if (atomic_compare_exchange_strong(&state, &expected, HEAP_STARTING))
    {
        heap_start();
        atomic_store(&state, mspace != NULL ? HEAP_READY : HEAP_FAILED);
        return mspace != NULL;
    }
    while (expected == HEAP_STARTING)
    {
        sched_yield();
        expected = atomic_load(&state);
    }
    return expected == HEAP_READY;
}

static int heap_owns(const void *address)
{
    return atomic_load_explicit(&state, memory_order_acquire) == HEAP_READY &&
           (uintptr_t)address - heap_base < heap_size;
}

void *__wrap_malloc(size_t size)
{
    void *result = heap_ready() ? sceLibcMspaceMalloc(mspace, size) : NULL;
    return result != NULL ? result : __real_malloc(size);
}

void *__wrap_calloc(size_t count, size_t size)
{
    void *result = heap_ready() ? sceLibcMspaceCalloc(mspace, count, size) : NULL;
    return result != NULL ? result : __real_calloc(count, size);
}

void __wrap_free(void *address)
{
    if (heap_owns(address))
        sceLibcMspaceFree(mspace, address);
    else
        __real_free(address);
}

size_t __wrap_malloc_usable_size(void *address)
{
    return heap_owns(address) ? sceLibcMspaceMallocUsableSize(address) : __real_malloc_usable_size(address);
}

void *__wrap_realloc(void *address, size_t size)
{
    if (address == NULL)
        return __wrap_malloc(size);
    if (!heap_owns(address))
        return __real_realloc(address, size);
    void *result = sceLibcMspaceRealloc(mspace, address, size);
    if (result == NULL && size != 0)
    {
        /* The mspace is full: move the block to the libc heap. */
        result = __real_malloc(size);
        if (result != NULL)
        {
            size_t old = sceLibcMspaceMallocUsableSize(address);
            memcpy(result, address, old < size ? old : size);
            sceLibcMspaceFree(mspace, address);
        }
    }
    return result;
}

int __wrap_posix_memalign(void **address, size_t alignment, size_t size)
{
    if (heap_ready() && sceLibcMspacePosixMemalign(mspace, address, alignment, size) == 0)
        return 0;
    return __real_posix_memalign(address, alignment, size);
}

void *__wrap_memalign(size_t alignment, size_t size)
{
    void *result = NULL;
    return __wrap_posix_memalign(&result, alignment, size) == 0 ? result : NULL;
}

void *__wrap_aligned_alloc(size_t alignment, size_t size)
{
    return __wrap_memalign(alignment, size);
}
