#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "source.h"

bool
source_open(struct source *src, const char *path)
{
    char *buf;
    FILE *file;
    size_t fsize;

    file = fopen(path, "rb");
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
    if (fread(buf, fsize, 1, file) != 1) {
        free(buf);
        fclose(file);
        return false;
    }

    fclose(file);
    buf[fsize] = 0;

    src->path = path;
    src->str = buf;
    src->len = fsize;
    return true;
}
