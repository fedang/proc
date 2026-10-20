#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "source.h"
#include "report.h"

void
source_init(struct source *src, const char *path, struct symbol *name)
{
    size_t len;

    memset(src, 0, sizeof(struct source));
    src->name = name;

    len = strlen(path) + 1;
    src->path = malloc(len);
    memcpy(src->path, path, len);
}

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
