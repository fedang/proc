#include <stdlib.h>
#include <stdio.h>

#include "checker.h"
#include "report.h"

#define TYCHK_ERROR(state, span, ...) \
    report_error((state)->src, &(span), __VA_ARGS__)

/*
 * Scopes
 */
static struct scope *
scope_make(struct symbol *name, struct scope *parent)
{
    struct scope *scope = calloc(1, sizeof(struct scope));
    scope->name = name;
    scope->parent = parent;
    return scope;
}

static struct binding *
scope_push(struct scope *scope, struct symbol *sym,
           struct type *type, struct decl *decl, bool is_type)
{
    struct binding *bind;

    assert(scope->bindings_count < 256 && "too many bindings!");
    bind = &scope->bindings[scope->bindings_count++];

    bind->sym = sym;
    bind->status = decl ? RESOLVE_NONE : RESOLVE_DONE;
    bind->scope = scope;
    bind->is_type = is_type;
    bind->is_public = true;
    bind->decl = decl;
    bind->type = type;
    return bind;
}

static struct binding *
scope_lookup_local(struct scope *scope, struct symbol *sym, bool is_type)
{
    size_t i;

    for (i = 0; i < scope->bindings_count; i++) {
        if (scope->bindings[i].is_type == is_type &&
            scope->bindings[i].sym == sym)
            return &scope->bindings[i];
    }

    return NULL;
}

static struct binding *
scope_lookup(struct scope *scope, struct symbol *sym, bool is_type)
{
    struct binding *bind;

    do {
        bind = scope_lookup_local(scope, sym, is_type);
        if (bind)
            return bind;
    } while ((scope = scope->parent) != NULL);

    return NULL;
}

static void
checker_root_push(struct checker *state, const char *name, struct type *type)
{
    scope_push(state->root_scope, symbol_make(name), type, NULL, true);
}

static void
checker_local_push(struct checker *state, struct symbol *sym,
                   struct type *type)
{
    assert(state->locals_count < 256 && "too many locals!");

    state->locals[state->locals_count].sym = sym;
    state->locals[state->locals_count].type = type;
    state->locals_count++;
}

static void
checker_local_pop(struct checker *state, unsigned n)
{
    // XXX
    state->locals_count = n;
}

void
checker_init(struct checker *state, struct source *src)
{
    struct scope *root;

    state->src = src;
    state->ret_type = NULL;
    state->loop_depth = 0;
    state->locals_count = 0;

    root = scope_make(NULL, NULL);
    state->root_scope = root;
    state->curr_scope = root;

    checker_root_push(state, "i8", type_get_int(NULL, 8));
    checker_root_push(state, "i16", type_get_int(NULL, 16));
    checker_root_push(state, "i32", type_get_int(NULL, 32));
    checker_root_push(state, "i64", type_get_int(NULL, 64));
    checker_root_push(state, "int", type_get_int(NULL, 0));
    checker_root_push(state, "u8", type_get_uint(NULL, 8));
    checker_root_push(state, "u16", type_get_uint(NULL, 16));
    checker_root_push(state, "u32", type_get_uint(NULL, 32));
    checker_root_push(state, "u64", type_get_uint(NULL, 64));
    checker_root_push(state, "uint", type_get_uint(NULL, 0));
    checker_root_push(state, "bool", type_get_bool(NULL));
    checker_root_push(state, "void", type_get_void(NULL));
}

/*
 * Compile time evaluation
 */
static bool checker_resolve(struct checker *state, struct type **type);

static bool checker_resolve_binding(struct checker *state, struct binding *bind);

static struct type *checker_eval_raw(struct checker *state, struct expr *expr);

static bool checker_expr(struct checker *state, struct expr *expr);

static bool checker_expr_macro(struct checker *state, struct expr_macro *expr);

