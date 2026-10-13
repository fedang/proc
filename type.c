#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include "type.h"
#include "report.h"

#define MAX_TYPES 4096

struct type type_pool[MAX_TYPES];

size_t type_count;

static struct type *
type_get_fresh(struct attr *attr, enum type_tag tag)
{
    struct type *type;

    assert(type_count < MAX_TYPES && "type pool full");
    type = &type_pool[type_count++];
    type->attr = attr;
    type->tag = tag;
    return type;
}

static struct type *
type_get_xint(struct attr *attr, size_t bit_size, enum type_tag tag)
{
    struct type *type;
    size_t i;

    if (bit_size == 0)
        bit_size = INT_SIZE * 8;

    for (i = 0; i < type_count; i++) {
        type = &type_pool[i];

        if (type->tag == tag && type->attr == attr
                && type->bit_size == bit_size) {
            return type;
        }
    }

    type = type_get_fresh(attr, tag);
    type->bit_size = bit_size;
    return type;
}

static struct type *
type_get_simple(struct attr *attr, enum type_tag tag)
{
    struct type *type;
    size_t i;

    for (i = 0; i < type_count; i++) {
        type = &type_pool[i];

        if (type->tag == tag && type->attr == attr) {
            return type;
        }
    }

    type = type_get_fresh(attr, tag);
    return type;
}

struct type *
type_make_raw(struct expr *raw)
{
    struct type *type;

    type = malloc(sizeof(struct type));
    type->attr = NULL;
    type->tag = TYPE_RAW;
    type->raw = raw;
    return type;
}

struct type *
type_get_bool(struct attr *attr)
{
    return type_get_simple(attr, TYPE_BOOL);
}

struct type *
type_get_void(struct attr *attr)
{
    return type_get_simple(attr, TYPE_VOID);
}

struct type *
type_get_int(struct attr *attr, size_t bit_size)
{
    return type_get_xint(attr, bit_size, TYPE_INT);
}

struct type *
type_get_uint(struct attr *attr, size_t bit_size)
{
    return type_get_xint(attr, bit_size, TYPE_UINT);
}

struct type *
type_get_ptr(struct attr *attr, struct type *pointer)
{
    struct type *type;
    size_t i;

    for (i = 0; i < type_count; i++) {
        type = &type_pool[i];

        if (type->tag == TYPE_PTR && type->attr == attr
                && type->pointer == pointer) {
            return type;
        }
    }

    type = type_get_fresh(attr, TYPE_PTR);
    type->pointer = pointer;
    return type;
}

struct type *
type_get_array(struct attr *attr, struct type *item, size_t count)
{
    struct type *type;
    size_t i;

    for (i = 0; i < type_count; i++) {
        type = &type_pool[i];

        if (type->tag == TYPE_ARRAY && type->attr == attr
                && type->array.item == item && type->array.count == count) {
            return type;
        }
    }

    type = type_get_fresh(attr, TYPE_ARRAY);
    type->array.item = item;
    type->array.count = count;
    return type;
}

struct type *
type_get_struct(struct attr *attr, struct symbol *name,
                struct struct_field *fields, size_t fields_count)
{
    struct type *type;
    size_t i, j;
    bool found;

    for (i = 0; i < type_count; i++) {
        type = &type_pool[i];

        if (type->tag != TYPE_STRUCT || type->attr != attr)
            continue;

        if (type->strukt.name == name
                && type->strukt.fields_count == fields_count) {
            found = true;

            for (j = 0; j < fields_count; j++) {
                if (fields[j].name != type->strukt.fields[j].name
                        || fields[j].type != type->strukt.fields[j].type) {
                    found = false;
                    break;
                }
            }

            if (found)
                return type;
        }
    }

    type = type_get_fresh(attr, TYPE_STRUCT);
    type->strukt.name = name;
    type->strukt.fields = fields;
    type->strukt.fields_count = fields_count;
    return type;
}

struct type *
type_get_proc(struct attr *attr, struct type *out,
              struct type **args, size_t args_count)
{
    struct type *type;
    size_t i, j;
    bool found;

    for (i = 0; i < type_count; i++) {
        type = &type_pool[i];

        if (type->tag != TYPE_PROC || type->attr != attr)
            continue;

        if (type->proc.out == out && type->proc.args_count == args_count) {
            found = true;

            for (j = 0; j < args_count; j++) {
                if (args[j] != type->proc.args[j]) {
                    found = false;
                    break;
                }
            }

            if (found)
                return type;
        }
    }

    type = type_get_fresh(attr, TYPE_PROC);
    type->proc.out = out;
    type->proc.args_count = args_count;

    if (args_count > 0) {
        type->proc.args = malloc(args_count * sizeof(struct type *));
        memcpy(type->proc.args, args, args_count * sizeof(struct type *));
    } else {
        type->proc.args = NULL;
    }
    return type;
}

#define ROUND_UP(x, y) \
    ((x) + (y) - 1) & ~((y) - 1)

size_t
type_sizeof(const struct type *type)
{
    size_t i, size, align;

    switch (type->tag) {
        case TYPE_VOID:
            return 0;

        case TYPE_BOOL:
            return 1;

        case TYPE_INT:
        case TYPE_UINT:
            return type_integer_bits(type) / 8;

        case TYPE_PTR:
            return PTR_SIZE;

        case TYPE_ARRAY:
            return type->array.count * type_sizeof(type->array.item);

        case TYPE_STRUCT:
            for (i = 0, size = 0; i < type->strukt.fields_count; i++) {
                align = type_alignof(type->strukt.fields[i].type);
                size = ROUND_UP(size, align);
                size += type_sizeof(type->strukt.fields[i].type);
            }

            align = type_alignof(type);
            return ROUND_UP(size, align);

        default:
            unreachable();
    }
}

size_t
type_alignof(const struct type *type)
{
    size_t i, align, max;

    switch (type->tag) {
        case TYPE_VOID:
        case TYPE_BOOL:
            return 1;

        case TYPE_INT:
        case TYPE_UINT:
            return type_integer_bits(type) / 8;

        case TYPE_PTR:
            return PTR_SIZE;

        case TYPE_ARRAY:
            return type_alignof(type->array.item);

        case TYPE_STRUCT:
            for (i = 0, max = 1; i < type->strukt.fields_count; i++) {
                align = type_alignof(type->strukt.fields[i].type);
                if (align > max)
                    max = align;
            }
            return max;

        default:
            unreachable();
    }
}
