#ifndef _SOURCE_H
#define _SOURCE_H

#include <stdbool.h>

#include "symbol.h"

struct source {
    const char *path;
    const char *str;
    size_t len;
    /* Associated module name */
    struct symbol *name;
};

void source_init(struct source *src, const char *path, struct symbol *name);

bool source_load(struct source *src);

#endif
