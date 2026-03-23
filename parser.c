#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include "parser.h"

static struct symbol *sym_let;
static struct symbol *sym_var;
static struct symbol *sym_if;
static struct symbol *sym_else;
static struct symbol *sym_return;
static struct symbol *sym_proc;

static struct symbol *sym_int;
static struct symbol *sym_uint;
static struct symbol *sym_bool;
static struct symbol *sym_void;

void
parser_init(struct parser *state, struct token *tokens)
{
    state->tokens = tokens;
    state->curr = 0;
    state->error = false;
    state->perfect = true;

    /*
     * Intern keyword symbols
     */
    sym_let = symbol_make("let");
    sym_var = symbol_make("var");
    sym_if = symbol_make("if");
    sym_else = symbol_make("else");
    sym_return = symbol_make("return");
    sym_proc = symbol_make("proc");

    /*
     * Intern type symbols
     */
    sym_int = symbol_make("int");
    sym_uint = symbol_make("uint");
    sym_bool = symbol_make("bool");
    sym_void = symbol_make("void");
}

static inline struct token *
parser_curr(struct parser *state)
{
    return &state->tokens[state->curr];
}

static inline struct token *
parser_prev(struct parser *state)
{
    return &state->tokens[state->curr - 1];
}

static inline bool
parser_eof(struct parser *state)
{
    return parser_curr(state)->tag == TOKEN_EOF;
}

static inline void
parser_advance(struct parser *state)
{
    if (!parser_eof(state))
        state->curr++;
}

static inline bool
parser_match(struct parser *state, enum token_tag tag)
{
    if (parser_curr(state)->tag != tag)
        return false;

    parser_advance(state);
    return true;
}

static void
parser_error(struct parser *state, const char *msg)
{
    printf("Error at line %u: %s\n",
           parser_curr(state)->source.start_line, msg);

    state->error = true;
    state->perfect = false;
}

static inline bool
parser_check(struct parser *state, enum token_tag tag, const char *msg)
{
    if (parser_match(state, tag))
        return true;

    parser_error(state, msg);
    return false;
}

static bool
parser_peek_sym(struct parser *state, struct symbol **sym)
{
    struct token *token;

    if (parser_curr(state)->tag != TOKEN_SYMBOL)
        return false;

    token = parser_curr(state);
    *sym = symbol_intern(token->source.start,
                         token->source.end - token->source.start);
    return true;
}

static bool
parser_match_sym(struct parser *state, struct symbol **sym)
{
    if (!parser_peek_sym(state, sym))
        return false;

    parser_advance(state);
    return true;
}

static bool
parser_check_sym(struct parser *state, struct symbol **sym, const char *msg)
{
    if (parser_match_sym(state, sym))
        return true;

    parser_error(state, msg);
    return false;
}

static void
parser_sync(struct parser *state)
{
    struct symbol *sym;

    state->error = false;

    while (!parser_eof(state)) {
        if (parser_peek_sym(state, &sym)) {
            if (sym == sym_proc)
                return;
        }

        parser_advance(state);
    }
}

/*
 * Types
 */
static bool
parser_type(struct parser *state, struct type **type)
{
    struct symbol *sym;
    struct type *pointer;

    if (parser_match(state, TOKEN_STAR)) {
        if (!parser_type(state, &pointer))
            return false;

        *type = type_get_ptr(NULL, pointer);
        return true;
    }

    if (!parser_check_sym(state, &sym, "Expected primitive type"))
        return false;

    if (sym == sym_int) {
        *type = type_get_int(NULL, 0);
        return true;
    }

    if (sym == sym_uint) {
        *type = type_get_uint(NULL, 0);
        return true;
    }

    if (sym == sym_bool) {
        *type = type_get_bool(NULL);
        return true;
    }

    if (sym == sym_void) {
        *type = type_get_void(NULL);
        return true;
    }

    return false;
}

/*
 * Expressions
 */
static bool parser_expr(struct parser *state, struct expr **expr);

static bool
parser_expr(struct parser *state, struct expr **expr)
{
    int64_t value;

    if (parser_match(state, TOKEN_INT)) {
        value = strtol(parser_prev(state)->source.start, NULL, 10);
        *expr = expr_make_const(value);
        return true;
    }

    return false;
}

/*
 * Statements
 */
static bool parser_stmt(struct parser *state, struct stmt **stmt);

static bool
parser_stmt_block(struct parser *state, struct stmt **stmt)
{
    struct stmt *tmp[32];
    unsigned n;

    n = 0;

    while (!parser_eof(state)) {
        if (parser_match(state, TOKEN_RBRACE))
            break;

        assert(n++ <= 32);

        if (!parser_stmt(state, &tmp[n-1]))
            return false;
    }

    *stmt = stmt_make_block(n);
    memcpy(((struct stmt_block *)*stmt)->items, tmp, n * sizeof(struct stmt *));
    return true;
}

