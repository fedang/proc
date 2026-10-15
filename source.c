#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

#include "source.h"
#include "report.h"

static struct symbol *
source_module_name(struct source *src)
{
    const char *base, *dot, *ptr;

    /*
     * Find the last directory separator
     */
    for (base = ptr = src->path; *ptr != '\0'; ptr++) {
        if (*ptr == '/' || *ptr == '\\') {
            base = ptr + 1;
        }
    }

    /*
     * Find the first dot after the base name
     */
    for (ptr = base, dot = NULL; *ptr != '\0'; ptr++) {
        if (*ptr == '.') {
            dot = ptr;
            break;
        }
    }

    if (!dot) {
        dot = ptr;
    }

    if (dot == base) {
        report_error(src, NULL,
                     "Empty or hidden filenames are not allowed as module names\n");
        return symbol_invalid_mod;
    }

    if (!isalpha(*base) && *base != '_') {
        report_error(src, NULL,
                     "File name can only start with a letter or underscore\n");
        return symbol_invalid_mod;
    }

    for (ptr = base + 1; ptr < dot; ptr++) {
        if (!isalnum(*ptr) && *ptr != '_') {
            report_error(src, NULL,
                         "File name can only contain alphanumerics and underscores\n");
            return symbol_invalid_mod;
        }
    }

    return symbol_intern(base, dot - base);
}

void
source_init(struct source *src, const char *path, struct symbol *name)
{
    src->path = path;
    src->str = NULL;
    src->len = 0;
    src->name = name ? name : source_module_name(src);
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
    if (fread(buf, fsize, 1, file) != 1) {
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
