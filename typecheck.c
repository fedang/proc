#include <stdlib.h>
#include <stdio.h>

#include <assert.h>
#define unreachable() assert(!"unreachable")

#include "typecheck.h"

#define TYCHK_FIRST_PASS    0
#define TYCHK_SECOND_PASS   1
#define TYCHK_THIRD_PASS    2

static void
typecheck_type_push(struct typecheck *tychk, struct symbol *sym,
                    struct type *type)
{
    assert(tychk->types_count < 256 && "too many types!");

    tychk->types[tychk->types_count].sym = sym;
    tychk->types[tychk->types_count].type = type;
    tychk->types_count++;
}

void
typecheck_init(struct typecheck *tychk)
{
    tychk->pass = TYCHK_FIRST_PASS;
    tychk->ret_type = NULL;
    tychk->locals_count = 0;
    tychk->globals_count = 0;
    tychk->types_count = 0;

    typecheck_type_push(tychk, symbol_make("i8"), type_get_int(NULL, 8));
    typecheck_type_push(tychk, symbol_make("i16"), type_get_int(NULL, 16));
    typecheck_type_push(tychk, symbol_make("i32"), type_get_int(NULL, 32));
    typecheck_type_push(tychk, symbol_make("i64"), type_get_int(NULL, 64));
    typecheck_type_push(tychk, symbol_make("int"), type_get_int(NULL, 0));
    typecheck_type_push(tychk, symbol_make("u8"), type_get_uint(NULL, 8));
    typecheck_type_push(tychk, symbol_make("u16"), type_get_uint(NULL, 16));
    typecheck_type_push(tychk, symbol_make("u32"), type_get_uint(NULL, 32));
    typecheck_type_push(tychk, symbol_make("u64"), type_get_uint(NULL, 64));
    typecheck_type_push(tychk, symbol_make("uint"), type_get_uint(NULL, 0));
    typecheck_type_push(tychk, symbol_make("bool"), type_get_bool(NULL));
    typecheck_type_push(tychk, symbol_make("void"), type_get_void(NULL));
}

static void
typecheck_local_push(struct typecheck *tychk, struct symbol *sym,
                     struct type *type)
{
    assert(tychk->locals_count < 256 && "too many locals!");

    tychk->locals[tychk->locals_count].sym = sym;
    tychk->locals[tychk->locals_count].type = type;
    tychk->locals_count++;
}

static void
typecheck_local_pop(struct typecheck *tychk, unsigned n)
{
    assert(n <= tychk->locals_count && "locals overflow");

    // TODO: Cleanup memory?
    tychk->locals_count = n;
}

static void
typecheck_global_push(struct typecheck *tychk, struct symbol *sym,
                      struct type *type)
{
    assert(tychk->globals_count < 256 && "too many globals!");

    tychk->globals[tychk->globals_count].sym = sym;
    tychk->globals[tychk->globals_count].type = type;
    tychk->globals_count++;
}

static struct type *
typecheck_lookup(struct typecheck *tychk, struct symbol *sym)
{
    size_t i;

    /*
     * First search the locals, starting from the latest,
     * then continue with the globals (in any order)
     */
    for (i = tychk->locals_count; i > 0; i--) {
        if (tychk->locals[i - 1].sym == sym)
            return tychk->locals[i - 1].type;
    }

    for (i = 0; i < tychk->globals_count; i++) {
        if (tychk->globals[i].sym == sym)
            return tychk->globals[i].type;
    }

    return NULL;
}

