/*
 * MIPS Architecture bytecode implementation
 *
 * Copyright (C) 2014 Jack Chung <whchung@gmail.com>
 */
#include <util.h>

#include <libyasm.h>

#include "mipsarch.h"


/* Bytecode callback function prototypes */

static void mips_bc_insn_destroy(void *contents);
static void mips_bc_insn_print(const void *contents, FILE *f,
                               int indent_level);
static int mips_bc_insn_calc_len(yasm_bytecode *bc,
                                 yasm_bc_add_span_func add_span,
                                 void *add_span_data);
static int mips_bc_insn_expand(yasm_bytecode *bc, int span, long old_val,
                               long new_val, /*@out@*/ long *neg_thres,
                               /*@out@*/ long *pos_thres);
static int mips_bc_insn_tobytes(yasm_bytecode *bc, unsigned char **bufp,
                                unsigned char *bufstart,
                                void *d, yasm_output_value_func output_value,
                                /*@null@*/ yasm_output_reloc_func output_reloc);


/**
 * [JC]: TBD
 */
/* Bytecode callback structures, see bytecode.h */
static const yasm_bytecode_callback mips_bc_callback_insn = {
    mips_bc_insn_destroy,       /* TBD, destroys the implementation-specific data, called from yasm_bc_destroy() */
    mips_bc_insn_print,         /* TBD, prints the implementation-specific data, called from yasm_bc_print() */
    yasm_bc_finalize_common,
    NULL,                       /* TBD, return elements size of a data bytecode */
    mips_bc_insn_calc_len,      /* TBD, calculates the minimum size of a bytecode, called from yasm_bc_calc_len() */
    mips_bc_insn_expand,        /* TBD, recalculates the bytecode's length baed on an expanded span length, called from yasm_bc_expand() */
    mips_bc_insn_tobytes,       /* TBD, covert a bytecode into its byte representation, called from yasm_bc_tobytes() */
    0
};


/**
 * [JC]: TBD
 */
void
yasm_mips__bc_transform_insn(yasm_bytecode *bc, mips_insn *insn)
{
    printf("[%s], going to use mips_bc_callback_insn\n", __FUNCTION__);
    yasm_bc_transform(bc, &mips_bc_callback_insn, insn);
}

/**
 * [JC]: TBD
 */
static void
mips_bc_insn_destroy(void *contents)
{
    mips_insn *insn = (mips_insn *)contents;
    yasm_value_delete(&insn->imm);
    yasm_xfree(contents);
    printf("[%s]\n", __FUNCTION__);
}

/**
 * [JC]: TBD
 */
static void
mips_bc_insn_print(const void *contents, FILE *f, int indent_level)
{
    const mips_insn *insn = (const mips_insn *)contents;
    printf("[%s]\n", __FUNCTION__);

    fprintf(f, "%*s_Instruction_\n", indent_level, "");
    fprintf(f, "%*sImmediate Value:", indent_level, "");
    if (!insn->imm.abs)
        fprintf(f, " (nil)\n");
    else {
        indent_level++;
        fprintf(f, "\n");
        yasm_value_print(&insn->imm, f, indent_level);
        fprintf(f, "%*sType=", indent_level, "");
        switch (insn->imm_type) {
            case MIPS_IMM_NONE:
                fprintf(f, "NONE-SHOULDN'T HAPPEN");
                break;
            case MIPS_IMM_4:
                fprintf(f, "4-bit");
                break;
            case MIPS_IMM_5:
                fprintf(f, "5-bit");
                break;
            case MIPS_IMM_6_WORD:
                fprintf(f, "6-bit, word-multiple");
                break;
            case MIPS_IMM_6_BYTE:
                fprintf(f, "6-bit, byte-multiple");
                break;
            case MIPS_IMM_8:
                fprintf(f, "8-bit, word-multiple");
                break;
            case MIPS_IMM_9:
                fprintf(f, "9-bit, signed, word-multiple");
                break;
            case MIPS_IMM_9_PC:
                fprintf(f, "9-bit, signed, word-multiple, PC-relative");
                break;
        }
        indent_level--;
    }
    /* FIXME
    fprintf(f, "\n%*sOrigin=", indent_level, "");
    if (insn->origin) {
        fprintf(f, "\n");
        yasm_symrec_print(insn->origin, f, indent_level+1);
    } else
        fprintf(f, "(nil)\n");
    */
    fprintf(f, "%*sOpcode: %04x\n", indent_level, "",
            (unsigned int)insn->opcode);
}

/**
 * [JC]: TBD
 */
