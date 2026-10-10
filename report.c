#include <stdio.h>
#include <stdarg.h>

#include "report.h"

#define REPORT_ERROR    0
#define REPORT_WARNING  1

static void
report_message(struct source *src, struct span span, int level,
               const char *fmt, va_list args)
{
    if (level == REPORT_ERROR) {
        fprintf(stderr, "error: ");
    } else if (level == REPORT_WARNING) {
        fprintf(stderr, "warning: ");
    }

    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
}

void
report_error(struct source *src, struct span span, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    report_message(src, span, REPORT_ERROR, fmt, args);
    va_end(args);
}

void
report_warning(struct source *src, struct span span, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    report_message(src, span, REPORT_WARNING, fmt, args);
    va_end(args);
}
