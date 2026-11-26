#ifndef _ARENA_H
#define _ARENA_H

#include <stddef.h>
#include <stdint.h>

#define ROUND_UP(x, y) \
    (((x) + (y) - 1) & ~((y) - 1))

struct arena {
    uint8_t *base;
    size_t size;
    size_t top;
};

extern struct arena arena_ast;
extern struct arena arena_blob;
extern struct arena arena_type;
extern struct arena arena_sym;
extern struct arena arena_scope;

void arena_init(void);

void arena_setup(struct arena *arena, size_t size);

void *arena_alloc(struct arena *arena, size_t size);

void *arena_alloc_zero(struct arena *arena, size_t size);

void *arena_append(struct arena *arena, size_t size);

char *arena_strdup(struct arena *arena, const char *str);

char *arena_strndup(struct arena *arena, const char *str, size_t len);

#endif