static bool
checker_eval_const(struct checker *state, struct expr *expr, int64_t *out)
{
    struct expr_literal *lit;
    struct expr_unop *unop;
    struct expr_binop *binop;
    struct expr_macro *macro;
    int64_t lhs, rhs;

    switch (expr->tag) {
        case EXPR_LITERAL: {
            lit = (struct expr_literal *)expr;
            if (lit->literal != LIT_INTEGER) {
                TYCHK_ERROR(state, expr->span,
                        "Expected integer literal in constant expression\n");
                return false;
            }

            *out = lit->integer;
            return true;
        }

        case EXPR_UNOP: {
            unop = (struct expr_unop *)expr;
            if (!checker_eval_const(state, unop->op, out))
                return false;

            if (unop->unop == UNOP_NEG) {
                *out *= -1;
                return true;
            }

            TYCHK_ERROR(state, expr->span,
                        "Invalid unary operator in constant expression\n");
            return false;
        }

        case EXPR_BINOP: {
            binop = (struct expr_binop *)expr;
            if (!checker_eval_const(state, binop->op_lhs, &lhs) ||
                !checker_eval_const(state, binop->op_rhs, &rhs))
                return false;

            switch (binop->binop) {
                case BINOP_ADD:
                    *out = lhs + rhs;
                    return true;

                case BINOP_SUB:
                    *out = lhs - rhs;
                    return true;

                case BINOP_MUL:
                    *out = lhs * rhs;
                    return true;

                case BINOP_DIV:
                    if (rhs == 0) {
                        TYCHK_ERROR(state, expr->span,
                                    "Division by zero in constant expression\n");
                        return false;
                    }
                    *out = lhs / rhs;
                    return true;

                case BINOP_MOD:
                    if (rhs == 0) {
                        TYCHK_ERROR(state, expr->span,
                                    "Modulo by zero in constant expression\n");
                        return false;
                    }
                    *out = lhs % rhs;
                    return true;

                case BINOP_SHL:
                    *out = lhs << rhs;
                    return true;

                case BINOP_SHR:
                    *out = lhs >> rhs;
                    return true;

                case BINOP_OR:
                    *out = lhs | rhs;
                    return true;

                case BINOP_AND:
                    *out = lhs & rhs;
                    return true;

                case BINOP_XOR:
                    *out = lhs ^ rhs;
                    return true;

                default:
                    TYCHK_ERROR(state, expr->span,
                                "Operator not allowed in constant expression\n");
                    return false;
            }
        }

        case EXPR_MACRO: {
            macro = (struct expr_macro *)expr;
            if (!checker_expr_macro(state, macro))
                return false;

            switch (macro->macro.tag) {
                case MACRO_SIZEOF:
                case MACRO_ALIGNOF:
                    *out = macro->macro.value;
                    return true;

                case MACRO_EXPR:
                    return checker_eval_const(state, macro->macro.expr, out);

                default:
                    TYCHK_ERROR(state, expr->span,
                                "Macro '@%s' cannot be evaluated at compile time\n",
                                macro->name->str);
                    return false;
            }
        }

        default:
            TYCHK_ERROR(state, expr->span,
                        "Expression is not a compile-time constant\n");
            return false;
    }
}

static struct type *
checker_eval_raw(struct checker *state, struct expr *expr)
{
    struct binding *bind;
    struct type *type;
    struct expr_ident *ident;
    struct expr_unop *unop;
    struct expr_array *array;
    struct expr_macro *macro;
    int64_t count;

    switch (expr->tag) {
        case EXPR_IDENT:
            ident = (struct expr_ident *)expr;

            bind = scope_lookup(state->curr_scope, ident->sym, true);
            if (!bind) {
                TYCHK_ERROR(state, expr->span,
                            "Unknown type '%s'\n", ident->sym->str);
                return NULL;
            }

            /*
             * Force resolution if the type object was not created yet
             */
            if (!bind->type) {
                if (!checker_resolve_binding(state, bind))
                    return NULL;
            } else if (bind->status == RESOLVE_PENDING && bind->decl->tag != DECL_STRUCT) {
                TYCHK_ERROR(state, expr->span,
                            "Cyclic definition involving '%s'\n", bind->sym->str);
                return NULL;
            }

            return bind->type;

        case EXPR_UNOP:
            unop = (struct expr_unop *)expr;
            if (unop->unop != UNOP_DEREF) {
                TYCHK_ERROR(state, expr->span,
                            "Invalid unary operator in type\n");
                return NULL;
            }

            type = checker_eval_raw(state, unop->op);
            if (!type)
                return NULL;

            return type_get_ptr(NULL, type);

        case EXPR_INDEX:
            // Offer a tip to confused users
            TYCHK_ERROR(state, expr->span,
                        "Array types are [type, count], not type[count]\n");
            return NULL;

        case EXPR_ARRAY:
            array = (struct expr_array *)expr;
            if (array->args_count != 2) {
                TYCHK_ERROR(state, expr->span,
                            "Expected [type, count] for array type\n");
                return NULL;
            }

            type = checker_eval_raw(state, array->args[0]);
            if (!type || !checker_resolve(state, &type))
                return NULL;

            if (!type_is_sized(type)) {
                TYCHK_ERROR(state, array->args[0]->span,
                            "Invalid array element type\n");
                return NULL;
            }

            if (!checker_eval_const(state, array->args[1], &count))
                return NULL;

            if (count <= 0) {
                TYCHK_ERROR(state, array->args[1]->span,
                            "Array size must be greater than 0\n");
                return NULL;
            }

            return type_get_array(NULL, type, count);

        case EXPR_MACRO:
            macro = (struct expr_macro *)expr;
            if (macro->name == symbol_type_of) {
                if (macro->body || macro->args_count != 1) {
                    TYCHK_ERROR(state, expr->span,
                                "Expected 1 argument and no block\n");
                    return NULL;
                }

                if (!checker_expr(state, macro->args[0]))
                    return NULL;

                return macro->args[0]->type;
            }

            TYCHK_ERROR(state, expr->span,
                        "Macro '@%s' cannot be used as a type\n",
                        macro->name->str);
            return NULL;

        default:
            TYCHK_ERROR(state, expr->span,
                        "Invalid type expression\n");
            return NULL;
    }
}

