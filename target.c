#include <stdio.h>
#include <string.h>

#include "target.h"

static const struct target targets[] = {
    {
        .name         = "x86_64-linux",
        .triple       = "x86_64-pc-linux-gnu",
        .fn_attrs     = NULL,
        .word_size    = 8,
        .freestanding = false,
    },
    {
        .name         = "i686-linux",
        .triple       = "i686-pc-linux-gnu",
        .fn_attrs     = NULL,
        .word_size    = 4,
        .freestanding = false,
    },
    {
        .name         = "aarch64-linux",
        .triple       = "aarch64-unknown-linux-gnu",
        .fn_attrs     = NULL,
        .word_size    = 8,
        .freestanding = false,
    },
    {
        .name         = "riscv64-linux",
        .triple       = "riscv64-unknown-linux-gnu",
        .fn_attrs     = NULL,
        .word_size    = 8,
        .freestanding = false,
    },
    {
        .name         = "x86_64-elf",
        .triple       = "x86_64-unknown-none-elf",
        .fn_attrs     = "noredzone \"target-features\"=\"-mmx,-sse,-sse2\"",
        .word_size    = 8,
        .freestanding = true,
    },
    {
        .name         = "i686-elf",
        .triple       = "i686-unknown-none-elf",
        .fn_attrs     = "\"target-features\"=\"-mmx,-sse,-sse2\"",
        .word_size    = 4,
        .freestanding = true,
    },
    {
        .name         = "aarch64-elf",
        .triple       = "aarch64-unknown-none-elf",
        .fn_attrs     = "\"target-features\"=\"-fp-armv8,-neon\"",
        .word_size    = 8,
        .freestanding = true,
    },
    {
        .name         = "riscv64-elf",
        .triple       = "riscv64-unknown-none-elf",
        .fn_attrs     = "\"target-features\"=\"-f,-d\"",
        .word_size    = 8,
        .freestanding = true,
    },
    {
        .name         = "riscv32-elf",
        .triple       = "riscv32-unknown-none-elf",
        .fn_attrs     = "\"target-features\"=\"-f,-d\"",
        .word_size    = 4,
        .freestanding = true,
    },
    { 0 }
};

const struct target *
target_default(void)
{
    const struct target *target;

#if defined(__x86_64__)
    target = target_find("x86_64-linux");
#elif defined(__i386__)
    target = target_find("i686-linux");
#elif defined(__aarch64__)
    target = target_find("aarch64-linux");
#elif defined(__riscv) && (__riscv_xlen == 64)
    target = target_find("riscv64-linux");
#else
    target = &targets[0];
#endif

    return target ? target : &targets[0];
}

const struct target *
target_find(const char *name)
{
    size_t i;

    for (i = 0; targets[i].name; i++) {
        if (!strcmp(targets[i].name, name) || !strcmp(targets[i].triple, name))
            return &targets[i];
    }
    return NULL;
}

void
target_list(void)
{
    size_t i;

    for (i = 0; targets[i].name; i++) {
        printf("  %-18s %s%s\n",
               targets[i].name,
               targets[i].triple,
               targets[i].freestanding ? " (freestanding)" : "");
    }
}
