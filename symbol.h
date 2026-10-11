#ifndef _SYMBOL_H
#define _SYMBOL_H

#include <stddef.h>

#define SYMBOL_LIST(X) \
    X(let) \
    X(var) \
    X(if) \
    X(else) \
    X(return) \
    X(while) \
    X(break) \
    X(continue) \
    X(proc) \
    X(struct) \
    X(type) \
    X(size_of) \
    X(align_of) \
    X(cast)

struct symbol {
    const char *str;
    size_t len;
};

#define X(sym) \
    extern struct symbol *symbol_ ## sym;
SYMBOL_LIST(X)
#undef X

struct symbol *symbol_intern(const char *str, size_t len);

struct symbol *symbol_make(const char *str);

void symbol_init(void);

#endif