static bool
typecheck_resolve(struct typecheck *tychk, struct type **type)
{
    size_t i;
    struct type *t = *type;
    bool ok = true;

    if (!type)
        return true;

    if (t->tag == TYPE_NAMED) {
        for (i = 0; i < tychk->types_count; i++) {
            if (tychk->types[i].sym == t->named) {
                *type = tychk->types[i].type;
                return typecheck_resolve(tychk, type);
            }
        }

        printf("Unknown type '%s'\n", t->named->str);
        return false;
    }

    if (t->tag == TYPE_PTR) {
        return typecheck_resolve(tychk, &t->pointer);
    }

    if (t->tag == TYPE_ARRAY) {
        return typecheck_resolve(tychk, &t->array.item);
    }

    if (t->tag == TYPE_PROC) {
        if (!typecheck_resolve(tychk, &t->proc.out)) {
            ok = false;
        }

        for (i = 0; i < t->proc.args_count; i++) {
            if (!typecheck_resolve(tychk, &t->proc.args[i])) {
                ok = false;
            }
        }
        return ok;
    }

    return true;
}

static bool
typecheck_cmp(struct typecheck *tychk, struct type *t1, struct type *t2)
{
    return t1 == t2;
}

static bool typecheck_expr(struct typecheck *tychk, struct expr *expr);

static bool
typecheck_expr_literal(struct typecheck *tychk, struct expr_literal *expr)
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
typecheck_expr_ident(struct typecheck *tychk, struct expr_ident *expr)
{
    struct type *type;

    type = typecheck_lookup(tychk, expr->sym);
    if (!type) {
        printf("Undefined variable '%s'\n", expr->sym->str);
        return false;
    }

    expr->expr.type = type;
    return true;
}

static bool
typecheck_expr_binop(struct typecheck *tychk, struct expr_binop *expr)
{
    if (!typecheck_expr(tychk, expr->op_lhs) || !typecheck_expr(tychk, expr->op_rhs))
        return false;

    if (!typecheck_cmp(tychk, expr->op_lhs->type, expr->op_rhs->type)) {
        printf("Binop operands have different types\n");
        return false;
    }

    if (expr->binop >= BINOP_SET && expr->binop <= BINOP_MOD_SET) {
        if (!expr_is_lvalue(expr->op_lhs)) {
            printf("Binop left operand is not an lvalue\n");
            return false;
        }
    }

    switch (expr->binop) {
        case BINOP_EQ:
        case BINOP_NOTEQ:
        case BINOP_GT:
        case BINOP_GTEQ:
        case BINOP_LT:
        case BINOP_LTEQ:
        case BINOP_BOOL_OR:
        case BINOP_BOOL_AND:
            expr->expr.type = type_get_bool(NULL);
            break;

        default:
            expr->expr.type = expr->op_lhs->type;
            break;
    }

    return true;
}

