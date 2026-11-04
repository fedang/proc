#include "eval.h"
#include "report.h"

#define DEFAULT_FUEL    1000

#define EVAL_REPORT(state, span, ...) \
    report_error((state)->src_map, &(span), __VA_ARGS__)

void
eval_init(struct eval *state, struct source_map *src_map)
{
    state->src_map = src_map;
    state->env = NULL;
    state->fuel = 0;
}

void
eval_env_push(struct eval *state, struct environ *env)
{
    env->parent = state->env;
    state->env = env;
}

void
eval_env_pop(struct eval *state)
{
    assert(state->env != NULL);
    state->env = state->env->parent;
}

void
eval_env_bind(struct eval *state, struct named var)
{
    assert(state->env->vars_count < 64 && "too many vars!");
    state->env->vars[state->env->vars_count] = var;
    state->env->vars_count++;
}

static enum eval_status
eval_expr(struct eval *state, struct expr *expr, struct value *out)
{
    enum eval_status status;
    struct expr_unop *unop;
    struct expr_binop *binop;
    struct expr_macro *macro;
    struct value lhs, rhs;

    if (state->fuel == 0)
        return EVAL_EXHAUSTED;
    state->fuel--;

    switch (expr->tag) {
        case EXPR_LITERAL:
            *out = expr->ctval;
            return EVAL_OK;

        case EXPR_UNOP:
            unop = (struct expr_unop *)expr;
            status = eval_expr(state, unop->op, out);
            if (status != EVAL_OK)
                return status;

            if (unop->unop == UNOP_NEG) {
                out->integer *= -1;
                return EVAL_OK;
            }

            if (unop->unop == UNOP_NOT) {
                out->integer = !out->integer;
                return EVAL_OK;
            }

            EVAL_REPORT(state, expr->span,
                      "Invalid unary operator in constant expression\n");
            return EVAL_ERROR;

        case EXPR_BINOP:
            binop = (struct expr_binop *)expr;
            status = eval_expr(state, binop->op_lhs, &lhs);
            if (status != EVAL_OK)
                return status;

            status = eval_expr(state, binop->op_rhs, &rhs);
            if (status != EVAL_OK)
                return status;

            out->tag = VALUE_INTEGER;
            switch (binop->binop) {
                case BINOP_ADD:
                    out->integer = lhs.integer + rhs.integer;
                    return EVAL_OK;

                case BINOP_SUB:
                    out->integer = lhs.integer - rhs.integer;
                    return EVAL_OK;

                case BINOP_MUL:
                    out->integer = lhs.integer * rhs.integer;
                    return EVAL_OK;

                case BINOP_DIV:
                    if (rhs.integer == 0) {
                        EVAL_REPORT(state, expr->span,
                                   "Division by zero in constant expression\n");
                        return EVAL_ERROR;
                    }
                    out->integer = lhs.integer / rhs.integer;
                    return EVAL_OK;

                case BINOP_MOD:
                    if (rhs.integer == 0) {
                        EVAL_REPORT(state, expr->span,
                                   "Modulo by zero in constant expression\n");
                        return EVAL_ERROR;
                    }
                    out->integer = lhs.integer % rhs.integer;
                    return EVAL_OK;

                case BINOP_SHL:
                    out->integer = lhs.integer << rhs.integer;
                    return EVAL_OK;

                case BINOP_SHR:
                    out->integer = lhs.integer >> rhs.integer;
                    return EVAL_OK;

                case BINOP_OR:
                    out->integer = lhs.integer | rhs.integer;
                    return EVAL_OK;

                case BINOP_AND:
                    out->integer = lhs.integer & rhs.integer;
                    return EVAL_OK;

                case BINOP_XOR:
                    out->integer = lhs.integer ^ rhs.integer;
                    return EVAL_OK;

                default:
                    EVAL_REPORT(state, expr->span,
                               "Operator not allowed in constant expression\n");
                    return EVAL_ERROR;
            }

        case EXPR_MACRO:
            macro = (struct expr_macro *)expr;
            switch (macro->macro.tag) {
                case MACRO_SIZEOF:
                case MACRO_ALIGNOF:
                    *out = value_make_integer(macro->macro.value);
                    return EVAL_OK;

                case MACRO_EXPR:
                    if (!macro->macro.expr)
                        return EVAL_NOT_CONST;
                    return eval_expr(state, macro->macro.expr, out);

                default:
                    return EVAL_NOT_CONST;
            }

        default:
            return EVAL_NOT_CONST;
    }
}

enum eval_status
eval_try_ctval(struct eval *state, struct expr *expr)
{
    struct value ctval;
    enum eval_status status;

    /*
     * We already computed the compile-time value
     */
    if (expr->ctval.tag != VALUE_INVALID) {
        assert(expr->ctval.tag != VALUE_PENDING);
        return EVAL_OK;
    }

    state->fuel = DEFAULT_FUEL;

    status = eval_expr(state, expr, &ctval);
    if (status == EVAL_EXHAUSTED) {
        EVAL_REPORT(state, expr->span,
                    "Expression exceeded constant evaluation steps limit\n");
        return EVAL_ERROR;
    }

    if (status == EVAL_OK)
        expr->ctval = ctval;

    return status;
}
