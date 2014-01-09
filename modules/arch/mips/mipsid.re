/*
 * MIPS Architecture instruction parser
 *
 * Copyright (C) 2014 Jack Chung <whchung@gmail.com>
 */
#include <util.h>

#include <libyasm.h>

#include "modules/arch/mips/mipsarch.h"


/* Opcode modifiers.  The opcode bytes are in "reverse" order because the
 * parameters are read from the arch-specific data in LSB->MSB order.
 * (only for asthetic reasons in the lexer code below, no practical reason).
 */
#define MOD_OpHAdd  (1UL<<0)    /* Parameter adds to upper 8 bits of insn */
#define MOD_OpLAdd  (1UL<<1)    /* Parameter adds to lower 8 bits of insn */

/* Operand types.  These are more detailed than the "general" types for all
 * architectures, as they include the size, for instance.
 * Bit Breakdown (from LSB to MSB):
 *  - 1 bit = general type (must be exact match, except for =3):
 *            0 = immediate
 *            1 = register
 *
 * MSBs than the above are actions: what to do with the operand if the
 * instruction matches.  Essentially describes what part of the output bytecode
 * gets the operand.  This may require conversion (e.g. a register going into
 * an ea field).  Naturally, only one of each of these may be contained in the
 * operands of a single insn_info structure.
 *  - 2 bits = action:
 *             0 = does nothing (operand data is discarded)
 *             1 = DR field
 *             2 = SR field
 *             3 = immediate
 *
 * Immediate operands can have different sizes.
 *  - 3 bits = size:
 *             0 = no immediate
 *             1 = 4-bit immediate
 *             2 = 5-bit immediate
 *             3 = 6-bit index, word (16 bit)-multiple
 *             4 = 6-bit index, byte-multiple
 *             5 = 8-bit immediate, word-multiple
 *             6 = 9-bit signed immediate, word-multiple
 *             7 = 9-bit signed offset from next PC ($+2), word-multiple
 */
#define OPT_Imm         0x0
#define OPT_Reg         0x1
#define OPT_MASK        0x1

#define OPA_None        (0<<1)
#define OPA_DR          (1<<1)
#define OPA_SR          (2<<1)
#define OPA_Imm         (3<<1)
#define OPA_MASK        (3<<1)

#define OPI_None        (MIPS_IMM_NONE<<3)
#define OPI_4           (MIPS_IMM_4<<3)
#define OPI_5           (MIPS_IMM_5<<3)
#define OPI_6W          (MIPS_IMM_6_WORD<<3)
#define OPI_6B          (MIPS_IMM_6_BYTE<<3)
#define OPI_8           (MIPS_IMM_8<<3)
#define OPI_9           (MIPS_IMM_9<<3)
#define OPI_9PC         (MIPS_IMM_9_PC<<3)
#define OPI_MASK        (7<<3)

typedef struct mips_insn_info {
    /* Opcode modifiers for variations of instruction.  As each modifier reads
     * its parameter in LSB->MSB order from the arch-specific data[1] from the
     * lexer data, and the LSB of the arch-specific data[1] is reserved for the
     * count of insn_info structures in the instruction grouping, there can
     * only be a maximum of 3 modifiers.
     */
    unsigned int modifiers;

    /* The basic 2 byte opcode */
    unsigned int opcode;

    /* The number of operands this form of the instruction takes */
    unsigned char num_operands;

    /* The types of each operand, see above */
    unsigned int operands[3];
} mips_insn_info;

typedef struct mips_id_insn {
    yasm_insn insn;     /* base structure */

    /* instruction parse group - NULL if empty instruction (just prefixes) */
    /*@null@*/ const mips_insn_info *group;

    /* Modifier data */
    unsigned long mod_data;

    /* Number of elements in the instruction parse group */
    unsigned int num_info:8;
} mips_id_insn;

static void mips_id_insn_destroy(void *contents);
static void mips_id_insn_print(const void *contents, FILE *f, int indent_level);
static void mips_id_insn_finalize(yasm_bytecode *bc, yasm_bytecode *prev_bc);

static const yasm_bytecode_callback mips_id_insn_callback = {
    mips_id_insn_destroy,
    mips_id_insn_print,
    mips_id_insn_finalize,
    NULL,
    yasm_bc_calc_len_common,
    yasm_bc_expand_common,
    yasm_bc_tobytes_common,
    YASM_BC_SPECIAL_INSN
};

/*
 * Instruction groupings
 */

static const mips_insn_info empty_insn[] = {
    { 0, 0, 0, {0, 0, 0} }
};

static const mips_insn_info addand_insn[] = {
    { MOD_OpHAdd, 0x1000, 3,
      {OPT_Reg|OPA_DR, OPT_Reg|OPA_SR, OPT_Reg|OPA_Imm|OPI_5} },
    { MOD_OpHAdd, 0x1020, 3,
      {OPT_Reg|OPA_DR, OPT_Reg|OPA_SR, OPT_Imm|OPA_Imm|OPI_5} }
};

