#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "symbol.h"

#define MAX_SYMBOLS 4096

static struct symbol sym_pool[MAX_SYMBOLS];

static size_t sym_count;

struct symbol *
symbol_intern(const char *str, size_t len)
{
    struct symbol *sym;
    char *copy;
    size_t i;

    for (i = 0; i < sym_count; i++) {
        if (sym_pool[i].len == len && !strcmp(sym_pool[i].str, str)) {
            return &sym_pool[i];
        }
    }

    assert(sym_count < MAX_SYMBOLS && "intern pool full");

    copy = malloc(len + 1);
    memcpy(copy, str, len);
    copy[len] = 0;

    sym = &sym_pool[sym_count++];
    sym->str = copy;
    sym->len = len;
    return sym;
}


struct symbol *
symbol_make(const char *str)
{
    return symbol_intern(str, strlen(str));
}
