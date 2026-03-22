#ifndef _LEXER_H
#define _LEXER_H

#include <stddef.h>

#include "token.h"

struct lexer {
    const char *str;
    size_t size;
    size_t curr;
    size_t start;
    unsigned line;
    unsigned start_line;

    struct token *tokens;
    size_t tokens_count;
    size_t tokens_max;
};

void lexer_init(struct lexer *state, const char *str, size_t size);

void lexer_tokenize(struct lexer *state, struct token **tokens);

#endif