static bool
checker_resolve(struct checker *state, struct type **type_ptr)
{
    struct type *type;
    struct binding *bind;
    size_t i;
    bool ok;

    if (!type_ptr || !*type_ptr)
        return true;

    type = *type_ptr;
    ok = true;

    switch (type->tag) {
        case TYPE_RAW:
            type = checker_eval_raw(state, type->raw);
            if (!type)
                return false;
            *type_ptr = type;
            return true;

        case TYPE_PTR:
            // Delay resolution of the inner type
            return true;

        case TYPE_ARRAY:
            return checker_resolve(state, &type->array.item);


        case TYPE_STRUCT:
            bind = scope_lookup(state->curr_scope, type->strukt.name, true);
            if (bind && bind->status != RESOLVE_DONE)
                return checker_resolve_binding(state, bind);
            break;

        case TYPE_PROC:
            if (!checker_resolve(state, &type->proc.out)) {
                ok = false;
            }

            for (i = 0; i < type->proc.args_count; i++) {
                if (!checker_resolve(state, &type->proc.args[i])) {
                    ok = false;
                }
            }
            break;

        default:
            break;
    }

    return ok;
}

static bool
checker_resolve_binding(struct checker *state, struct binding *bind)
{
    struct decl_proc *proc;
    struct decl_type *d_type;
    struct type *type, *args[32];
    struct proc_arg *ptr;
    struct scope *saved;
    size_t i;
    bool ok = true;

    if (bind->status == RESOLVE_DONE)
        return true;

    if (bind->status == RESOLVE_PENDING) {
        TYCHK_ERROR(state, bind->decl->span,
                    "Cyclic definition involving '%s'\n", bind->sym->str);
        return false;
    }

    saved = state->curr_scope;
    state->curr_scope = bind->scope;
    bind->status = RESOLVE_PENDING;

    switch (bind->decl->tag) {
        case DECL_TYPE:
            d_type = (struct decl_type *)bind->decl;

            if (!d_type->type) {
                TYCHK_ERROR(state, bind->decl->span,
                            "Opaque types not implemented yet\n");
                ok = false;
                break;
            }

            type = checker_eval_raw(state, d_type->type->raw);
            if (!type) {
                ok = false;
                break;
            }
            d_type->type = type;
            bind->type = type;
            break;

        case DECL_STRUCT:
            type = bind->type;
            for (i = 0; i < type->strukt.fields_count; i++) {
                if (!checker_resolve(state, &type->strukt.fields[i].type)) {
                    ok = false;
                } else if (!type_is_sized(type->strukt.fields[i].type)) {
                    TYCHK_ERROR(state, bind->decl->span,
                                "Struct field '%s' must have a sized type\n",
                                type->strukt.fields[i].name->str);
                    ok = false;
                }
            }
            break;

        case DECL_PROC:
            proc = (struct decl_proc *)bind->decl;

            if (!proc->out) {
                proc->out = type_get_void(proc->decl.attr);
            }

            if (!checker_resolve(state, &proc->out)) {
                ok = false;
            } else if (!type_is_sized(proc->out)) {
                TYCHK_ERROR(state, proc->decl.span,
                            "Return type must be a sized type\n");
                ok = false;
            }

            for (i = 0, ptr = proc->args; ptr != NULL; ptr = ptr->next) {
                if (!checker_resolve(state, &ptr->type)) {
                    ok = false;
                } else if (!type_is_sized(ptr->type)) {
                    TYCHK_ERROR(state, proc->decl.span,
                                "Parameter must be a sized type\n");
                    ok = false;
                }
                assert(i < 32 && "too many args!");
                args[i++] = ptr->type;
            }

            if (ok) {
                bind->type = type_get_proc(proc->decl.attr, proc->out,
                                           args, proc->args_count);
            }
            break;

        default:
            unreachable();
    }

    bind->status = RESOLVE_DONE;
    state->curr_scope = saved;
    return ok;
}

