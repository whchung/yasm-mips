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


/* Bytecode callback structures, see bytecode.h */
static const yasm_bytecode_callback mips_bc_callback_insn = {
    mips_bc_insn_destroy,       /* destroys the implementation-specific data, called from yasm_bc_destroy() */
    mips_bc_insn_print,         /* prints the implementation-specific data, called from yasm_bc_print() */
    yasm_bc_finalize_common,
    NULL,                       /* return elements size of a data bytecode */
    mips_bc_insn_calc_len,      /* calculates the minimum size of a bytecode, called from yasm_bc_calc_len() */
    mips_bc_insn_expand,        /* recalculates the bytecode's length baed on an expanded span length, called from yasm_bc_expand() */
    mips_bc_insn_tobytes,       /* covnert a bytecode into its byte representation, called from yasm_bc_tobytes() */
    0
};


void
yasm_mips__bc_transform_insn(yasm_bytecode *bc, mips_insn *insn)
{
    yasm_bc_transform(bc, &mips_bc_callback_insn, insn);
}

/*
 * destroys the implementation-specific data, called from yasm_bc_destroy()
 */
static void
mips_bc_insn_destroy(void *contents)
{
    int iter;
    mips_insn *insn = (mips_insn *)contents;
    for (iter = 0; iter < 4; iter++) {
        if (insn->operand_type[iter] != MIPS_OPT_NONE) {
            yasm_value_delete(&insn->operand[iter]);
        }
    }
    yasm_xfree(contents);
}

/*
 * prints the implementation-specific data, called from yasm_bc_print()
 */
static void
mips_bc_insn_print(const void *contents, FILE *f, int indent_level)
{
    mips_insn *insn = (mips_insn *)contents;
    yasm_intnum *delta;
    int iter;
    int count = 0;

    /* TBD, need a better way to find how to print jump targets */
    fprintf(f, "instr: o0x%02x ", insn->opcode);
    count += 6; /* opcode is always 6-bit */
    for (iter = 0; iter < 4; iter++) {
        switch (insn->operand_type[iter]) {
            case MIPS_OPT_NONE:
            break;
            case MIPS_OPT_CONST:
                count += 5;
                delta =  yasm_value_get_intnum(&insn->operand[iter], NULL, 0);
                if (delta) {
                    fprintf(f, "%*sc0x%02lx ", indent_level, "", yasm_intnum_get_uint(yasm_value_get_intnum(&insn->operand[iter], NULL, 0)));
                } else { 
                    fprintf(f, "%*s[%s] ", indent_level, "", yasm_symrec_get_name(insn->operand[iter].rel)); 
                }
            break;
            case MIPS_OPT_REG:
                count += 5;
                delta =  yasm_value_get_intnum(&insn->operand[iter], NULL, 0);
                if (delta) {
                    fprintf(f, "%*sr0x%02lx ", indent_level, "", yasm_intnum_get_uint(yasm_value_get_intnum(&insn->operand[iter], NULL, 0)));
                } else { 
                    fprintf(f, "%*s[%s] ", indent_level, "", yasm_symrec_get_name(insn->operand[iter].rel)); 
                }
            break;
            case MIPS_OPT_IMM_5:
                count += 5;
                delta =  yasm_value_get_intnum(&insn->operand[iter], NULL, 0);
                if (delta) {
                    fprintf(f, "%*si0x%02lx ", indent_level, "", yasm_intnum_get_uint(yasm_value_get_intnum(&insn->operand[iter], NULL, 0)));
                } else { 
                    fprintf(f, "%*s[%s] ", indent_level, "", yasm_symrec_get_name(insn->operand[iter].rel)); 
                }
            break;
            case MIPS_OPT_IMM_16:
                count += 16;
                delta =  yasm_value_get_intnum(&insn->operand[iter], NULL, 0);
                if (delta) {
                    fprintf(f, "%*si0x%04lx ", indent_level, "", yasm_intnum_get_uint(yasm_value_get_intnum(&insn->operand[iter], NULL, 0)));
                } else { 
                    fprintf(f, "%*s[%s] ", indent_level, "", yasm_symrec_get_name(insn->operand[iter].rel)); 
                }
            break;
            case MIPS_OPT_IMM_26:
                count += 26;
                delta =  yasm_value_get_intnum(&insn->operand[iter], NULL, 0);
                if (delta) {
                    fprintf(f, "%*si0x%04lx ", indent_level, "", yasm_intnum_get_uint(yasm_value_get_intnum(&insn->operand[iter], NULL, 0)));
                } else { 
                    fprintf(f, "%*s[%s] ", indent_level, "", yasm_symrec_get_name(insn->operand[iter].rel)); 
                }
            break;
            default:
            break;
        }
    }
    if (count != 32) {
        fprintf(f, "%*sf0x%02x\n", indent_level, "", insn->func);
        count += 6;
    } else {
        fprintf(f, "%*s\n", indent_level, "");
    }
    /* count should always be 32 here */
}

