/*
 * MIPS Architecture header file
 *
 * Copyright (C) 2014 Jack Chung <whchung@gmail.com>
 */
#ifndef YASM_MIPSARCH_H
#define YASM_MIPSARCH_H

/* utility macro for binary numbers */
#define B_000000 ( 0)
#define B_000001 ( 1)
#define B_000010 ( 2)
#define B_000011 ( 3)
#define B_000100 ( 4)
#define B_000101 ( 5)
#define B_000110 ( 6)
#define B_000111 ( 7)
#define B_001000 ( 8)
#define B_001001 ( 9)
#define B_001010 (10)
#define B_001011 (11)
#define B_001100 (12)
#define B_001101 (13)
#define B_001110 (14)
#define B_001111 (15)
#define B_010000 (16)
#define B_010001 (17)
#define B_010010 (18)
#define B_010011 (19)
#define B_010100 (20)
#define B_010101 (21)
#define B_010110 (22)
#define B_010111 (23)
#define B_011000 (24)
#define B_011001 (25)
#define B_011010 (26)
#define B_011011 (27)
#define B_011100 (28)
#define B_011101 (29)
#define B_011110 (30)
#define B_011111 (31)
#define B_100000 (32)
#define B_100001 (33)
#define B_100010 (34)
#define B_100011 (35)
#define B_100100 (36)
#define B_100101 (37)
#define B_100110 (38)
#define B_100111 (39)
#define B_101000 (40)
#define B_101001 (41)
#define B_101010 (42)
#define B_101011 (43)
#define B_101100 (44)
#define B_101101 (45)
#define B_101110 (46)
#define B_101111 (47)
#define B_110000 (48)
#define B_110001 (49)
#define B_110010 (50)
#define B_110011 (51)
#define B_110100 (52)
#define B_110101 (53)
#define B_110110 (54)
#define B_110111 (55)
#define B_111000 (56)
#define B_111001 (57)
#define B_111010 (58)
#define B_111011 (59)
#define B_111100 (60)
#define B_111101 (61)
#define B_111110 (62)
#define B_111111 (63)

/* Types of operand */
typedef enum mips_operand_type {
    MIPS_OPT_NONE = 0,  /* no immediate */
    MIPS_OPT_CONST,     /* 5-bit constant operand */
    MIPS_OPT_REG,       /* 5-bit register operand */
    MIPS_OPT_IMM_5,     /* 5-bit immediate operand */
    MIPS_OPT_IMM_16,    /* 16-bit immediate operand */
    MIPS_OPT_IMM_26,    /* 26-bit immediate operand */
} mips_operand_type;

/* Bytecode format */
typedef struct mips_insn {
    unsigned char opcode;               /* 6-bit opcode */

    mips_operand_type operand_type[4];  /* type of operand */

    yasm_value operand[4];              /* operand values, there would be at most 4 operands */

    unsigned char func;                 /* 6-bit function value. 
                                         * used in case of an R-type instruction.
                                         * ignored in case of an I-type or J-type instruction.
                                         */
} mips_insn;


void yasm_mips__bc_transform_insn(yasm_bytecode *bc, mips_insn *insn);

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

/*@only@*/ yasm_bytecode *yasm_mips__create_empty_insn(yasm_arch *arch,
                                                       unsigned long line);

void yasm_mips__ea_destroy(/*@only@*/ yasm_effaddr *ea);

#endif
