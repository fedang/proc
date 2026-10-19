#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "checker.h"
#include "report.h"

#define TYCHK_ERROR(state, span, ...) \
    report_error((state)->src, &(span), __VA_ARGS__)

#define TYCHK_WARN(state, span, ...) \
    report_warning((state)->src, &(span), __VA_ARGS__)

#define TYCHK_INFO(state, span, ...) \
    report_info((state)->src, &(span), __VA_ARGS__)

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
checker_init(struct checker *state, struct modcache *cache)
{
    struct scope *root;

    state->cache = cache;
    state->src = NULL;
    state->ret_type = NULL;
    state->loop_depth = 0;
    state->locals_count = 0;

    root = scope_make(NULL, NULL, NULL);
    state->root_scope = root;
    state->curr_scope = root;

#define X(type, init) \
    checker_root_push(state, #type, init);
    TYPE_LIST(X)
#undef X
}

/*
 * Compile time evaluation
 */
static bool checker_resolve_type(struct checker *state, struct type **type);

static bool checker_resolve_binding(struct checker *state, struct binding *bind);

static struct type *checker_eval_raw(struct checker *state, struct expr *expr);

static bool checker_expr(struct checker *state, struct expr *expr);

static bool checker_expr_coerce(struct checker *state, struct expr *expr,
                                struct type *target);

static bool checker_expr_typed(struct checker *state, struct expr *expr,
                               struct type *target);

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
                                "Macro '@%pS' cannot be evaluated at compile time\n",
                                macro->name);
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
checker_binding_type(struct checker *state, struct binding *bind,
                     struct span span)
{
    /*
     * If we don't have a type yet, this is cyclic
     */
    if (bind->status == RESOLVE_PENDING && !bind->type) {
        TYCHK_ERROR(state, span,
                    "Cyclic definition involving symbol '%pS'\n",
                    bind->sym);

        if (bind->decl) {
            TYCHK_INFO(state, bind->decl->span,
                       "Symbol '%pS' was originally declared here\n",
                       bind->sym);
        }
        return NULL;
    }

    if (!bind->type) {
        if (!checker_resolve_binding(state, bind))
            return NULL;
    }

    return bind->type;
}

static struct type *
checker_eval_raw(struct checker *state, struct expr *expr)
{
    struct binding *bind;
    struct type *type;
    struct expr_ident *ident;
    struct expr_unop *unop;
    struct expr_access *access;
    struct expr_array *array;
    struct expr_struct *strukt;
    struct struct_field *fields;
    struct expr_macro *macro;
    int64_t count;
    size_t i, j;

    switch (expr->tag) {
        case EXPR_IDENT:
            ident = (struct expr_ident *)expr;

            bind = scope_lookup(state->curr_scope, ident->sym);
            if (!bind) {
                TYCHK_ERROR(state, expr->span,
                            "Unknown type '%pS'\n",
                            ident->sym);
                return NULL;
            }

            type = checker_binding_type(state, bind, expr->span);
            if (!type)
                return NULL;

            if (!bind->is_type && type->tag != TYPE_MODULE) {
                TYCHK_ERROR(state, expr->span, "'%pS' is not a type\n", ident->sym);
                return NULL;
            }

            return type;

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

            if (type->tag == TYPE_MODULE) {
                TYCHK_ERROR(state, expr->span,
                            "Cannot make a pointer type from module '%pS'\n",
                            type->module.scope->name);
                return NULL;
            }

            return type_get_ptr(NULL, type);

        case EXPR_INDEX:
            /*
             * Offer a tip to confused users
             */
            TYCHK_ERROR(state, expr->span,
                        "Array types are '[type, count]', not 'type[count]'\n");
            return NULL;

        case EXPR_ACCESS:
            access = (struct expr_access *)expr;

            type = checker_eval_raw(state, access->op);
            if (!type)
                return NULL;

            if (type_is_invalid(type))
                return type_get_invalid();

            if (type->tag != TYPE_MODULE) {
                TYCHK_ERROR(state, expr->span,
                            "Expected a module for member access, but got '%pT'\n",
                            type);
                return NULL;
            }

            bind = scope_lookup_local(type->module.scope, access->field);
            if (!bind) {
                TYCHK_ERROR(state, expr->span,
                            "Module '%pS' has no member named '%pS'\n",
                            type->module.scope->name, access->field);
                return NULL;
            }

            type = checker_binding_type(state, bind, expr->span);
            if (!type)
                return NULL;

            if (!bind->is_type && type->tag != TYPE_MODULE) {
                TYCHK_ERROR(state, expr->span, "'%pS' is not a type\n", access->field);
                return NULL;
            }

            return type;

        case EXPR_ARRAY:
            array = (struct expr_array *)expr;
            if (array->args_count != 1 && array->args_count != 2) {
                TYCHK_ERROR(state, expr->span,
                            "Expected '[type]' or '[type, count]' for array/slice\n");
                return NULL;
            }

            type = checker_eval_raw(state, array->args[0]);
            if (!type || !checker_resolve_type(state, &type))
                return NULL;

            if (!type_is_sized(type)) {
                TYCHK_ERROR(state, array->args[0]->span,
                            "Array element type '%pT' is unsized\n",
                            type);
                return NULL;
            }

            if (array->args_count == 1)
                return type_get_slice(NULL, type);

            if (!checker_eval_const(state, array->args[1], &count))
                return NULL;

            if (count < 0) {
                TYCHK_ERROR(state, array->args[1]->span,
                            "Array size must be positive\n");
                return NULL;
            }

            return type_get_array(NULL, type, count);

        case EXPR_STRUCT:
            strukt = (struct expr_struct *)expr;
            if (strukt->op != NULL) {
                TYCHK_ERROR(state, expr->span,
                            "Named struct literal is not a type\n");
                return NULL;
            }

            for (i = 0; i < strukt->fields_count; i++) {
                if (!strukt->fields[i].value) {
                    TYCHK_ERROR(state, strukt->fields[i].field->expr.span,
                                "Field '%pS' of anonymous struct type has no type\n",
                                strukt->fields[i].field->sym);
                    return NULL;
                }

                if (!strukt->fields[i].colon) {
                    TYCHK_ERROR(state, expr->span,
                                "Struct type must use ':' instead of '='\n");
                    return NULL;
                }

                /*
                 * Iterate fields to prevent invalid duplicates
                 */
                for (j = 0; j < i; j++) {
                    if (strukt->fields[j].field->sym == strukt->fields[i].field->sym) {
                        TYCHK_ERROR(state, strukt->fields[i].field->expr.span,
                                    "Field '%pS' declared more than once\n",
                                    strukt->fields[i].field->sym);
                        return NULL;
                    }
                }
            }

            // XXX: leaks
            fields = calloc(strukt->fields_count, sizeof(struct struct_field));

            for (i = 0; i < strukt->fields_count; i++) {
                fields[i].name = strukt->fields[i].field->sym;
                fields[i].type = checker_eval_raw(state, strukt->fields[i].value);
                fields[i].index = i;

                if (!fields[i].type)
                    return NULL;

                if (!type_is_sized(fields[i].type)) {
                    TYCHK_ERROR(state, strukt->fields[i].value->span,
                                "Anonymous struct field '%pS' has unsized type '%pT'\n",
                                fields[i].name, fields[i].type);
                    return NULL;
                }
            }

            return type_get_struct(NULL, NULL, NULL,
                                   fields, strukt->fields_count);

        case EXPR_MACRO:
            macro = (struct expr_macro *)expr;
            if (macro->name == symbol_typeof) {
                if (macro->body || macro->args_count != 1) {
                    TYCHK_ERROR(state, expr->span,
                                "Expected 1 argument and no block\n");
                    return NULL;
                }

                if (!checker_expr_typed(state, macro->args[0], NULL)) {
                    if (!type_is_invalid(macro->args[0]->type)) {
                        TYCHK_ERROR(state, macro->args[0]->span,
                                    "Cannot infer type of @type_of argument\n");
                    }
                    return NULL;
                }

                return macro->args[0]->type;
            }

            TYCHK_ERROR(state, expr->span,
                        "Macro '@%pS' cannot be used as a type\n",
                        macro->name);
            return NULL;

        default:
            TYCHK_ERROR(state, expr->span,
                        "Invalid type expression\n");
            return NULL;
    }
}

