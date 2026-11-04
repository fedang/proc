#include "eval.h"
#include "report.h"

#define DEFAULT_FUEL    1000

#define EVAL_REPORT(state, span, ...) \
    report_error((state)->src_map, &(span), __VA_ARGS__)

#define EVAL_BURN(state) \
    do { \
        if ((state)->fuel == 0) \
            return EVAL_EXHAUSTED; \
        (state)->fuel--; \
    } while (false)

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
    assert(state->env != NULL);
    assert(state->env->vars_count < 64 && "too many vars!");
    state->env->vars[state->env->vars_count] = var;
    state->env->vars_count++;
}

static int64_t
eval_normalize(struct type *type, int64_t val)
{
    size_t bits;
    uint64_t mask, tmp;

    /*
     * Don't change the value while in an untyped contex (e.g. array sizes)
     */
    switch (type->tag) {
        case TYPE_INT:
        case TYPE_UINT:
        case TYPE_PTR:
            break;

        case TYPE_BOOL:
            return val ? 1 : 0;

        default:
            return val;
    }

    bits = type_bit_width(type);
    if (bits >= 64)
        return val;

    mask = (1ULL << bits) - 1;
    tmp = (uint64_t)val & mask;

    if (type->tag == TYPE_INT && (tmp & (1ULL << (bits - 1))))
        return (int64_t)(tmp | ~mask);

    return (int64_t)tmp;
}

static enum eval_status eval_expr(struct eval *state, struct expr *expr,
                                  struct value *out);

static enum eval_status
eval_expr_unop(struct eval *state, struct expr_unop *expr, struct value *out)
{
    enum eval_status status;

    status = eval_expr(state, expr->op, out);
    if (status != EVAL_OK)
        return status;

    switch (expr->unop) {
        case UNOP_NEG:
            assert(out->tag == VALUE_INTEGER);
            out->integer = eval_normalize(expr->expr.type, -out->integer);
            return EVAL_OK;

        case UNOP_NOT:
            assert(out->tag == VALUE_BOOL);
            out->integer = !out->integer;
            return EVAL_OK;

        default:
            return EVAL_NOT_CONST;
    }
}

static enum eval_status
eval_binop_arith(struct eval *state, struct expr_binop *expr,
                 int64_t lhs, int64_t rhs, struct value *out)
{
    size_t width;
    int64_t result;
    uint64_t u_lhs, u_rhs;
    bool unsigned_op;

    width = expr->expr.type->tag != TYPE_ANY_INT
          ? type_bit_width(expr->expr.type)
          : INT_SIZE * 8;

    unsigned_op = expr->expr.type->tag == TYPE_UINT
               || expr->expr.type->tag == TYPE_PTR;

    u_lhs = (uint64_t)lhs;
    u_rhs = (uint64_t)rhs;

    switch (expr->binop) {
        case BINOP_ADD:
            result = (int64_t)(u_lhs + u_rhs);
            break;

        case BINOP_SUB:
            result = (int64_t)(u_lhs - u_rhs);
            break;

        case BINOP_MUL:
            result = (int64_t)(u_lhs * u_rhs);
            break;

        case BINOP_DIV:
            if (rhs == 0) {
                EVAL_REPORT(state, expr->expr.span,
                            "Division by zero in constant expression\n");
                return EVAL_ERROR;
            }
            result = unsigned_op ? (int64_t)(u_lhs / u_rhs) : lhs / rhs;
            break;

        case BINOP_MOD:
            if (rhs == 0) {
                EVAL_REPORT(state, expr->expr.span,
                            "Modulo by zero in constant expression\n");
                return EVAL_ERROR;
            }
            result = unsigned_op ? (int64_t)(u_lhs % u_rhs) : lhs % rhs;
            break;

        case BINOP_SHL:
            if (rhs < 0 || (size_t)rhs >= width) {
                EVAL_REPORT(state, expr->expr.span,
                            "Shift amount out of bounds in constant expression\n");
                return EVAL_ERROR;
            }
            result = (int64_t)(u_lhs << rhs);
            break;

        case BINOP_SHR:
            if (rhs < 0 || (size_t)rhs >= width) {
                EVAL_REPORT(state, expr->expr.span,
                            "Shift amount out of bounds in constant expression\n");
                return EVAL_ERROR;
            }
            result = unsigned_op ? (int64_t)(u_lhs >> rhs) : lhs >> rhs;
            break;

        case BINOP_OR:
            result = (int64_t)(u_lhs | u_rhs);
            break;

        case BINOP_AND:
            result = (int64_t)(u_lhs & u_rhs);
            break;

        case BINOP_XOR:
            result = (int64_t)(u_lhs ^ u_rhs);
            break;

        default:
            return EVAL_NOT_CONST;
    }

    *out = value_make_integer(eval_normalize(expr->expr.type, result));
    return EVAL_OK;
}