/*
 * calculates the minimum size of a bytecode, called from yasm_bc_calc_len()
 */
static int
mips_bc_insn_calc_len(yasm_bytecode *bc, yasm_bc_add_span_func add_span,
                      void *add_span_data)
{
    /* TBD, really need to understand how this works */

    /* Fixed size instruction length */
    bc->len += 4;

    return 0;
}

/*
 * recalculates the bytecode's length baed on an expanded span length, called from yasm_bc_expand()
 */
static int
mips_bc_insn_expand(yasm_bytecode *bc, int span, long old_val, long new_val,
                    /*@out@*/ long *neg_thres, /*@out@*/ long *pos_thres)
{
    yasm_error_set(YASM_ERROR_VALUE, N_("jump target out of range"));
    return -1;
}

/*
 * covnert a bytecode into its byte representation, called from yasm_bc_tobytes()
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
    int iter;
    int bit_offset = 32;
    unsigned int value = 0;
    unsigned int instr = 0x0;

    printf("instr: o0x%02x ", insn->opcode);

    bit_offset -= 6; /* opcode is always 6-bit */
    instr |= ((unsigned int)insn->opcode << bit_offset);

    for (iter = 0; iter < 4; iter++) {
        switch (insn->operand_type[iter]) {
            case MIPS_OPT_NONE:
            break;
            case MIPS_OPT_CONST:
                value = yasm_intnum_get_uint(yasm_value_get_intnum(&insn->operand[iter], bc, 0));
                printf("c0x%02x ", value);
                bit_offset -= 5;
                instr |= (value << bit_offset);
            break;
            case MIPS_OPT_REG:
                value = yasm_intnum_get_uint(yasm_value_get_intnum(&insn->operand[iter], bc, 0));
                printf("r0x%02x ", value);
                bit_offset -= 5;
                instr |= (value << bit_offset);
            break;
            case MIPS_OPT_IMM_5:
                delta =  yasm_value_get_intnum(&insn->operand[iter], bc, 0);
                if (delta) {
                    value = yasm_intnum_get_uint(delta);
                    printf("i0x%02x ", value);
                } else {
                    value = 0; /* TBD, need to caluclate the correct value */
                    printf("[%s] ", yasm_symrec_get_name(insn->operand[iter].rel)); 
                }
                bit_offset -= 5;
                instr |= (value << bit_offset);
            break;
            case MIPS_OPT_IMM_16:
                delta =  yasm_value_get_intnum(&insn->operand[iter], bc, 0);
                if (delta) {
                    value = yasm_intnum_get_uint(delta);
                    printf("i0x%04x ", value);
                } else {
                    value = 0; /* TBD, need to caluclate the correct value */
                    printf("[%s] ", yasm_symrec_get_name(insn->operand[iter].rel)); 
                }
                bit_offset -= 16;
                instr |= (value << bit_offset);
            break;
            case MIPS_OPT_IMM_26:
                delta =  yasm_value_get_intnum(&insn->operand[iter], bc, 0);
                if (delta) {
                    value = yasm_intnum_get_uint(delta);
                    printf("i0x%06x ", value);
                } else {
                    value = 0; /* TBD, need to caluclate the correct value */
                    printf("[%s] ", yasm_symrec_get_name(insn->operand[iter].rel)); 
                }
                bit_offset -= 26;
                instr |= (value << bit_offset);
            break;
            default:
            break;
        }
    }
    if (bit_offset != 0) {
        printf("f0x%02x\n", insn->func);
        bit_offset -= 6;
        instr |= (unsigned int)insn->func;
    } else {
        printf("\n");
    }

    /* output instruction */
    /* all MIPS instructions are 4 bytes in size */
    YASM_SAVE_32_L(*bufp, instr);
    *bufp += 4;     

    return 0;
}

int
yasm_mips__intnum_tobytes(yasm_arch *arch, const yasm_intnum *intn,
                          unsigned char *buf, size_t destsize, size_t valsize,
                          int shift, const yasm_bytecode *bc, int warn)
{
    /* Write value out. */
    yasm_intnum_get_sized(intn, buf, destsize, valsize, shift, 0, warn);        /* [JC]: Only support little endian for now */
    return 0;
}