static bool
parser_stmt_var(struct parser *state, struct stmt **stmt)
{
    struct symbol *name;
    struct type *type;
    struct expr *value;

    if (!parser_check_sym(state, &name, "Expected var name"))
        return false;

    type = NULL;
    if (parser_match(state, TOKEN_COLON)) {
        if (!parser_type(state, &type))
            return false;
    }

    if (!parser_check(state, TOKEN_EQ, "Expected '=' after var"))
        return false;

    if (!parser_expr(state, &value))
        return false;

    if (!parser_check(state, TOKEN_SEMI, "Expected ';' after var"))
        return false;

    *stmt = stmt_make_var(name, type, value);
    return true;
}

static bool
parser_stmt_return(struct parser *state, struct stmt **stmt)
{
    struct expr *value;

    value = NULL;
    if (parser_curr(state)->tag != TOKEN_SEMI) {
        if (!parser_expr(state, &value))
            return false;

    }

    if (!parser_check(state, TOKEN_SEMI, "Expect ';' after return"))
        return false;

    *stmt = stmt_make_return(value);
    return true;
}

static bool
parser_stmt_if(struct parser *state, struct stmt **stmt)
{
    struct symbol *sym;
    struct expr *cond;
    struct stmt *t_branch, *f_branch;

    if (!parser_expr(state, &cond))
        return false;

    if (!parser_check(state, TOKEN_LBRACE, "Expect '{' after if condition"))
        return false;

    if (!parser_stmt_block(state, &t_branch))
        return false;

    f_branch = NULL;
    if (parser_peek_sym(state, &sym) && sym == sym_else) {
        parser_advance(state);

        if (!parser_check(state, TOKEN_LBRACE, "Expect '{' after else"))
            return false;

        if (!parser_stmt_block(state, &f_branch))
            return false;
    }

    *stmt = stmt_make_if(cond, t_branch, f_branch);
    return true;
}

static bool
parser_stmt(struct parser *state, struct stmt **stmt)
{
    struct symbol *sym;
    struct expr *expr;

    if (parser_match(state, TOKEN_LBRACE)) {
        return parser_stmt_block(state, stmt);
    }

    if (parser_peek_sym(state, &sym)) {
        if (sym == sym_return) {
            parser_advance(state);
            return parser_stmt_return(state, stmt);
        }

        if (sym == sym_if) {
            parser_advance(state);
            return parser_stmt_if(state, stmt);
        }

        if (sym == sym_var) {
            parser_advance(state);
            return parser_stmt_var(state, stmt);
        }
    }

    if (!parser_expr(state, &expr))
        return false;

    if (!parser_check(state, TOKEN_SEMI, "Expect ';' after stmt"))
        return false;

    *stmt = stmt_make_expr(expr);
    return true;
}

/*
 * Declarations
 */
static bool
parser_decl_proc(struct parser *state, struct decl **decl)
{
    struct symbol *sym, *arg_sym;
    struct type *out, *arg_type;
    struct stmt *body;
    struct proc_arg *args, **tail, *tmp;

    if (!parser_check_sym(state, &sym, "Expected procedure name"))
        return false;

    parser_check(state, TOKEN_LPAREN, "Expected '(' after function name");

    args = NULL;
    tail = &args;

    if (!parser_match(state, TOKEN_RPAREN)) {
        do {
            if (!parser_check_sym(state, &arg_sym, "Expected arg name"))
                return false;

            parser_check(state, TOKEN_COLON, "Expected ':' after arg name");

            arg_type = NULL;
            if (!parser_type(state, &arg_type))
                return false;

            tmp = calloc(1, sizeof(struct proc_arg));
            tmp->sym = arg_sym;
            tmp->type = arg_type;

            *tail = tmp;
            tail = &tmp->next;
        } while (parser_match(state, TOKEN_COMMA));

        parser_check(state, TOKEN_RPAREN, "Expected ')' after function args");
    }

    out = NULL;
    if (parser_match(state, TOKEN_COLON)) {
        if (!parser_type(state, &out))
            return false;
    }

    if (parser_match(state, TOKEN_SEMI)) {
        body = NULL;
    } else {
        parser_check(state, TOKEN_LBRACE, "Expected '{' or ';' after function signature.");
        if (!parser_stmt_block(state, &body))
            return false;
    }

    *decl = decl_make_proc(sym, out, args, body);
    return true;
}

static bool
parser_decl(struct parser *state, struct decl **decl)
{
    struct symbol *sym;

    if (!parser_check_sym(state, &sym, "Expected proc decl"))
        return false;

    if (sym == sym_proc) {
        return parser_decl_proc(state, decl);
    }

    parser_error(state, "Unknown declaration type");
    return false;
}

bool
parser_module(struct parser *state, struct decl ***decls, size_t *decls_count)
{
    size_t max;

    max = 0;
    *decls = NULL;
    *decls_count = 0;

    while (!parser_eof(state)) {
        if (*decls_count >= max) {
            max = max < 32 ? 32 : max * 2;
            *decls = realloc(*decls, max * sizeof(struct decl *));
            assert(*decls && "out of mem");
        }

        if (parser_decl(state, &(*decls)[*decls_count])) {
            (*decls_count)++;
        } else {
            parser_sync(state);
        }
    }

    return state->perfect;
}
