#ifndef _TYPE_H
#define _TYPE_H

#include <stddef.h>
#include <stdint.h>

enum type_tag {
    TYPE_INVALID,
    TYPE_INT,
    TYPE_UINT,
    TYPE_BOOL,
    TYPE_VOID,
    TYPE_PTR,
};

struct type {
    struct attr *attr;
    enum type_tag tag;
    union {
        size_t bit_size;
        struct type *pointer;
    };
};

struct type *type_get_int(struct attr *attr, size_t bit_size);

struct type *type_get_uint(struct attr *attr, size_t bit_size);

struct type *type_get_bool(struct attr *attr);

struct type *type_get_void(struct attr *attr);

struct type *type_get_ptr(struct attr *attr, struct type *pointer);

#endif
