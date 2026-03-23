#include <ctype.h>
#include <stdbool.h>
#include <assert.h>
#include <stdlib.h>

#include "lexer.h"

void
lexer_init(struct lexer *state, const char *str, size_t size)
{
    state->str = str;
    state->size = size;
    state->curr = 0;
    state->start = 0;
    state->line = 1;
    state->start_line = 1;

    state->tokens = NULL;
    state->tokens_count = 0;
    state->tokens_max = 0;
}

static inline bool
lexer_eof(struct lexer *state)
{
    return state->curr >= state->size;
}

static inline char
lexer_curr(struct lexer *state)
{
    return lexer_eof(state) ? '\0' : state->str[state->curr];
}

static inline char
lexer_peek(struct lexer *state)
{
    return lexer_eof(state) ? '\0' : state->str[state->curr + 1];
}

static inline char
lexer_advance(struct lexer *state)
{
    return lexer_eof(state) ? '\0' : state->str[state->curr++];
}

static void
lexer_push(struct lexer *state, enum token_tag tag)
{
    struct token *token;
    size_t max;

    if (state->tokens_count + 1 >= state->tokens_max) {
        max = state->tokens_max < 32 ? 32 : state->tokens_max * 2;
        state->tokens = realloc(state->tokens, max * sizeof(struct token));

        assert(state->tokens && "out of memory");
        state->tokens_max = max;
    }

    token = &state->tokens[state->tokens_count++];
    token->source.start = &state->str[state->start];
    token->source.end = &state->str[state->curr];
    token->source.start_line = state->start_line;
    token->source.end_line = state->line;
    token->tag = tag;
}

static void
lexer_skip(struct lexer *state)
{
    char c;

    while (true) {
        switch (lexer_curr(state)) {
            case ' ':
            case '\r':
            case '\t':
                lexer_advance(state);
                break;

            case '\n':
                ++state->line;
                lexer_advance(state);
                break;

            case '/':
                if (lexer_peek(state) != '/')
                    return;

                do {
                    c = lexer_advance(state);
                    if (c == '\n') {
                        ++state->line;
                        break;
                    }
                } while (!lexer_eof(state));
                break;

            default:
                return;
        }
    }
}

#define LEXER_OPEQ(tag) \
    do { \
        if (lexer_curr(state) == '=') { \
            lexer_advance(state); \
            lexer_push(state, tag ## EQ); \
        } else { \
            lexer_push(state, tag); \
        } \
    } while (false)

static void
lexer_next(struct lexer *state)
{
    char c;

    lexer_skip(state);

    state->start = state->curr;
    state->start_line = state->line;

    if (lexer_eof(state))
        return;

    c = lexer_advance(state);

    if (isalpha(c) || c == '_') {
        while (isalnum(lexer_curr(state))
                || lexer_curr(state) == '_') {
            lexer_advance(state);
        }

        lexer_push(state, TOKEN_SYMBOL);
        return;
    }

    if (isdigit(c)) {
        while (isdigit(lexer_curr(state))) {
            lexer_advance(state);
        }

        lexer_push(state, TOKEN_INT);
        return;
    }

    if (c == '"') {
        do {
            c = lexer_advance(state);
            if (c == '\n')
                ++state->line;
            else if (c == '"')
                break;
        } while (!lexer_eof(state));

        // TODO: Unterminated string error
        lexer_push(state, TOKEN_INVALID);
        return;
    }

    switch (c) {
        case '=':
            LEXER_OPEQ(TOKEN_EQ);
            break;

        case '|':
            if (lexer_curr(state) == '|') {
                lexer_advance(state);
                lexer_push(state, TOKEN_OROR);
            } else {
                LEXER_OPEQ(TOKEN_OR);
            }
            break;

        case '&':
            if (lexer_curr(state) == '&') {
                lexer_advance(state);
                lexer_push(state, TOKEN_ANDAND);
            } else {
                LEXER_OPEQ(TOKEN_AND);
            }
            break;

        case '^':
            LEXER_OPEQ(TOKEN_XOR);
            break;

        case '!':
            LEXER_OPEQ(TOKEN_NOT);
            break;

        case '>':
            if (lexer_curr(state) == '>') {
                lexer_advance(state);
                LEXER_OPEQ(TOKEN_SHR);
            } else {
                LEXER_OPEQ(TOKEN_GT);
            }
            break;

        case '<':
            if (lexer_curr(state) == '<') {
                lexer_advance(state);
                LEXER_OPEQ(TOKEN_SHL);
            } else {
                LEXER_OPEQ(TOKEN_LT);
            }
            break;

        case '+':
            LEXER_OPEQ(TOKEN_PLUS);
            break;

        case '-':
            LEXER_OPEQ(TOKEN_MINUS);
            break;

        case '*':
            LEXER_OPEQ(TOKEN_STAR);
            break;

        case '/':
            LEXER_OPEQ(TOKEN_SLASH);
            break;

        case '%':
            LEXER_OPEQ(TOKEN_PERC);
            break;

        case '(':
            lexer_push(state, TOKEN_LPAREN);
            break;

        case ')':
            lexer_push(state, TOKEN_RPAREN);
            break;

        case '[':
            lexer_push(state, TOKEN_LBRACK);
            break;

        case ']':
            lexer_push(state, TOKEN_RBRACK);
            break;

        case '{':
            lexer_push(state, TOKEN_LBRACE);
            break;

        case '}':
            lexer_push(state, TOKEN_RBRACE);
            break;

        case ':':
            lexer_push(state, TOKEN_COLON);
            break;

        case ';':
            lexer_push(state, TOKEN_SEMI);
            break;

        case ',':
            lexer_push(state, TOKEN_COMMA);
            break;

        case '.':
            lexer_push(state, TOKEN_DOT);
            break;

        case '#':
            lexer_push(state, TOKEN_HASH);
            break;

        default:
            // TODO: Unknown char error
            lexer_push(state, TOKEN_INVALID);
            break;
    }
}

void
lexer_tokenize(struct lexer *state, struct token **tokens)
{
    while (!lexer_eof(state)) {
        lexer_next(state);
    }

    lexer_push(state, TOKEN_EOF);
    *tokens = state->tokens;
}
