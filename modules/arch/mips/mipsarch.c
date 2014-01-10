/*
 * MIPS Architecture implementation
 *
 * Copyright (C) 2014 Jack Chung <whchung@gmail.com>
 */
#include <util.h>

#include <libyasm.h>

#include "mipsarch.h"


yasm_arch_module yasm_mips_LTX_arch;


static /*@only@*/ yasm_arch *
mips_create(const char *machine, const char *parser,
            /*@out@*/ yasm_arch_create_error *error)
{
    yasm_arch_base *arch;

    *error = YASM_ARCH_CREATE_OK;

    if (yasm__strcasecmp(machine, "mips") != 0) {
        *error = YASM_ARCH_CREATE_BAD_MACHINE;
        return NULL;
    }

    if (yasm__strcasecmp(parser, "nasm") != 0) {
        *error = YASM_ARCH_CREATE_BAD_PARSER;
        return NULL;
    }

    arch = yasm_xmalloc(sizeof(yasm_arch_base));
    arch->module = &yasm_mips_LTX_arch;
    return (yasm_arch *)arch;
}

static void
mips_destroy(/*@only@*/ yasm_arch *arch)
{
    yasm_xfree(arch);
}

static const char *
mips_get_machine(/*@unused@*/ const yasm_arch *arch)
{
    return "mips";
}

static unsigned int
mips_get_address_size(/*@unused@*/ const yasm_arch *arch)
{
    return 32;
}

static int
mips_set_var(yasm_arch *arch, const char *var, unsigned long val)
{
    return 1;
}

static unsigned int
mips_get_reg_size(/*@unused@*/ yasm_arch *arch, /*@unused@*/ uintptr_t reg)
{
    return 32;
}

static uintptr_t
mips_reggroup_get_reg(/*@unused@*/ yasm_arch *arch,
                      /*@unused@*/ uintptr_t reggroup,
                      /*@unused@*/ unsigned long regindex)
{
    return 0;
}

static void
mips_reg_print(/*@unused@*/ yasm_arch *arch, uintptr_t reg, FILE *f)
{
    fprintf(f, "r%u", (unsigned int)(reg&31));
}

static int
mips_floatnum_tobytes(yasm_arch *arch, const yasm_floatnum *flt,
                      unsigned char *buf, size_t destsize, size_t valsize,
                      size_t shift, int warn)
{
    yasm_error_set(YASM_ERROR_FLOATING_POINT,
                   N_("MIPS floating point is not implemented yet"));
    return 1;
}

/*
 * [JC]: TBD
 */
static const unsigned char **
mips_get_fill(const yasm_arch *arch)
{
    /* NOP pattern is all 0's */
    static const unsigned char *fill[16] = {
        NULL,           /* unused */
        NULL,           /* 1 - illegal; all opcodes are 2 bytes long */
        (const unsigned char *)
        "\x00\x00",                     /* 4 */
        NULL,                           /* 3 - illegal */
        (const unsigned char *)
        "\x00\x00\x00\x00",             /* 4 */
        NULL,                           /* 5 - illegal */
        (const unsigned char *)
        "\x00\x00\x00\x00\x00\x00",     /* 6 */
        NULL,                           /* 7 - illegal */
        (const unsigned char *)
        "\x00\x00\x00\x00\x00\x00"      /* 8 */
        "\x00\x00",
        NULL,                           /* 9 - illegal */
        (const unsigned char *)
        "\x00\x00\x00\x00\x00\x00"      /* 10 */
        "\x00\x00\x00\x00",
        NULL,                           /* 11 - illegal */
        (const unsigned char *)
        "\x00\x00\x00\x00\x00\x00"      /* 12 */
        "\x00\x00\x00\x00\x00\x00",
        NULL,                           /* 13 - illegal */
        (const unsigned char *)
        "\x00\x00\x00\x00\x00\x00"      /* 14 */
        "\x00\x00\x00\x00\x00\x00\x00\x00",
        NULL                            /* 15 - illegal */
    };
    return fill;
}

static yasm_effaddr *
mips_ea_create_expr(yasm_arch *arch, yasm_expr *e)
{
    yasm_effaddr *ea = yasm_xmalloc(sizeof(yasm_effaddr));
    yasm_value_initialize(&ea->disp, e, 0);
    ea->need_nonzero_len = 0;
    ea->need_disp = 1;
    ea->nosplit = 0;
    ea->strong = 0;
    ea->segreg = 0;
    ea->pc_rel = 0;
    ea->not_pc_rel = 0;
    return ea;
}

void
yasm_mips__ea_destroy(/*@only@*/ yasm_effaddr *ea)
{
    yasm_value_delete(&ea->disp);
    yasm_xfree(ea);
}

static void
mips_ea_print(const yasm_effaddr *ea, FILE *f, int indent_level)
{
    fprintf(f, "%*sDisp:\n", indent_level, "");
    yasm_value_print(&ea->disp, f, indent_level+1);
}

/* Define mips machines -- see arch.h for details */
static yasm_arch_machine mips_machines[] = {
    { "MIPS", "mips" },
    { NULL, NULL }
};

/* Define arch structure -- see arch.h for details */
yasm_arch_module yasm_mips_LTX_arch = {
    "MIPS32 (No FPU support, Little endian)",
    "mips",
    NULL,
    mips_create,
    mips_destroy,
    mips_get_machine,
    mips_get_address_size,
    mips_set_var,
    yasm_mips__parse_check_insnprefix,          /* [JC]: TBD */
    yasm_mips__parse_check_regtmod,
    mips_get_fill,                              /* [JC]: TBD */
    mips_floatnum_tobytes,
    yasm_mips__intnum_tobytes,
    mips_get_reg_size,
    mips_reggroup_get_reg,
    mips_reg_print,
    NULL,       /*yasm_mips__segreg_print*/
    mips_ea_create_expr,                     
    yasm_mips__ea_destroy,                    
    mips_ea_print,
    yasm_mips__create_empty_insn,
    mips_machines,
    "mips",
    32,
    4
};
