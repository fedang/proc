#ifndef _SOURCE_H
#define _SOURCE_H

#include <stdbool.h>

#include "symbol.h"
#include "span.h"

struct source {
    unsigned src_id;
    char *path;
    const char *str;
    size_t len;
};

struct source_map {
    struct source srcs[256];
    unsigned count;
};

#define SOURCE_SPAN(src) \
    ((struct span) { .src_id = (src)->src_id })

bool source_load(struct source *src, const char *abs_path);

void source_map_init(struct source_map *map);

struct source *source_map_push(struct source_map *map, const char *path);

const struct source *source_map_get(const struct source_map *map,
                                    unsigned src_id);

#endif
