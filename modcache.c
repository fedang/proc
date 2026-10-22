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

static const char *
modcache_relpath(const char *abs_path, const char *rootdir)
{
    size_t len;

    len = strlen(rootdir);
    if (len == 1 && rootdir[0] == '/') {
        return abs_path[0] == '/'
            ? abs_path + 1 : abs_path;
    }

    if (!strncmp(abs_path, rootdir, len)
            && (abs_path[len] == '/' || abs_path[len] == '\0')) {
        /*
         * Strip the prefix when the path lies in the compiler root directory
         */
        abs_path += len;
        while (*abs_path == '/')
            abs_path++;
    }

    return abs_path;
}

static bool
modcache_valid_ident(struct source_map *map, struct source *src,
                     const char *str, size_t len)
{
    size_t i;

    if (len == 0) {
        report_error(map, &SOURCE_SPAN(src),
                     "Invalid empty module filename '%.*s'\n",
                     (int)len, str);
        return false;
    }

    if (!isalpha(str[0]) && str[0] != '_') {
        report_error(map, &SOURCE_SPAN(src),
                     "Invalid filename '%.*s' does not start with [A-Za-z_]\n",
                     (int)len, str);
        return false;
    }

    for (i = 1; i < len; i++) {
        if (!isalnum(str[i]) && str[i] != '_') {
            report_error(map, &SOURCE_SPAN(src),
                         "Invalid filename '%.*s' does not contain only [0-9A-Za-z_]\n",
                         (int)len, str);
            return false;
        }
    }

    return true;
}

/*
 * Derive the dotted logical path of the module file, calculated from the
 * compiler working directory. This value is not used for import paths,
 * but rather to guarantee a unique module prefix for linking.
 *
 *  <root>/A.lang           -> "A"
 *  <root>/A/B/module.lang  -> "A.B"
 *  <root>/A/B/C.lang       -> "A.B.C"
 */
static struct symbol *
modcache_derive_path(struct source_map *map, struct source *src,
                     const char *path)
{
    struct symbol *prefix, *seg;
    const char *ptr, *end;
    size_t len;
    bool last_seg;

    // XXX
    if (path[0] == '/') {
        report_error(map, &SOURCE_SPAN(src),
                     "File '%s' is outside of the compiler root directory\n",
                     path);
        return NULL;
    }

    prefix = NULL;
    ptr = path;

    for (last_seg = false; !last_seg; ) {
        end = strchr(ptr, '/');
        last_seg = end == NULL;

        if (last_seg) {
            end = ptr + strlen(ptr);
        }
        len = end - ptr;

        if (last_seg) {
            if (len < 5 || strncmp(end - 5, ".lang", 5) != 0) {
                report_error(map, &SOURCE_SPAN(src),
                             "File '%.*s' does not end in '.lang'\n",
                             (int)len, ptr);
                return NULL;
            }

            len -= 5;
            if (!modcache_valid_ident(map, src, ptr, len))
                return NULL;

            /*
             * If the file is called "module" it uses the name of the folder
             */
            if (len == 6 && !memcmp(ptr, "module", 6)) {
                if (!prefix) {
                    report_error(map, &SOURCE_SPAN(src),
                                 "'module.lang' must be inside a named directory\n");
                    return NULL;
                }
                break;
            }
        } else if (!modcache_valid_ident(map, src, ptr, len)) {
            return NULL;
        }

        seg = symbol_intern(ptr, len);
        prefix = symbol_mangle(prefix, seg);
        ptr = end + 1;
    }

    return prefix;
}

static struct symbol *
modcache_extract_name(struct symbol *path)
{
    size_t i;

    for (i = path->len; i > 0; i--) {
        if (path->str[i - 1] == '.')
            return symbol_intern(path->str + i, path->len - i);
    }

    return path;
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
    struct decl_module *mod;
    struct symbol *prefix;
    struct modfile *file;
    size_t i;

    if (!realpath(path, abs_buf)) {
        report_error(NULL, NULL, "File '%s' was not found\n", path);
        return false;
    }

    /*
     * Deduplicate modules by path
     */
    for (i = 0; i < cache->mods_count; i++) {
        if (!strcmp(cache->mods[i].abs_path, abs_buf)) {
            mod = (struct decl_module *)cache->mods[i].decl;

            if (name && name != mod->sym) {
                report_error(NULL, NULL,
                             "Module '%pS' already loaded as '%pS'\n",
                             name, mod->sym);
                return false;
            }

            if (out) {
                *out = &cache->mods[i];
            }
            return true;
        }
    }

    path = modcache_relpath(abs_buf, cache->rootdir);
    src = source_map_push(&cache->src_map, path);

    prefix = modcache_derive_path(&cache->src_map, src, path);
    if (!prefix)
        return false;

    if (!source_load(src, abs_buf)) {
        report_error(&cache->src_map, &SOURCE_SPAN(src),
                     "Failed to load module file '%s'\n", path);
        return false;
    }

    lexer_init(&lexer, src);
    lexer_tokenize(&lexer, &tokens);

    parser_init(&parser, &cache->src_map, src, tokens);
    if (!parser_module(&parser, &decl))
        return false;

    if (!name) {
        /*
         * If we don't have a specific name we need to get the last segment
         * of the calculated module prefix
         */
        name = modcache_extract_name(prefix);
    }

    assert(decl && decl->tag == DECL_MODULE);
    mod = (struct decl_module *)decl;
    mod->sym = name;

    assert(cache->mods_count < 256 && "too many modules!");
    file = &cache->mods[cache->mods_count++];

    file->src = src;
    file->prefix = prefix;
    file->decl = decl;
    file->abs_path = strdup(abs_buf);
    file->dir_path = modcache_dirname(abs_buf);

    if (out)
        *out = file;
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
