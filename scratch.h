#ifndef _SCRATCH_H
#define _SCRATCH_H

#include <stddef.h>

struct scratch_frame {
    const char *func;
    size_t top;
    size_t start;
    struct scratch_frame *prev;
};

void scratch_init(void);

void scratch_begin_at(struct scratch_frame *frame, const char *func);

#define scratch_begin(frame) \
    scratch_begin_at((frame), __func__)

void *scratch_alloc(size_t size);

void *scratch_append(size_t size);

void *scratch_data(const struct scratch_frame *frame);

void scratch_end(struct scratch_frame *frame);

#endif