static bool
checker_cmp(struct checker *state, struct type *t1, struct type *t2)
{
    // XXX
    assert(t1->tag != TYPE_RAW && t2->tag != TYPE_RAW);
    return t1 == t2;
}

/*
 * Expressions
 */
static bool
checker_expr_literal(struct checker *state, struct expr_literal *expr)
{
    switch (expr->literal) {
        case LIT_INTEGER:
            expr->expr.type = type_get_int(NULL, 0);
            break;

        case LIT_STRING:
            expr->expr.type = type_get_ptr(NULL, type_get_uint(NULL, 8));
            break;

        default:
            unreachable();
    }

    return true;
}

static bool
checker_expr_ident(struct checker *state, struct expr_ident *expr)
{
    struct binding *bind;
    size_t i;

    for (i = state->locals_count; i > 0; i--) {
        if (state->locals[i - 1].sym == expr->sym) {
            expr->expr.type = state->locals[i - 1].type;
            return true;
        }
    }

    bind = scope_lookup(state->curr_scope, expr->sym, false);
    if (!bind) {
        TYCHK_ERROR(state, expr->expr.span,
                    "Undefined variable '%s'\n", expr->sym->str);
        return false;
    }

    if (!checker_resolve_binding(state, bind))
        return false;

    expr->expr.type = bind->type;
    return true;
}

static bool
checker_expr_binop(struct checker *state, struct expr_binop *expr)
{
    if (!checker_expr(state, expr->op_lhs) || !checker_expr(state, expr->op_rhs))
        return false;

    if (!checker_cmp(state, expr->op_lhs->type, expr->op_rhs->type)) {
        TYCHK_ERROR(state, expr->expr.span,
                    "Binop operands have different types\n");
        return false;
    }

    if (expr->binop >= BINOP_SET && expr->binop <= BINOP_MOD_SET) {
        if (!expr_is_lvalue(expr->op_lhs)) {
            TYCHK_ERROR(state, expr->expr.span,
                        "Binop left operand is not an lvalue\n");
            return false;
        }

        if (!type_is_sized(expr->op_lhs->type)) {
            TYCHK_ERROR(state, expr->expr.span, "Cannot assign to unsized type\n");
            return false;
        }
    }

    switch (expr->binop) {
        case BINOP_BOOL_OR:
        case BINOP_BOOL_AND:
            if (expr->op_lhs->type->tag != TYPE_BOOL) {
                TYCHK_ERROR(state, expr->expr.span,
                            "Expected bool operands for and/or\n");
                return false;
            }
            expr->expr.type = type_get_bool(NULL);
            break;

        case BINOP_EQ:
        case BINOP_NOTEQ:
            if (!type_is_scalar(expr->op_lhs->type)
                    && expr->op_lhs->type->tag != TYPE_BOOL) {
                TYCHK_ERROR(state, expr->expr.span,
                            "Invalid operand types for equality\n");
                return false;
            }
            expr->expr.type = type_get_bool(NULL);
            break;

        case BINOP_GT:
        case BINOP_GTEQ:
        case BINOP_LT:
        case BINOP_LTEQ:
            if (!type_is_scalar(expr->op_lhs->type)) {
                TYCHK_ERROR(state, expr->expr.span,
                            "Invalid operand types for comparison\n");
                return false;
            }
            expr->expr.type = type_get_bool(NULL);
            break;

        case BINOP_SET:
            break;

        default:
            if (!type_is_integer(expr->op_lhs->type)) {
                TYCHK_ERROR(state, expr->expr.span,
                            "Expected integer operands for arithmetic/bitwise operator\n");
                return false;
            }
            expr->expr.type = expr->op_lhs->type;
            break;
    }

    if (expr->binop >= BINOP_SET && expr->binop <= BINOP_MOD_SET) {
            expr->expr.type = type_get_void(NULL);
    }

    return true;
}

