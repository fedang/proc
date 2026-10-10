#include <stdlib.h>
#include <stdio.h>

#include <assert.h>
#define unreachable() assert(!"unreachable")

#include "typecheck.h"
#include "report.h"

#define TYCHK_FIRST_PASS    0
#define TYCHK_SECOND_PASS   1
#define TYCHK_THIRD_PASS    2

#define TYCHK_ERROR(state, span, ...) \
    report_error((state)->src, (span), __VA_ARGS__)

static void
typecheck_type_push(struct typecheck *state, struct symbol *sym,
                    struct type *type)
{
    assert(state->types_count < 256 && "too many types!");

    state->types[state->types_count].sym = sym;
    state->types[state->types_count].type = type;
    state->types_count++;
}

void
typecheck_init(struct typecheck *state, struct source *src)
{
    state->src = src;
    state->pass = TYCHK_FIRST_PASS;
    state->ret_type = NULL;
    state->loop_depth = 0;
    state->locals_count = 0;
    state->globals_count = 0;
    state->types_count = 0;

    typecheck_type_push(state, symbol_make("i8"), type_get_int(NULL, 8));
    typecheck_type_push(state, symbol_make("i16"), type_get_int(NULL, 16));
    typecheck_type_push(state, symbol_make("i32"), type_get_int(NULL, 32));
    typecheck_type_push(state, symbol_make("i64"), type_get_int(NULL, 64));
    typecheck_type_push(state, symbol_make("int"), type_get_int(NULL, 0));
    typecheck_type_push(state, symbol_make("u8"), type_get_uint(NULL, 8));
    typecheck_type_push(state, symbol_make("u16"), type_get_uint(NULL, 16));
    typecheck_type_push(state, symbol_make("u32"), type_get_uint(NULL, 32));
    typecheck_type_push(state, symbol_make("u64"), type_get_uint(NULL, 64));
    typecheck_type_push(state, symbol_make("uint"), type_get_uint(NULL, 0));
    typecheck_type_push(state, symbol_make("bool"), type_get_bool(NULL));
    typecheck_type_push(state, symbol_make("void"), type_get_void(NULL));
}

static void
typecheck_local_push(struct typecheck *state, struct symbol *sym,
                     struct type *type)
{
    assert(state->locals_count < 256 && "too many locals!");

    state->locals[state->locals_count].sym = sym;
    state->locals[state->locals_count].type = type;
    state->locals_count++;
}

static void
typecheck_local_pop(struct typecheck *state, unsigned n)
{
    assert(n <= state->locals_count && "locals overflow");

    // TODO: Cleanup memory?
    state->locals_count = n;
}

static void
typecheck_global_push(struct typecheck *state, struct symbol *sym,
                      struct type *type)
{
    assert(state->globals_count < 256 && "too many globals!");

    state->globals[state->globals_count].sym = sym;
    state->globals[state->globals_count].type = type;
    state->globals_count++;
}

static struct type *
typecheck_lookup(struct typecheck *state, struct symbol *sym)
{
    size_t i;

    /*
     * First search the locals, starting from the latest,
     * then continue with the globals (in any order)
     */
    for (i = state->locals_count; i > 0; i--) {
        if (state->locals[i - 1].sym == sym)
            return state->locals[i - 1].type;
    }

    for (i = 0; i < state->globals_count; i++) {
        if (state->globals[i].sym == sym)
            return state->globals[i].type;
    }

    return NULL;
}

static bool
typecheck_resolve(struct typecheck *state, struct type **type)
{
    size_t i;
    struct type *t = *type;
    bool ok = true;

    if (!type)
        return true;

    if (t->tag == TYPE_NAMED) {
        for (i = 0; i < state->types_count; i++) {
            if (state->types[i].sym == t->named) {
                *type = state->types[i].type;
                return typecheck_resolve(state, type);
            }
        }

        printf("Unknown type '%s'\n", t->named->str);
        return false;
    }

    if (t->tag == TYPE_PTR) {
        return typecheck_resolve(state, &t->pointer);
    }

    if (t->tag == TYPE_ARRAY) {
        return typecheck_resolve(state, &t->array.item);
    }

    if (t->tag == TYPE_PROC) {
        if (!typecheck_resolve(state, &t->proc.out)) {
            ok = false;
        }

        for (i = 0; i < t->proc.args_count; i++) {
            if (!typecheck_resolve(state, &t->proc.args[i])) {
                ok = false;
            }
        }
        return ok;
    }

    return true;
}

