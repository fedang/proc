#ifndef _LEXER_H
#define _LEXER_H

#include <stddef.h>

#include "token.h"
#include "source.h"
#include "scratch.h"

struct lexer {
    struct source *src;
    size_t curr;
    size_t start;
    unsigned line;
    unsigned start_line;
    unsigned prev_line;
    struct scratch_frame tokens;
};

void lexer_init(struct lexer *state, struct source *src);

bool lexer_tokenize(struct lexer *state, struct token **tokens);

void lexer_release(struct lexer *state);

#endif
