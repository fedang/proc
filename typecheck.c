#include <stdlib.h>
#include <stdio.h>

#include <assert.h>
#define unreachable() assert(!"unreachable")

#include "typecheck.h"

#define TYCHK_FIRST_PASS 0
#define TYCHK_SECOND_PASS 1

void
typecheck_init(struct typecheck *tychk)
{
    tychk->pass = TYCHK_FIRST_PASS;
    tychk->locals_count = 0;
    tychk->globals_count = 0;
    tychk->ret_type = NULL;
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
typecheck_cmp(struct typecheck *tychk, struct type *t1, struct type *t2)
{
    return t1 == t2;
}

static bool typecheck_expr(struct typecheck *tychk, struct expr *expr);

static bool
typecheck_expr_const(struct typecheck *tychk, struct expr_const *expr)
{
    expr->expr.type = type_get_int(NULL, 0);
    return true;
}

static bool
typecheck_expr_ident(struct typecheck *tychk, struct expr_ident *expr)
{
    struct type *type;

    type = typecheck_lookup(tychk, expr->sym);
    if (!type)
        return false;

    expr->expr.type = type;
    return true;
}

static bool
typecheck_expr_binop(struct typecheck *tychk, struct expr_binop *expr)
{
    if (!typecheck_expr(tychk, expr->op_lhs) || !typecheck_expr(tychk, expr->op_rhs))
        return false;

    if (!typecheck_cmp(tychk, expr->op_lhs->type, expr->op_rhs->type))
        return false;

    if (expr->binop >= BINOP_SET && expr->binop <= BINOP_MOD_SET) {
        if (!expr_is_lvalue(expr->op_lhs))
            return false;
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
    int ret = typecheck_expr(tychk, expr->op);
    expr->expr.type = expr->op->type;
    return ret;
}

static bool
typecheck_expr_cast(struct typecheck *tychk, struct expr_cast *expr)
{
    bool ok = typecheck_expr(tychk, expr->op);
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
    if (proc->tag != TYPE_PROC || proc->proc.args_count != expr->args_count)
        return false;

    for (i = 0; i < expr->args_count; i++) {
        if (!typecheck_expr(tychk, expr->args[i]))
            return false;

        if (!typecheck_cmp(tychk, proc->proc.args[i], expr->args[i]->type))
            return false;
    }

    expr->expr.type = proc->proc.out;
    return true;
}

static bool
typecheck_expr_index(struct typecheck *tychk, struct expr_index *expr)
{
    return false;
}

static bool
typecheck_expr_access(struct typecheck *tychk, struct expr_access *expr)
{
    return false;
}

static bool
typecheck_expr(struct typecheck *tychk, struct expr *expr)
{
    switch (expr->tag) {
        case EXPR_CONST:
            return typecheck_expr_const(tychk, (struct expr_const *)expr);

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
    bool ok;

    ok = typecheck_expr(tychk, stmt->value);

    typecheck_local_push(tychk, stmt->sym, stmt->type);

    if (!ok)
        return ok;

    return typecheck_cmp(tychk, stmt->type, stmt->value->type);
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

    if (!typecheck_cmp(tychk, stmt->cond->type, type_get_bool(NULL)))
        return false;

    if (!typecheck_stmt(tychk, stmt->b_true))
        return false;

    if (stmt->b_false && !typecheck_stmt(tychk, stmt->b_false))
        return false;

    return true;
}

static bool
typecheck_stmt_return(struct typecheck *tychk, struct stmt_return *stmt)
{
    if (!stmt->expr)
        return typecheck_cmp(tychk, tychk->ret_type, type_get_void(NULL));

    if (!typecheck_expr(tychk, stmt->expr))
        return false;

    return typecheck_cmp(tychk, stmt->expr->type, tychk->ret_type);
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

    /*
     * Proc without a body should not be checked in the second pass
     */
    if (decl->body) {
        for (ptr = decl->args; ptr != NULL; ptr = ptr->next) {
            typecheck_local_push(tychk, ptr->sym, ptr->type);
        }

        tychk->ret_type = decl->out;

        ok = typecheck_stmt(tychk, decl->body);

        typecheck_local_pop(tychk, 0);

        assert(tychk->locals_count == 0 && "leftover locals!");
    }

    return ok;
}

static bool
typecheck_decl(struct typecheck *tychk, struct decl *decl)
{
    switch (decl->tag) {
        case DECL_PROC:
            return typecheck_decl_proc(tychk, (struct decl_proc *)decl);

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

done:
    return ok;
}
