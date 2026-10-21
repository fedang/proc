#define _XOPEN_SOURCE 500
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "source.h"
#include "report.h"

bool
source_load(struct source *src)
{
    char *buf;
    FILE *file;
    long fsize;

    assert(!src->str && !src->len && "double read?");

    file = fopen(src->path, "rb");
    if (!file)
        return false;

    fseek(file, 0, SEEK_END);
    fsize = ftell(file);

    if (fsize < 0) {
        fclose(file);
        return false;
    }

    rewind(file);

    buf = malloc(fsize + 1);
    if (fread(buf, 1, fsize, file) != (size_t)fsize) {
        free(buf);
        fclose(file);
        return false;
    }

    fclose(file);
    buf[fsize] = 0;

    src->str = buf;
    src->len = fsize;
    return true;
}

void
source_map_init(struct source_map *map)
{
    memset(map, 0, sizeof(struct source_map));

    /*
     * Let the first source be a sentinel for invalid spans
     */
    map->srcs[0].path = "<unknown>";
    map->count++;
}

struct source *
source_map_push(struct source_map *map, const char *path, struct symbol *name)
{
    struct source *src;
    assert(map->count < 256 && "too many sources");

    src = &map->srcs[map->count];
    src->src_id = map->count;
    src->path = strdup(path);
    src->name = name;
    map->count++;
    return src;
}

const struct source *
source_map_get(const struct source_map *map, unsigned src_id)
{
    if (src_id >= map->count)
        return &map->srcs[0];

    return &map->srcs[src_id];
}
