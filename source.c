#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <stdbool.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "source.h"
#include "report.h"

bool
source_load(struct source *src, const char *abs_path)
{
    const char *path;
    struct stat st;
    void *map;
    int fd;

    assert(!src->str && !src->len && "double read?");

    path = abs_path ? abs_path : src->path;
    fd = open(path, O_RDONLY);
    if (fd < 0)
        return false;

    if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode)) {
        close(fd);
        return false;
    }

    if (st.st_size == 0) {
        close(fd);
        src->str = "";
        src->len = 0;
        return true;
    }

    map = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map == MAP_FAILED)
        return false;

    src->str = map;
    src->len = (size_t)st.st_size;
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
source_map_push(struct source_map *map, const char *path)
{
    struct source *src;
    assert(map->count < 256 && "too many sources");

    src = &map->srcs[map->count];
    src->src_id = map->count;
    src->path = strdup(path);
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
