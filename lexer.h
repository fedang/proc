#ifndef _LEXER_H
#define _LEXER_H

#include <stddef.h>

#include "token.h"
#include "source.h"

struct lexer {
    struct source *src;
    size_t curr;
    size_t start;
    unsigned line;
    unsigned start_line;

    struct token *tokens;
    size_t tokens_count;
    size_t tokens_max;
};

void lexer_init(struct lexer *state, struct source *src);

void lexer_tokenize(struct lexer *state, struct token **tokens);

#endif
