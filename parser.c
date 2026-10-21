#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "parser.h"
#include "report.h"

// TODO: Improve flag tracking for recovery

void
parser_init(struct parser *state, struct source_map *src_map,
            struct source *src,  struct token *tokens)
{
    state->src_map = src_map;
    state->src = src;
    state->tokens = tokens;
    state->curr = 0;
    state->error = false;
    state->perfect = true;
    state->parens = false;
    state->no_block = false;
}

static inline struct token *
parser_curr(struct parser *state)
{
    return &state->tokens[state->curr];
}

static inline struct token *
parser_prev(struct parser *state)
{
    assert(state->curr > 0 && "parser_prev of the first token!");
    return &state->tokens[state->curr - 1];
}

static inline bool
parser_eof(struct parser *state)
{
    return parser_curr(state)->tag == TOKEN_EOF;
}

static inline struct token *
parser_next(struct parser *state)
{
    assert(!parser_eof(state) && "oob read forward");
    return &state->tokens[state->curr + 1];
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
    report_error(state->src_map, &parser_curr(state)->span, "%s\n", msg);

    state->error = true;
    state->perfect = false;
}

static void
parser_error_prev(struct parser *state, const char *msg)
{
    report_error(state->src_map, &parser_prev(state)->span, "%s\n", msg);

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

static inline bool
parser_same_line(struct parser *state)
{
    return parser_curr(state)->same_line;
}

static bool
parser_check_end(struct parser *state, const char *msg)
{
    if (parser_match(state, TOKEN_SEMI))
        return true;

    if (parser_eof(state))
        return true;

    // XXX: {a;b;} or {a;b}
    if (parser_curr(state)->tag == TOKEN_RBRACE)
        return true;

    /*
     * Implicit newline termination
     */
    if (!parser_same_line(state))
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
    *sym = symbol_intern(&state->src->str[token->span.start],
                         token->span.end - token->span.start);
    return true;
}

static enum token_tag
parser_peek_two(struct parser *state)
{
    if (parser_eof(state))
        return TOKEN_EOF;

    return parser_next(state)->tag;
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
    state->parens = false;
    state->no_block = false;

    while (!parser_eof(state)) {
        if (parser_peek_sym(state, &sym)) {
            if (sym == symbol_proc || sym == symbol_struct ||
                sym == symbol_type || sym == symbol_var ||
                sym == symbol_module || sym == symbol_sig)
                return;
        }

        parser_advance(state);
    }
}

/*
 * Types
 */
static bool parser_expr_prec(struct parser *state, struct expr **expr,
                             unsigned base_prec);

static bool
parser_type(struct parser *state, struct type **type)
{
    struct expr *expr;
    bool saved_noblock;

    saved_noblock = state->no_block;
    state->no_block = true;

    /*
     * Parse types as expressions at precedence 12 (prefix & postfix)
     */
    if (!parser_expr_prec(state, &expr, 12))
        return false;

    state->no_block = saved_noblock;

    *type = type_make_raw(expr);
    return true;
}


/*
 * Expressions
 */
static bool parser_decl(struct parser *state, struct decl **decl);

static bool
parser_expr(struct parser *state, struct expr **expr)
{
    return parser_expr_prec(state, expr, 0);
}

static bool
parser_expr_strict(struct parser *state, struct expr **expr)
{
    bool ok, saved = state->no_block;

    state->no_block = true;
    ok = parser_expr_prec(state, expr, 0);
    state->no_block = saved;
    return ok;
}

static bool
parser_expr_block(struct parser *state, struct expr **expr)
{
    struct decl *items[64];
    size_t items_count;
    bool saved_parens, saved_noblock;
    struct span start;

    start = parser_prev(state)->span;

    saved_parens = state->parens;
    saved_noblock = state->no_block;
    state->parens = false;
    state->no_block = false;

    for (items_count = 0; !parser_eof(state); items_count++) {
        if (parser_match(state, TOKEN_RBRACE))
            break;

        assert(items_count < 64 && "too many block items!");
        if (!parser_decl(state, &items[items_count]))
            return false;
    }

    state->parens = saved_parens;
    state->no_block = saved_noblock;

    *expr = expr_make_block(items_count);
    memcpy(((struct expr_block *)*expr)->items, items, items_count * sizeof(struct decl *));
    expr_set_span(*expr, span_merge(start, parser_prev(state)->span));
    return true;
}

static bool
parser_expr_return(struct parser *state, struct expr **expr)
{
    struct expr *value;
    struct span start;
    enum token_tag tag;

    start = parser_prev(state)->span;
    tag = parser_curr(state)->tag;

    value = NULL;
    if (parser_same_line(state)
            && tag != TOKEN_COMMA
            && tag != TOKEN_SEMI
            && tag != TOKEN_RPAREN
            && tag != TOKEN_RBRACK
            && tag != TOKEN_RBRACE) {
        if (!parser_expr(state, &value))
            return false;
    }

    *expr = expr_make_return(value);
    expr_set_span(*expr, span_merge(start, parser_prev(state)->span));
    return true;
}

static bool
parser_expr_if(struct parser *state, struct expr **expr)
{
    struct symbol *sym;
    struct expr *cond;
    struct expr *t_branch, *f_branch;
    struct span start;

    start = parser_prev(state)->span;

    if (!parser_expr_strict(state, &cond))
        return false;

    if (!parser_check(state, TOKEN_LBRACE, "Expected '{' after if condition"))
        return false;

    if (!parser_expr_block(state, &t_branch))
        return false;

    f_branch = NULL;
    if (parser_peek_sym(state, &sym) && sym == symbol_else) {
        parser_advance(state);

        /*
         * Desugar the 'else if' in another expr_if node
         */
        if (parser_peek_sym(state, &sym) && sym == symbol_if) {
            parser_advance(state);

            if (!parser_expr_if(state, &f_branch))
                return false;
        } else {
            if (!parser_check(state, TOKEN_LBRACE, "Expected '{' or 'if' after else"))
                return false;

            if (!parser_expr_block(state, &f_branch))
                return false;
        }
    }

    *expr = expr_make_if(cond, t_branch, f_branch);
    expr_set_span(*expr, span_merge(start, parser_prev(state)->span));
    return true;
}

static bool
parser_expr_while(struct parser *state, struct expr **expr)
{
    struct expr *cond;
    struct expr *body;
    struct span start;

    start = parser_prev(state)->span;

    if (!parser_expr_strict(state, &cond))
        return false;

    if (!parser_check(state, TOKEN_LBRACE, "Expected '{' after while condition"))
        return false;

    if (!parser_expr_block(state, &body))
        return false;

    *expr = expr_make_while(cond, body);
    expr_set_span(*expr, span_merge(start, parser_prev(state)->span));
    return true;
}

static bool
parser_integer(struct parser *state, int64_t *value)
{
    unsigned start, end;
    size_t len, i;
    int digit, base;
    const char *str;

    start = parser_prev(state)->span.start;
    end = parser_prev(state)->span.end;
    len = end - start;
    str = &state->src->str[start];

    base = 10;
    *value = 0;

    if (len > 2 && str[0] == '0') {
        if (str[1] == 'x') {
            base = 16;
        } else if (str[1] == 'o') {
            base = 8;
        } else if (str[1] == 'b') {
            base = 2;
        } else {
            parser_error_prev(state, "Malformed integer literal");
            return false;
        }

        len -= 2;
        str += 2;

        if (len == 0) {
            parser_error_prev(state, "Missing digits after integer prefix");
            return false;
        }
    }

    for (i = 0; i < len; i++) {
        digit = -1;
        if (isdigit(str[i])) {
            digit = str[i] - '0';
        } else if (isupper(str[i])) {
            digit = str[i] - 'A' + 10;
        } else if (islower(str[i])) {
            digit = str[i] - 'a' + 10;
        }

        if (digit < 0 || digit >= base) {
            parser_error_prev(state, "Invalid digit in integer literal");
            return false;
        }

        if (*value > (INT64_MAX - digit) / base) {
            parser_error_prev(state, "Integer literal out of range");
            return false;
        }

        *value *= base;
        *value += digit;
        start++;
    }

    // TODO: Handle unsigned literals as well
    return true;
}

static bool
parser_escape_string(struct parser *state, const char **string)
{
    const char *start;
    struct span span;
    size_t i, j, len;
    char c, *buf;

    span = parser_prev(state)->span;
    start = &state->src->str[span.start];
    len = (span.end - span.start) - 2;
    buf = malloc(len + 1);

    for (i = 0, j = 0; j < len; ++j) {
        c = start[j + 1];

        if (c == '\\' && j + 1 < len) {
            j++;
            c = start[j + 1];

            switch (c) {
                case 'n':
                    buf[i++] = '\n';
                    break;
                case 't':
                    buf[i++] = '\t';
                    break;
                case 'r':
                    buf[i++] = '\r';
                    break;
                case '0':
                    buf[i++] = '\0';
                    break;
                case '\\':
                    buf[i++] = '\\';
                    break;
                case '"':
                    buf[i++] = '"';
                    break;
                default:
                    buf[i++] = c;
                    break;
            }
        } else {
            buf[i++] = c;
        }
    }

    buf[i] = '\0';
    *string = buf;
    return true;
}

static bool
parser_expr_struct(struct parser *state, struct expr *op, struct expr **expr)
{
    struct expr *lhs, *rhs;
    struct struct_init fields[32];
    struct symbol *field;
    bool saved_parens, saved_noblock, colon;
    unsigned count;

    saved_parens = state->parens;
    saved_noblock = state->no_block;
    state->parens = true;
    state->no_block = false;

    count = 0;
    while (!parser_match(state, TOKEN_RBRACE)) {
        if (!parser_check_sym(state, &field, "Expected field name"))
            return false;

        lhs = expr_make_ident(field);
        expr_set_span(lhs, parser_prev(state)->span);

        rhs = NULL;
        colon = parser_curr(state)->tag == TOKEN_COLON;

        if (parser_match(state, TOKEN_EQ) || parser_match(state, TOKEN_COLON)) {
            if (!parser_expr(state, &rhs))
                return false;

            if (!parser_check_end(state, "Expected newline or ';' after field assignment"))
                return false;
        } else {
            if (!parser_check_end(state, "Expected separator or terminator after field name"))
                return false;
        }

        assert(count < 32 && "too many fields");
        fields[count].field = (struct expr_ident *)lhs;
        fields[count].value = rhs;
        fields[count].colon = colon;
        count++;
    }

    state->parens = saved_parens;
    state->no_block = saved_noblock;

    *expr = expr_make_struct(op, count);
    memcpy(((struct expr_struct *)*expr)->fields, fields, count * sizeof(struct struct_init));
    return true;
}

static bool
parser_expr_simple(struct parser *state, struct expr **expr)
{
    int64_t value;
    struct symbol *sym;
    struct expr *op, *args[32];
    const char *string;
    struct expr *body;
    size_t count, i;
    bool saved_parens, saved_noblock;
    struct span start;

    start = parser_curr(state)->span;

    if (parser_match(state, TOKEN_INT)) {
        if (!parser_integer(state, &value))
            return false;

        *expr = expr_make_literal(LIT_INTEGER, value);
        goto done;
    }

    if (parser_match(state, TOKEN_STRING)) {
        if (!parser_escape_string(state, &string))
            return false;

        *expr = expr_make_literal(LIT_STRING, (intptr_t)string);
        goto done;
    }

    if (parser_match(state, TOKEN_LBRACE)) {
        /*
         * Parse as a full-fledged block, not as a struct literal
         */
        return parser_expr_block(state, expr);
    }

    if (parser_match_sym(state, &sym)) {
        /*
         * Expression statements
         */
        if (sym == symbol_return)
            return parser_expr_return(state, expr);

        if (sym == symbol_if)
            return parser_expr_if(state, expr);

        if (sym == symbol_while)
            return parser_expr_while(state, expr);

        if (sym == symbol_break) {
            *expr = expr_make_break();
            goto done;
        }

        if (sym == symbol_continue) {
            *expr = expr_make_continue();
            goto done;
        }

        /*
         * Literals
         */
        if (sym == symbol_struct) {
            if (!parser_check(state, TOKEN_LBRACE, "Expected '{' after 'struct'"))
                return false;

            if (!parser_expr_struct(state, NULL, expr))
                return false;
        } else if (sym == symbol_true || sym == symbol_false) {
            *expr = expr_make_literal(LIT_BOOL, sym == symbol_true);
        } else {
            // TODO: Check for disallowed keywords?
            *expr = expr_make_ident(sym);
        }
        goto done;
    }

    if (parser_match(state, TOKEN_LPAREN)) {
        saved_parens = state->parens;
        saved_noblock = state->no_block;
        state->parens = true;
        state->no_block = false;

        if (!parser_expr_prec(state, expr, 0))
            return false;

        if (!parser_check(state, TOKEN_RPAREN, "Expected ')' after expr"))
            return false;

        state->parens = saved_parens;
        state->no_block = saved_noblock;

        // Do not merge the span containing parenthesis
        return true;
    }

    if (parser_match(state, TOKEN_LBRACK)) {
        saved_parens = state->parens;
        saved_noblock = state->no_block;
        state->parens = true;
        state->no_block = false;

        count = 0;
        if (!parser_match(state, TOKEN_RBRACK)) {
            do {
                if (parser_curr(state)->tag == TOKEN_RBRACK)
                    break;

                assert(count < 32 && "too many array elements");
                if (!parser_expr_prec(state, &args[count++], 0))
                    return false;
            } while (parser_match(state, TOKEN_COMMA));

            if (!parser_check(state, TOKEN_RBRACK, "Expected ']' after array"))
                return false;
        }

        state->parens = saved_parens;
        state->no_block = saved_noblock;

        *expr = expr_make_array(count);
        for (i = 0; i < count; i++) {
            ((struct expr_array *)*expr)->args[i] = args[i];
        }
        goto done;
    }

    if (parser_match(state, TOKEN_MINUS)) {
        if (!parser_expr_prec(state, &op, 12))
            return false;

        *expr = expr_make_unop(op, UNOP_NEG);
        goto done;
    }

    if (parser_match(state, TOKEN_NOT)) {
        if (!parser_expr_prec(state, &op, 12))
            return false;

        *expr = expr_make_unop(op, UNOP_NOT);
        goto done;
    }

    if (parser_match(state, TOKEN_STAR)) {
        if (!parser_expr_prec(state, &op, 12))
            return false;

        *expr = expr_make_unop(op, UNOP_DEREF);
        goto done;
    }

    if (parser_match(state, TOKEN_AND)) {
        if (!parser_expr_prec(state, &op, 12))
            return false;

        *expr = expr_make_unop(op, UNOP_ADDROF);
        goto done;
    }

    if (parser_match(state, TOKEN_AT)) {
        if (!parser_check_sym(state, &sym, "Expected macro name after '@'"))
            return false;

        count = 0;
        if (parser_match(state, TOKEN_LPAREN)) {
            saved_parens = state->parens;
            saved_noblock = state->no_block;
            state->parens = true;
            state->no_block = false;

            if (parser_curr(state)->tag != TOKEN_RPAREN) {
                do {
                    assert(count < 32 && "too many args");
                    if (!parser_expr_prec(state, &args[count++], 0)) {
                        return false;
                    }
                } while (parser_match(state, TOKEN_COMMA));
            }

            if (!parser_check(state, TOKEN_RPAREN, "Expected ')' after macro arguments"))
                return false;

            state->parens = saved_parens;
            state->no_block = saved_noblock;
        }

        body = NULL;
        if (!state->no_block && parser_match(state, TOKEN_LBRACE)) {
            if (!parser_expr_block(state, &body))
                return false;
        }

        *expr = expr_make_macro(sym, body, count);
        for (i = 0; i < count; i++) {
            ((struct expr_macro *)*expr)->args[i] = args[i];
        }
        goto done;
    }

    parser_error(state, "Expected expression");
    return false;

done:
    expr_set_span(*expr, span_merge(start, parser_prev(state)->span));
    return true;
}

static inline unsigned
parser_token_prec(enum token_tag tag)
{
    static const unsigned prec_table[TOKEN_EOF] = {
        [TOKEN_EQ] = 1,
        [TOKEN_ANDEQ] = 1,
        [TOKEN_OREQ] = 1,
        [TOKEN_XOREQ] = 1,
        [TOKEN_SHLEQ] = 1,
        [TOKEN_SHREQ] = 1,
        [TOKEN_PLUSEQ] = 1,
        [TOKEN_MINUSEQ] = 1,
        [TOKEN_STAREQ] = 1,
        [TOKEN_SLASHEQ] = 1,
        [TOKEN_PERCEQ] = 1,
        [TOKEN_OROR] = 2,
        [TOKEN_ANDAND] = 3,
        [TOKEN_OR] = 4,
        [TOKEN_XOR] = 5,
        [TOKEN_AND] = 6,
        [TOKEN_EQEQ] = 7,
        [TOKEN_NOTEQ] = 7,
        [TOKEN_LT] = 8,
        [TOKEN_GT] = 8,
        [TOKEN_LTEQ] = 8,
        [TOKEN_GTEQ] = 8,
        [TOKEN_SHL] = 9,
        [TOKEN_SHR] = 9,
        [TOKEN_PLUS] = 10,
        [TOKEN_MINUS] = 10,
        [TOKEN_STAR] = 11,
        [TOKEN_SLASH] = 11,
        [TOKEN_PERC] = 11,
        [TOKEN_LPAREN] = 13,
        [TOKEN_LBRACE] = 13,
        [TOKEN_LBRACK] = 13,
        [TOKEN_DOT] = 13,
    };
    return prec_table[tag];
}

static inline enum binop_tag
parser_token_binop(enum token_tag tag)
{
    static const enum binop_tag binop_table[TOKEN_EOF] = {
        [TOKEN_OR] = BINOP_OR,
        [TOKEN_AND] = BINOP_AND,
        [TOKEN_OROR] = BINOP_BOOL_OR,
        [TOKEN_ANDAND] = BINOP_BOOL_AND,
        [TOKEN_XOR] = BINOP_XOR,
        [TOKEN_EQ] = BINOP_SET,
        [TOKEN_GT] = BINOP_GT,
        [TOKEN_LT] = BINOP_LT,
        [TOKEN_SHL] = BINOP_SHL,
        [TOKEN_SHR] = BINOP_SHR,
        [TOKEN_PLUS] = BINOP_ADD,
        [TOKEN_MINUS] = BINOP_SUB,
        [TOKEN_STAR] = BINOP_MUL,
        [TOKEN_SLASH] = BINOP_DIV,
        [TOKEN_PERC] = BINOP_MOD,
        [TOKEN_EQEQ] = BINOP_EQ,
        [TOKEN_NOTEQ] = BINOP_NOTEQ,
        [TOKEN_GTEQ] = BINOP_GTEQ,
        [TOKEN_LTEQ] = BINOP_LTEQ,
        [TOKEN_OREQ] = BINOP_OR_SET,
        [TOKEN_ANDEQ] = BINOP_AND_SET,
        [TOKEN_XOREQ] = BINOP_XOR_SET,
        [TOKEN_SHLEQ] = BINOP_SHL_SET,
        [TOKEN_SHREQ] = BINOP_SHR_SET,
        [TOKEN_PLUSEQ] = BINOP_ADD_SET,
        [TOKEN_MINUSEQ] = BINOP_SUB_SET,
        [TOKEN_STAREQ] = BINOP_MUL_SET,
        [TOKEN_SLASHEQ] = BINOP_DIV_SET,
        [TOKEN_PERCEQ] = BINOP_MOD_SET,
    };
    return binop_table[tag];
}

static bool
parser_expr_prec(struct parser *state, struct expr **expr, unsigned base_prec)
{
    struct expr *rhs, *args[32];
    struct symbol *field;
    enum token_tag op_tag;
    unsigned prec, count, i;
    bool saved_parens, saved_noblock;
    struct span start;

    start = parser_curr(state)->span;

    if (!parser_expr_simple(state, expr))
        return false;

    while (true) {
        if (!state->parens && !parser_same_line(state))
            break;

        op_tag = parser_curr(state)->tag;
        prec = parser_token_prec(op_tag);

        if (prec < base_prec || prec == 0)
            break;

        if (op_tag == TOKEN_LBRACE && state->no_block)
            break;

        parser_advance(state);

        /*
         * Special cases
         */
        if (op_tag == TOKEN_LPAREN) {
            saved_parens = state->parens;
            saved_noblock = state->no_block;
            state->parens = true;
            state->no_block = false;

            count = 0;
            if (parser_curr(state)->tag != TOKEN_RPAREN) {
                do {
                    assert(count < 32 && "too many args");
                    if (!parser_expr_prec(state, &args[count++], 0))
                        return false;
                } while (parser_match(state, TOKEN_COMMA));
            }

            if (!parser_check(state, TOKEN_RPAREN, "Expected ')' after arguments"))
                return false;

            state->parens = saved_parens;
            state->no_block = saved_noblock;

            *expr = expr_make_call(*expr, count);
            for (i = 0; i < count; i++) {
                ((struct expr_call *)*expr)->args[i] = args[i];
            }

            expr_set_span(*expr, span_merge(start, parser_prev(state)->span));
            continue;
        }

        if (op_tag == TOKEN_LBRACK) {
            saved_parens = state->parens;
            saved_noblock = state->no_block;
            state->parens = true;
            state->no_block = false;

            if (!parser_expr_prec(state, &rhs, 0))
                return false;

            if (!parser_check(state, TOKEN_RBRACK, "Expected ']' after index"))
                return false;

            state->parens = saved_parens;
            state->no_block = saved_noblock;

            *expr = expr_make_index(*expr, rhs);
            expr_set_span(*expr, span_merge(start, parser_prev(state)->span));
            continue;
        }

        if (op_tag == TOKEN_LBRACE) {
            if (!parser_expr_struct(state, *expr, expr))
                return false;

            expr_set_span(*expr, span_merge(start, parser_prev(state)->span));
            continue;
        }

        if (op_tag == TOKEN_DOT) {
            if (!parser_check_sym(state, &field, "Expected field name after '.'"))
                return false;

            *expr = expr_make_access(*expr, field);
            expr_set_span(*expr, span_merge(start, parser_prev(state)->span));
            continue;
        }

        /*
         * Assignment operators are right associative
         */
        if (prec != 1) {
            prec++;
        }

        if (!parser_expr_prec(state, &rhs, prec))
            return false;

        *expr = expr_make_binop(*expr, rhs, parser_token_binop(op_tag));
        expr_set_span(*expr, span_merge(start, parser_prev(state)->span));
    }

    return true;
}

/*
 * Declarations
 */
static bool parser_decl(struct parser *state, struct decl **decl);

static bool
parser_decl_proc(struct parser *state, struct decl **decl)
{
    struct symbol *sym, *arg_sym;
    struct type *out, *arg_type;
    struct expr *body;
    struct proc_arg *args, **tail, *tmp;
    size_t args_count;
    struct span start;

    start = parser_prev(state)->span;

    if (!parser_check_sym(state, &sym, "Expected procedure name"))
        return false;

    if (!parser_check(state, TOKEN_LPAREN, "Expected '(' after function name"))
        return false;

    args = NULL;
    args_count = 0;
    tail = &args;

    if (!parser_match(state, TOKEN_RPAREN)) {
        do {
            if (!parser_check_sym(state, &arg_sym, "Expected arg name"))
                return false;

            if (!parser_check(state, TOKEN_COLON, "Expected ':' after arg name"))
                return false;

            arg_type = NULL;
            if (!parser_type(state, &arg_type))
                return false;

            tmp = calloc(1, sizeof(struct proc_arg));
            tmp->sym = arg_sym;
            tmp->type = arg_type;

            *tail = tmp;
            tail = &tmp->next;
            args_count++;
        } while (parser_match(state, TOKEN_COMMA));

        if (!parser_check(state, TOKEN_RPAREN, "Expected ')' after function args"))
            return false;
    }

    out = NULL;
    if (parser_match(state, TOKEN_COLON)) {
        if (!parser_type(state, &out))
            return false;
    }

    body = NULL;
    if (parser_match(state, TOKEN_LBRACE)) {
        if (!parser_expr_block(state, &body))
            return false;
    } else {
        if (!parser_check_end(state, "Expected '{' or termination after proc signature"))
            return false;
    }

    if (!parser_check_end(state, "Expected newline or ';' after declaration"))
        return false;

    *decl = decl_make_proc(sym, out, args, args_count, body);
    decl_set_span(*decl, span_merge(start, parser_prev(state)->span));
    return true;
}

static bool
parser_decl_struct(struct parser *state, struct decl **decl)
{
    struct symbol *sym, *field_sym;
    struct type *field_type;
    struct struct_field *copy, fields[64];
    size_t fields_count;
    struct span start;

    start = parser_prev(state)->span;

    if (!parser_check_sym(state, &sym, "Expected structure name"))
        return false;

    if (!parser_check(state, TOKEN_LBRACE, "Expected '{' after structure"))
        return false;

    fields_count = 0;
    while (!parser_match(state, TOKEN_RBRACE)) {
        if (!parser_check_sym(state, &field_sym, "Expected field name"))
            return false;

        if (!parser_check(state, TOKEN_COLON, "Expected ':' after field name"))
            return false;

        field_type = NULL;
        if (!parser_type(state, &field_type))
            return false;

        assert(fields_count < 64 && "too many fields!");
        fields[fields_count].name = field_sym;
        fields[fields_count].type = field_type;
        fields[fields_count].index = fields_count;
        fields_count++;

        if (!parser_check_end(state, "Expected newline or ';' after field"))
            return false;
    }

    if (!parser_check_end(state, "Expected newline or ';' after declaration"))
        return false;

    copy = malloc(fields_count * sizeof(struct struct_field));
    memcpy(copy, fields, fields_count * sizeof(struct struct_field));

    *decl = decl_make_struct(sym, copy, fields_count);
    decl_set_span(*decl, span_merge(start, parser_prev(state)->span));
    return true;
}

static bool
parser_decl_type(struct parser *state, struct decl **decl)
{
    struct symbol *sym;
    struct type *type;
    struct span start;

    start = parser_prev(state)->span;

    if (!parser_check_sym(state, &sym, "Expected type name"))
        return false;

    type = NULL;
    if (parser_match(state, TOKEN_EQ)) {
        if (!parser_type(state, &type))
            return false;
    }

    if (!parser_check_end(state, "Expected newline or ';' after declaration"))
        return false;

    *decl = decl_make_type(sym, type);
    decl_set_span(*decl, span_merge(start, parser_prev(state)->span));
    return true;
}

static bool
parser_decl_module(struct parser *state, struct decl **decl)
{
    struct symbol *sym;
    struct type *sig;
    struct expr *block;
    struct span start;

    start = parser_prev(state)->span;

    if (!parser_check_sym(state, &sym, "Expected module name"))
        return false;

    // module name [: sig] [ { ... } ]

    sig = NULL;
    if (parser_match(state, TOKEN_COLON)) {
        if (!parser_type(state, &sig))
            return false;
    }

    block = NULL;
    if (parser_match(state, TOKEN_LBRACE)) {
        if (!parser_expr_block(state, &block))
            return false;
    }

    if (!parser_check_end(state, "Expected newline or ';' after declaration"))
        return false;

    *decl = decl_make_module(sym, sig, block);
    decl_set_span(*decl, span_merge(start, parser_prev(state)->span));
    return true;
}

static bool
parser_decl_sig(struct parser *state, struct decl **decl)
{
    struct symbol *sym;
    struct expr *block;
    struct span start;

    start = parser_prev(state)->span;

    if (!parser_check_sym(state, &sym, "Expected signature name"))
        return false;

    block = NULL;
    if (parser_match(state, TOKEN_LBRACE)) {
        if (!parser_expr_block(state, &block))
            return false;
    }

    if (!parser_check_end(state, "Expected newline or ';' after declaration"))
        return false;

    *decl = decl_make_sig(sym, block);
    decl_set_span(*decl, span_merge(start, parser_prev(state)->span));
    return true;
}

static bool
parser_decl_import(struct parser *state, struct decl **decl)
{
    struct expr *expr;
    bool saved_parens, saved_noblock;
    struct span start;

    start = parser_prev(state)->span;

    saved_parens = state->parens;
    saved_noblock = state->no_block;
    state->parens = false;
    state->no_block = false;

    if (!parser_expr(state, &expr))
        return false;

    state->parens = saved_parens;
    state->no_block = saved_noblock;

    if (!parser_check_end(state, "Expected newline or ';' after declaration"))
        return false;

    *decl = decl_make_import(expr);
    decl_set_span(*decl, span_merge(start, parser_prev(state)->span));
    return true;
}

static bool
parser_decl_var(struct parser *state, struct decl **decl)
{
    struct symbol *name;
    struct type *type;
    struct expr *value;
    struct span start;

    start = parser_prev(state)->span;

    if (!parser_check_sym(state, &name, "Expected var name"))
        return false;

    type = NULL;
    if (parser_match(state, TOKEN_COLON)) {
        if (!parser_type(state, &type))
            return false;
    }

    value = NULL;
    if (parser_match(state, TOKEN_EQ)) {
        if (!parser_expr(state, &value))
            return false;
    }

    if (!parser_check_end(state, "Expected newline or ';' after statement"))
        return false;

    *decl = decl_make_var(name, type, value);
    decl_set_span(*decl, span_merge(start, parser_prev(state)->span));
    return true;
}

static bool
parser_decl(struct parser *state, struct decl **decl)
{
    struct symbol *sym;
    struct expr *expr;

    if (parser_peek_sym(state, &sym)) {
        if (sym == symbol_proc) {
            parser_advance(state);
            return parser_decl_proc(state, decl);
        } else if (sym == symbol_struct) {
            /*
             * There is a slight ambiguity with struct literals expressions
             *
             * struct { ... } vs struct Sym { ... }
             */
            if (parser_peek_two(state) != TOKEN_LBRACE) {
                parser_advance(state);
                return parser_decl_struct(state, decl);
            }
        } else if (sym == symbol_type) {
            parser_advance(state);
            return parser_decl_type(state, decl);
        } else if (sym == symbol_module) {
            parser_advance(state);
            return parser_decl_module(state, decl);
        } else if (sym == symbol_sig) {
            parser_advance(state);
            return parser_decl_sig(state, decl);
        } else if (sym == symbol_import) {
            parser_advance(state);
            return parser_decl_import(state, decl);
        } else if (sym == symbol_var) {
            parser_advance(state);
            return parser_decl_var(state, decl);
        }
    }

    if (!parser_expr(state, &expr))
        return false;

    if (!parser_check_end(state, "Expected newline or ';' after expression"))
        return false;

    *decl = decl_make_expr(expr);
    decl_set_span(*decl, expr->span);
    return true;
}

bool
parser_module(struct parser *state, struct decl **decl)
{
    struct expr *block;
    struct decl **items;
    size_t items_count, items_size;
    struct span span;

    span = parser_curr(state)->span;

    items = NULL;
    items_size = 0;
    items_count = 0;

    while (!parser_eof(state)) {
        if (items_count >= items_size) {
            items_size = items_size < 32 ? 32 : items_size * 2;
            items = realloc(items, items_size * sizeof(struct decl *));
            assert(items && "out of mem");
        }

        if (parser_decl(state, &items[items_count])) {
            items_count++;
        } else {
            parser_sync(state);
        }
    }

    if (state->curr) {
        span = span_merge(span, parser_prev(state)->span);
    }

    /*
     * Simulate parser_expr_block and create the block manually
     */
    block = expr_make_block(items_count);
    memcpy(((struct expr_block *)block)->items, items, items_count * sizeof(struct decl *));
    expr_set_span(block, span);
    free(items);

    *decl = decl_make_module(NULL, NULL, block);
    decl_set_span(*decl, span);
    return state->perfect;
}