static bool
typecheck_cmp(struct typecheck *state, struct type *t1, struct type *t2)
{
    return t1 == t2;
}

static bool typecheck_expr(struct typecheck *state, struct expr *expr);

static bool
typecheck_expr_literal(struct typecheck *state, struct expr_literal *expr)
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
typecheck_expr_ident(struct typecheck *state, struct expr_ident *expr)
{
    struct type *type;

    type = typecheck_lookup(state, expr->sym);
    if (!type) {
        TYCHK_ERROR(state, expr->expr.span,
                    "Undefined variable '%s'\n", expr->sym->str);
        return false;
    }

    expr->expr.type = type;
    return true;
}

static bool
typecheck_expr_binop(struct typecheck *state, struct expr_binop *expr)
{
    if (!typecheck_expr(state, expr->op_lhs) || !typecheck_expr(state, expr->op_rhs))
        return false;

    if (!typecheck_cmp(state, expr->op_lhs->type, expr->op_rhs->type)) {
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
    }

    switch (expr->binop) {
        case BINOP_BOOL_OR:
        case BINOP_BOOL_AND:
            if (!typecheck_cmp(state, expr->op_lhs->type, type_get_bool(NULL))) {
                TYCHK_ERROR(state, expr->expr.span,
                            "Expected bool operands for and/or\n");
                return false;
            }
            /* fall through */

        case BINOP_EQ:
        case BINOP_NOTEQ:
        case BINOP_GT:
        case BINOP_GTEQ:
        case BINOP_LT:
        case BINOP_LTEQ:
            expr->expr.type = type_get_bool(NULL);
            break;

        default:
            expr->expr.type = expr->op_lhs->type;
            break;
    }

    return true;
}

static bool
typecheck_expr_unop(struct typecheck *state, struct expr_unop *expr)
{
    if (expr->unop == UNOP_ADDROF) {
        if (!expr_is_lvalue(expr->op)) {
            TYCHK_ERROR(state, expr->expr.span,
                        "Unop operand is not an lvalue\n");
            return false;
        }
    }

    if (!typecheck_expr(state, expr->op))
        return false;

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
typecheck_expr_call(struct typecheck *state, struct expr_call *expr)
{
    struct type *proc;
    size_t i;

    if (!typecheck_expr(state, expr->op))
        return false;

    proc = expr->op->type;
    if (proc->tag != TYPE_PROC || proc->proc.args_count != expr->args_count) {
        TYCHK_ERROR(state, expr->expr.span,
                    "Call target is not a proc\n");
        return false;
    }

    for (i = 0; i < expr->args_count; i++) {
        if (!typecheck_expr(state, expr->args[i]))
            return false;

        if (!typecheck_cmp(state, proc->proc.args[i], expr->args[i]->type)) {
            TYCHK_ERROR(state, expr->expr.span,
                        "Call argument does not match proc\n");
            return false;
        }
    }

    expr->expr.type = proc->proc.out;
    return true;
}

static bool
typecheck_expr_index(struct typecheck *state, struct expr_index *expr)
{
    if (!typecheck_expr(state, expr->op) || !typecheck_expr(state, expr->index))
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
typecheck_expr_access(struct typecheck *state, struct expr_access *expr)
{
    struct type *type;
    size_t i;

    if (!typecheck_expr(state, expr->op))
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
            expr->offset = i;
            return true;
        }
    }

    TYCHK_ERROR(state, expr->expr.span,
                "Found no field named '%s'\n", expr->field->str);
    return false;
}

static bool
typecheck_expr(struct typecheck *state, struct expr *expr)
{
    switch (expr->tag) {
        case EXPR_LITERAL:
            return typecheck_expr_literal(state, (struct expr_literal *)expr);

        case EXPR_IDENT:
            return typecheck_expr_ident(state, (struct expr_ident *)expr);

        case EXPR_BINOP:
            return typecheck_expr_binop(state, (struct expr_binop *)expr);

        case EXPR_UNOP:
            return typecheck_expr_unop(state, (struct expr_unop *)expr);

        case EXPR_CALL:
            return typecheck_expr_call(state, (struct expr_call *)expr);

        case EXPR_INDEX:
            return typecheck_expr_index(state, (struct expr_index *)expr);

        case EXPR_ACCESS:
            return typecheck_expr_access(state, (struct expr_access *)expr);

        default:
            unreachable();
    }
}