static bool
checker_expr_unop(struct checker *state, struct expr_unop *expr)
{
    if (!checker_expr(state, expr->op))
        return false;

    if (expr->unop == UNOP_ADDROF) {
        if (!expr_is_lvalue(expr->op)) {
            TYCHK_ERROR(state, expr->expr.span,
                        "Unop operand is not an lvalue\n");
            return false;
        }
    }

    switch (expr->unop) {
        case UNOP_DEREF:
            if (expr->op->type->tag != TYPE_PTR) {
                TYCHK_ERROR(state, expr->expr.span,
                            "Only pointer types can be dereferenced\n");
                return false;
            }

            expr->expr.type = expr->op->type->pointer;
            break;

        case UNOP_ADDROF:
            expr->expr.type = type_get_ptr(NULL, expr->op->type);
            break;

        case UNOP_NOT:
            if (expr->op->type->tag != TYPE_BOOL) {
                TYCHK_ERROR(state, expr->expr.span,
                            "Only bool values can be negated\n");
                return false;
            }

            expr->expr.type = type_get_bool(NULL);
            break;

        case UNOP_NEG:
            if (expr->op->type->tag != TYPE_INT && expr->op->type->tag != TYPE_UINT) {
                TYCHK_ERROR(state, expr->expr.span,
                            "Only integers can be negated\n");
                return false;
            }

            expr->expr.type = expr->op->type;
            break;

        default:
            unreachable();
    }

    return true;
}

static bool
checker_expr_call(struct checker *state, struct expr_call *expr)
{
    struct type *proc;
    size_t i;

    if (!checker_expr(state, expr->op))
        return false;

    proc = expr->op->type;

    /*
     * Allow one level of automatic pointer dereference for proc pointers
     */
    if (proc->tag == TYPE_PTR && proc->pointer->tag == TYPE_PROC) {
        proc = proc->pointer;
    }

    if (proc->tag != TYPE_PROC) {
        TYCHK_ERROR(state, expr->expr.span,
                    "Call target is not a proc\n");
        return false;
    }

    if (proc->proc.args_count != expr->args_count) {
        TYCHK_ERROR(state, expr->expr.span,
                    "Call argument count does not match (expected %d, got %d)\n",
                    proc->proc.args_count, expr->args_count);
        return false;
    }

    for (i = 0; i < expr->args_count; i++) {
        if (!checker_expr(state, expr->args[i]))
            return false;

        if (!checker_cmp(state, proc->proc.args[i], expr->args[i]->type)) {
            TYCHK_ERROR(state, expr->expr.span,
                        "Call argument type does not match proc\n");
            return false;
        }
    }

    expr->expr.type = proc->proc.out;
    return true;
}

static bool
checker_expr_index(struct checker *state, struct expr_index *expr)
{
    if (!checker_expr(state, expr->op) || !checker_expr(state, expr->index))
        return false;

    if (expr->index->type->tag != TYPE_INT && expr->index->type->tag != TYPE_UINT) {
        TYCHK_ERROR(state, expr->expr.span,
                    "Invalid index type\n");
        return false;
    }

    if (expr->op->type->tag == TYPE_ARRAY) {
        expr->expr.type = expr->op->type->array.item;
        return true;
    } else if (expr->op->type->tag == TYPE_PTR) {
        expr->expr.type = expr->op->type->pointer;
        return true;
    }

    TYCHK_ERROR(state, expr->expr.span,
                "Invalid type used for indexing\n");
    return false;
}

static bool
checker_expr_access(struct checker *state, struct expr_access *expr)
{
    struct type *type;
    size_t i;

    if (!checker_expr(state, expr->op))
        return false;

    type = expr->op->type;

    /*
     * Automatically dereference struct pointers
     */
    while (type->tag == TYPE_PTR) {
        type = type->pointer;
    }

    if (type->tag != TYPE_STRUCT) {
        TYCHK_ERROR(state, expr->expr.span,
                    "Tried to access a field from a non-struct type\n");
        return false;
    }

    for (i = 0; i < type->strukt.fields_count; i++) {
        if (type->strukt.fields[i].name == expr->field) {
            expr->expr.type = type->strukt.fields[i].type;
            expr->index = type->strukt.fields[i].index;
            return true;
        }
    }

    TYCHK_ERROR(state, expr->expr.span,
                "Found no field named '%s'\n", expr->field->str);
    return false;
}

static bool
checker_expr_array(struct checker *state, struct expr_array *expr)
{
    struct type *type;
    size_t i;

    if (expr->args_count == 0) {
        TYCHK_ERROR(state, expr->expr.span,
                    "Empty array literal is not allowed\n");
        return false;
    }

    if (!checker_expr(state, expr->args[0]))
        return false;

    type = expr->args[0]->type;
    if (!type_is_sized(type)) {
        TYCHK_ERROR(state, expr->args[0]->span,
                    "Invalid array element type\n");
        return false;
    }

    for (i = 1; i < expr->args_count; i++) {
        if (!checker_expr(state, expr->args[i]))
            return false;

        if (!checker_cmp(state, type, expr->args[i]->type)) {
            TYCHK_ERROR(state, expr->args[i]->span,
                        "Array literal elements have mismatched types\n");
            return false;
        }
    }

    expr->expr.type = type_get_array(NULL, type, expr->args_count);
    return true;
}

