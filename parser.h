#ifndef _PARSER_H
#define _PARSER_H

#include <stdbool.h>
#include <stddef.h>

#include "token.h"
#include "decl.h"

struct parser {
    struct token *tokens;
    size_t curr;
    bool error;
    bool perfect;
};

void parser_init(struct parser *state, struct token *tokens);

bool parser_module(struct parser *state, struct decl ***decls,
                   size_t *decls_count);

#endif
