#ifndef _REPORT_H
#define _REPORT_H

#include "source.h"
#include "span.h"

void report_error(struct source *src, struct span span, const char *fmt, ...);

void report_warning(struct source *src, struct span span, const char *fmt, ...);

#endif
