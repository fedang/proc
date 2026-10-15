#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "modcache.h"
#include "lexer.h"
#include "parser.h"
#include "report.h"

void
modcache_init(struct modcache *cache)
{
    memset(cache, 0, sizeof(struct modcache));
}

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

bool
modcache_load(struct modcache *cache, const char *path,
              struct symbol *name, struct modfile **out)
{
    struct source src;
    struct lexer lexer;
    struct parser parser;
    struct token *tokens;
    struct decl *decl;
    size_t i;

    /*
     * Deduplicate modules by path
     */
    for (i = 0; i < cache->mods_count; i++) {
        if (!strcmp(cache->mods[i].src.path, path)) {
            if (out) {
                *out = &cache->mods[i];
            }
            return true;
        }
    }

    source_init(&src, path, name);
    if (!source_load(&src)) {
        report_error(&src, NULL, "Failed to load module file\n");
        return false;
    }

    lexer_init(&lexer, &src);
    lexer_tokenize(&lexer, &tokens);

    parser_init(&parser, &src, tokens);
    if (!parser_module(&parser, &decl))
        return false;

    assert(cache->mods_count < 256 && "Too many modules!");

    if (out) {
        *out = &cache->mods[cache->mods_count];
    }

    cache->mods[cache->mods_count].src = src;
    cache->mods[cache->mods_count].decl = decl;
    cache->mods_count++;
    return true;
}

bool
modcache_import(struct modcache *cache, struct symbol *name,
                const char *cwd, struct modfile **out)
{
    char path[1024];

    snprintf(path, sizeof(path), "%s/%s.lang", cwd, name->str);
    if (modcache_detect(path)) {
        return modcache_load(cache, path, name, out);
    }

    snprintf(path, sizeof(path), "%s/%s/module.lang", cwd, name->str);
    if (modcache_detect(path)) {
        return modcache_load(cache, path, name, out);
    }

    return false;
}
