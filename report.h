#ifndef _REPORT_H
#define _REPORT_H

#include "source.h"
#include "span.h"

void report_error(const struct source *src, const struct span *span,
                  const char *fmt, ...);

void report_warning(const struct source *src, const struct span *span,
                    const char *fmt, ...);

void report_info(const struct source *src, const struct span *span,
                 const char *fmt, ...);

#endif
