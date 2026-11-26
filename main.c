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

int
main(int argc, const char **argv)
{
    struct modcache cache;
    struct checker check;
    struct irgen irgen;
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

    ir_out = fopen(opts.ir_file, "wb");
    if (!ir_out) {
        report_info(NULL, NULL, "Failed to open IR output file\n");
        return 1;
    }

    /*
     * Generate the LLVM IR
     */
    irgen_init(&irgen, ir_out);
    if (!irgen_modcache(&irgen, &cache)) {
        report_info(NULL, NULL, "Failed to generate IR\n");
        fclose(ir_out);
        return 1;
    }

    fclose(ir_out);
    if (opts.emit_ir)
        return 0;

    if (!options_run_clang()) {
        report_info(NULL, NULL, "Failed to compile IR\n");
        return 1;
    }

    return 0;
}
