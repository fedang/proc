#ifndef _MODCACHE_H
#define _MODCACHE_H

#include <stdbool.h>

#include "source.h"
#include "scope.h"
#include "decl.h"

enum import_status {
    IMPORT_NOT_FOUND,
    IMPORT_ERROR,
    IMPORT_DONE,
};

struct modfile {
    struct source src;
    struct decl *decl;
    char *abs_path;
    char *dir_path;
    bool registered;
};

struct modcache {
    const char *rootdir;
    struct modfile mods[256];
    size_t mods_count;
};

void modcache_init(struct modcache *cache, const char *rootdir);

bool modcache_load(struct modcache *cache, const char *path,
                   struct symbol *name, struct modfile **out);

enum import_status modcache_import(struct modcache *cache, struct symbol *name,
                                   const char *workdir, struct modfile **out);

#endif
