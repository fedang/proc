#define _DEFAULT_SOURCE
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "arena.h"
#include "scratch.h"
#include "modcache.h"
#include "checker.h"
#include "irgen.h"
#include "report.h"
#include "options.h"

static bool
open_ir_file(const char **path, FILE **out)
{
    // XXX: not reentrant
    static char tmp[] = "/tmp/comp-XXXXXX.ll";
    int fd;

    if (opts.emit_ir) {
        *path = opts.output;
        *out = fopen(*path, "wb");
    } else {
        *path = tmp;
        fd = mkstemps(tmp, 3);
        *out = fd >= 0 ? fdopen(fd, "wb") : NULL;
    }
    return *out != NULL;
}

int
main(int argc, const char **argv)
{
    struct modcache cache;
    struct checker check;
    struct irgen irgen;
    const char *ir_file;
    FILE *ir_out;
    size_t i;
    bool ok = true;

    if (!options_init(argc, argv))
        return 1;

    arena_init();
    scratch_init();
    symbol_init();

    modcache_init(&cache, ".");

    /*
     * Load all the root modules asked by the user
     */
    for (i = 0; i < opts.inputs_count; i++) {
        if (!modcache_load(&cache, opts.inputs[i], NULL, NULL)) {
            ok = false;
        }
    }

    if (!ok)
        return 1;

    /*
     * Verify and typecheck the modules
     */
    checker_init(&check, &cache);
    if (!checker_modcache(&check)) {
        report_info(NULL, NULL, "Failed semantic analysis\n");
        return 1;
    }

    if (!opts.output) {
        options_set_output(modcache_root_name(&cache));
    }

    if (!open_ir_file(&ir_file, &ir_out)) {
        report_info(NULL, NULL, "Failed to open IR output file\n");
        return 1;
    }

    /*
     * Generate the LLVM IR
     */
    irgen_init(&irgen, ir_out);
    ok = irgen_modcache(&irgen, &cache);
    fclose(ir_out);
    if (!ok) {
        report_info(NULL, NULL, "Failed to generate IR\n");
        if (!opts.emit_ir)
            unlink(ir_file);
        return 1;
    }

    if (opts.emit_ir)
        return 0;

    ok = options_run_clang(ir_file);
    unlink(ir_file);
    if (!ok) {
        report_info(NULL, NULL, "Failed to compile IR\n");
        return 1;
    }

    return 0;
}