static bool typecheck_stmt(struct typecheck *state, struct stmt *stmt);

static bool
typecheck_stmt_var(struct typecheck *state, struct stmt_var *stmt)
{
    /*
     * The user could have provided an explicit type
     */
    if (stmt->type) {
        if (!typecheck_resolve(state, &stmt->type))
            return false;
    }

    if (stmt->value) {
        if (!typecheck_expr(state, stmt->value))
            return false;

        if (!stmt->type) {
            stmt->type = stmt->value->type;
        } else if (!typecheck_cmp(state, stmt->type, stmt->value->type)) {
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

    typecheck_local_push(state, stmt->sym, stmt->type);
    return true;
}

static bool
typecheck_stmt_expr(struct typecheck *state, struct stmt_expr *stmt)
{
    return typecheck_expr(state, stmt->expr);
}

static bool
typecheck_stmt_block(struct typecheck *state, struct stmt_block *stmt)
{
    size_t i;
    unsigned locals;
    bool ok = true;

    locals = state->locals_count;

    for (i = 0; i < stmt->items_count; i++) {
        if (!typecheck_stmt(state, stmt->items[i])) {
            ok = false;
        }
    }

    typecheck_local_pop(state, locals);

    return ok;
}

static bool
typecheck_stmt_if(struct typecheck *state, struct stmt_if *stmt)
{
    if (!typecheck_expr(state, stmt->cond))
        return false;

    if (!typecheck_cmp(state, stmt->cond->type, type_get_bool(NULL))) {
        TYCHK_ERROR(state, stmt->stmt.span,
                    "If condition is not of type bool\n");
        return false;
    }

    if (!typecheck_stmt(state, stmt->b_true))
        return false;

    if (stmt->b_false && !typecheck_stmt(state, stmt->b_false))
        return false;

    return true;
}

static bool
typecheck_stmt_return(struct typecheck *state, struct stmt_return *stmt)
{
    if (!stmt->expr) {
        if (!typecheck_cmp(state, state->ret_type, type_get_void(NULL))) {
            TYCHK_ERROR(state, stmt->stmt.span,
                        "Expected return value\n");
            return false;
        }
    } else {
        if (!typecheck_expr(state, stmt->expr))
            return false;

        if (!typecheck_cmp(state, stmt->expr->type, state->ret_type)) {
            TYCHK_ERROR(state, stmt->stmt.span,
                        "Return value type does not match\n");
            return false;
        }
    }

    return true;
}

static bool
typecheck_stmt_while(struct typecheck *state, struct stmt_while *stmt)
{
    if (!typecheck_expr(state, stmt->cond))
        return false;

    if (!typecheck_cmp(state, stmt->cond->type, type_get_bool(NULL))) {
        TYCHK_ERROR(state, stmt->stmt.span,
                    "While condition is not of type bool\n");
        return false;
    }

    state->loop_depth++;
    if (!typecheck_stmt(state, stmt->body))
        return false;

    state->loop_depth--;
    return true;
}

static bool
typecheck_stmt(struct typecheck *state, struct stmt *stmt)
{
    switch (stmt->tag) {
        case STMT_VAR:
            return typecheck_stmt_var(state, (struct stmt_var *)stmt);

        case STMT_EXPR:
            return typecheck_stmt_expr(state, (struct stmt_expr *)stmt);

        case STMT_BLOCK:
            return typecheck_stmt_block(state, (struct stmt_block *)stmt);

        case STMT_IF:
            return typecheck_stmt_if(state, (struct stmt_if *)stmt);

        case STMT_RETURN:
            return typecheck_stmt_return(state, (struct stmt_return *)stmt);

        case STMT_WHILE:
            return typecheck_stmt_while(state, (struct stmt_while *)stmt);

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

static bool
typecheck_decl_proc(struct typecheck *state, struct decl_proc *decl)
{
    struct type *proc;
    struct type **args;
    struct proc_arg *ptr;
    size_t i;
    bool ok = true;

    if (state->pass == TYCHK_FIRST_PASS) {
        args = malloc(decl->args_count * sizeof(struct type *));

        for (i = 0, ptr = decl->args; ptr != NULL; ptr = ptr->next) {
            args[i++] = ptr->type;
        }

        if (typecheck_lookup(state, decl->sym) != NULL) {
            TYCHK_ERROR(state, decl->decl.span,
                        "Invalid redefinition of symbol '%s'\n", decl->sym->str);
            return false;
        }

        // TODO: Maybe do this in the parser directly?
        if (!decl->out) {
            decl->out = type_get_void(decl->decl.attr);
        }

        proc = type_get_proc(decl->decl.attr, decl->out, args, decl->args_count);
        typecheck_global_push(state, decl->sym, proc);

        return true;
    }

    if (state->pass == TYCHK_SECOND_PASS) {
        if (!typecheck_resolve(state, &decl->out)) {
            ok = false;
        }

        for (ptr = decl->args; ptr != NULL; ptr = ptr->next) {
            if (!typecheck_resolve(state, &ptr->type)) {
                ok = false;
            }
        }

        proc = typecheck_lookup(state, decl->sym);
        if (proc && proc->tag == TYPE_PROC) {
            if (!typecheck_resolve(state, &proc->proc.out)) {
                ok = false;
            }

            for (i = 0; i < proc->proc.args_count; i++) {
                if (!typecheck_resolve(state, &proc->proc.args[i])) {
                    ok = false;
                }
            }
        }
        return ok;
    }

    /*
     * Proc without a body should not be checked...
     */
    if (state->pass == TYCHK_THIRD_PASS && decl->body) {
        assert(state->locals_count == 0 && "leftover locals!");

        for (ptr = decl->args; ptr != NULL; ptr = ptr->next) {
            typecheck_local_push(state, ptr->sym, ptr->type);
        }

        state->loop_depth = 0;
        state->ret_type = decl->out;
        ok = typecheck_stmt(state, decl->body);
        typecheck_local_pop(state, 0);
    }

    return ok;
}

static bool
typecheck_decl_struct(struct typecheck *state, struct decl_struct *decl)
{
    struct type *type;
    size_t i;
    bool ok = true;

    if (state->pass == TYCHK_FIRST_PASS) {
        type = type_get_struct(decl->decl.attr, decl->sym,
                               decl->fields, decl->fields_count);

        typecheck_type_push(state, decl->sym, type);
    } else if (state->pass == TYCHK_SECOND_PASS) {
        for (i = 0; i < decl->fields_count; i++) {
            if (!typecheck_resolve(state, &decl->fields[i].type)) {
                ok = false;
            }
        }

        for (i = 0; i < state->types_count; i++) {
            if (state->types[i].sym == decl->sym) {
                type = state->types[i].type;
                break;
            }
        }

        if (type && type->tag == TYPE_STRUCT) {
            for (size_t i = 0; i < type->strukt.fields_count; i++) {
                if (!typecheck_resolve(state, &type->strukt.fields[i].type)) {
                    ok = false;
                }
            }
        }
    }

    return ok;
}

static bool
typecheck_decl_type(struct typecheck *state, struct decl_type *decl)
{
    size_t i;

    if (state->pass == TYCHK_FIRST_PASS) {
        typecheck_type_push(state, decl->sym, decl->type);
    } else if (state->pass == TYCHK_SECOND_PASS) {
        if (!typecheck_resolve(state, &decl->type))
            return false;

        for (i = 0; i < state->types_count; i++) {
            if (state->types[i].sym == decl->sym) {
                state->types[i].type = decl->type;
                break;
            }
        }
    }

    return true;
}

static bool
typecheck_decl(struct typecheck *state, struct decl *decl)
{
    switch (decl->tag) {
        case DECL_PROC:
            return typecheck_decl_proc(state, (struct decl_proc *)decl);

        case DECL_STRUCT:
            return typecheck_decl_struct(state, (struct decl_struct *)decl);

        case DECL_TYPE:
            return typecheck_decl_type(state, (struct decl_type *)decl);

        default:
            unreachable();
    }
}

bool
typecheck_module(struct typecheck *state, struct decl **decls, size_t decls_count)
{
    size_t i;
    bool ok = true;

    state->pass = TYCHK_FIRST_PASS;

    for (i = 0; i < decls_count; i++) {
        if (!typecheck_decl(state, decls[i])) {
            ok = false;
        }
    }

    if (!ok)
        goto done;

    state->pass = TYCHK_SECOND_PASS;

    for (i = 0; i < decls_count; i++) {
        if (!typecheck_decl(state, decls[i])) {
            ok = false;
        }
    }

    if (!ok)
        goto done;

    state->pass = TYCHK_THIRD_PASS;

    for (i = 0; i < decls_count; i++) {
        if (!typecheck_decl(state, decls[i])) {
            ok = false;
        }
    }

done:
    return ok;
}
