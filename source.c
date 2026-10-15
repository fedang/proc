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
    const char *base, *end, *ptr, *dir;
    size_t len;

    /*
     * Find the last directory separator
     */
    for (base = ptr = src->path; *ptr != '\0'; ptr++) {
        if (*ptr == '/') {
            base = ptr + 1;
        }
    }

    /*
     * If the file is called "module" it uses the name of the folder
     */
    if (strcmp(base, "module.lang") == 0) {
        if (base == src->path) {
            report_error(src, NULL,
                         "'module.lang' must be inside a named directory\n");
            return symbol_invalid_mod;
        }

        dir = base - 1;
        while (dir > src->path && dir[-1] == '/') {
            dir--;
        }

        ptr = dir;
        while (ptr > src->path && ptr[-1] != '/') {
            ptr--;
        }

        base = ptr;
        end = dir;
    } else {
        /*
         * Ordinary files use their basename before the dot
         */
        for (end = base; *end != '\0'; end++) {
            if (*end == '.') {
                break;
            }
        }
    }

    /*
     * Validate the derived module name.
     */
    len = end - base;

    if (len == 0) {
        report_error(src, NULL,
                     "Empty or hidden filenames are not allowed as module names\n");
        return symbol_invalid_mod;
    }

    if (!isalpha(*base) && *base != '_') {
        report_error(src, NULL,
                     "Module name must start with a letter or underscore\n");
        return symbol_invalid_mod;
    }

    for (ptr = base + 1; ptr < end; ptr++) {
        if (!isalnum(*ptr) && *ptr != '_') {
            report_error(src, NULL,
                         "Module name can only contain alphanumerics and underscores\n");
            return symbol_invalid_mod;
        }
    }

    if (len == 6 && !memcmp(base, "module", 6)) {
        report_error(src, NULL,
                     "Module name 'module' is reserved\n");
        return symbol_invalid_mod;
    }

    return symbol_intern(base, len);
}

void
source_init(struct source *src, const char *path, struct symbol *name)
{
    size_t len;

    memset(src, 0, sizeof(struct source));

    len = strlen(path) + 1;
    src->path = malloc(len);
    memcpy(src->path, path, len);

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