static bool
checker_expr_struct(struct checker *state, struct expr_struct *expr)
{
    struct type *type;
    size_t i, j;
    bool found, ok;

    type = checker_eval_raw(state, expr->op);
    if (!type)
        return false;

    if (type->tag != TYPE_STRUCT) {
        TYCHK_ERROR(state, expr->op->span, "Expected struct type for literal\n");
        return false;
    }

    expr->expr.type = type;
    ok = true;

    for (i = 0; i < expr->fields_count; i++) {
        if (!checker_expr(state, expr->fields[i].value))
            return false;

        found = false;
        for (j = 0; j < type->strukt.fields_count; j++) {
            if (type->strukt.fields[j].name == expr->fields[i].field) {
                if (!checker_cmp(state, type->strukt.fields[j].type,
                                 expr->fields[i].value->type)) {
                    TYCHK_ERROR(state, expr->fields[i].value->span,
                                "Field '%s' type mismatch\n", expr->fields[i].field->str);
                    ok = false;
                }
                found = true;
                break;
            }
        }

        if (!found) {
            TYCHK_ERROR(state, expr->expr.span,
                        "Struct '%s' has no field named '%s'\n",
                        type->strukt.name->str, expr->fields[i].field->str);
            ok = false;
        }
    }

    return ok;
}

static bool
checker_expr_macro(struct checker *state, struct expr_macro *expr)
{
    struct type *type;

    if (expr->macro.tag != MACRO_INVALID)
        return true;

    if (expr->name == symbol_size_of || expr->name == symbol_align_of) {
        if (expr->body || expr->args_count != 1) {
            TYCHK_ERROR(state, expr->expr.span,
                        "Expected 1 argument and no block\n");
            return false;
        }

        type = checker_eval_raw(state, expr->args[0]);
        if (!type)
            return false;

        if (!type_is_sized(type)) {
            TYCHK_ERROR(state, expr->expr.span,
                        "Expected a sized type\n");
            return false;
        }

        if (expr->name == symbol_size_of) {
            expr->macro.tag = MACRO_SIZEOF;
            expr->macro.value = type_sizeof(type);
        } else {
            expr->macro.tag = MACRO_ALIGNOF;
            expr->macro.value = type_alignof(type);
        }

        expr->expr.type = type_get_uint(NULL, 64);
        return true;
    }

    if (expr->name == symbol_cast) {
        if (expr->body || expr->args_count != 2) {
            TYCHK_ERROR(state, expr->expr.span,
                        "Expected 2 arguments: @cast(type, expr)\n");
            return false;
        }

        if (!checker_expr(state, expr->args[1]))
            return false;

        type = checker_eval_raw(state, expr->args[0]);
        if (!type)
            return false;

        if (!type_is_scalar(expr->args[1]->type) || !type_is_scalar(type)) {
            TYCHK_ERROR(state, expr->expr.span,
                        "Cast can only be used with scalar types\n");
            return false;
        }

        expr->macro.tag = MACRO_CAST;
        expr->expr.type = type;
        return true;
    }

    if (expr->name == symbol_type_of) {
        TYCHK_ERROR(state, expr->expr.span,
                    "'@%s' produces a type and cannot be used as a value\n",
                    expr->name->str);
        return false;
    }

    TYCHK_ERROR(state, expr->expr.span,
                "Unknown builtin macro '@%s'\n", expr->name->str);
    return false;
}

static bool
checker_expr(struct checker *state, struct expr *expr)
{
    switch (expr->tag) {
        case EXPR_LITERAL:
            return checker_expr_literal(state, (struct expr_literal *)expr);

        case EXPR_IDENT:
            return checker_expr_ident(state, (struct expr_ident *)expr);

        case EXPR_BINOP:
            return checker_expr_binop(state, (struct expr_binop *)expr);

        case EXPR_UNOP:
            return checker_expr_unop(state, (struct expr_unop *)expr);

        case EXPR_CALL:
            return checker_expr_call(state, (struct expr_call *)expr);

        case EXPR_INDEX:
            return checker_expr_index(state, (struct expr_index *)expr);

        case EXPR_ACCESS:
            return checker_expr_access(state, (struct expr_access *)expr);

        case EXPR_ARRAY:
            return checker_expr_array(state, (struct expr_array *)expr);

        case EXPR_STRUCT:
            return checker_expr_struct(state, (struct expr_struct *)expr);

        case EXPR_MACRO:
            return checker_expr_macro(state, (struct expr_macro *)expr);

        default:
            unreachable();
    }
}

