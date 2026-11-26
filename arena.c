#include <assert.h>
#include <string.h>
#include <sys/mman.h>

#include "arena.h"

#define ARENA_SIZE  (512UL * 1024 * 1024)
#define ARENA_ALIGN (sizeof(void *))

struct arena arena_ast;
struct arena arena_blob;
struct arena arena_type;
struct arena arena_sym;
struct arena arena_scope;

static void *
arena_reserve(size_t size)
{
    void *ptr;

    ptr = mmap(NULL, size, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    assert(ptr != MAP_FAILED && "mmap arena reserve failed");

    return ptr;
}

void
arena_setup(struct arena *arena, size_t size)
{
    arena->base = arena_reserve(size);
    arena->size = size;
    arena->top = 0;
}

void
arena_init(void)
{
    arena_setup(&arena_ast, ARENA_SIZE);
    arena_setup(&arena_blob, ARENA_SIZE);
    arena_setup(&arena_type, ARENA_SIZE);
    arena_setup(&arena_sym, ARENA_SIZE);
    arena_setup(&arena_scope, ARENA_SIZE);
}

void *
arena_alloc(struct arena *arena, size_t size)
{
    size_t aligned;
    void *ptr;

    assert(arena->base && "arena not initialized");

    aligned = ROUND_UP(arena->top, ARENA_ALIGN);
    assert(aligned <= arena->size && size <= arena->size - aligned && "arena exhausted");

    ptr = arena->base + aligned;
    arena->top = aligned + size;
    return ptr;
}

void *
arena_alloc_zero(struct arena *arena, size_t size)
{
    void *ptr;

    ptr = arena_alloc(arena, size);
    if (size > 0) {
        memset(ptr, 0, size);
    }
    return ptr;
}

char *
arena_strndup(struct arena *arena, const char *str, size_t len)
{
    char *copy;

    copy = arena_alloc(arena, len + 1);
    if (len > 0) {
        memcpy(copy, str, len);
    }
    copy[len] = '\0';
    return copy;
}

char *
arena_strdup(struct arena *arena, const char *str)
{
    return arena_strndup(arena, str, strlen(str));
}
