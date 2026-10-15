#ifndef _SYMBOL_H
#define _SYMBOL_H

#include <stddef.h>

#define SYMBOL_LIST(X) \
    /* Keywords */ \
    X(var, var) \
    X(if, if) \
    X(else, else) \
    X(return, return) \
    X(while, while) \
    X(break, break) \
    X(continue, continue) \
    X(proc, proc) \
    X(struct, struct) \
    X(type, type) \
    X(module, module) \
    X(sig, sig) \
    X(import, import) \
    /* Special idents */ \
    X(len, len) \
    X(ptr, ptr) \
    /* Builtins */ \
    X(sizeof, size_of) \
    X(alignof, align_of) \
    X(typeof, type_of) \
    X(cast, cast) \
    X(slice, slice) \
    /* Misc */ \
    X(invalid_mod, <invalid>) \
    /* End */

struct symbol {
    const char *str;
    size_t len;
};

#define X(sym, _) \
    extern struct symbol *symbol_ ## sym;
SYMBOL_LIST(X)
#undef X

struct symbol *symbol_intern(const char *str, size_t len);

struct symbol *symbol_make(const char *str);

void symbol_init(void);

#endif
