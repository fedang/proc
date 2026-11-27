#include <stdlib.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include "symbol.h"
#include "arena.h"
#include "scratch.h"

static struct symbol **sym_table;
static size_t sym_count;
static size_t sym_size;

#define X(sym, _) \
    struct symbol *symbol_ ## sym;
SYMBOL_LIST(X)
#undef X

/*
 * Classic FNV 32-bit hash
 */
static uint32_t
symbol_hash(const char *str, size_t len)
{
    uint32_t hash = 2166136261U;
    size_t i;

    for (i = 0; i < len; i++) {
        hash ^= (uint8_t)str[i];
        hash *= 16777619U;
    }
    return hash;
}

static void
symbol_grow(size_t size)
{
    struct symbol **table;
    size_t i, idx;

    table = calloc(size, sizeof(struct symbol *));
    assert(table && "out of memory");

    for (i = 0; i < sym_size; i++) {
        if (!sym_table[i])
            continue;

        idx = sym_table[i]->hash & (size - 1);
        while (table[idx])
            idx = (idx + 1) & (size - 1);
        table[idx] = sym_table[i];
    }

    free(sym_table);
    sym_table = table;
    sym_size = size;
}

struct symbol *
symbol_intern(const char *str, size_t len)
{
    struct symbol *sym;
    uint32_t hash;
    size_t idx;

    assert(len <= UINT32_MAX && "symbol too long");

    if (sym_count * 2 >= sym_size)
        symbol_grow(sym_size * 2);

    hash = symbol_hash(str, len);

    /*
     * Open-addressing hashtable loop
     */
    idx = hash & (sym_size - 1);
    while (sym_table[idx]) {
        sym = sym_table[idx];
        if (sym->hash == hash && sym->len == len && !memcmp(sym->str, str, len))
            return sym;
        idx = (idx + 1) & (sym_size - 1);
    }

    sym = arena_alloc(&arena_sym, sizeof(struct symbol));
    sym->str = arena_strndup(&arena_sym, str, len);
    sym->len = len;
    sym->hash = hash;

    sym_table[idx] = sym;
    sym_count++;
    return sym;
}

struct symbol *
symbol_make(const char *str)
{
    return symbol_intern(str, strlen(str));
}

struct symbol *
symbol_mangle(struct symbol *prefix, struct symbol *sym)
{
    struct scratch_frame frame;
    struct symbol *out;
    char *buf;
    size_t len;

    if (!prefix)
        return sym;

    scratch_begin(&frame);

    len = prefix->len + 1 + sym->len;
    buf = scratch_alloc(len + 1);
    memcpy(buf, prefix->str, prefix->len);
    buf[prefix->len] = '.';
    memcpy(buf + prefix->len + 1, sym->str, sym->len);
    buf[len] = '\0';

    out = symbol_intern(buf, len);
    scratch_end(&frame);
    return out;
}

void
symbol_init(void)
{
    /*
     * Allocate a sensible size for the symbol table
     */
    symbol_grow(4096);

#define X(sym, str) \
    symbol_ ## sym = symbol_make(#str);
    SYMBOL_LIST(X)
#undef X
}