static const mips_insn_info br_insn[] = {
    { MOD_OpHAdd, 0x0000, 1, {OPT_Imm|OPA_Imm|OPI_9PC, 0, 0} }
};

static const mips_insn_info jmp_insn[] = {
    { 0, 0xC000, 2, {OPT_Reg|OPA_DR, OPT_Imm|OPA_Imm|OPI_9, 0} }
};

static const mips_insn_info lea_insn[] = {
    { 0, 0xE000, 2, {OPT_Reg|OPA_DR, OPT_Imm|OPA_Imm|OPI_9PC, 0} }
};

static const mips_insn_info ldst_insn[] = {
    { MOD_OpHAdd, 0x0000, 3,
      {OPT_Reg|OPA_DR, OPT_Reg|OPA_SR, OPT_Imm|OPA_Imm|OPI_6W} }
};

static const mips_insn_info ldstb_insn[] = {
    { MOD_OpHAdd, 0x0000, 3,
      {OPT_Reg|OPA_DR, OPT_Reg|OPA_SR, OPT_Imm|OPA_Imm|OPI_6B} }
};

static const mips_insn_info not_insn[] = {
    { 0, 0x903F, 2, {OPT_Reg|OPA_DR, OPT_Reg|OPA_SR, 0} }
};

static const mips_insn_info nooperand_insn[] = {
    { MOD_OpHAdd, 0x0000, 0, {0, 0, 0} }
};

static const mips_insn_info shift_insn[] = {
    { MOD_OpLAdd, 0xD000, 3,
      {OPT_Reg|OPA_DR, OPT_Reg|OPA_SR, OPT_Imm|OPA_Imm|OPI_4} }
};

static const mips_insn_info trap_insn[] = {
    { 0, 0xF000, 1, {OPT_Imm|OPA_Imm|OPI_8, 0, 0} }
};

static void
mips_id_insn_finalize(yasm_bytecode *bc, yasm_bytecode *prev_bc)
{
    mips_id_insn *id_insn = (mips_id_insn *)bc->contents;
    mips_insn *insn;
    int num_info = id_insn->num_info;
    const mips_insn_info *info = id_insn->group;
    unsigned long mod_data = id_insn->mod_data;
    int found = 0;
    yasm_insn_operand *op;
    int i;

    yasm_insn_finalize(&id_insn->insn);

    printf("[%s]\n", __FUNCTION__);

    /* Just do a simple linear search through the info array for a match.
     * First match wins.
     */
    for (; num_info>0 && !found; num_info--, info++) {
        int mismatch = 0;

        /* Match # of operands */
        if (id_insn->insn.num_operands != info->num_operands)
            continue;

        if (id_insn->insn.num_operands == 0) {
            found = 1;      /* no operands -> must have a match here. */
            break;
        }

        /* Match each operand type and size */
        for(i = 0, op = yasm_insn_ops_first(&id_insn->insn);
            op && i<info->num_operands && !mismatch;
            op = yasm_insn_op_next(op), i++) {
            /* Check operand type */
            switch ((int)(info->operands[i] & OPT_MASK)) {
                case OPT_Imm:
                    if (op->type != YASM_INSN__OPERAND_IMM)
                        mismatch = 1;
                    break;
                case OPT_Reg:
                    if (op->type != YASM_INSN__OPERAND_REG)
                        mismatch = 1;
                    break;
                default:
                    yasm_internal_error(N_("invalid operand type"));
            }

            if (mismatch)
                break;
        }

        if (!mismatch) {
            found = 1;
            break;
        }
    }

    if (!found) {
        /* Didn't find a matching one */
        yasm_error_set(YASM_ERROR_TYPE,
                       N_("invalid combination of opcode and operands"));
        return;
    }

    /* Copy what we can from info */
    insn = yasm_xmalloc(sizeof(mips_insn));
    yasm_value_initialize(&insn->imm, NULL, 0);
    insn->imm_type = MIPS_IMM_NONE;
    insn->opcode = info->opcode;

    /* Apply modifiers */
    if (info->modifiers & MOD_OpHAdd) {
        insn->opcode += ((unsigned int)(mod_data & 0xFF))<<8;
        mod_data >>= 8;
    }
    if (info->modifiers & MOD_OpLAdd) {
        insn->opcode += (unsigned int)(mod_data & 0xFF);
        /*mod_data >>= 8;*/
    }

    /* Go through operands and assign */
    if (id_insn->insn.num_operands > 0) {
        for(i = 0, op = yasm_insn_ops_first(&id_insn->insn);
            op && i<info->num_operands; op = yasm_insn_op_next(op), i++) {

            switch ((int)(info->operands[i] & OPA_MASK)) {
                case OPA_None:
                    /* Throw away the operand contents */
                    if (op->type == YASM_INSN__OPERAND_IMM)
                        yasm_expr_destroy(op->data.val);
                    break;
                case OPA_DR:
                    if (op->type != YASM_INSN__OPERAND_REG)
                        yasm_internal_error(N_("invalid operand conversion"));
                    insn->opcode |= ((unsigned int)(op->data.reg & 0x7)) << 9;
                    break;
                case OPA_SR:
                    if (op->type != YASM_INSN__OPERAND_REG)
                        yasm_internal_error(N_("invalid operand conversion"));
                    insn->opcode |= ((unsigned int)(op->data.reg & 0x7)) << 6;
                    break;
                case OPA_Imm:
                    insn->imm_type = (info->operands[i] & OPI_MASK)>>3;
                    switch (op->type) {
                        case YASM_INSN__OPERAND_IMM:
                            if (insn->imm_type == MIPS_IMM_6_WORD
                                || insn->imm_type == MIPS_IMM_8
                                || insn->imm_type == MIPS_IMM_9
                                || insn->imm_type == MIPS_IMM_9_PC)
                                op->data.val = yasm_expr_create(YASM_EXPR_SHR,
                                    yasm_expr_expr(op->data.val),
                                    yasm_expr_int(yasm_intnum_create_uint(1)),
                                    op->data.val->line);
                            if (yasm_value_finalize_expr(&insn->imm,
                                                         op->data.val,
                                                         prev_bc, 0))
                                yasm_error_set(YASM_ERROR_TOO_COMPLEX,
                                    N_("immediate expression too complex"));
                            break;
                        case YASM_INSN__OPERAND_REG:
                            if (yasm_value_finalize_expr(&insn->imm,
                                    yasm_expr_create_ident(yasm_expr_int(
                                    yasm_intnum_create_uint(op->data.reg & 0x7)),
                                    bc->line), prev_bc, 0))
                                yasm_internal_error(N_("reg expr too complex?"));
                            break;
                        default:
                            yasm_internal_error(N_("invalid operand conversion"));
                    }
                    break;
                default:
                    yasm_internal_error(N_("unknown operand action"));
            }

            /* Clear so it doesn't get destroyed */
            op->type = YASM_INSN__OPERAND_REG;
        }

        if (insn->imm_type == MIPS_IMM_9_PC) {
            if (insn->imm.seg_of || insn->imm.rshift > 1
                || insn->imm.curpos_rel)
                yasm_error_set(YASM_ERROR_VALUE, N_("invalid jump target"));
            insn->imm.curpos_rel = 1;
        }
    }

    /* Transform the bytecode */
    yasm_mips__bc_transform_insn(bc, insn);
}