/*
 * Statements
 */
static bool checker_stmt(struct checker *state, struct stmt *stmt);

static bool
checker_stmt_var(struct checker *state, struct stmt_var *stmt)
{
    /*
     * The user could have provided an explicit type
     */
    if (stmt->type) {
        if (!checker_resolve(state, &stmt->type))
            return false;
    }

    if (stmt->value) {
        if (!checker_expr(state, stmt->value))
            return false;

        if (!stmt->type) {
            stmt->type = stmt->value->type;
        } else if (!checker_cmp(state, stmt->type, stmt->value->type)) {
            TYCHK_ERROR(state, stmt->stmt.span,
                        "Type mismatch in variable assignment\n");
            return false;
        }
    }

    if (!stmt->type) {
        TYCHK_ERROR(state, stmt->stmt.span,
                    "Cannot infer type for variable '%s'\n", stmt->sym->str);
        return false;
    }

    if (!type_is_sized(stmt->type)) {
        TYCHK_ERROR(state, stmt->stmt.span,
                    "Variable '%s' cannot have an unsized type (use a pointer)\n",
                    stmt->sym->str);
        return false;
    }

    checker_local_push(state, stmt->sym, stmt->type);
    return true;
}

static bool
checker_stmt_expr(struct checker *state, struct stmt_expr *stmt)
{
    return checker_expr(state, stmt->expr);
}

static bool
checker_stmt_block(struct checker *state, struct stmt_block *stmt)
{
    size_t i;
    unsigned locals;
    bool ok;

    locals = state->locals_count;
    ok = true;

    for (i = 0; i < stmt->items_count; i++) {
        if (!checker_stmt(state, stmt->items[i])) {
            ok = false;
        }
    }

    checker_local_pop(state, locals);
    return ok;
}

static bool
checker_stmt_if(struct checker *state, struct stmt_if *stmt)
{
    if (!checker_expr(state, stmt->cond))
        return false;

    if (!checker_cmp(state, stmt->cond->type, type_get_bool(NULL))) {
        TYCHK_ERROR(state, stmt->stmt.span,
                    "If condition is not of type bool\n");
        return false;
    }

    if (!checker_stmt(state, stmt->b_true))
        return false;

    if (stmt->b_false && !checker_stmt(state, stmt->b_false))
        return false;

    return true;
}

static bool
checker_stmt_return(struct checker *state, struct stmt_return *stmt)
{
    if (!stmt->expr) {
        if (!checker_cmp(state, state->ret_type, type_get_void(NULL))) {
            TYCHK_ERROR(state, stmt->stmt.span,
                        "Expected return value\n");
            return false;
        }
    } else {
        if (!checker_expr(state, stmt->expr))
            return false;

        if (!checker_cmp(state, stmt->expr->type, state->ret_type)) {
            TYCHK_ERROR(state, stmt->stmt.span,
                        "Return value type does not match\n");
            return false;
        }
    }

    return true;
}

static bool
checker_stmt_while(struct checker *state, struct stmt_while *stmt)
{
    bool ok;

    if (!checker_expr(state, stmt->cond))
        return false;

    if (!checker_cmp(state, stmt->cond->type, type_get_bool(NULL))) {
        TYCHK_ERROR(state, stmt->stmt.span,
                    "While condition is not of type bool\n");
        return false;
    }

    state->loop_depth++;
    ok = checker_stmt(state, stmt->body);
    state->loop_depth--;
    return ok;
}

static bool
checker_stmt(struct checker *state, struct stmt *stmt)
{
    switch (stmt->tag) {
        case STMT_VAR:
            return checker_stmt_var(state, (struct stmt_var *)stmt);

        case STMT_EXPR:
            return checker_stmt_expr(state, (struct stmt_expr *)stmt);

        case STMT_BLOCK:
            return checker_stmt_block(state, (struct stmt_block *)stmt);

        case STMT_IF:
            return checker_stmt_if(state, (struct stmt_if *)stmt);

        case STMT_RETURN:
            return checker_stmt_return(state, (struct stmt_return *)stmt);

        case STMT_WHILE:
            return checker_stmt_while(state, (struct stmt_while *)stmt);

        case STMT_BREAK:
        case STMT_CONTINUE:
            if (state->loop_depth == 0) {
                TYCHK_ERROR(state, stmt->span,
                            "Cannot break/continue outside of a loop\n");
                return false;
            }
            return true;

        default:
            unreachable();
    }
}

