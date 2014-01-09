/*
 * MIPS Architecture header file
 *
 * Copyright (C) 2014 Jack Chung <whchung@gmail.com>
 */
#ifndef YASM_MIPSARCH_H
#define YASM_MIPSARCH_H

/**
 * [JC]: TBD
 */
/* Types of immediate.  All immediates are stored in the LSBs of the insn. */
typedef enum mips_imm_type {
    MIPS_IMM_NONE = 0,  /* no immediate */
    MIPS_IMM_4,         /* 4-bit */
    MIPS_IMM_5,         /* 5-bit */
    MIPS_IMM_6_WORD,    /* 6-bit, word-multiple (byte>>1) */
    MIPS_IMM_6_BYTE,    /* 6-bit, byte-multiple */
    MIPS_IMM_8,         /* 8-bit, word-multiple (byte>>1) */
    MIPS_IMM_9,         /* 9-bit, signed, word-multiple (byte>>1) */
    MIPS_IMM_9_PC       /* 9-bit, signed, word-multiple, PC relative */
} mips_imm_type;

/**
 * [JC]: TBD
 */
/* Bytecode types */

typedef struct mips_insn {
    yasm_value imm;             /* immediate or relative value */
    mips_imm_type imm_type;     /* size of the immediate */

    unsigned int opcode;        /* opcode */
} mips_insn;

/**
 * [JC]: TBD
 */
void yasm_mips__bc_transform_insn(yasm_bytecode *bc, mips_insn *insn);

/**
 * [JC]: TBD
 */
yasm_arch_insnprefix yasm_mips__parse_check_insnprefix
    (yasm_arch *arch, const char *id, size_t id_len, unsigned long line,
     /*@out@*/ /*@only@*/ yasm_bytecode **bc, /*@out@*/ uintptr_t *prefix);

/**
 * Check an generic identifier to see if it matches architecture specific names for registers or target modifiers.
 *
 * Currently support r0-r31.
 */
yasm_arch_regtmod yasm_mips__parse_check_regtmod
    (yasm_arch *arch, const char *id, size_t id_len,
     /*@out@*/ uintptr_t *data);

int yasm_mips__intnum_tobytes
    (yasm_arch *arch, const yasm_intnum *intn, unsigned char *buf,
     size_t destsize, size_t valsize, int shift, const yasm_bytecode *bc,
     int warn);

/**
 * [JC]: TBD
 */
/*@only@*/ yasm_bytecode *yasm_mips__create_empty_insn(yasm_arch *arch,
                                                       unsigned long line);

/**
 * [JC]: TBD
 */
void yasm_mips__ea_destroy(/*@only@*/ yasm_effaddr *ea);

#endif