static bool
checker_resolve_type(struct checker *state, struct type **type_ptr)
{
    struct type *type;
    size_t i;
    bool ok;

    if (!type_ptr || !*type_ptr)
        return true;

    type = *type_ptr;
    ok = true;

    switch (type->tag) {
        case TYPE_INVALID:
            return false;

        case TYPE_RAW:
            type = checker_eval_raw(state, type->raw);
            if (!type) {
                ok = false;
            } else {
                *type_ptr = type;
            }
            break;

        case TYPE_PTR:
            /*
             * Delay resolution of the inner type
             */
            return true;

        case TYPE_ARRAY:
            ok = checker_resolve_type(state, &type->array.item);
            break;

        case TYPE_SLICE:
            ok = checker_resolve_type(state, &type->slice.item);
            break;

        case TYPE_STRUCT:
            /*
             * Struct types should already be resolved by checker_eval_raw
             */
            break;

        case TYPE_PROC:
            if (!checker_resolve_type(state, &type->proc.out)) {
                ok = false;
            }

            for (i = 0; i < type->proc.args_count; i++) {
                if (!checker_resolve_type(state, &type->proc.args[i])) {
                    ok = false;
                }
            }
            break;

        default:
            break;
    }

    if (!ok) {
        *type_ptr = type_get_invalid();
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
    struct scope *saved, *scope;
    size_t i;
    bool ok = true;

    if (bind->status == RESOLVE_DONE)
        return true;

    if (bind->status == RESOLVE_PENDING) {
        /*
         * Bindings whose type is set before resolution (e.g. modules)
         * can be safely looked up recursively
         */
        if (bind->type)
            return true;

        assert(bind->decl && "primitives should not be pending");

        TYCHK_ERROR(state, bind->decl->span,
                    "Cyclic definition involving symbol '%pS'\n",
                    bind->sym);
        TYCHK_INFO(state, bind->decl->span,
                   "Symbol '%pS' was originally declared here\n",
                   bind->sym);
        return false;
    }

    saved = state->curr_scope;
    state->curr_scope = bind->scope;
    bind->status = RESOLVE_PENDING;

    switch (bind->decl->tag) {
        case DECL_TYPE:
            d_type = (struct decl_type *)bind->decl;
            if (!d_type->type)
                break;

            if (!checker_resolve_type(state, &d_type->type)) {
                ok = false;
            } else if (d_type->type->tag == TYPE_MODULE) {
                TYCHK_ERROR(state, bind->decl->span,
                            "Cannot alias type '%pS' to module '%pS'\n",
                            bind->sym, d_type->type->module.scope->name);
                ok = false;
                d_type->type = type_get_invalid();
            }

            bind->type = d_type->type;
            break;

        case DECL_STRUCT:
            type = bind->type;
            for (i = 0; i < type->strukt.fields_count; i++) {
                if (!checker_resolve_type(state, &type->strukt.fields[i].type)) {
                    ok = false;
                } else if (!type_is_sized(type->strukt.fields[i].type)) {
                    TYCHK_ERROR(state, bind->decl->span,
                                "Struct field '%pS' has unsized type '%pT'\n",
                                type->strukt.fields[i].name,
                                type->strukt.fields[i].type);
                    ok = false;
                    type->strukt.fields[i].type = type_get_invalid();
                }
            }
            break;

        case DECL_PROC:
            proc = (struct decl_proc *)bind->decl;

            if (!proc->out) {
                proc->out = type_get_void(proc->decl.attr);
            }

            if (!checker_resolve_type(state, &proc->out)) {
                ok = false;
            } else if (!type_is_sized(proc->out)) {
                TYCHK_ERROR(state, proc->decl.span,
                            "Procedure '%pS' has unsized return type '%pT'\n",
                            proc->sym, proc->out);
                ok = false;
                proc->out = type_get_invalid();
            }

            for (i = 0, ptr = proc->args; ptr != NULL; ptr = ptr->next) {
                if (!checker_resolve_type(state, &ptr->type)) {
                    ok = false;
                } else if (!type_is_sized(ptr->type)) {
                    TYCHK_ERROR(state, proc->decl.span,
                                "Parameter '%pS' has unsized type '%pT', consider using a pointer instead\n",
                                ptr->sym, ptr->type);
                    ok = false;
                    ptr->type = type_get_invalid();
                }
                assert(i < 32 && "too many args!");
                args[i++] = ptr->type;
            }

            if (ok) {
                bind->type = type_get_proc(proc->decl.attr, proc->out,
                                           args, proc->args_count);
            } else {
                bind->type = type_get_invalid();
            }
            break;

        case DECL_MODULE:
            scope = bind->type->module.scope;
            for (i = 0; i < scope->bindings_count; i++) {
                if (!checker_resolve_binding(state, &scope->bindings[i])) {
                    ok = false;
                }
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
    /*
     * Treat invalid types as wildcard to prevent error cascades
     */
    if (type_is_invalid(t1) || type_is_invalid(t2))
        return true;

    // XXX
    assert(t1->tag != TYPE_RAW && t2->tag != TYPE_RAW);
    return t1 == t2;
}

/*
 * Expressions
 */
static bool
checker_literal_width(int64_t value, struct type *type)
{
    int64_t i_max, i_min;
    uint64_t u_max;
    size_t bits;

    if (type->tag == TYPE_INT) {
        bits = type_bit_width(type);
        if (bits >= 64)
            return true;

        i_max = (1LL << (bits - 1)) - 1;
        i_min = -(1LL << (bits - 1));
        return value >= i_min && value <= i_max;
    }

    if (type->tag == TYPE_UINT) {
        bits = type_bit_width(type);
        if (bits >= 64)
            return value >= 0;

        u_max = (1ULL << bits) - 1;
        return value >= 0 && (uint64_t)value <= u_max;
    }

    return true;
}

static bool
checker_expr_coerce(struct checker *state, struct expr *expr,
                    struct type *target)
{
    struct expr_literal *lit;
    struct expr_unop *unop;
    struct expr_binop *binop;
    struct expr_array *array;
    struct expr_index *index;
    struct expr_access *access;
    struct expr_struct *strukt;
    struct expr_macro *macro;
    struct type *type, *inner, *coerce;
    struct struct_field *fields;
    size_t i, j;

    if (!expr->type || type_is_invalid(expr->type))
        return true;

    if (target && type_is_invalid(target))
        target = NULL;

    /*
     * Reject impossible coercion to non-integer type
     */
    if (expr->type->tag == TYPE_ANY_INT && target && !type_is_integer(target))
        return false;

#define COERCE_PROPAGATE(child, target) \
    do { \
        if (!checker_expr_coerce(state, (child), (target))) { \
            if (type_is_invalid((child)->type)) \
                expr->type = type_get_invalid(); \
            return false; \
        } \
    } while (false)

    switch (expr->tag) {
        case EXPR_LITERAL:
            lit = (struct expr_literal *)expr;
            if (expr->type->tag != TYPE_ANY_INT)
                break;

            type = target ? target : type_get_int(NULL, 0);
            if (!checker_literal_width(lit->integer, type)) {
                TYCHK_ERROR(state, expr->span,
                            "Integer literal '%ld' out of range for '%pT'\n",
                            lit->integer, type);
                expr->type = type_get_invalid();
                return false;
            }
            expr->type = type;
            break;

        case EXPR_UNOP:
            unop = (struct expr_unop *)expr;

            type = target;
            if (target && unop->unop == UNOP_ADDROF) {
                if (target->tag != TYPE_PTR)
                    break;
                type = target->pointer;
            } else if (target && unop->unop == UNOP_DEREF) {
                type = type_get_ptr(NULL, target);
            }

            COERCE_PROPAGATE(unop->op, type);

            if (expr->type->tag == TYPE_ANY_INT) {
                expr->type = target ? target : type_get_int(NULL, 0);
            } else if (unop->unop == UNOP_ADDROF) {
                expr->type = type_get_ptr(NULL, unop->op->type);
            } else if (unop->unop == UNOP_DEREF) {
                expr->type = unop->op->type->pointer;
            }
            break;

        case EXPR_BINOP:
            binop = (struct expr_binop *)expr;
            if (expr->type->tag == TYPE_ANY_INT) {
                COERCE_PROPAGATE(binop->op_lhs, target);
                COERCE_PROPAGATE(binop->op_rhs, target);
                expr->type = target ? target : type_get_int(NULL, 0);
            }
            break;

        case EXPR_ARRAY:
            array = (struct expr_array *)expr;

            type = NULL;
            if (target) {
                if (target->tag != TYPE_ARRAY)
                    break;
                if (target->array.count != array->args_count)
                    break;
                type = target->array.item;
            }

            for (i = 0; i < array->args_count; i++) {
                COERCE_PROPAGATE(array->args[i], type);
            }

            expr->type = target
                       ? target
                       : type_get_array(NULL, array->args[0]->type,
                                        array->args_count);
            break;

        case EXPR_INDEX:
            index = (struct expr_index *)expr;

            type = NULL;
            if (target) {
                inner = index->op->type;
                if (inner->tag == TYPE_ARRAY) {
                    type = type_get_array(NULL, target, inner->array.count);
                } else if (inner->tag == TYPE_SLICE) {
                    type = type_get_slice(NULL, target);
                } else if (inner->tag == TYPE_PTR) {
                    type = type_get_ptr(NULL, target);
                }
            }

            COERCE_PROPAGATE(index->op, type);

            if (index->op->type->tag == TYPE_ARRAY) {
                expr->type = index->op->type->array.item;
            } else if (index->op->type->tag == TYPE_SLICE) {
                expr->type = index->op->type->slice.item;
            } else {
                expr->type = index->op->type->pointer;
            }
            break;

        case EXPR_ACCESS:
            access = (struct expr_access *)expr;

            // XXX: Currently does not unify the inner type with target
            COERCE_PROPAGATE(access->op, NULL);

            inner = access->op->type;
            while (inner->tag == TYPE_PTR)
                inner = inner->pointer;

            if (inner->tag == TYPE_STRUCT &&
                access->index < inner->strukt.fields_count) {
                expr->type = inner->strukt.fields[access->index].type;
            } else if (inner->tag == TYPE_SLICE) {
                expr->type = access->index == 0
                           ? type_get_ptr(NULL, inner->slice.item)
                           : type_get_int(NULL, 64);
            }
            break;

        case EXPR_STRUCT: {
            strukt = (struct expr_struct *)expr;

            /*
             * Only try to coerce anonymous structs
             */
            if (strukt->op != NULL)
                break;

            if (target) {
                if (target->tag != TYPE_STRUCT || target->strukt.decl != NULL)
                    break;

                if (target->strukt.fields_count != strukt->fields_count)
                    break;
            }

            for (i = 0; i < strukt->fields_count; i++) {
                coerce = NULL;
                if (target) {
                    for (j = 0; j < target->strukt.fields_count; j++) {
                        if (target->strukt.fields[j].name
                                == strukt->fields[i].field->sym) {
                            coerce = target->strukt.fields[j].type;
                            break;
                        }
                    }

                    if (!coerce)
                        return false;
                }

                if (!checker_expr_coerce(state, strukt->fields[i].value, coerce)) {
                    if (!type_is_invalid(strukt->fields[i].value->type)) {
                        assert(coerce && "impossible?");
                        TYCHK_ERROR(state, strukt->fields[i].value->span,
                                    "Field '%pS' cannot be coerced to type '%pT'\n",
                                    strukt->fields[i].field->sym, coerce);
                    }
                    expr->type = type_get_invalid();
                    return false;
                }
            }

            if (target) {
                expr->type = target;
            } else {
                fields = calloc(strukt->fields_count, sizeof(*fields));
                for (i = 0; i < strukt->fields_count; i++) {
                    fields[i].name  = strukt->fields[i].field->sym;
                    fields[i].type  = strukt->fields[i].value->type;
                    fields[i].index = i;
                }
                expr->type = type_get_struct(NULL, NULL, NULL,
                                             fields, strukt->fields_count);
            }
            break;
        }

        case EXPR_MACRO:
            macro = (struct expr_macro *)expr;

            if (macro->macro.tag == MACRO_SLICE) {
                inner = macro->args[0]->type;
                if (inner->tag == TYPE_PTR) {
                    inner = inner->pointer;
                }

                if (inner->tag != TYPE_ARRAY)
                    break;

                type = NULL;
                if (target) {
                    if (target->tag != TYPE_SLICE)
                        break;
                    type = target->slice.item;
                }

                coerce = type
                       ? type_get_array(NULL, type, inner->array.count)
                       : NULL;
                if (coerce && macro->args[0]->type->tag == TYPE_PTR) {
                    coerce = type_get_ptr(NULL, coerce);
                }

                COERCE_PROPAGATE(macro->args[0], coerce);

                inner = macro->args[0]->type;
                if (inner->tag == TYPE_PTR)
                    inner = inner->pointer;

                expr->type = target
                           ? target
                           : type_get_slice(NULL, inner->array.item);
            } else if (macro->macro.tag == MACRO_EXPR) {
                COERCE_PROPAGATE(macro->macro.expr, target);
                expr->type = macro->macro.expr->type;
            } else if (expr->type->tag == TYPE_ANY_INT) {
                expr->type = target ? target : type_get_int(NULL, 0);
            }
            break;

        default:
            // XXX: Implement all expr checks
            assert(expr->type->tag != TYPE_ANY_INT);
            break;
    }

#undef COERCE_PROPAGATE

    return !target || expr->type == target;
}

/*
 * If checker_expr_typed and checker_expr_coerce fail, an error message
 * was already displayed iff type_is_invalid(expr->type)
 */
static bool
checker_expr_typed(struct checker *state, struct expr *expr,
                   struct type *target)
{
    if (!checker_expr(state, expr)) {
        // XXX: if (!expr->type)
        expr->type = type_get_invalid();
        return false;
    }

    if (type_is_invalid(expr->type))
        return true;

    return checker_expr_coerce(state, expr, target);
}

static bool
checker_expr_literal(struct checker *state, struct expr_literal *expr)
{
    switch (expr->literal) {
        case LIT_INTEGER:
            expr->expr.type = type_get_any_int();
            break;

        case LIT_STRING:
            expr->expr.type = type_get_ptr(NULL, type_get_uint(NULL, 8));
            break;

        case LIT_BOOL:
            expr->expr.type = type_get_bool(NULL);
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
    struct type *type;
    size_t i;

    for (i = state->locals_count; i > 0; i--) {
        if (state->locals[i - 1].sym == expr->sym) {
            expr->expr.type = state->locals[i - 1].type;
            return true;
        }
    }

    bind = scope_lookup(state->curr_scope, expr->sym);
    if (!bind) {
        TYCHK_ERROR(state, expr->expr.span,
                    "Undefined variable '%pS'\n",
                    expr->sym);
        return false;
    }

    type = checker_binding_type(state, bind, expr->expr.span);
    if (!type)
        return false;

    if (bind->is_type) {
        TYCHK_ERROR(state, expr->expr.span,
                    "Type '%pS' cannot be used as a value\n",
                    expr->sym);
        return false;
    }

    expr->expr.type = type;
    expr->link_sym = bind->link_sym;
    return true;
}

static bool
checker_expr_binop(struct checker *state, struct expr_binop *expr)
{
    if (!checker_expr(state, expr->op_lhs) ||
        !checker_expr(state, expr->op_rhs))
        return false;

    if (type_is_invalid(expr->op_lhs->type) ||
        type_is_invalid(expr->op_rhs->type)) {
        expr->expr.type = type_get_invalid();
        return true;
    }

    /*
     * Try to coerce the operands into each other
     */
    if (!checker_expr_coerce(state, expr->op_rhs, expr->op_lhs->type)) {
        if (!checker_expr_coerce(state, expr->op_lhs, expr->op_rhs->type)) {
            if (!type_is_invalid(expr->op_lhs->type) &&
                !type_is_invalid(expr->op_rhs->type)) {
                TYCHK_ERROR(state, expr->expr.span,
                            "Cannot apply binary operator to '%pT' and '%pT'\n",
                            expr->op_lhs->type, expr->op_rhs->type);
            }
            expr->expr.type = type_get_invalid();
            return false;
        }
    }

    /*
     * Assignment require stricter checks on the lvalue
     */
    if (expr->binop >= BINOP_SET && expr->binop <= BINOP_MOD_SET) {
        if (!expr_is_lvalue(expr->op_lhs)) {
            TYCHK_ERROR(state, expr->expr.span,
                        "Assignments require an lvalue as left operand\n");
            expr->expr.type = type_get_invalid();
            return false;
        }

        if (!type_is_sized(expr->op_lhs->type)) {
            TYCHK_ERROR(state, expr->expr.span,
                        "Assignments disallowed on unsized type '%pT'\n",
                        expr->op_lhs->type);
            expr->expr.type = type_get_invalid();
            return false;
        }
    }

    switch (expr->binop) {
        case BINOP_BOOL_OR:
        case BINOP_BOOL_AND:
            if (expr->op_lhs->type->tag != TYPE_BOOL) {
                TYCHK_ERROR(state, expr->expr.span,
                            "Logical operators require 'bool' operands, but got '%pT'\n",
                            expr->op_lhs->type);
                expr->expr.type = type_get_invalid();
                return false;
            }
            expr->expr.type = type_get_bool(NULL);
            break;

        case BINOP_EQ:
        case BINOP_NOTEQ:
            if (!type_is_scalar(expr->op_lhs->type)) {
                TYCHK_ERROR(state, expr->expr.span,
                            "Equality operator requires scalar type, but got '%pT'\n",
                            expr->op_lhs->type);
                expr->expr.type = type_get_invalid();
                return false;
            }
            expr->expr.type = type_get_bool(NULL);
            break;

        case BINOP_GT:
        case BINOP_GTEQ:
        case BINOP_LT:
        case BINOP_LTEQ:
            if (!type_is_ordered(expr->op_lhs->type)) {
                TYCHK_ERROR(state, expr->expr.span,
                            "Comparison operator requires ordered type, but got '%pT'\n",
                            expr->op_lhs->type);
                expr->expr.type = type_get_invalid();
                return false;
            }
            expr->expr.type = type_get_bool(NULL);
            break;

        case BINOP_SET:
            break;

        default:
            if (!type_is_integer(expr->op_lhs->type)) {
                TYCHK_ERROR(state, expr->expr.span,
                            "Operator requires integer type, but got '%pT'\n",
                            expr->op_lhs->type);
                expr->expr.type = type_get_invalid();
                return false;
            }
            expr->expr.type = expr->op_lhs->type;
            break;
    }

    if (expr->binop >= BINOP_SET && expr->binop <= BINOP_MOD_SET) {
            expr->expr.type = type_get_void(NULL);
    }

    /*
     * When the result is a concrete type we need to make sure
     * there are no lingering untyped sub-expressions
     */
    if (expr->expr.type->tag != TYPE_ANY_INT) {
        if (!checker_expr_coerce(state, expr->op_lhs, NULL)) {
            if (!type_is_invalid(expr->op_lhs->type)) {
                TYCHK_ERROR(state, expr->op_lhs->span,
                            "Cannot infer type of left operand\n");
            }
            expr->expr.type = type_get_invalid();
            return false;
        }
        if (!checker_expr_coerce(state, expr->op_rhs, NULL)) {
            if (!type_is_invalid(expr->op_rhs->type)) {
                TYCHK_ERROR(state, expr->op_rhs->span,
                            "Cannot infer type of right operand\n");
            }
            expr->expr.type = type_get_invalid();
            return false;
        }
    }

    return true;
}

static bool
checker_expr_unop(struct checker *state, struct expr_unop *expr)
{
    if (!checker_expr(state, expr->op))
        return false;

    if (expr->unop == UNOP_ADDROF) {
        if (!expr_is_mem(expr->op)) {
            TYCHK_ERROR(state, expr->expr.span,
                        "Address-of operator requires a memory location\n");
            return false;
        }
    }

    if (type_is_invalid(expr->op->type)) {
        expr->expr.type = type_get_invalid();
        return true;
    }

    switch (expr->unop) {
        case UNOP_DEREF:
            if (expr->op->type->tag != TYPE_PTR) {
                TYCHK_ERROR(state, expr->expr.span,
                            "Cannot dereference non-pointer type '%pT'\n",
                            expr->op->type);
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
                            "Expected 'bool' type, but got '%pT'\n",
                            expr->op->type);
                return false;
            }

            expr->expr.type = type_get_bool(NULL);
            break;

        case UNOP_NEG:
            if (!type_is_integer(expr->op->type)) {
                TYCHK_ERROR(state, expr->expr.span,
                            "Expected integer type, but got '%pT'\n",
                            expr->op->type);
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

    if (!checker_expr_typed(state, expr->op, NULL)) {
        if (!type_is_invalid(expr->op->type)) {
            TYCHK_ERROR(state, expr->expr.span,
                        "Cannot infer type of callee expression\n");
        }
        return false;
    }

    proc = expr->op->type;
    if (type_is_invalid(proc)) {
        expr->expr.type = type_get_invalid();
        return true;
    }

    /*
     * Allow one level of automatic pointer dereference for proc pointers
     */
    if (proc->tag == TYPE_PTR && proc->pointer->tag == TYPE_PROC) {
        proc = proc->pointer;
    }

    if (proc->tag != TYPE_PROC) {
        TYCHK_ERROR(state, expr->expr.span,
                    "Expected procedure type, but got '%pT'\n",
                    proc);
        return false;
    }

    if (proc->proc.args_count != expr->args_count) {
        TYCHK_ERROR(state, expr->expr.span,
                    "Procedure expects %zu arguments, but %zu were provided\n",
                    proc->proc.args_count, expr->args_count);
        return false;
    }

    for (i = 0; i < expr->args_count; i++) {
        if (!checker_expr_typed(state, expr->args[i], proc->proc.args[i])) {
            if (!type_is_invalid(expr->args[i]->type)) {
                TYCHK_ERROR(state, expr->expr.span,
                            "Argument has type '%pT', expected '%pT'\n",
                            expr->args[i]->type, proc->proc.args[i]);
                expr->args[i]->type = type_get_invalid();
            }
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

    if (type_is_invalid(expr->op->type) ||
        type_is_invalid(expr->index->type)) {
        expr->expr.type = type_get_invalid();
        return true;
    }

    if (!type_is_integer(expr->index->type)) {
        TYCHK_ERROR(state, expr->expr.span,
                    "Expected index of integer type, but got '%pT'\n",
                    expr->index->type);
        return false;
    }

    if (expr->index->type->tag == TYPE_ANY_INT) {
        if (!checker_expr_coerce(state, expr->index, NULL)) {
            if (!type_is_invalid(expr->index->type)) {
                TYCHK_ERROR(state, expr->index->span,
                            "Cannot infer type of index value\n");
            }
            return false;
        }
    }

    if (expr->op->type->tag == TYPE_ARRAY) {
        expr->expr.type = expr->op->type->array.item;
        return true;
    }

    if (expr->op->type->tag == TYPE_SLICE) {
        expr->expr.type = expr->op->type->slice.item;
        return true;
    }

    if (expr->op->type->tag == TYPE_PTR) {
        expr->expr.type = expr->op->type->pointer;
        return true;
    }

    TYCHK_ERROR(state, expr->expr.span,
                "Cannot index into type '%pT'\n",
                expr->op->type);
    return false;
}

static bool
checker_expr_access(struct checker *state, struct expr_access *expr)
{
    struct type *type;
    struct binding *bind;
    size_t i;

    if (!checker_expr(state, expr->op))
        return false;

    type = expr->op->type;
    if (type_is_invalid(type)) {
        expr->expr.type = type_get_invalid();
        return true;
    }

    if (type->tag == TYPE_MODULE) {
        bind = scope_lookup_local(type->module.scope, expr->field);
        if (!bind) {
            TYCHK_ERROR(state, expr->expr.span,
                        "Module '%pS' has no member named '%pS'\n",
                        type->module.scope->name, expr->field);
            return false;
        }

        type = checker_binding_type(state, bind, expr->expr.span);
        if (!type)
            return false;

        if (bind->is_type) {
            TYCHK_ERROR(state, expr->expr.span,
                        "Type '%pS' cannot be used as a value\n",
                        expr->field);
            return false;
        }

        expr->expr.type = type;
        expr->link_sym = bind->link_sym;
        return true;
    }

    /*
     * Slices expose the fields 'ptr' and 'len'
     */
    if (type->tag == TYPE_SLICE) {
        if (expr->field == symbol_ptr) {
            expr->expr.type = type_get_ptr(NULL, type->slice.item);
            expr->index = 0;
            return true;
        }

        if (expr->field == symbol_len) {
            expr->expr.type = type_get_int(NULL, 64);
            expr->index = 1;
            return true;
        }

        TYCHK_ERROR(state, expr->expr.span,
                    "Slices have no field named '%pS'\n",
                    expr->field);
        return false;
    }

    /*
     * Automatically dereference struct pointers
     */
    while (type->tag == TYPE_PTR) {
        type = type->pointer;
    }

    if (type->tag != TYPE_STRUCT) {
        TYCHK_ERROR(state, expr->expr.span,
                    "Cannot access field '%pS' on non-struct type '%pT'\n",
                    expr->field, type);
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
                "Struct has no field named '%pS'\n",
                expr->field);
    return false;
}

static bool
checker_expr_array(struct checker *state, struct expr_array *expr)
{
    struct type *type;
    size_t i, j;

    // XXX: Allow [] in the future?
    if (expr->args_count == 0) {
        TYCHK_ERROR(state, expr->expr.span,
                    "Empty array literal is not allowed\n");
        expr->expr.type = type_get_invalid();
        return false;
    }

    if (!checker_expr(state, expr->args[0])) {
        expr->expr.type = type_get_invalid();
        return false;
    }

    type = expr->args[0]->type;
    if (type_is_invalid(type)) {
        expr->expr.type = type_get_invalid();
        return true;
    }

    if (type->tag != TYPE_ANY_INT && !type_is_sized(type)) {
        TYCHK_ERROR(state, expr->args[0]->span,
                    "Array element has unsized type '%pT'\n",
                    type);
        expr->expr.type = type_get_invalid();
        return false;
    }

    for (i = 1; i < expr->args_count; i++) {
        if (!checker_expr(state, expr->args[i])) {
            expr->expr.type = type_get_invalid();
            return false;
        }

        if (checker_cmp(state, type, expr->args[i]->type))
            continue;

        /*
         * Try coercing the new element to the accumulated type
         */
        if (checker_expr_coerce(state, expr->args[i], type))
            continue;

        if (type_is_invalid(expr->args[i]->type)) {
            expr->expr.type = type_get_invalid();
            return false;
        }

        /*
         * If the accumulated type was untyped, coerce all previous elements
         * to this element's type and make this the reference
         */
        for (j = 0; j < i; j++) {
            if (!checker_expr_coerce(state, expr->args[j], expr->args[i]->type)) {
                if (!type_is_invalid(expr->args[j]->type)) {
                    TYCHK_ERROR(state, expr->args[j]->span,
                                "Elements have mismatched types '%pT' and '%pT'\n",
                                expr->args[j]->type, expr->args[i]->type);
                }
                expr->expr.type = type_get_invalid();
                return false;
            }
        }

        type = expr->args[i]->type;
    }

    expr->expr.type = type_get_array(NULL, type, expr->args_count);
    return true;
}

static bool
checker_expr_struct(struct checker *state, struct expr_struct *expr)
{
    struct type *type;
    struct struct_field *fields;
    size_t i, j;
    bool ok = true;

    /*
     * Anonymous struct literals
     */
    if (!expr->op) {
        // XXX: leaks
        fields = calloc(expr->fields_count, sizeof(struct struct_field));

        for (i = 0; i < expr->fields_count; i++) {
            if (expr->fields[i].colon && expr->fields[i].value) {
                TYCHK_ERROR(state, expr->expr.span,
                            "Struct literals use '=', not ':'\n");
                ok = false;
                continue;
            }

            /*
             * Iterate fields to prevent invalid duplicates
             */
            for (j = 0; j < i; j++) {
                if (expr->fields[j].field->sym == expr->fields[i].field->sym) {
                    TYCHK_ERROR(state, expr->fields[i].field->expr.span,
                                "Field '%pS' initialized more than once\n",
                                expr->fields[i].field->sym);
                    ok = false;
                    break;
                }
            }

            if (!expr->fields[i].value) {
                expr->fields[i].value = &expr->fields[i].field->expr;
            }

            /*
             * Leave untyped literals here and coerce the whole struct later on
             */
            if (!checker_expr(state, expr->fields[i].value)) {
                expr->fields[i].value->type = type_get_invalid();
                ok = false;
            }

            fields[i].name = expr->fields[i].field->sym;
            fields[i].type = expr->fields[i].value->type;
            fields[i].index = i;
        }

        type = ok
             ? type_get_struct(NULL, NULL, NULL, fields, expr->fields_count)
             : type_get_invalid();
        expr->expr.type = type;
        return ok;
    }

    /*
     * Named struct literals
     */
    type = checker_eval_raw(state, expr->op);
    if (!type) {
        expr->expr.type = type_get_invalid();
        return false;
    }

    if (type_is_invalid(type)) {
        expr->expr.type = type_get_invalid();
        return true;
    }

    if (type->tag != TYPE_STRUCT) {
        TYCHK_ERROR(state, expr->op->span,
                    "Expected struct type for literal, but got '%pT'\n",
                    type);
        expr->expr.type = type_get_invalid();
        return false;
    }

    for (i = 0; i < expr->fields_count; i++) {
        fields = NULL;

        if (expr->fields[i].colon && expr->fields[i].value) {
            TYCHK_ERROR(state, expr->expr.span,
                        "Struct literals use '=', not ':'\n");
            ok = false;
            continue;
        }

        for (j = 0; j < i; j++) {
            if (expr->fields[j].field->sym == expr->fields[i].field->sym) {
                TYCHK_ERROR(state, expr->fields[i].field->expr.span,
                            "Field '%pS' initialized more than once\n",
                            expr->fields[i].field->sym);
                ok = false;
                break;
            }
        }

        for (j = 0; j < type->strukt.fields_count; j++) {
            if (type->strukt.fields[j].name == expr->fields[i].field->sym) {
                fields = &type->strukt.fields[j];
                break;
            }
        }

        if (!fields) {
            TYCHK_ERROR(state, expr->fields[i].field->expr.span,
                        "Struct '%pT' has no field named '%pS'\n",
                        type, expr->fields[i].field->sym);

            if (type->strukt.decl) {
                TYCHK_INFO(state, type->strukt.decl->decl.span,
                           "Struct '%pT' was declared here\n",
                           type);
            }
            ok = false;
            continue;
        }

        // XXX: Maybe customize the error for this?
        if (!expr->fields[i].value) {
            /*
             * Synthesize value if we don't get an explicit expression
             */
            expr->fields[i].value = &expr->fields[i].field->expr;
        }

        if (!checker_expr_typed(state, expr->fields[i].value, fields->type)) {
            if (!type_is_invalid(expr->fields[i].value->type)) {
                TYCHK_ERROR(state, expr->fields[i].value->span,
                            "Field '%pS' type mismatch, got '%pT' instead of '%pT'\n",
                            expr->fields[i].field->sym,
                            expr->fields[i].value->type,
                            fields->type);
                expr->fields[i].value->type = type_get_invalid();
            }
            ok = false;
        }
    }

    expr->expr.type = ok ? type : type_get_invalid();
    return ok;
}

static bool
checker_expr_macro(struct checker *state, struct expr_macro *expr)
{
    struct type *arg, *target, *type;

    if (expr->macro.tag != MACRO_INVALID)
        return true;

    if (expr->name == symbol_typeof) {
        TYCHK_ERROR(state, expr->expr.span,
                    "'@%pS' produces a type and cannot be used as a value\n",
                    expr->name);
        return false;
    }

    if (expr->name == symbol_sizeof || expr->name == symbol_alignof) {
        if (expr->body || expr->args_count != 1) {
            TYCHK_ERROR(state, expr->expr.span,
                        "Expected 1 argument and no block\n");
            return false;
        }

        type = checker_eval_raw(state, expr->args[0]);
        if (!type)
            return false;

        if (type_is_invalid(type)) {
            expr->expr.type = type_get_invalid();
            return true;
        }

        if (!type_is_sized(type)) {
            TYCHK_ERROR(state, expr->expr.span,
                        "Expected a sized type, but got '%pT'\n",
                        type);
            return false;
        }

        if (expr->name == symbol_sizeof) {
            expr->macro.tag = MACRO_SIZEOF;
            expr->macro.value = type_sizeof(type);
        } else {
            expr->macro.tag = MACRO_ALIGNOF;
            expr->macro.value = type_alignof(type);
        }

        expr->expr.type = type_get_any_int();
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

        if (type_is_invalid(type) || type_is_invalid(expr->args[1]->type)) {
            expr->expr.type = type_get_invalid();
            return true;
        }

        if (expr->args[1]->type->tag == TYPE_ANY_INT) {
            /*
             * For concrete integers and pointers try to coerce them
             * to prevent errors on the overflow of large literals
             */
            target = NULL;
            if (type->tag == TYPE_INT || type->tag == TYPE_UINT) {
                // XXX: Kludge to not error out on truncations
                if (type_bit_width(type) > INT_SIZE * 8)
                    target = type;
            } else if (type->tag == TYPE_PTR) {
                target = type_get_uint(NULL, PTR_SIZE * 8);
            }

            if (!checker_expr_coerce(state, expr->args[1], target)) {
                if (!type_is_invalid(expr->args[1]->type)) {
                    TYCHK_ERROR(state, expr->args[1]->span,
                                "Cannot coerce argument of @cast to '%pT'\n",
                                type);
                }
                return false;
            }
        }

        if (!type_is_scalar(expr->args[1]->type) || !type_is_scalar(type)) {
            TYCHK_ERROR(state, expr->expr.span,
                        "Unsupported non-scalar cast from type '%pT' to '%pT'\n",
                        expr->args[1]->type, type);
            return false;
        }

        expr->macro.tag = MACRO_CAST;
        expr->expr.type = type;
        return true;
    }

    if (expr->name == symbol_slice) {
        if (expr->body || expr->args_count != 1) {
            TYCHK_ERROR(state, expr->expr.span,
                        "Expected 1 argument: @slice(array)\n");
            return false;
        }

        if (!checker_expr(state, expr->args[0]))
            return false;

        arg = expr->args[0]->type;
        if (type_is_invalid(arg)) {
            expr->expr.type = type_get_invalid();
            return true;
        }

        if (arg->tag == TYPE_ARRAY) {
            type = arg->array.item;
        } else if (arg->tag == TYPE_PTR && arg->pointer->tag == TYPE_ARRAY) {
            type = arg->pointer->array.item;
        } else {
            TYCHK_ERROR(state, expr->expr.span,
                        "Expected an array or pointer to array, but got '%pT'\n",
                        arg);
            return false;
        }

        expr->macro.tag = MACRO_SLICE;
        expr->expr.type = type_get_slice(NULL, type);
        return true;
    }

    TYCHK_ERROR(state, expr->expr.span,
                "Unknown builtin macro '@%pS'\n",
                expr->name);
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
    bool ok = true;

    /*
     * The user could have provided an explicit type
     */
    if (stmt->type) {
        if (!checker_resolve_type(state, &stmt->type)) {
            ok = false;
        }
    }

    if (stmt->value) {
        if (!checker_expr(state, stmt->value)) {
            stmt->value->type = type_get_invalid();
            ok = false;
        } else if (!type_is_invalid(stmt->value->type)) {
            if (!stmt->type) {
                if (!checker_expr_coerce(state, stmt->value, NULL)) {
                    if (!type_is_invalid(stmt->value->type)) {
                        TYCHK_ERROR(state, stmt->value->span,
                                    "Cannot infer type of variable '%pS'\n",
                                    stmt->sym);
                    }
                    ok = false;
                    stmt->type = type_get_invalid();
                } else {
                    stmt->type = stmt->value->type;
                }
            } else if (!checker_expr_coerce(state, stmt->value, stmt->type)) {
                if (!type_is_invalid(stmt->value->type)) {
                    TYCHK_ERROR(state, stmt->stmt.span,
                                "Cannot initialize variable of type '%pT' with type '%pT'\n",
                                stmt->type, stmt->value->type);
                }
                ok = false;
            }
        }
    }

    if (!stmt->type) {
        if (!stmt->value || !type_is_invalid(stmt->value->type)) {
            TYCHK_ERROR(state, stmt->stmt.span,
                        "Cannot infer type for variable '%pS', explicit annotation needed\n",
                        stmt->sym);
            ok = false;
        }
        stmt->type = type_get_invalid();
    } else if (!type_is_invalid(stmt->type) && !type_is_sized(stmt->type)) {
        TYCHK_ERROR(state, stmt->stmt.span,
                    "Variable '%pS' has unsized type '%pT', consider using a pointer instead\n",
                    stmt->sym, stmt->type);
        ok = false;
        stmt->type = type_get_invalid();
    }

    checker_local_push(state, stmt->sym, stmt->type);
    return ok;
}

static bool
checker_stmt_expr(struct checker *state, struct stmt_expr *stmt)
{
    if (!checker_expr_typed(state, stmt->expr, NULL)) {
        if (!type_is_invalid(stmt->expr->type)) {
            TYCHK_ERROR(state, stmt->expr->span,
                        "Cannot infer type of expression statement\n");
            stmt->expr->type = type_get_invalid();
        }
        return false;
    }
    return true;
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
    bool ok = true;

    if (!checker_expr_typed(state, stmt->cond, NULL)) {
        if (!type_is_invalid(stmt->cond->type)) {
            TYCHK_ERROR(state, stmt->cond->span,
                        "Cannot infer type of condition expression\n");
            stmt->cond->type = type_get_invalid();
        }
        ok = false;
    }

    if (!checker_cmp(state, stmt->cond->type, type_get_bool(NULL))) {
        TYCHK_ERROR(state, stmt->stmt.span,
                    "Condition must be 'bool', but got '%pT'\n",
                    stmt->cond->type);
        ok = false;
    }

    if (!checker_stmt(state, stmt->b_true)) {
        ok = false;
    }

    if (stmt->b_false && !checker_stmt(state, stmt->b_false)) {
        ok = false;
    }

    return ok;
}

static bool
checker_stmt_return(struct checker *state, struct stmt_return *stmt)
{
    if (!stmt->expr) {
        if (!checker_cmp(state, state->ret_type, type_get_void(NULL))) {
            TYCHK_ERROR(state, stmt->stmt.span,
                        "Expected return value of type '%pT'\n",
                        state->ret_type);
            return false;
        }
    } else if (!checker_expr_typed(state, stmt->expr, state->ret_type)) {
        if (!type_is_invalid(stmt->expr->type)) {
            TYCHK_ERROR(state, stmt->stmt.span,
                        "Cannot return type '%pT' from procedure expecting '%pT'\n",
                        stmt->expr->type, state->ret_type);
            stmt->expr->type = type_get_invalid();
        }
        return false;
    }

    return true;
}

static bool
checker_stmt_while(struct checker *state, struct stmt_while *stmt)
{
    bool ok = true;

    if (!checker_expr_typed(state, stmt->cond, NULL)) {
        if (!type_is_invalid(stmt->cond->type)) {
            TYCHK_ERROR(state, stmt->cond->span,
                        "Cannot infer type of condition expression\n");
            stmt->cond->type = type_get_invalid();
        }
        ok = false;
    }

    if (!checker_cmp(state, stmt->cond->type, type_get_bool(NULL))) {
        TYCHK_ERROR(state, stmt->stmt.span,
                    "Condition must be 'bool', but got '%pT'\n",
                    stmt->cond->type);
        ok = false;
    }

    state->loop_depth++;
    if (!checker_stmt(state, stmt->body)) {
        ok = false;
    }
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
static bool checker_register_decl(struct checker *state, struct decl *decl);

static bool checker_register_file(struct checker *state, struct modfile *file);

static bool
checker_decl_import(struct checker *state, struct decl_import *decl)
{
    struct symbol *mod;
    struct type *type;
    struct binding *bind;
    struct modfile *file;

    if (decl->expr->tag != EXPR_IDENT) {
        TYCHK_ERROR(state, decl->decl.span,
                    "Only simple imports are supported for now\n");
        return false;
    }

    mod = ((struct expr_ident *)decl->expr)->sym;

    bind = scope_lookup_local(state->curr_scope, mod);
    if (!bind) {
        file = NULL;
        if (!modcache_import(state->cache, mod, state->cache->rootdir, &file)) {
            TYCHK_ERROR(state, decl->decl.span,
                        "Cannot find module '%pS'\n",
                        mod);
            return false;
        }
        if (!checker_register_file(state, file))
            return false;

        type = ((struct decl_module *)file->decl)->type;
    } else {
        type = bind->type;
    }

    if (!type || type->tag != TYPE_MODULE) {
        TYCHK_ERROR(state, decl->decl.span,
                    "'%pS' is not a module\n",
                    mod);
        return false;
    }

    if (scope_lookup_local(state->curr_scope, mod)) {
        TYCHK_ERROR(state, decl->decl.span,
                    "Redefinition of module '%pS'\n",
                    mod);
        return false;
    }

    bind = scope_push(state->curr_scope, mod, type, &decl->decl, false);
    bind->status = RESOLVE_DONE;
    return true;
}

static bool
checker_register_module(struct checker *state, struct decl_module *decl)
{
    size_t i;
    bool ok = true;

    for (i = 0; i < decl->items_count; i++) {
        if (decl->items[i]->tag != DECL_IMPORT) {
            if (!checker_register_decl(state, decl->items[i]))
                ok = false;
        }
    }

    for (i = 0; i < decl->items_count; i++) {
        if (decl->items[i]->tag == DECL_IMPORT) {
            if (!checker_decl_import(state, (struct decl_import *)decl->items[i]))
                ok = false;
        }
    }

    return ok;
}

static bool
checker_register_decl(struct checker *state, struct decl *decl)
{
    struct decl_proc *proc;
    struct decl_struct *strukt;
    struct decl_type *d_type;
    struct decl_module *mod;
    struct scope *saved, *scope;
    struct binding *bind;
    struct symbol *prefix;
    struct type *type;
    bool ok = true;

    switch (decl->tag) {
        case DECL_PROC:
            proc = (struct decl_proc *)decl;
            bind = scope_lookup_local(state->curr_scope, proc->sym);
            if (bind)
                goto redef;

            bind = scope_push(state->curr_scope, proc->sym, NULL, decl, false);

            // XXX: do this with attrs/extern
            if (!proc->body || !strcmp(proc->sym->str, "main")) {
                bind->link_sym = proc->sym;
            }

            proc->link_sym = bind->link_sym;
            return true;

        case DECL_STRUCT:
            strukt = (struct decl_struct *)decl;
            bind = scope_lookup_local(state->curr_scope, strukt->sym);
            if (bind)
                goto redef;

            if (!strukt->type) {
                strukt->type = type_get_struct(decl->attr, decl, strukt->sym,
                                               strukt->fields,
                                               strukt->fields_count);
            }

            bind = scope_push(state->curr_scope, strukt->sym,
                              strukt->type, decl, true);
            strukt->link_sym = bind->link_sym;
            return true;

        case DECL_TYPE:
            d_type = (struct decl_type *)decl;
            bind = scope_lookup_local(state->curr_scope, d_type->sym);
            if (bind)
                goto redef;

            /*
             * If no alias type is given, treat as opaque
             */
            type = d_type->type
                 ? NULL
                 : type_get_opaque(decl->attr, decl, d_type->sym);

            scope_push(state->curr_scope, d_type->sym, type, decl, true);
            return true;

        case DECL_MODULE:
            mod = (struct decl_module *)decl;
            bind = scope_lookup_local(state->curr_scope, mod->sym);
            if (bind)
                goto redef;

            /*
             * Principal modules get their prefix from their path,
             * while module declarations inherit it from the enclosing scope
             */
            prefix = mod->prefix
                   ? mod->prefix
                   : scope_mangle(mod->sym, state->curr_scope->prefix);

            if (!mod->type) {
                scope = scope_make(mod->sym, prefix, state->curr_scope);
                mod->type = type_get_module(decl->attr, scope);
            }

            scope_push(state->curr_scope, mod->sym, mod->type, decl, false);

            saved = state->curr_scope;
            state->curr_scope = mod->type->module.scope;

            if (!checker_register_module(state, mod)) {
                ok = false;
            }

            state->curr_scope = saved;
            return ok;

        case DECL_SIG:
            TYCHK_ERROR(state, decl->span,
                        "Signature declarations are not yet supported\n");
            return false;

        default:
            unreachable();
    }

redef:
    TYCHK_ERROR(state, decl->span,
                "Redefinition of symbol '%pS'\n",
                bind->sym);

    if (bind->decl) {
        TYCHK_INFO(state, bind->decl->span,
                   "Symbol '%pS' was originally declared here\n",
                   bind->sym);
    }
    return false;
}

static bool
checker_register_file(struct checker *state, struct modfile *file)
{
    struct decl_module *mod;
    struct scope *saved_scope, *scope;
    struct source *saved_src;
    bool ok = true;

    if (file->registered)
        return true;

    file->registered = true;

    mod = (struct decl_module *)file->decl;
    if (!mod->type) {
        scope = scope_make(mod->sym, mod->prefix, state->root_scope);
        mod->type = type_get_module(mod->decl.attr, scope);
    }

    saved_src = state->src;
    saved_scope = state->curr_scope;

    state->src = &file->src;
    state->curr_scope = mod->type->module.scope;

    ok = checker_register_module(state, mod);

    state->src = saved_src;
    state->curr_scope = saved_scope;
    return ok;
}

static bool
checker_validate_decl(struct checker *state, struct decl *decl)
{
    struct decl_module *mod;
    struct decl_proc *proc;
    struct proc_arg *ptr;
    struct scope *saved;
    size_t i;
    bool ok = true;

    if (decl->tag == DECL_MODULE) {
        mod = (struct decl_module *)decl;
        if (!mod->type) {
            /*
             * Registration failed, do not print error here
             */
            return false;
        }

        if (mod->type->tag != TYPE_MODULE) {
            TYCHK_ERROR(state, decl->span,
                        "Module '%pS' was not registered\n",
                        mod->sym);
            return false;
        }

        saved = state->curr_scope;
        state->curr_scope = mod->type->module.scope;

        for (i = 0; i < mod->items_count; i++) {
            if (!checker_validate_decl(state, mod->items[i]))
                ok = false;
        }

        state->curr_scope = saved;
        return ok;
    }

    if (decl->tag != DECL_PROC)
        return true;

     /*
     * Proc without a body should not be checked...
     */
    proc = (struct decl_proc *)decl;
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

    if (!proc->out)
        return false;

    if (ok && proc->out->tag != TYPE_VOID && !checker_proc_returns(proc->body)) {
        TYCHK_ERROR(state, decl->span,
                    "Procedure '%pS' does not return a value on all branches\n",
                    proc->sym);
        ok = false;
    }

    return ok;
}

static bool
checker_validate_file(struct checker *state, struct modfile *file)
{
    struct source *saved_src;
    bool ok;

    saved_src = state->src;
    state->src = &file->src;

    ok = checker_validate_decl(state, file->decl);

    state->src = saved_src;
    return ok;
}

static bool
checker_resolve_file(struct checker *state, struct modfile *file)
{
    struct decl_module *mod;
    struct scope *scope;
    struct source *saved_src;
    size_t i;
    bool ok = true;

    mod = (struct decl_module *)file->decl;
    assert(mod->type && "impossible?");
    scope = mod->type->module.scope;

    saved_src = state->src;
    state->src = &file->src;

    for (i = 0; i < scope->bindings_count; i++) {
        if (!checker_resolve_binding(state, &scope->bindings[i]))
            ok = false;
    }

    state->src = saved_src;
    return ok;
}

bool
checker_modcache(struct checker *state)
{
    struct modfile *file;
    size_t i;
    bool ok = true;

    /*
     * Register all declarations recursively (the list grows as we go)
     */
    for (i = 0; i < state->cache->mods_count; i++) {
        file = &state->cache->mods[i];
        if (!checker_register_file(state, file)) {
            ok = false;
        }
    }

    /*
     * Resolve bindings starting from the root modules
     */
    for (i = 0; i < state->cache->mods_count; i++) {
        file = &state->cache->mods[i];
        if (!checker_resolve_file(state, file)) {
            ok = false;
        }
    }

    /*
     * Validate AST bodies (proc, modules, etc)
     */
    for (i = 0; i < state->cache->mods_count; i++) {
        file = &state->cache->mods[i];
        if (!checker_validate_file(state, file)) {
            ok = false;
        }
    }

    return ok;
}