static int
mips_bc_insn_calc_len(yasm_bytecode *bc, yasm_bc_add_span_func add_span,
                      void *add_span_data)
{
    mips_insn *insn = (mips_insn *)bc->contents;
    yasm_bytecode *target_prevbc;
    printf("[%s]\n", __FUNCTION__);

    /* Fixed size instruction length */
    bc->len += 2;

    /* Only need to worry about out-of-range to PC-relative */
    if (insn->imm_type != MIPS_IMM_9_PC)
        return 0;

    if (insn->imm.rel
        && (!yasm_symrec_get_label(insn->imm.rel, &target_prevbc)
             || target_prevbc->section != bc->section)) {
        /* External or out of segment, so we can't check distance. */
        return 0;
    }

    /* 9-bit signed, word-multiple displacement */
    add_span(add_span_data, bc, 1, &insn->imm, -512+(long)bc->len,
             511+(long)bc->len);
    return 0;
}

/**
 * [JC]: TBD
 */
static int
mips_bc_insn_expand(yasm_bytecode *bc, int span, long old_val, long new_val,
                    /*@out@*/ long *neg_thres, /*@out@*/ long *pos_thres)
{
    printf("[%s]\n", __FUNCTION__);
    yasm_error_set(YASM_ERROR_VALUE, N_("jump target out of range"));
    return -1;
}

/**
 * [JC]: TBD
 */
static int
mips_bc_insn_tobytes(yasm_bytecode *bc, unsigned char **bufp,
                     unsigned char *bufstart, void *d,
                     yasm_output_value_func output_value,
                     /*@unused@*/ yasm_output_reloc_func output_reloc)
{
    mips_insn *insn = (mips_insn *)bc->contents;
    /*@only@*/ yasm_intnum *delta;
    unsigned long buf_off = (unsigned long)(*bufp - bufstart);

    printf("[%s]\n", __FUNCTION__);
    /* Output opcode */
    YASM_SAVE_16_L(*bufp, insn->opcode);

    /* Insert immediate into opcode. */
    switch (insn->imm_type) {
        case MIPS_IMM_NONE:
            break;
        case MIPS_IMM_4:
            insn->imm.size = 4;
            if (output_value(&insn->imm, *bufp, 2, buf_off, bc, 1, d))
                return 1;
            break;
        case MIPS_IMM_5:
            insn->imm.size = 5;
            insn->imm.sign = 1;
            if (output_value(&insn->imm, *bufp, 2, buf_off, bc, 1, d))
                return 1;
            break;
        case MIPS_IMM_6_WORD:
            insn->imm.size = 6;
            if (output_value(&insn->imm, *bufp, 2, buf_off, bc, 1, d))
                return 1;
            break;
        case MIPS_IMM_6_BYTE:
            insn->imm.size = 6;
            insn->imm.sign = 1;
            if (output_value(&insn->imm, *bufp, 2, buf_off, bc, 1, d))
                return 1;
            break;
        case MIPS_IMM_8:
            insn->imm.size = 8;
            if (output_value(&insn->imm, *bufp, 2, buf_off, bc, 1, d))
                return 1;
            break;
        case MIPS_IMM_9_PC:
            /* Adjust relative displacement to end of bytecode */
            delta = yasm_intnum_create_int(-1);
            if (!insn->imm.abs)
                insn->imm.abs = yasm_expr_create_ident(yasm_expr_int(delta),
                                                       bc->line);
            else
                insn->imm.abs =
                    yasm_expr_create(YASM_EXPR_ADD,
                                     yasm_expr_expr(insn->imm.abs),
                                     yasm_expr_int(delta), bc->line);

            insn->imm.size = 9;
            insn->imm.sign = 1;
            if (output_value(&insn->imm, *bufp, 2, buf_off, bc, 1, d))
                return 1;
            break;
        case MIPS_IMM_9:
            insn->imm.size = 9;
            if (output_value(&insn->imm, *bufp, 2, buf_off, bc, 1, d))
                return 1;
            break;
        default:
            yasm_internal_error(N_("Unrecognized immediate type"));
    }

    *bufp += 2;     /* all instructions are 2 bytes in size */
    return 0;
}

int
yasm_mips__intnum_tobytes(yasm_arch *arch, const yasm_intnum *intn,
                          unsigned char *buf, size_t destsize, size_t valsize,
                          int shift, const yasm_bytecode *bc, int warn)
{
    printf("[%s]\n", __FUNCTION__);
    /* Write value out. */
    yasm_intnum_get_sized(intn, buf, destsize, valsize, shift, 0, warn);        /* [JC]: Only support little endian for now */
    return 0;
}
