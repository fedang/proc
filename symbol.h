#ifndef _SYMBOL_H
#define _SYMBOL_H

#include <stddef.h>

struct symbol {
    const char *str;
    size_t len;
};

struct symbol *symbol_intern(const char *str, size_t len);

struct symbol *symbol_make(const char *str);

#endif
