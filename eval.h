#ifndef _EVAL_H
#define _EVAL_H

#include "value.h"
#include "decl.h"
#include "source.h"

enum eval_status {
    EVAL_OK,
    EVAL_NOT_CONST,
    EVAL_EXHAUSTED,
    EVAL_RETURN,
    EVAL_BREAK,
    EVAL_CONTINUE,
    EVAL_ERROR,
};

struct named {
    struct symbol *sym;
    struct value value;
};

struct environ {
    struct environ *parent;
    struct named vars[64];
    size_t vars_count;
};

struct eval {
    struct source_map *src_map;
    struct environ *env;
    unsigned fuel;
};

void eval_init(struct eval *state, struct source_map *src_map);

void eval_env_push(struct eval *state, struct environ *env);

void eval_env_pop(struct eval *state);

void eval_env_bind(struct eval *state, struct named var);

enum eval_status eval_try_ctval(struct eval *state, struct expr *expr);

#endif
