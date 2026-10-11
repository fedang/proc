#ifndef _REPORT_H
#define _REPORT_H

#include <assert.h>

#include "source.h"
#include "span.h"

#define unreachable() \
    assert(!"unreachable")

void report_error(const struct source *src, const struct span *span,
                  const char *fmt, ...);

void report_warning(const struct source *src, const struct span *span,
                    const char *fmt, ...);

void report_info(const struct source *src, const struct span *span,
                 const char *fmt, ...);

// TODO: report_blunder for ICEs

#endif