#define YYCTYPE         unsigned char
#define YYCURSOR        id
#define YYLIMIT         id
#define YYMARKER        marker
#define YYFILL(n)       (void)(n)

yasm_arch_regtmod
yasm_mips__parse_check_regtmod(yasm_arch *arch, const char *oid, size_t id_len,
                               uintptr_t *data)
{
    const YYCTYPE *id = (const YYCTYPE *)oid;
    /*const char *marker;*/
    /*!re2c
        /* integer registers */
        /* register r0-r9 */
        'r' [0-9]       {
            *data = (oid[1]-'0');
            return YASM_ARCH_REG;
        }

        /* register r10-r29 */
        'r' [1-2][0-9]       {
            *data = (oid[1]-'0') * 10 + (oid[2] - '0');
            return YASM_ARCH_REG;
        }

        /* register r30-r31 */
        'r' '3'[0-1]       {
            *data = (oid[1]-'0') * 10 + (oid[2] - '0');
            return YASM_ARCH_REG;
        }

        /* catchalls */
        [\001-\377]+    {
            return YASM_ARCH_NOTREGTMOD;
        }
        [\000]  {
            return YASM_ARCH_NOTREGTMOD;
        }
    */
}

#define RET_INSN(g, m) \
    do { \
        group = g##_insn; \
        mod = m; \
        nelems = NELEMS(g##_insn); \
        goto done; \
    } while(0)

yasm_arch_insnprefix
yasm_mips__parse_check_insnprefix(yasm_arch *arch, const char *oid,
                                  size_t id_len, unsigned long line,
                                  yasm_bytecode **bc, uintptr_t *prefix)
{
    const YYCTYPE *id = (const YYCTYPE *)oid;
    const mips_insn_info *group = empty_insn;
    unsigned long mod = 0;
    unsigned int nelems = NELEMS(empty_insn);
    mips_id_insn *id_insn;

    *bc = (yasm_bytecode *)NULL;
    *prefix = 0;

    /*const char *marker;*/
    /*!re2c
        /* instructions */

        /* (original LC-3b instructions)
        'add' { RET_INSN(addand, 0x00); }
        'and' { RET_INSN(addand, 0x40); }
        'yand' { RET_INSN(addand, 0x40); }

        'br' { RET_INSN(br, 0x00); }
        'brn' { RET_INSN(br, 0x08); }
        'brz' { RET_INSN(br, 0x04); }
        'brp' { RET_INSN(br, 0x02); }
        'brnz' { RET_INSN(br, 0x0C); }
        'brnp' { RET_INSN(br, 0x0A); }
        'brzp' { RET_INSN(br, 0x06); }
        'brnzp' { RET_INSN(br, 0x0E); }
        'jsr' { RET_INSN(br, 0x40); }

        'jmp' { RET_INSN(jmp, 0); }

        'lea' { RET_INSN(lea, 0); }

        'ld' { RET_INSN(ldst, 0x20); }
        'ldi' { RET_INSN(ldst, 0xA0); }
        'st' { RET_INSN(ldst, 0x30); }
        'sti' { RET_INSN(ldst, 0xB0); }

        'ldb' { RET_INSN(ldstb, 0x60); }
        'stb' { RET_INSN(ldstb, 0x70); }

        'not' { RET_INSN(not, 0); }

        'ret' { RET_INSN(nooperand, 0xCE); }
        'rti' { RET_INSN(nooperand, 0x80); }
        'nop' { RET_INSN(nooperand, 0); }

        'lshf' { RET_INSN(shift, 0x00); }
        'rshfl' { RET_INSN(shift, 0x10); }
        'rshfa' { RET_INSN(shift, 0x30); }

        'trap' { RET_INSN(trap, 0); }
        */

        /*********************/
        /* MIPS instructions */
        /*********************/

        /* CPU arithmetic instructions */
        'add'   { RET_INSN(addand, 0x00); }             /* add word */
        'addi'  { RET_INSN(addand, 0x00); }             /* add immediate word */
        'addu'  { RET_INSN(addand, 0x00); }             /* add immediate unsigned word */
        'clo'   { RET_INSN(addand, 0x00); }             /* count leading ones in word */
        'clz'   { RET_INSN(addand, 0x00); }             /* count leading zeros in word */
        'div'   { RET_INSN(addand, 0x00); }             /* divide word */
        'divu'  { RET_INSN(addand, 0x00); }             /* divide unsigned word */
        'madd'  { RET_INSN(addand, 0x00); }             /* multiply and add word to HI, LO */
        'maddu' { RET_INSN(addand, 0x00); }             /* multiply and add unsigned word to HI, LO */
        'msub'  { RET_INSN(addand, 0x00); }             /* multiply and subtract word to HI, LO */
        'msubu' { RET_INSN(addand, 0x00); }             /* multiply and subtract unsigned word to HI, LO */
        'mul'   { RET_INSN(addand, 0x00); }             /* multiply word to GPR */
        'mult'  { RET_INSN(addand, 0x00); }             /* multiply word */
        'multu' { RET_INSN(addand, 0x00); }             /* multiply unsigned word */
        'slt'   { RET_INSN(addand, 0x00); }             /* set on less than */
        'slti'  { RET_INSN(addand, 0x00); }             /* set on less than immediate */
        'sltiu' { RET_INSN(addand, 0x00); }             /* set on less than immediate unsigned */
        'sltu'  { RET_INSN(addand, 0x00); }             /* set on less than unsigned */
        'sub'   { RET_INSN(addand, 0x00); }             /* subtract word */
        'subu'  { RET_INSN(addand, 0x00); }             /* subtract word unsigned */

        /* CPU branch and jump instructions */
        'b'     { RET_INSN(br, 0x00); }                 /* unconditional branch */
        'ba'    { RET_INSN(br, 0x00); }                 /* branch and link */
        'beq'   { RET_INSN(br, 0x00); }                 /* branch on equal */
        'bgez'  { RET_INSN(br, 0x00); }                 /* branch on greater than or equal to zero */
        'bgezal'{ RET_INSN(br, 0x00); }                 /* branch on greater than or equal to zero and link */
        'bgtz'  { RET_INSN(br, 0x00); }                 /* branch on greater than zero */
        'blez'  { RET_INSN(br, 0x00); }                 /* branch on less than or equal to zero */
        'bltz'  { RET_INSN(br, 0x00); }                 /* branch on less than zero */
        'bltzal'{ RET_INSN(br, 0x00); }                 /* branch on less than zero and link */
        'bne'   { RET_INSN(br, 0x00); }                 /* branch on not equal */
        'j'     { RET_INSN(br, 0x00); }                 /* jump */
        'jal'   { RET_INSN(br, 0x00); }                 /* jump and link */
        'jalr'  { RET_INSN(br, 0x00); }                 /* jump and link register */
        'jr'    { RET_INSN(br, 0x00); }                 /* jump register */

        /* CPU instruction control insutrctions */
        'nop'   { RET_INSN(addand, 0x00); }             /* no operation */
        'ssnop' { RET_INSN(addand, 0x00); }             /* superscalar no operation */

        /* CPU load, store, and memory control instructions */
        'lb'    { RET_INSN(ldst, 0x00); }               /* load byte */
        'lbu'   { RET_INSN(ldst, 0x00); }               /* load byte unsigned */
        'lh'    { RET_INSN(ldst, 0x00); }               /* load halfword */
        'lhu'   { RET_INSN(ldst, 0x00); }               /* load halfword unsigned */
        'll'    { RET_INSN(ldst, 0x00); }               /* load linked word */
        'lw'    { RET_INSN(ldst, 0x00); }               /* load word */
        'lwl'   { RET_INSN(ldst, 0x00); }               /* load word left */
        'lwr'   { RET_INSN(ldst, 0x00); }               /* load word right */
        'pref'  { RET_INSN(ldst, 0x00); }               /* prefetch */
        'sb'    { RET_INSN(ldst, 0x00); }               /* store byte */
        'sc'    { RET_INSN(ldst, 0x00); }               /* store conditional word */
        'sd'    { RET_INSN(ldst, 0x00); }               /* store doubleword */
        'sh'    { RET_INSN(addand, 0x00); }             /* store halfword */
        'sw'    { RET_INSN(addand, 0x00); }             /* store word */
        'swl'   { RET_INSN(addand, 0x00); }             /* store word left */
        'swr'   { RET_INSN(addand, 0x00); }             /* store word right */
        'sync'  { RET_INSN(addand, 0x00); }             /* synchronize shared memory */

        /* CPU logical instructions */
        'and'   { RET_INSN(addand, 0x00); }             /* and */
        'andi'  { RET_INSN(addand, 0x00); }             /* and immediate */
        'lui'   { RET_INSN(addand, 0x00); }             /* load upper immediate */
        'nor'   { RET_INSN(addand, 0x00); }             /* not or */
        'or'    { RET_INSN(addand, 0x00); }             /* or */
        'ori'   { RET_INSN(addand, 0x00); }             /* or immediate */
        'xor'   { RET_INSN(addand, 0x00); }             /* exclusive or */
        'xori'  { RET_INSN(addand, 0x00); }             /* exclusive or immediate */

        /* CPU move instructions */
        'mfhi'  { RET_INSN(addand, 0x00); }             /* move from HI register */
        'mflo'  { RET_INSN(addand, 0x00); }             /* move from LO register */
        'movn'  { RET_INSN(addand, 0x00); }             /* move conditional on not zero */
        'movz'  { RET_INSN(addand, 0x00); }             /* move conditional on zero */
        'mthi'  { RET_INSN(addand, 0x00); }             /* move to HI register */
        'mtlo'  { RET_INSN(addand, 0x00); }             /* move to LO register */

        /* CPU shift instructions */
        'sll'   { RET_INSN(addand, 0x00); }             /* shift word left logical */
        'sllv'  { RET_INSN(addand, 0x00); }             /* shift word left logical variable */
        'sra'   { RET_INSN(addand, 0x00); }             /* shift word right arithmetic */
        'srav'  { RET_INSN(addand, 0x00); }             /* shift word right arithmetic variable */
        'srl'   { RET_INSN(addand, 0x00); }             /* shift word right logical */
        'srlv'  { RET_INSN(addand, 0x00); }             /* shift word right logical variable */

        /********************************/
        /* NOT implemented insturctions */
        /********************************/

        /* CPU trap instructions */
        'break'     { return YASM_ARCH_NOTINSNPREFIX; } /* breakpoint */
        'syscall'   { return YASM_ARCH_NOTINSNPREFIX; } /* system call */
        'teq'       { return YASM_ARCH_NOTINSNPREFIX; } /* trap if equal */
        'teqi'      { return YASM_ARCH_NOTINSNPREFIX; } /* trap if equal immediate */
        'tge'       { return YASM_ARCH_NOTINSNPREFIX; } /* trap if greater or equal */
        'tgei'      { return YASM_ARCH_NOTINSNPREFIX; } /* trap if greater or equal immediate */
        'tgeiu'     { return YASM_ARCH_NOTINSNPREFIX; } /* trap if greater or equal immediate unsigned */
        'tgeu'      { return YASM_ARCH_NOTINSNPREFIX; } /* trap if greater or equal unsigned */
        'tlt'       { return YASM_ARCH_NOTINSNPREFIX; } /* trap if less than */
        'tlti'      { return YASM_ARCH_NOTINSNPREFIX; } /* trap if less than immediate */
        'tltiu'     { return YASM_ARCH_NOTINSNPREFIX; } /* trap if less than immediate unsigned */
        'tltu'      { return YASM_ARCH_NOTINSNPREFIX; } /* trap if less than unsigned */
        'tne'       { return YASM_ARCH_NOTINSNPREFIX; } /* trap if not equal */
        'tnei'      { return YASM_ARCH_NOTINSNPREFIX; } /* trap if not equal immediate */

        /* obsolete CPU branch instructions */
        'beql'      { return YASM_ARCH_NOTINSNPREFIX; } /* branch on equal likely */
        'bgezall'   { return YASM_ARCH_NOTINSNPREFIX; } /* branch on greater than or equal to zero and link likely */
        'bgezl'     { return YASM_ARCH_NOTINSNPREFIX; } /* branch on greater than or equal to zero likely */
        'bgtzl'     { return YASM_ARCH_NOTINSNPREFIX; } /* branch on greater than zero likely */
        'blezl'     { return YASM_ARCH_NOTINSNPREFIX; } /* branch on less than or equal to zero likely */
        'bltzall'   { return YASM_ARCH_NOTINSNPREFIX; } /* branch on less than zero and link likely */
        'bltzl'     { return YASM_ARCH_NOTINSNPREFIX; } /* branch on less than zero likely */
        'bnel'      { return YASM_ARCH_NOTINSNPREFIX; } /* branch on not equal likely */

        /* FPU arithmetic instructions */
        'abs\.d'    { return YASM_ARCH_NOTINSNPREFIX; } /* floating point absolute value (double precision) */
        'abs\.s'    { return YASM_ARCH_NOTINSNPREFIX; } /* floating point absolute value (single precision) */
        'add\.d'    { return YASM_ARCH_NOTINSNPREFIX; } /* floating point add (double precision) */
        'add\.s'    { return YASM_ARCH_NOTINSNPREFIX; } /* floating point add (single precision) */
        'div\.d'    { return YASM_ARCH_NOTINSNPREFIX; } /* floating point divide (double precision) */
        'div\.s'    { return YASM_ARCH_NOTINSNPREFIX; } /* floating point divide (single precision) */
        'madd\.d'   { return YASM_ARCH_NOTINSNPREFIX; } /* floating point multiply add (double precision) */
        'madd\.s'   { return YASM_ARCH_NOTINSNPREFIX; } /* floating point multiply add (single precision) */
        'msub\.d'   { return YASM_ARCH_NOTINSNPREFIX; } /* floating point multiply subtract (double precision) */
        'msub\.s'   { return YASM_ARCH_NOTINSNPREFIX; } /* floating point multiply subtract (single precision) */
        'mul\.d'    { return YASM_ARCH_NOTINSNPREFIX; } /* floating point multiply (double precision) */
        'mul\.s'    { return YASM_ARCH_NOTINSNPREFIX; } /* floating point multiply (single precision) */
        'neg\.d'    { return YASM_ARCH_NOTINSNPREFIX; } /* floating point negate (double precision) */
        'neg\.s'    { return YASM_ARCH_NOTINSNPREFIX; } /* floating point negate (single precision) */
        'nmadd\.d'  { return YASM_ARCH_NOTINSNPREFIX; } /* floating point negative multiply add (double precision) */
        'nmadd\.s'  { return YASM_ARCH_NOTINSNPREFIX; } /* floating point negative multiply add (single precision) */
        'nmsub\.d'  { return YASM_ARCH_NOTINSNPREFIX; } /* floating point negative multiply subtract (double precision) */
        'nmsub\.s'  { return YASM_ARCH_NOTINSNPREFIX; } /* floating point negative multiply subtract (single precision) */
        'recip\.d'  { return YASM_ARCH_NOTINSNPREFIX; } /* reciprocal approximation (double precision) */
        'recip\.s'  { return YASM_ARCH_NOTINSNPREFIX; } /* reciprocal approximation (single precision) */
        'rsqrt\.d'  { return YASM_ARCH_NOTINSNPREFIX; } /* reciprocal square root approximation (double precision) */
        'rsqrt\.s'  { return YASM_ARCH_NOTINSNPREFIX; } /* reciprocal square root approximation (single precision) */
        'sqrt\.d'   { return YASM_ARCH_NOTINSNPREFIX; } /* floating point square root (double precision) */
        'sqrt\.s'   { return YASM_ARCH_NOTINSNPREFIX; } /* floating point square root (single precision) */
        'sub\.d'    { return YASM_ARCH_NOTINSNPREFIX; } /* floating point subtract (double precision) */
        'sub\.s'    { return YASM_ARCH_NOTINSNPREFIX; } /* floating point subtract (single precision) */

        /* FPU branch insutrctions */
        'bc1f'      { return YASM_ARCH_NOTINSNPREFIX; } /* branch on FP false */
        'bc1t'      { return YASM_ARCH_NOTINSNPREFIX; } /* branch on FP true */

        /* FPU compare insutrctions */
        'c\.cond\.d'{ return YASM_ARCH_NOTINSNPREFIX; } /* floating point compare (double precision) */
        'c\.cond\.s'{ return YASM_ARCH_NOTINSNPREFIX; } /* floating point compare (single precision) */

        /* FPU convert instructions */
        'ceil\.w\.d'{ return YASM_ARCH_NOTINSNPREFIX; } /* floating point ceiling convert to word fixed point (double precision) */
        'ceil\.w\.s'{ return YASM_ARCH_NOTINSNPREFIX; } /* floating point ceiling convert to word fixed point (single precision) */
        'cvt\.d\.s' { return YASM_ARCH_NOTINSNPREFIX; } /* floating point convert to double floating point (single precision) */
        'cvt\.d\.w' { return YASM_ARCH_NOTINSNPREFIX; } /* floating point convert to double floating point (word fixed point) */
        'cvt\.s\.d' { return YASM_ARCH_NOTINSNPREFIX; } /* floating point convert to single floating point (double precision) */
        'cvt\.s\.w' { return YASM_ARCH_NOTINSNPREFIX; } /* floating point convert to single floating point (word fixed point) */
        'cvt\.w\.d' { return YASM_ARCH_NOTINSNPREFIX; } /* floating point convert to word fixed point (double precision) */
        'cvt\.w\.s' { return YASM_ARCH_NOTINSNPREFIX; } /* floating point convert to word fixed point (single precision) */
        'floor\.w\.d'{ return YASM_ARCH_NOTINSNPREFIX; }/* floating point floor convert to word fixed point (double precision) */
        'floor\.w\.s'{ return YASM_ARCH_NOTINSNPREFIX; }/* floating point floor convert to word fixed point (single precision) */
        'round\.w\.d'{ return YASM_ARCH_NOTINSNPREFIX; }/* floating point round to word fixed point (double precision) */
        'round\.w\.s'{ return YASM_ARCH_NOTINSNPREFIX; }/* floating point round to word fixed point (single precision) */
        'trunc\.w\.d'{ return YASM_ARCH_NOTINSNPREFIX; }/* floating point truncate to word fixed point (double precision) */
        'trunc\.w\.s'{ return YASM_ARCH_NOTINSNPREFIX; }/* floating point truncate to word fixed point (single precision) */

        /* FPU load, store, and memory control instructions */
        'ldc1'      { return YASM_ARCH_NOTINSNPREFIX; } /* load doubleword to floating point */
        'lwc1'      { return YASM_ARCH_NOTINSNPREFIX; } /* load word to floating point */
        'sdc1'      { return YASM_ARCH_NOTINSNPREFIX; } /* store doubleword from floating point */
        'swc1'      { return YASM_ARCH_NOTINSNPREFIX; } /* store word from floating point */

        /* FPU move instructions */        
        'cfc1'      { return YASM_ARCH_NOTINSNPREFIX; } /* move control word from floating point */
        'ctc1'      { return YASM_ARCH_NOTINSNPREFIX; } /* move control word to floating point */
        'mfc1'      { return YASM_ARCH_NOTINSNPREFIX; } /* move word from floating point */
        'mov\.d'    { return YASM_ARCH_NOTINSNPREFIX; } /* floating point move (double precision) */
        'mov\.s'    { return YASM_ARCH_NOTINSNPREFIX; } /* floating point move (single precision) */
        'movf'      { return YASM_ARCH_NOTINSNPREFIX; } /* move conditional on floating point false */
        'movf\.d'   { return YASM_ARCH_NOTINSNPREFIX; } /* floating point move conditional on floating point false (double precision) */
        'movf\.s'   { return YASM_ARCH_NOTINSNPREFIX; } /* floating point move conditional on floating point false (single precision) */
        'movn\.d'   { return YASM_ARCH_NOTINSNPREFIX; } /* floating point move conditional on not zero (double precision) */
        'movn\.s'   { return YASM_ARCH_NOTINSNPREFIX; } /* floating point move conditional on not zero (single precision) */
        'movt'      { return YASM_ARCH_NOTINSNPREFIX; } /* move conditional on floating point true */
        'movt\.d'   { return YASM_ARCH_NOTINSNPREFIX; } /* floating point move conditional on floating point true (double precision) */
        'movt\.s'   { return YASM_ARCH_NOTINSNPREFIX; } /* floating point move conditional on floating point true (single precision) */
        'movz\.d'   { return YASM_ARCH_NOTINSNPREFIX; } /* floating point move conditional on zero (double precision) */
        'movz\.s'   { return YASM_ARCH_NOTINSNPREFIX; } /* floating point move conditional on zero (single precision) */
        'mtc1'      { return YASM_ARCH_NOTINSNPREFIX; } /* move word to floating point */

        /* obsolete FPU branch instructions */
        'bc1fl'     { return YASM_ARCH_NOTINSNPREFIX; } /* branch on FP false likely */
        'bc1tl'     { return YASM_ARCH_NOTINSNPREFIX; } /* branch on FP true likely */

        /* coprocessor branch instructions */        
        'bc2f'      { return YASM_ARCH_NOTINSNPREFIX; } /* branch on COP2 false */
        'bc2t'      { return YASM_ARCH_NOTINSNPREFIX; } /* branch on COP2 true */

        /* coprocessor execute instructions */
        'cop2'      { return YASM_ARCH_NOTINSNPREFIX; } /* coprocessor operation to coprocessor 2 */

        /* coprocessor load and store instructions */
        'ldc2'      { return YASM_ARCH_NOTINSNPREFIX; } /* load doubleword to coprocessor 2 */
        'lwc2'      { return YASM_ARCH_NOTINSNPREFIX; } /* load word to coprocessor 2 */
        'sdc2'      { return YASM_ARCH_NOTINSNPREFIX; } /* store doubleword from coprocessor 2 */
        'swc2'      { return YASM_ARCH_NOTINSNPREFIX; } /* store word from coprocessor 2 */

        /* coprocessor move instructions */
        'cfc2'      { return YASM_ARCH_NOTINSNPREFIX; } /* move control word from coprocessor 2 */
        'ctc2'      { return YASM_ARCH_NOTINSNPREFIX; } /* move control word to coprocessor 2 */
        'mfc2'      { return YASM_ARCH_NOTINSNPREFIX; } /* move word from coprocessor 2 */
        'mtc2'      { return YASM_ARCH_NOTINSNPREFIX; } /* move word to coprocessor 2 */

        /* obsolete coprocessor branch instructions */
        'bc2fl'     { return YASM_ARCH_NOTINSNPREFIX; } /* branch on COP2 false likely */
        'bc2tl'     { return YASM_ARCH_NOTINSNPREFIX; } /* branch on COP2 true likely */

        /* privileged instructions */
        'cache'     { return YASM_ARCH_NOTINSNPREFIX; } /* perform cache operation */
        'eret'      { return YASM_ARCH_NOTINSNPREFIX; } /* exception return */
        'mfc0'      { return YASM_ARCH_NOTINSNPREFIX; } /* move from coprocessor 0 */
        'mtc0'      { return YASM_ARCH_NOTINSNPREFIX; } /* move to coprocessor 0 */
        'tlbp'      { return YASM_ARCH_NOTINSNPREFIX; } /* probe TLB for matching entry */
        'tlbr'      { return YASM_ARCH_NOTINSNPREFIX; } /* read indexed TLB entry */
        'tlbwi'     { return YASM_ARCH_NOTINSNPREFIX; } /* write indexed TLB entry */
        'tlbwr'     { return YASM_ARCH_NOTINSNPREFIX; } /* write random TLB entry */
        'wait'      { return YASM_ARCH_NOTINSNPREFIX; } /* enter standby mode */

        /* EJTAG instructions */
        'deret'     { return YASM_ARCH_NOTINSNPREFIX; } /* debug exception return */
        'sdbbp'     { return YASM_ARCH_NOTINSNPREFIX; } /* software debug breakpoint */


        /* catchalls */
        [\001-\377]+    {
            return YASM_ARCH_NOTINSNPREFIX;
        }
        [\000]  {
            return YASM_ARCH_NOTINSNPREFIX;
        }
    */

done:
    id_insn = yasm_xmalloc(sizeof(mips_id_insn));
    yasm_insn_initialize(&id_insn->insn);
    id_insn->group = group;
    id_insn->mod_data = mod;
    id_insn->num_info = nelems;
    printf("id = %s\n", oid);
    printf("[%s], going to use mips_id_insn_callback\n", __FUNCTION__);
    *bc = yasm_bc_create_common(&mips_id_insn_callback, id_insn, line);
    return YASM_ARCH_INSN;
}

static void
mips_id_insn_destroy(void *contents)
{
    mips_id_insn *id_insn = (mips_id_insn *)contents;
    yasm_insn_delete(&id_insn->insn, yasm_mips__ea_destroy);
    yasm_xfree(contents);
}

static void
mips_id_insn_print(const void *contents, FILE *f, int indent_level)
{
    const mips_id_insn *id_insn = (const mips_id_insn *)contents;
    yasm_insn_print(&id_insn->insn, f, indent_level);
    /*TODO*/
}

/*@only@*/ yasm_bytecode *
yasm_mips__create_empty_insn(yasm_arch *arch, unsigned long line)
{
    mips_id_insn *id_insn = yasm_xmalloc(sizeof(mips_id_insn));

    yasm_insn_initialize(&id_insn->insn);
    id_insn->group = empty_insn;
    id_insn->mod_data = 0;
    id_insn->num_info = NELEMS(empty_insn);
    printf("[%s], going to use mips_id_insn_callback\n", __FUNCTION__);

    return yasm_bc_create_common(&mips_id_insn_callback, id_insn, line);
}