static enum eval_status
eval_binop_compare(struct eval *state, struct expr_binop *expr,
                   int64_t lhs, int64_t rhs, struct value *out)
{
    uint64_t u_lhs, u_rhs;
    bool unsigned_op;

    unsigned_op = expr->op_lhs->type->tag == TYPE_UINT
               || expr->op_lhs->type->tag == TYPE_PTR;

    u_lhs = (uint64_t)lhs;
    u_rhs = (uint64_t)rhs;

    switch (expr->binop) {
        case BINOP_EQ:
			*out = value_make_bool(lhs == rhs);
            return EVAL_OK;

        case BINOP_NOTEQ:
			*out = value_make_bool(lhs != rhs);
            return EVAL_OK;

        case BINOP_GT:
			*out = value_make_bool(unsigned_op ? u_lhs >  u_rhs : lhs >  rhs);
            return EVAL_OK;

        case BINOP_GTEQ:
			*out = value_make_bool(unsigned_op ? u_lhs >= u_rhs : lhs >= rhs);
            return EVAL_OK;

        case BINOP_LT:
			*out = value_make_bool(unsigned_op ? u_lhs <  u_rhs : lhs <  rhs);
            return EVAL_OK;

        case BINOP_LTEQ:
			*out = value_make_bool(unsigned_op ? u_lhs <= u_rhs : lhs <= rhs);
            return EVAL_OK;

        default:
            return EVAL_NOT_CONST;
    }
}

static enum eval_status
eval_expr_binop(struct eval *state, struct expr_binop *expr, struct value *out)
{
    enum eval_status status;
    struct value lhs, rhs;

    status = eval_expr(state, expr->op_lhs, &lhs);
    if (status != EVAL_OK)
        return status;

    /*
     * Short-circuit logic for && and ||
     */
    if (expr->binop == BINOP_BOOL_AND) {
        assert(lhs.tag == VALUE_BOOL);
        if (!lhs.integer) {
            *out = value_make_bool(false);
            return EVAL_OK;
        }
    } else if (expr->binop == BINOP_BOOL_OR) {
        assert(lhs.tag == VALUE_BOOL);
        if (lhs.integer) {
            *out = value_make_bool(true);
            return EVAL_OK;
        }
    }

    status = eval_expr(state, expr->op_rhs, &rhs);
    if (status != EVAL_OK)
        return status;

    if (expr->binop == BINOP_BOOL_AND || expr->binop == BINOP_BOOL_OR) {
        assert(lhs.tag == VALUE_BOOL && rhs.tag == VALUE_BOOL);
        *out = value_make_bool(rhs.integer != 0);
        return EVAL_OK;
    }

    if (expr->binop >= BINOP_EQ && expr->binop <= BINOP_LTEQ) {
        if (lhs.tag != VALUE_INTEGER && lhs.tag != VALUE_BOOL)
            return EVAL_NOT_CONST;

        if (rhs.tag != VALUE_INTEGER && rhs.tag != VALUE_BOOL)
            return EVAL_NOT_CONST;

        return eval_binop_compare(state, expr, lhs.integer, rhs.integer, out);
    }

    assert(lhs.tag == VALUE_INTEGER && rhs.tag == VALUE_INTEGER);
    return eval_binop_arith(state, expr, lhs.integer, rhs.integer, out);
}

static enum eval_status
eval_expr_macro(struct eval *state, struct expr_macro *expr, struct value *out)
{
    enum eval_status status;
    struct value op;

    switch (expr->macro.tag) {
        case MACRO_SIZEOF:
        case MACRO_ALIGNOF:
            *out = value_make_integer(expr->macro.value);
            return EVAL_OK;

        case MACRO_CAST:
            status = eval_expr(state, expr->args[1], &op);
            if (status != EVAL_OK)
                return status;

            if (op.tag != VALUE_INTEGER && op.tag != VALUE_BOOL)
                return EVAL_NOT_CONST;

            switch (expr->expr.type->tag) {
                case TYPE_INT:
                case TYPE_UINT:
                case TYPE_PTR:
                    *out = value_make_integer(eval_normalize(expr->expr.type,
                                                             op.integer));
                    return EVAL_OK;

                case TYPE_BOOL:
                    *out = value_make_bool(op.integer != 0);
                    return EVAL_OK;

                default:
                    return EVAL_NOT_CONST;
            }

        case MACRO_EXPR:
            if (!expr->macro.expr)
                return EVAL_NOT_CONST;
            return eval_expr(state, expr->macro.expr, out);

        default:
            return EVAL_NOT_CONST;
    }
}

static enum eval_status
eval_expr(struct eval *state, struct expr *expr, struct value *out)
{
    EVAL_BURN(state);

    switch (expr->tag) {
        case EXPR_LITERAL:
            *out = expr->ctval;
            return EVAL_OK;

        case EXPR_UNOP:
            return eval_expr_unop(state, (struct expr_unop *)expr, out);

        case EXPR_BINOP:
            return eval_expr_binop(state, (struct expr_binop *)expr, out);

        case EXPR_MACRO:
            return eval_expr_macro(state, (struct expr_macro *)expr, out);

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

    if (!expr->type || type_is_invalid(expr->type))
        return EVAL_ERROR;

    /*
     * Refill the steps limit. In the future this should be tweakable
     */
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
