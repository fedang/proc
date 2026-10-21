#define _XOPEN_SOURCE 500
#include <stdlib.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include "modcache.h"
#include "lexer.h"
#include "parser.h"
#include "report.h"

// XXX
static bool
modcache_detect(const char *path)
{
    FILE *file;

    file = fopen(path, "rb");
    if (file) {
        fclose(file);
        return true;
    }
    return false;
}

void
modcache_init(struct modcache *cache, const char *rootdir)
{
    char abs_buf[PATH_MAX];

    memset(cache, 0, sizeof(struct modcache));
    assert(realpath(rootdir ? rootdir : ".", abs_buf));
    cache->rootdir = strdup(abs_buf);

    source_map_init(&cache->src_map);
}

static struct symbol *
modcache_derive_name(struct source_map *map, struct source *src)
{
    const char *base, *end, *ptr, *dir;
    size_t len;

    /*
     * Find the last directory separator
     */
    for (base = ptr = src->path; *ptr != '\0'; ptr++) {
        if (*ptr == '/' || *ptr == '\\') {
            base = ptr + 1;
        }
    }

    /*
     * If the file is called "module" it uses the name of the folder
     */
    if (strcmp(base, "module.lang") == 0) {
        if (base == src->path) {
            report_error(map, &SOURCE_SPAN(src),
                         "'module.lang' must be inside a named directory\n");
            return NULL;
        }

        dir = base - 1;
        while (dir > src->path && (dir[-1] == '/' || dir[-1] == '\\')) {
            dir--;
        }

        ptr = dir;
        while (ptr > src->path && ptr[-1] != '/' && ptr[-1] != '\\') {
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
        report_error(map, &SOURCE_SPAN(src),
                     "Empty or hidden filenames are not allowed as module names\n");
        return NULL;
    }

    if (!isalpha(*base) && *base != '_') {
        report_error(map, &SOURCE_SPAN(src),
                     "Module name must start with a letter or underscore\n");
        return NULL;
    }

    for (ptr = base + 1; ptr < end; ptr++) {
        if (!isalnum(*ptr) && *ptr != '_') {
            report_error(map, &SOURCE_SPAN(src),
                         "Module name can only contain alphanumerics and underscores\n");
            return NULL;
        }
    }

    if (len == 6 && !memcmp(base, "module", 6)) {
        report_error(map, &SOURCE_SPAN(src),
                     "Module name 'module' is reserved\n");
        return NULL;
    }

    return symbol_intern(base, len);
}

static struct symbol *
modcache_compute_prefix(const char *path, const char *workdir)
{
    char buf[PATH_MAX];
    const char *ptr;
    size_t len;
    char *c;

    len = strlen(workdir);
    ptr = path;

    if (!strncmp(path, workdir, len) &&
            (path[len] == '/' || path[len] == '\\' || path[len] == '\0')) {
        ptr = path + len;
        if (*ptr == '/' || *ptr == '\\')
            ptr++;
    }

    strncpy(buf, ptr, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    len = strlen(buf);
    if (len >= 12 && strcmp(buf + len - 12, "/module.lang") == 0) {
        buf[len - 12] = '\0';
    } else if (len >= 5 && strcmp(buf + len - 5, ".lang") == 0) {
        buf[len - 5] = '\0';
    }

    for (c = buf; *c; c++) {
        if (*c == '/' || *c == '\\')
            *c = '.';
    }

    return symbol_make(buf);
}

static char *
modcache_dirname(const char *abs_path)
{
    const char *slash;
    char *buf;
    size_t len;

    slash = strrchr(abs_path, '/');
    if (!slash)
        return strdup(".");

    if (slash == abs_path)
        return strdup("/");

    len = (size_t)(slash - abs_path);
    buf = malloc(len + 1);
    memcpy(buf, abs_path, len);
    buf[len] = '\0';
    return buf;
}

bool
modcache_load(struct modcache *cache, const char *path,
              struct symbol *name, struct modfile **out)
{
    char abs_buf[PATH_MAX];
    struct source *src;
    struct lexer lexer;
    struct parser parser;
    struct token *tokens;
    struct decl *decl;
    struct symbol *prefix;
    size_t i;

    if (!realpath(path, abs_buf)) {
        report_error(NULL, NULL, "File not found: %s\n", path);
        return false;
    }

    /*
     * Deduplicate modules by path
     */
    for (i = 0; i < cache->mods_count; i++) {
        if (!strcmp(cache->mods[i].abs_path, abs_buf)) {
            if (name && name != cache->mods[i].src->name) {
                report_error(NULL, NULL,
                             "Module '%pS' already loaded as '%pS'\n",
                             name, cache->mods[i].src->name);
                return false;
            }

            if (out) {
                *out = &cache->mods[i];
            }
            return true;
        }
    }

    src = source_map_push(&cache->src_map, path, name);
    if (!src->name) {
        src->name = modcache_derive_name(&cache->src_map, src);
        if (!src->name)
            return false;
    }

    if (!source_load(src)) {
        report_error(&cache->src_map, &SOURCE_SPAN(src),
                     "Failed to load module file '%pS'\n", src->name);
        return false;
    }

    lexer_init(&lexer, src);
    lexer_tokenize(&lexer, &tokens);

    parser_init(&parser, &cache->src_map, src, tokens);
    if (!parser_module(&parser, &decl))
        return false;

    assert(cache->mods_count < 256 && "Too many modules!");

    if (out) {
        *out = &cache->mods[cache->mods_count];
    }

    cache->mods[cache->mods_count].src = src;
    cache->mods[cache->mods_count].decl = decl;
    cache->mods[cache->mods_count].abs_path = strdup(abs_buf);
    cache->mods[cache->mods_count].dir_path = modcache_dirname(abs_buf);
    cache->mods_count++;

    prefix = modcache_compute_prefix(abs_buf, cache->rootdir);
    ((struct decl_module *)decl)->prefix = prefix;
    return true;
}

enum import_status
modcache_import(struct modcache *cache, struct symbol *name,
                const char *workdir, struct modfile **out)
{
    char path[PATH_MAX];

    snprintf(path, sizeof(path), "%s/%s.lang", workdir, name->str);
    if (modcache_detect(path)) {
        return modcache_load(cache, path, name, out)
             ? IMPORT_DONE
             : IMPORT_ERROR;
    }

    snprintf(path, sizeof(path), "%s/%s/module.lang", workdir, name->str);
    if (modcache_detect(path)) {
        return modcache_load(cache, path, name, out)
             ? IMPORT_DONE
             : IMPORT_ERROR;
    }

    return IMPORT_NOT_FOUND;
}
