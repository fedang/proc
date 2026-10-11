#ifndef _TYPE_H
#define _TYPE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

enum type_tag {
    TYPE_INVALID,
    TYPE_RAW,
    TYPE_BOOL,
    TYPE_VOID,
    TYPE_INT,
    TYPE_UINT,
    TYPE_PTR,
    TYPE_ARRAY,
    TYPE_STRUCT,
    TYPE_PROC,
};

struct struct_field {
    struct symbol *name;
    struct type *type;
    unsigned offset;
};

struct type {
    struct attr *attr;
    enum type_tag tag;
    union {
        struct expr *raw;
        size_t bit_size;
        struct type *pointer;
        struct {
            struct type *item;
            size_t count;
        } array;
        struct {
            struct symbol *name;
            struct struct_field *fields;
            size_t fields_count;
        } strukt;
        struct {
            struct type *out;
            struct type **args;
            size_t args_count;
        } proc;
    };
};

static inline bool
type_is_integer(const struct type *type)
{
    return type->tag == TYPE_INT
        || type->tag == TYPE_UINT;
}

static inline bool
type_is_scalar(const struct type *type)
{
    return type_is_integer(type)
        || type->tag == TYPE_BOOL
        || type->tag == TYPE_PTR;
}

static inline size_t
type_integer_bits(const struct type *type)
{
    if (type->tag == TYPE_BOOL)
        return 1;
    if (type->tag == TYPE_PTR)
        return sizeof(void *) * 8;

    assert(type_is_integer(type));
    return type->bit_size
        ? type->bit_size
        : sizeof(int) * 8;
}

struct type *type_make_raw(struct expr *raw);

struct type *type_get_bool(struct attr *attr);

struct type *type_get_void(struct attr *attr);

struct type *type_get_int(struct attr *attr, size_t bit_size);

struct type *type_get_uint(struct attr *attr, size_t bit_size);

struct type *type_get_ptr(struct attr *attr, struct type *pointer);

struct type *type_get_array(struct attr *attr, struct type *item, size_t count);

struct type *type_get_struct(struct attr *attr, struct symbol *name,
                             struct struct_field *fields, size_t fields_count);

struct type *type_get_proc(struct attr *attr, struct type *out,
                           struct type **args, size_t args_count);

size_t type_sizeof(struct type *type);

size_t type_alignof(struct type *type);

#endif
