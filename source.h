#ifndef _SOURCE_H
#define _SOURCE_H

#include <stdbool.h>

struct source {
    const char *path;
    const char *str;
    size_t len;
};

bool source_open(struct source *src, const char *path);

#endif