static bool
typecheck_expr_unop(struct typecheck *tychk, struct expr_unop *expr)
{
    if (expr->unop == UNOP_ADDROF) {
        if (!expr_is_lvalue(expr->op)) {
            printf("Unop operand is not an lvalue\n");
            return false;
        }
    }

    if (!typecheck_expr(tychk, expr->op))
        return false;

    switch (expr->unop) {
        case UNOP_DEREF:
            if (expr->op->type->tag != TYPE_PTR) {
                printf("Only pointer types can be dereferenced\n");
                return false;
            }

            expr->expr.type = expr->op->type->pointer;
            break;

        case UNOP_ADDROF:
            expr->expr.type = type_get_ptr(NULL, expr->op->type);
            break;

        case UNOP_NOT:
            if (expr->op->type->tag != TYPE_BOOL) {
                printf("Only bool values can be negated\n");
                return false;
            }

            expr->expr.type = type_get_bool(NULL);
            break;

        case UNOP_NEG:
            if (expr->op->type->tag != TYPE_INT && expr->op->type->tag != TYPE_UINT) {
                printf("Only integers can be negated\n");
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
typecheck_expr_cast(struct typecheck *tychk, struct expr_cast *expr)
{
    bool ok;

    if (!typecheck_resolve(tychk, &expr->cast))
        return false;

    ok = typecheck_expr(tychk, expr->op);
    expr->expr.type = expr->cast;
    return ok;
}

static bool
typecheck_expr_call(struct typecheck *tychk, struct expr_call *expr)
{
    struct type *proc;
    size_t i;

    if (!typecheck_expr(tychk, expr->op))
        return false;

    proc = expr->op->type;
    if (proc->tag != TYPE_PROC || proc->proc.args_count != expr->args_count) {
        printf("Call target is not a proc\n");
        return false;
    }

    for (i = 0; i < expr->args_count; i++) {
        if (!typecheck_expr(tychk, expr->args[i]))
            return false;

        if (!typecheck_cmp(tychk, proc->proc.args[i], expr->args[i]->type)) {
            printf("Call argument does not match proc\n");
            return false;
        }
    }

    expr->expr.type = proc->proc.out;
    return true;
}

static bool
typecheck_expr_index(struct typecheck *tychk, struct expr_index *expr)
{
    if (!typecheck_expr(tychk, expr->op) || !typecheck_expr(tychk, expr->index))
        return false;

    if (expr->index->type->tag != TYPE_INT && expr->index->type->tag != TYPE_UINT) {
        printf("Invalid index type\n");
        return false;
    }

    if (expr->op->type->tag == TYPE_ARRAY) {
        expr->expr.type = expr->op->type->array.item;
        return true;
    } else if (expr->op->type->tag == TYPE_PTR) {
        expr->expr.type = expr->op->type->pointer;
        return true;
    }

    printf("Invalid type used for indexing\n");
    return false;
}

static bool
typecheck_expr_access(struct typecheck *tychk, struct expr_access *expr)
{
    struct type *type;
    size_t i;

    if (!typecheck_expr(tychk, expr->op))
        return false;

    type = expr->op->type;

    /*
     * Automatically dereference struct pointers
     */
    while (type->tag == TYPE_PTR) {
        type = type->pointer;
    }

    if (type->tag != TYPE_STRUCT) {
        printf("Tried to access a field from a non-struct type\n");
        return false;
    }

    for (i = 0; i < type->strukt.fields_count; i++) {
        if (type->strukt.fields[i].name == expr->field) {
            expr->expr.type = type->strukt.fields[i].type;
            expr->offset = i;
            return true;
        }
    }

    printf("Found no field named '%s'\n", expr->field->str);
    return false;
}

static bool
typecheck_expr(struct typecheck *tychk, struct expr *expr)
{
    switch (expr->tag) {
        case EXPR_LITERAL:
            return typecheck_expr_literal(tychk, (struct expr_literal *)expr);

        case EXPR_IDENT:
            return typecheck_expr_ident(tychk, (struct expr_ident *)expr);

        case EXPR_BINOP:
            return typecheck_expr_binop(tychk, (struct expr_binop *)expr);

        case EXPR_UNOP:
            return typecheck_expr_unop(tychk, (struct expr_unop *)expr);

        case EXPR_CAST:
            return typecheck_expr_cast(tychk, (struct expr_cast *)expr);

        case EXPR_CALL:
            return typecheck_expr_call(tychk, (struct expr_call *)expr);

        case EXPR_INDEX:
            return typecheck_expr_index(tychk, (struct expr_index *)expr);

        case EXPR_ACCESS:
            return typecheck_expr_access(tychk, (struct expr_access *)expr);

        default:
            unreachable();
    }
}

static bool typecheck_stmt(struct typecheck *tychk, struct stmt *stmt);

static bool
typecheck_stmt_var(struct typecheck *tychk, struct stmt_var *stmt)
{
    /*
     * The user could have provided an explicit type
     */
    if (stmt->type) {
        if (!typecheck_resolve(tychk, &stmt->type))
            return false;
    }

    if (stmt->value) {
        if (!typecheck_expr(tychk, stmt->value))
            return false;

        if (!stmt->type) {
            stmt->type = stmt->value->type;
        } else if (!typecheck_cmp(tychk, stmt->type, stmt->value->type)) {
            printf("Type mismatch in variable assignment\n");
            return false;
        }
    }

    if (!stmt->type) {
        printf("Cannot infer type for variable '%s'\n", stmt->sym->str);
        return false;
    }

    typecheck_local_push(tychk, stmt->sym, stmt->type);
    return true;
}

static bool
typecheck_stmt_expr(struct typecheck *tychk, struct stmt_expr *stmt)
{
    return typecheck_expr(tychk, stmt->expr);
}

static bool
typecheck_stmt_block(struct typecheck *tychk, struct stmt_block *stmt)
{
    size_t i;
    unsigned locals;
    bool ok = true;

    locals = tychk->locals_count;

    for (i = 0; i < stmt->items_count; i++) {
        if (!typecheck_stmt(tychk, stmt->items[i])) {
            ok = false;
        }
    }

    typecheck_local_pop(tychk, locals);

    return ok;
}

static bool
typecheck_stmt_if(struct typecheck *tychk, struct stmt_if *stmt)
{
    if (!typecheck_expr(tychk, stmt->cond))
        return false;

    if (!typecheck_cmp(tychk, stmt->cond->type, type_get_bool(NULL))) {
        printf("If condition is not of type bool\n");
        return false;
    }

    if (!typecheck_stmt(tychk, stmt->b_true))
        return false;

    if (stmt->b_false && !typecheck_stmt(tychk, stmt->b_false))
        return false;

    return true;
}

static bool
typecheck_stmt_return(struct typecheck *tychk, struct stmt_return *stmt)
{
    if (!stmt->expr) {
        if (!typecheck_cmp(tychk, tychk->ret_type, type_get_void(NULL))) {
            printf("Expected return value\n");
            return false;
        }
    } else {
        if (!typecheck_expr(tychk, stmt->expr))
            return false;

        if (!typecheck_cmp(tychk, stmt->expr->type, tychk->ret_type)) {
            printf("Return value type does not match\n");
            return false;
        }
    }

    return true;
}

static bool
typecheck_stmt(struct typecheck *tychk, struct stmt *stmt)
{
    switch (stmt->tag) {
        case STMT_VAR:
            return typecheck_stmt_var(tychk, (struct stmt_var *)stmt);

        case STMT_EXPR:
            return typecheck_stmt_expr(tychk, (struct stmt_expr *)stmt);

        case STMT_BLOCK:
            return typecheck_stmt_block(tychk, (struct stmt_block *)stmt);

        case STMT_IF:
            return typecheck_stmt_if(tychk, (struct stmt_if *)stmt);

        case STMT_RETURN:
            return typecheck_stmt_return(tychk, (struct stmt_return *)stmt);

        default:
            unreachable();
    }
}

static bool
typecheck_decl_proc(struct typecheck *tychk, struct decl_proc *decl)
{
    struct type *proc;
    struct type **args;
    struct proc_arg *ptr;
    size_t i;
    bool ok = true;

    if (tychk->pass == TYCHK_FIRST_PASS) {
        args = malloc(decl->args_count * sizeof(struct type *));

        for (i = 0, ptr = decl->args; ptr != NULL; ptr = ptr->next) {
            args[i++] = ptr->type;
        }

        if (typecheck_lookup(tychk, decl->sym) != NULL) {
            printf("Invalid redefinition of symbol '%s'\n", decl->sym->str);
            return false;
        }

        // TODO: Maybe do this in the parser directly?
        if (!decl->out) {
            decl->out = type_get_void(decl->decl.attr);
        }

        proc = type_get_proc(decl->decl.attr, decl->out, args, decl->args_count);
        typecheck_global_push(tychk, decl->sym, proc);

        return true;
    }

    if (tychk->pass == TYCHK_SECOND_PASS) {
        if (!typecheck_resolve(tychk, &decl->out)) {
            ok = false;
        }

        for (ptr = decl->args; ptr != NULL; ptr = ptr->next) {
            if (!typecheck_resolve(tychk, &ptr->type)) {
                ok = false;
            }
        }

        proc = typecheck_lookup(tychk, decl->sym);
        if (proc && proc->tag == TYPE_PROC) {
            if (!typecheck_resolve(tychk, &proc->proc.out)) {
                ok = false;
            }

            for (i = 0; i < proc->proc.args_count; i++) {
                if (!typecheck_resolve(tychk, &proc->proc.args[i])) {
                    ok = false;
                }
            }
        }
        return ok;
    }

    /*
     * Proc without a body should not be checked...
     */
    if (tychk->pass == TYCHK_THIRD_PASS && decl->body) {
        assert(tychk->locals_count == 0 && "leftover locals!");

        for (ptr = decl->args; ptr != NULL; ptr = ptr->next) {
            typecheck_local_push(tychk, ptr->sym, ptr->type);
        }

        tychk->ret_type = decl->out;
        ok = typecheck_stmt(tychk, decl->body);
        typecheck_local_pop(tychk, 0);
    }

    return ok;
}

static bool
typecheck_decl_struct(struct typecheck *tychk, struct decl_struct *decl)
{
    struct type *type;
    size_t i;
    bool ok = true;

    if (tychk->pass == TYCHK_FIRST_PASS) {
        type = type_get_struct(decl->decl.attr, decl->sym,
                               decl->fields, decl->fields_count);

        typecheck_type_push(tychk, decl->sym, type);
    } else if (tychk->pass == TYCHK_SECOND_PASS) {
        for (i = 0; i < decl->fields_count; i++) {
            if (!typecheck_resolve(tychk, &decl->fields[i].type)) {
                ok = false;
            }
        }

        for (i = 0; i < tychk->types_count; i++) {
            if (tychk->types[i].sym == decl->sym) {
                type = tychk->types[i].type;
                break;
            }
        }

        if (type && type->tag == TYPE_STRUCT) {
            for (size_t i = 0; i < type->strukt.fields_count; i++) {
                if (!typecheck_resolve(tychk, &type->strukt.fields[i].type)) {
                    ok = false;
                }
            }
        }
    }

    return ok;
}

static bool
typecheck_decl_type(struct typecheck *tychk, struct decl_type *decl)
{
    size_t i;

    if (tychk->pass == TYCHK_FIRST_PASS) {
        typecheck_type_push(tychk, decl->sym, decl->type);
    } else if (tychk->pass == TYCHK_SECOND_PASS) {
        if (!typecheck_resolve(tychk, &decl->type))
            return false;

        for (i = 0; i < tychk->types_count; i++) {
            if (tychk->types[i].sym == decl->sym) {
                tychk->types[i].type = decl->type;
                break;
            }
        }
    }

    return true;
}

static bool
typecheck_decl(struct typecheck *tychk, struct decl *decl)
{
    switch (decl->tag) {
        case DECL_PROC:
            return typecheck_decl_proc(tychk, (struct decl_proc *)decl);

        case DECL_STRUCT:
            return typecheck_decl_struct(tychk, (struct decl_struct *)decl);

        case DECL_TYPE:
            return typecheck_decl_type(tychk, (struct decl_type *)decl);

        default:
            unreachable();
    }
}

bool
typecheck_module(struct typecheck *tychk, struct decl **decls, size_t decls_count)
{
    size_t i;
    bool ok = true;

    tychk->pass = TYCHK_FIRST_PASS;

    for (i = 0; i < decls_count; i++) {
        if (!typecheck_decl(tychk, decls[i])) {
            ok = false;
        }
    }

    if (!ok)
        goto done;

    tychk->pass = TYCHK_SECOND_PASS;

    for (i = 0; i < decls_count; i++) {
        if (!typecheck_decl(tychk, decls[i])) {
            ok = false;
        }
    }

    if (!ok)
        goto done;

    tychk->pass = TYCHK_THIRD_PASS;

    for (i = 0; i < decls_count; i++) {
        if (!typecheck_decl(tychk, decls[i])) {
            ok = false;
        }
    }

done:
    return ok;
}