/*
 * Return analysis
 */
static bool
checker_proc_returns(struct stmt *stmt)
{
    struct stmt_block *block;
    struct stmt_if *stmt_if;
    size_t i;

    if (!stmt)
        return false;

    switch (stmt->tag) {
        case STMT_RETURN:
            return true;

        case STMT_BLOCK:
            block = (struct stmt_block *)stmt;
            for (i = 0; i < block->items_count; i++) {
                if (checker_proc_returns(block->items[i]))
                    return true;
            }
            return false;

        case STMT_IF:
            stmt_if = (struct stmt_if *)stmt;
            if (!stmt_if->b_false)
                return false;
            return checker_proc_returns(stmt_if->b_true)
                && checker_proc_returns(stmt_if->b_false);

        case STMT_EXPR:
            // TODO: What if we have return as expressions?
            return false;

        default:
            return false;
    }
}

/*
 * Declarations
 */
static bool
checker_decl_register(struct checker *state, struct decl *decl)
{
    struct decl_proc *proc;
    struct decl_struct *strukt;
    struct decl_type *d_type;
    struct type *type;

    switch (decl->tag) {
        case DECL_PROC:
            proc = (struct decl_proc *)decl;
            if (scope_lookup_local(state->curr_scope, proc->sym, false)) {
                TYCHK_ERROR(state, decl->span,
                            "Redefinition of symbol '%s'\n", proc->sym->str);
                return false;
            }

            scope_push(state->curr_scope, proc->sym, NULL, decl, false);
            return true;

        case DECL_STRUCT:
            strukt = (struct decl_struct *)decl;
            if (scope_lookup_local(state->curr_scope, strukt->sym, true)) {
                TYCHK_ERROR(state, decl->span,
                            "Redefinition of symbol '%s'\n", strukt->sym->str);
                return false;
            }

            type = type_get_struct(decl->attr, strukt->sym,
                                   strukt->fields, strukt->fields_count);
            scope_push(state->curr_scope, strukt->sym, type, decl, true);
            return true;

        case DECL_TYPE:
            d_type = (struct decl_type *)decl;
            if (scope_lookup_local(state->curr_scope, d_type->sym, true)) {
                TYCHK_ERROR(state, decl->span,
                            "Redefinition of symbol '%s'\n", d_type->sym->str);
                return false;
            }

            scope_push(state->curr_scope, d_type->sym, NULL, decl, true);
            return true;

        default:
            unreachable();
    }
}

static bool
checker_proc_body(struct checker *state, struct decl *decl)
{
    struct decl_proc *proc;
    struct proc_arg *ptr;
    bool ok;

    if (decl->tag != DECL_PROC)
        return true;

    proc = (struct decl_proc *)decl;

     /*
     * Proc without a body should not be checked...
     */
    if (!proc->body)
        return true;

    assert(state->locals_count == 0 && "leftover locals!");

    for (ptr = proc->args; ptr != NULL; ptr = ptr->next) {
        checker_local_push(state, ptr->sym, ptr->type);
    }

    state->loop_depth = 0;
    state->ret_type = proc->out;
    ok = checker_stmt(state, proc->body);
    checker_local_pop(state, 0);

    if (ok && proc->out->tag != TYPE_VOID && !checker_proc_returns(proc->body)) {
        TYCHK_ERROR(state, decl->span,
                    "Procedure '%s' does not return a value on all branches\n",
                    proc->sym->str);
        ok = false;
    }

    return ok;
}

bool
checker_module(struct checker *state, struct decl **decls, size_t decls_count)
{
    struct scope *file_scope;
    size_t i;
    bool ok = true;

    file_scope = scope_make(NULL, state->root_scope);
    state->curr_scope = file_scope;

    /*
     * Register module declarations
     */
    for (i = 0; i < decls_count; i++) {
        if (!checker_decl_register(state, decls[i])) {
            ok = false;
        }
    }

    if (!ok)
        return false;

    /*
     * Resolve (recursively) all the bindings in scope
     */
    for (i = 0; i < file_scope->bindings_count; i++) {
        if (!checker_resolve_binding(state, &file_scope->bindings[i])) {
            ok = false;
        }
    }

    if (!ok)
        return false;

    /*
     * Validate procedure bodies
     */
    for (i = 0; i < decls_count; i++) {
        if (!checker_proc_body(state, decls[i])) {
            ok = false;
        }
    }

    return ok;
}
