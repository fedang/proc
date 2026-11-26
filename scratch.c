#include <assert.h>

#include "arena.h"
#include "scratch.h"

#define SCRATCH_SIZE  (512UL * 1024 * 1024)
#define SCRATCH_ALIGN (sizeof(void *))

static struct arena scratch_arena;
static struct scratch_frame *scratch_top;

void
scratch_init(void)
{
    arena_setup(&scratch_arena, SCRATCH_SIZE);
    scratch_top = NULL;
}

void
scratch_begin_at(struct scratch_frame *frame, const char *func)
{
    size_t aligned;

    assert(scratch_arena.base && "scratch arena not initialized");
    aligned = ROUND_UP(scratch_arena.top, SCRATCH_ALIGN);
    assert(aligned <= scratch_arena.size && "scratch arena exhausted");

    frame->prev = scratch_top;
    frame->func = func;
    frame->top = scratch_arena.top;
    frame->start = aligned;

    scratch_arena.top = aligned;
    scratch_top = frame;
}

void *
scratch_alloc(size_t size)
{
    assert(scratch_top && "active scratch_frame required");
    return arena_alloc(&scratch_arena, size);
}

void *
scratch_append(size_t size)
{
    assert(scratch_top && "active scratch_frame required");
    return arena_append(&scratch_arena, size);
}

void *
scratch_data(const struct scratch_frame *frame)
{
    return scratch_arena.base + frame->start;
}

void
scratch_end(struct scratch_frame *frame)
{
    assert(scratch_top == frame && "unbalanced scratch_frame pop");
    scratch_top = frame->prev;
    scratch_arena.top = frame->top;
}
