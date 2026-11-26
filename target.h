#ifndef _TARGET_H
#define _TARGET_H

#include <stddef.h>
#include <stdbool.h>

struct target {
    const char *name;
    const char *triple;
    const char *fn_attrs;
    size_t word_size;
    bool freestanding;
};

const struct target *target_default(void);

const struct target *target_find(const char *name);

void target_list(void);

#endif
