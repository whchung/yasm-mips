/*
 * MIPS Architecture instruction parser
 *
 * Copyright (C) 2014 Jack Chung <whchung@gmail.com>
 */
#include <util.h>

#include <libyasm.h>

#include "modules/arch/mips/mipsarch.h"

/*
 * Instruction formats.
 */

#define INS_R (0)
#define INS_I (1)
#define INS_J (2)

#define G_SPECIAL  (B_000000)
#define G_SPECIAL2 (B_011100)
#define G_REGIMM   (B_000001)

/*
 * Operands.
 *
 * An MIPS32 could take at most 4 operands.
 *
 * An operand can be of 3 types: constant, immediate, register.
 *
 * Constant operands will be ORed with the constant values.  Contant operands will always be 5-bits.
 *
 * Immediate operands can have different sizes.
 *       5-bits
 *      16-bits
 *      26-bits
 */
#define OPT_None        (0x0)
#define OPT_Con         (0x1 << 7)
#define OPT_Imm         (0x1 << 6)
#define OPT_Reg         (0x1 << 5)
#define OPT_Mask        (0x7 << 5)

#define OPC_Zero        (B_000000)
#define OPC_BAL         (B_010001)
#define OPC_BGEZ        (B_000001)
#define OPC_BGEZAL      (B_010001)
#define OPC_BLTZ        (B_000000)
#define OPC_BLTZAL      (B_010000)
#define OPC_SSNOP       (B_000001)
#define OPC_Mask        (B_011111)

#define OPI_5           (0x1 << 0)
#define OPI_16          (0x1 << 1)
#define OPI_26          (0x1 << 2)
#define OPI_Mask        (OPI_5 | OPI_16 | OPI_26)

typedef struct mips_insn_info {
    /* instruction name */
    const char *instr;

    /* The basic 6-bit opcode */
    unsigned char opcode;

    /* instruction format */
    unsigned char format;

    /* The number of non-constant operands this form of the instruction takes */
    unsigned int num_nonconst_operands;

    /* The types of each operand, see above */
    unsigned char operands[4];

    /* optional instruction function, only used in R-type instructions
     * The value will not be used in case of I-type instructions or J-type instructions
     */
    unsigned char func;
} mips_insn_info;

typedef struct mips_id_insn {
    yasm_insn insn;     /* base structure */

    /* instruction parse group - NULL if empty instruction (just prefixes) */
    /*@null@*/ const mips_insn_info *group;

    /* instruction name */
    const char *instr;

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
    { "", B_000000, INS_R, 0, { OPT_None, OPT_None, OPT_None, OPT_None }, B_000000 },
};

static const mips_insn_info arithmetic_insn[] = {
    { "add",     G_SPECIAL,    INS_R, 3, { OPT_Reg, OPT_Reg, OPT_Reg, OPT_Con | OPC_Zero }, B_100000 },
    { "addi",    B_001000,     INS_I, 3, { OPT_Reg, OPT_Reg, OPT_Imm | OPI_16, OPT_None }, B_000000 },
    { "addiu",   B_001001,     INS_I, 3, { OPT_Reg, OPT_Reg, OPT_Imm | OPI_16, OPT_None }, B_000000 },
    { "addu",    G_SPECIAL,    INS_R, 3, { OPT_Reg, OPT_Reg, OPT_Reg, OPT_Con | OPC_Zero }, B_100001 },
    { "clo",     G_SPECIAL2,   INS_R, 3, { OPT_Reg, OPT_Reg, OPT_Reg, OPT_Con | OPC_Zero }, B_100001 },
    { "clz",     G_SPECIAL2,   INS_R, 3, { OPT_Reg, OPT_Reg, OPT_Reg, OPT_Con | OPC_Zero }, B_100000 },
    { "div",     G_SPECIAL,    INS_R, 2, { OPT_Reg, OPT_Reg, OPT_Con | OPC_Zero, OPT_Con | OPC_Zero }, B_011010 },
    { "divu",    G_SPECIAL,    INS_R, 2, { OPT_Reg, OPT_Reg, OPT_Con | OPC_Zero, OPT_Con | OPC_Zero }, B_011011 },
    { "madd",    G_SPECIAL2,   INS_R, 2, { OPT_Reg, OPT_Reg, OPT_Con | OPC_Zero, OPT_Con | OPC_Zero }, B_000000 },
    { "maddu",   G_SPECIAL2,   INS_R, 2, { OPT_Reg, OPT_Reg, OPT_Con | OPC_Zero, OPT_Con | OPC_Zero }, B_000001 },
    { "msub",    G_SPECIAL2,   INS_R, 2, { OPT_Reg, OPT_Reg, OPT_Con | OPC_Zero, OPT_Con | OPC_Zero }, B_000100 },
    { "msubu",   G_SPECIAL2,   INS_R, 2, { OPT_Reg, OPT_Reg, OPT_Con | OPC_Zero, OPT_Con | OPC_Zero }, B_000101 },
    { "mul",     G_SPECIAL2,   INS_R, 3, { OPT_Reg, OPT_Reg, OPT_Reg, OPT_Con | OPC_Zero }, B_000010 },
    { "mult",    G_SPECIAL,    INS_R, 2, { OPT_Reg, OPT_Reg, OPT_Con | OPC_Zero, OPT_Con | OPC_Zero }, B_011000 },
    { "multu",   G_SPECIAL,    INS_R, 2, { OPT_Reg, OPT_Reg, OPT_Con | OPC_Zero, OPT_Con | OPC_Zero }, B_011001 },
    { "slt",     G_SPECIAL,    INS_R, 3, { OPT_Reg, OPT_Reg, OPT_Reg, OPT_Con | OPC_Zero }, B_101010 },
    { "slti",    B_001010,     INS_I, 3, { OPT_Reg, OPT_Reg, OPT_Imm | OPI_16, OPT_None }, B_000000 },
    { "sltiu",   B_001011,     INS_I, 3, { OPT_Reg, OPT_Reg, OPT_Imm | OPI_16, OPT_None  }, B_000000 },
    { "sltu",    G_SPECIAL,    INS_R, 3, { OPT_Reg, OPT_Reg, OPT_Reg, OPT_Con | OPC_Zero }, B_101011 },
    { "sub",     G_SPECIAL,    INS_R, 3, { OPT_Reg, OPT_Reg, OPT_Reg, OPT_Con | OPC_Zero }, B_100010 },
    { "subu",    G_SPECIAL,    INS_R, 3, { OPT_Reg, OPT_Reg, OPT_Reg, OPT_Con | OPC_Zero }, B_100011 },
};

static const mips_insn_info branchjump_insn[] = {
    { "b",       B_000100,     INS_I, 1, { OPT_Con | OPC_Zero, OPT_Con | OPC_Zero, OPT_Imm | OPI_16, OPT_None }, B_000000 },
    { "bal",     G_REGIMM,     INS_I, 1, { OPT_Con | OPC_Zero, OPT_Con | OPC_BAL, OPT_Imm | OPI_16, OPT_None }, B_000000 },
    { "beq",     B_000100,     INS_I, 3, { OPT_Reg, OPT_Reg, OPT_Imm | OPI_16, OPT_None }, B_000000 },
    { "bgez",    G_REGIMM,     INS_I, 2, { OPT_Reg, OPT_Con | OPC_BGEZ, OPT_Imm | OPI_16, OPT_None }, B_000000 },
    { "bgezal",  G_REGIMM,     INS_I, 2, { OPT_Reg, OPT_Con | OPC_BGEZAL, OPT_Imm | OPI_16, OPT_None }, B_000000 },
    { "bgtz",    B_000111,     INS_I, 2, { OPT_Reg, OPT_Con | OPC_Zero, OPT_Imm | OPI_16, OPT_None }, B_000000 },
    { "blez",    B_000110,     INS_I, 2, { OPT_Reg, OPT_Con | OPC_Zero, OPT_Imm | OPI_16, OPT_None }, B_000000 },
    { "bltz",    G_REGIMM,     INS_I, 2, { OPT_Reg, OPT_Con | OPC_BLTZ, OPT_Imm | OPI_16, OPT_None }, B_000000 },
    { "bltzal",  G_REGIMM,     INS_I, 2, { OPT_Reg, OPT_Con | OPC_BLTZAL, OPT_Imm | OPI_16, OPT_None }, B_000000 },
    { "bne",     B_000101,     INS_I, 3, { OPT_Reg, OPT_Reg, OPT_Imm | OPI_16, OPT_None }, B_000000 },
    { "j",       B_000010,     INS_J, 1, { OPT_Imm | OPI_26, OPT_None, OPT_None, OPT_None }, B_000000 },
    { "jal",     B_000011,     INS_J, 1, { OPT_Imm | OPI_26, OPT_None, OPT_None, OPT_None }, B_000000 },
    { "jalr",    G_SPECIAL,    INS_R, 2, { OPT_Reg, OPT_Con | OPC_Zero, OPT_Reg, OPT_Con | OPC_Zero }, B_001001 },
    { "jr",      G_SPECIAL,    INS_R, 1, { OPT_Reg, OPT_Con | OPC_Zero, OPT_Con | OPC_Zero, OPT_Con | OPC_Zero }, B_001000 },
};

static const mips_insn_info inscon_insn[] = {
    { "nop",     G_SPECIAL,    INS_R, 0, { OPT_Con | OPC_Zero, OPT_Con | OPC_Zero, OPT_Con | OPC_Zero, OPT_Con | OPC_Zero }, B_000000 },
    { "ssnop",   G_SPECIAL,    INS_R, 0, { OPT_Con | OPC_Zero, OPT_Con | OPC_Zero, OPT_Con | OPC_Zero, OPT_Con | OPC_SSNOP }, B_000000 },
};

static const mips_insn_info ldstmem_insn[] = {
    { "l",       B_100000,     INS_I, 3, { OPT_Reg, OPT_Reg, OPT_Imm | OPI_16, OPT_None }, B_000000 },
    { "lbu",     B_100100,     INS_I, 3, { OPT_Reg, OPT_Reg, OPT_Imm | OPI_16, OPT_None }, B_000000 },
    { "lh",      B_100001,     INS_I, 3, { OPT_Reg, OPT_Reg, OPT_Imm | OPI_16, OPT_None }, B_000000 },
    { "lhu",     B_100101,     INS_I, 3, { OPT_Reg, OPT_Reg, OPT_Imm | OPI_16, OPT_None }, B_000000 },
    { "ll",      B_110000,     INS_I, 3, { OPT_Reg, OPT_Reg, OPT_Imm | OPI_16, OPT_None }, B_000000 },
    { "lw",      B_100011,     INS_I, 3, { OPT_Reg, OPT_Reg, OPT_Imm | OPI_16, OPT_None }, B_000000 },
    { "lwl",     B_100010,     INS_I, 3, { OPT_Reg, OPT_Reg, OPT_Imm | OPI_16, OPT_None }, B_000000 },
    { "lwr",     B_100110,     INS_I, 3, { OPT_Reg, OPT_Reg, OPT_Imm | OPI_16, OPT_None }, B_000000 },
    { "pref",    B_110011,     INS_I, 2, { OPT_Reg, OPT_Con | OPC_Zero , OPT_Imm | OPI_16, OPT_None }, B_000000 },
    { "sb",      B_101000,     INS_I, 3, { OPT_Reg, OPT_Reg, OPT_Imm | OPI_16, OPT_None }, B_000000 },
    { "sc",      B_111000,     INS_I, 3, { OPT_Reg, OPT_Reg, OPT_Imm | OPI_16, OPT_None }, B_000000 },
    { "sh",      B_101001,     INS_I, 3, { OPT_Reg, OPT_Reg, OPT_Imm | OPI_16, OPT_None }, B_000000 },
    { "sw",      B_101011,     INS_I, 3, { OPT_Reg, OPT_Reg, OPT_Imm | OPI_16, OPT_None }, B_000000 },
    { "swl",     B_101010,     INS_I, 3, { OPT_Reg, OPT_Reg, OPT_Imm | OPI_16, OPT_None }, B_000000 },
    { "swr",     B_101110,     INS_I, 3, { OPT_Reg, OPT_Reg, OPT_Imm | OPI_16, OPT_None }, B_000000 },
    { "sync",    G_SPECIAL,    INS_R, 0, { OPT_Con | OPC_Zero, OPT_Con | OPC_Zero, OPT_Con | OPC_Zero, OPT_Con | OPC_Zero }, B_001111 },
};

static const mips_insn_info logical_insn[] = {
    { "and",     G_SPECIAL,    INS_R, 3, { OPT_Reg, OPT_Reg, OPT_Reg, OPT_Con | OPC_Zero }, B_100100 },
    { "andi",    B_001100,     INS_I, 3, { OPT_Reg, OPT_Reg, OPT_Imm | OPI_16, OPT_None }, B_000000 },
    { "lui",     B_001111,     INS_I, 2, { OPT_Con | OPC_Zero, OPT_Reg, OPT_Imm | OPI_16, OPT_None }, B_000000 },
    { "nor",     G_SPECIAL,    INS_R, 3, { OPT_Reg, OPT_Reg, OPT_Reg, OPT_Con | OPC_Zero }, B_100111 },
    { "or",      G_SPECIAL,    INS_R, 3, { OPT_Reg, OPT_Reg, OPT_Reg, OPT_Con | OPC_Zero }, B_100101 },
    { "ori",     B_001101,     INS_I, 3, { OPT_Reg, OPT_Reg, OPT_Imm | OPI_16, OPT_None }, B_000000 },
    { "xor",     G_SPECIAL,    INS_R, 3, { OPT_Reg, OPT_Reg, OPT_Reg, OPT_Con | OPC_Zero }, B_000000 },
    { "xori",    B_001110,     INS_I, 3, { OPT_Reg, OPT_Reg, OPT_Imm | OPI_16, OPT_None }, B_000000 },
};

static const mips_insn_info move_insn[] = {
    { "mfhi",    G_SPECIAL,    INS_R, 1, { OPT_Con | OPC_Zero, OPT_Con | OPC_Zero, OPT_Reg, OPT_Con | OPC_Zero }, B_010000 },
    { "mflo",    G_SPECIAL,    INS_R, 1, { OPT_Con | OPC_Zero, OPT_Con | OPC_Zero, OPT_Reg, OPT_Con | OPC_Zero }, B_010010 },
    { "movn",    G_SPECIAL,    INS_R, 3, { OPT_Reg, OPT_Reg, OPT_Reg, OPT_Con | OPC_Zero }, B_001011 },
    { "movz",    G_SPECIAL,    INS_R, 3, { OPT_Reg, OPT_Reg, OPT_Reg, OPT_Con | OPC_Zero }, B_001010 },
    { "mthi",    G_SPECIAL,    INS_R, 1, { OPT_Reg, OPT_Con | OPC_Zero, OPT_Con | OPC_Zero, OPT_Con | OPC_Zero }, B_010001 },
    { "mtlo",    G_SPECIAL,    INS_R, 1, { OPT_Reg, OPT_Con | OPC_Zero, OPT_Con | OPC_Zero, OPT_Con | OPC_Zero }, B_010011 },
};

static const mips_insn_info shift_insn[] = {
    { "sll",     G_SPECIAL,    INS_R, 3, { OPT_Con | OPC_Zero, OPT_Reg, OPT_Reg, OPT_Imm | OPI_5 }, B_000000 },
    { "sllv",    G_SPECIAL,    INS_R, 3, { OPT_Reg, OPT_Reg, OPT_Reg, OPT_Con | OPC_Zero }, B_000100 },
    { "sra",     G_SPECIAL,    INS_R, 3, { OPT_Con | OPC_Zero, OPT_Reg, OPT_Reg, OPT_Imm | OPI_5 }, B_000011 },
    { "srav",    G_SPECIAL,    INS_R, 3, { OPT_Con | OPC_Zero, OPT_Reg, OPT_Reg, OPT_Imm | OPI_5 }, B_000111 },
    { "srl",     G_SPECIAL,    INS_R, 3, { OPT_Con | OPC_Zero, OPT_Reg, OPT_Reg, OPT_Imm | OPI_5 }, B_000010 },
    { "srlv",    G_SPECIAL,    INS_R, 3, { OPT_Con | OPC_Zero, OPT_Reg, OPT_Reg, OPT_Imm | OPI_5 }, B_000110 },
};

static void
mips_id_insn_finalize(yasm_bytecode *bc, yasm_bytecode *prev_bc)
{
    mips_id_insn *id_insn = (mips_id_insn *)bc->contents;
    mips_insn *insn;
    int num_info = id_insn->num_info;
    const mips_insn_info *info = id_insn->group;
    int found = 0;
    yasm_insn_operand *op;
    int iter, count;

    yasm_insn_finalize(&id_insn->insn);

    /*
     * match the instruction among its group, and then match the operands to see if the types are correct
     */
    for (; num_info>0 && !found; num_info--, info++) {
        int mismatch = 0;

        /* Match instruction name */
        if (strcmp(id_insn->instr, info->instr)) {
            continue;
        }

        /* Match each operand type and size */
        for(count = 0, 
            iter = 0, 
            op = yasm_insn_ops_first(&id_insn->insn);
            
            op && 
            (count <= info->num_nonconst_operands) &&
            !mismatch;
            
            iter++) {

            if (iter >= 4) {
                mismatch = 1;
                break;
            }

            /* Check operand type */
            switch ((int)(info->operands[iter] & OPT_Mask)) {
                case OPT_Imm:
                    count++;
                    if (op->type != YASM_INSN__OPERAND_IMM){
                        mismatch = 1;
                    }
                    op = yasm_insn_op_next(op);
                    break;
                case OPT_Reg:
                    count++;
                    if (op->type != YASM_INSN__OPERAND_REG) {
                        mismatch = 1;
                    }
                    op = yasm_insn_op_next(op);
                    break;
                case OPT_Con:
                    break;
                case OPT_None:
                    break;
                default:
                    yasm_internal_error(N_("invalid operand type"));
            }

            if (mismatch)
                break;
        }

        if (count != info->num_nonconst_operands) {
            mismatch = 1;
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
    insn->opcode = info->opcode;
    insn->func = info->func;

    // printf("[INSTRUCTION BEGIN]: %s\n", id_insn->instr);

    /* Go through operands and assign */
    if (id_insn->insn.num_operands > 0) {
        for(iter = 0,
            op = yasm_insn_ops_first(&id_insn->insn);
            iter < 4;
            iter++) {

            switch ((int)(info->operands[iter] & OPT_Mask)) {
                case OPT_Reg:
                    if (op->type != YASM_INSN__OPERAND_REG)
                        yasm_internal_error(N_("invalid operand conversion"));

                    /* Extract register value */
                    //printf("\t[OPERAND REGISTER]: %d\n", op->data.reg;);
                    
                    insn->operand_type[iter] = MIPS_OPT_REG;

                    yasm_value_finalize_expr(&insn->operand[iter], 
                        yasm_expr_create_ident(yasm_expr_int(yasm_intnum_create_uint(op->data.reg)), bc->line), 
                            prev_bc, 5);

                    //yasm_value_print(&insn->operand[iter], stdout, 0);

                    /* move to the next operand */
                    op = yasm_insn_op_next(op);
                    break;

                case OPT_Imm:
                    if (op->type != YASM_INSN__OPERAND_IMM)
                        yasm_internal_error(N_("invalid operand conversion"));

                    /* Extract immediate value */
                    //printf("\t[OPERAND IMMEDIATE]: ");
                    //yasm_expr_print(op->data.val, stdout);
                    //printf("\n");
                    switch (info->operands[iter] & OPI_Mask) {
                        case OPI_5:
                            insn->operand_type[iter] = MIPS_OPT_IMM_5;
                            yasm_value_finalize_expr(&insn->operand[iter], op->data.val, prev_bc, 5);

                            //yasm_value_print(&insn->operand[iter], stdout, 0);
                            break;

                        case OPI_16:
                            insn->operand_type[iter] = MIPS_OPT_IMM_16;
                            yasm_value_finalize_expr(&insn->operand[iter], op->data.val, prev_bc, 16);

                            //yasm_value_print(&insn->operand[iter], stdout, 0);
                            break;

                        case OPI_26:
                            insn->operand_type[iter] = MIPS_OPT_IMM_26;
                            yasm_value_finalize_expr(&insn->operand[iter], op->data.val, prev_bc, 26);

                            //yasm_value_print(&insn->operand[iter], stdout, 0);
                            break;

                        default:
                            yasm_internal_error(N_("invalid immediate format"));
                            break;
                    }

                    /* Clear so it doesn't get destroyed
                       XXX: This line IS very important! */
                    op->type = YASM_INSN__OPERAND_REG;

                    /* move to the next operand */
                    op = yasm_insn_op_next(op);
                    break;

                case OPT_None:
                    insn->operand_type[iter] = MIPS_OPT_NONE;
                    break;

                case OPT_Con:
                    /* Extract constant value */
                    //printf("\t[OPERAND CONSTANT]: %d\n", info->operands[iter] & OPC_Mask);
                    insn->operand_type[iter] = MIPS_OPT_CONST;

                    yasm_value_finalize_expr(&insn->operand[iter], 
                        yasm_expr_create_ident(yasm_expr_int(yasm_intnum_create_uint(info->operands[iter] & OPC_Mask)), bc->line), 
                            prev_bc, 5);

                    //yasm_value_print(&insn->operand[iter], stdout, 0);

                    break;

                default:
                    yasm_internal_error(N_("unknown operand action"));
            }

        }
    }

    //printf("[INSTRUCTION END]: %s\n", id_insn->instr);

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

#define RET_INSN(g, i) \
    do { \
        group = g##_insn; \
        instr = i; \
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
    const char *instr = NULL;
    unsigned int nelems = NELEMS(empty_insn);
    mips_id_insn *id_insn;

    *bc = (yasm_bytecode *)NULL;
    *prefix = 0;

    /*const char *marker;*/
    /*!re2c
        /* instructions */

        /*********************/
        /* MIPS instructions */
        /*********************/

        /* CPU arithmetic instructions */
        'add'   { RET_INSN(arithmetic, "add"); }    /* add word */
        'addi'  { RET_INSN(arithmetic, "addi"); }   /* add immediate word */
        'addiu' { RET_INSN(arithmetic, "addiu"); }  /* add immediate unsigned word */
        'addu'  { RET_INSN(arithmetic, "addu"); }   /* add unsigned word */
        'clo'   { RET_INSN(arithmetic, "clo"); }    /* count leading ones in word */
        'clz'   { RET_INSN(arithmetic, "clz"); }    /* count leading zeros in word */
        'div'   { RET_INSN(arithmetic, "div"); }    /* divide word */
        'divu'  { RET_INSN(arithmetic, "divu"); }   /* divide unsigned word */
        'madd'  { RET_INSN(arithmetic, "madd"); }   /* multiply and add word to HI, LO */
        'maddu' { RET_INSN(arithmetic, "maddu"); }  /* multiply and add unsigned word to HI, LO */
        'msub'  { RET_INSN(arithmetic, "msub"); }   /* multiply and subtract word to HI, LO */
        'msubu' { RET_INSN(arithmetic, "msubu"); }  /* multiply and subtract unsigned word to HI, LO */
        'mul'   { RET_INSN(arithmetic, "mul"); }    /* multiply word to GPR */
        'mult'  { RET_INSN(arithmetic, "mult"); }   /* multiply word */
        'multu' { RET_INSN(arithmetic, "multu"); }  /* multiply unsigned word */
        'slt'   { RET_INSN(arithmetic, "slt"); }    /* set on less than */
        'slti'  { RET_INSN(arithmetic, "slti"); }   /* set on less than immediate */
        'sltiu' { RET_INSN(arithmetic, "sltiu"); }  /* set on less than immediate unsigned */
        'sltu'  { RET_INSN(arithmetic, "sltu"); }   /* set on less than unsigned */
        'sub'   { RET_INSN(arithmetic, "sub"); }    /* subtract word */
        'subu'  { RET_INSN(arithmetic, "subu"); }   /* subtract word unsigned */

        /* CPU branch and jump instructions */
        'b'     { RET_INSN(branchjump, "b"); }      /* unconditional branch (assembly idiom) */
        'bal'   { RET_INSN(branchjump, "bal"); }    /* branch and link (assembly idiom) */
        'beq'   { RET_INSN(branchjump, "beq"); }    /* branch on equal */
        'bgez'  { RET_INSN(branchjump, "bgez"); }   /* branch on greater than or equal to zero */
        'bgezal'{ RET_INSN(branchjump, "bgezal"); } /* branch on greater than or equal to zero and link */
        'bgtz'  { RET_INSN(branchjump, "bgtz"); }   /* branch on greater than zero */
        'blez'  { RET_INSN(branchjump, "blez"); }   /* branch on less than or equal to zero */
        'bltz'  { RET_INSN(branchjump, "bltz"); }   /* branch on less than zero */
        'bltzal'{ RET_INSN(branchjump, "bltzal"); } /* branch on less than zero and link */
        'bne'   { RET_INSN(branchjump, "bne"); }    /* branch on not equal */
        'j'     { RET_INSN(branchjump, "j"); }      /* jump */
        'jal'   { RET_INSN(branchjump, "jal"); }    /* jump and link */
        'jalr'  { RET_INSN(branchjump, "jalr"); }   /* jump and link register (hint always set to 00000)*/
        'jr'    { RET_INSN(branchjump, "jr"); }     /* jump register (hint always set to 00000)*/

        /* CPU instruction control insutrctions */
        'nop'   { RET_INSN(inscon,     "nop"); }    /* no operation (assembly idiom) */
        'ssnop' { RET_INSN(inscon,     "ssnop"); }  /* superscalar no operation (assembly idiom) */

        /* CPU load, store, and memory control instructions */
        'lb'    { RET_INSN(ldstmem,    "lb"); }     /* load byte */
        'lbu'   { RET_INSN(ldstmem,    "lbu"); }    /* load byte unsigned */
        'lh'    { RET_INSN(ldstmem,    "lh"); }     /* load halfword */
        'lhu'   { RET_INSN(ldstmem,    "lhu"); }    /* load halfword unsigned */
        'll'    { RET_INSN(ldstmem,    "ll"); }     /* load linked word */
        'lw'    { RET_INSN(ldstmem,    "lw"); }     /* load word */
        'lwl'   { RET_INSN(ldstmem,    "lwl"); }    /* load word left */
        'lwr'   { RET_INSN(ldstmem,    "lwr"); }    /* load word right */
        'pref'  { RET_INSN(ldstmem,    "pref"); }   /* prefetch */
        'sb'    { RET_INSN(ldstmem,    "sb"); }     /* store byte */
        'sc'    { RET_INSN(ldstmem,    "sc"); }     /* store conditional word */
        'sd'    { return YASM_ARCH_NOTINSNPREFIX; } /* (NOT implemented because it was not fully specified in MIPS spec) store doubleword */
        'sh'    { RET_INSN(ldstmem,    "sh"); }     /* store halfword */
        'sw'    { RET_INSN(ldstmem,    "sw"); }     /* store word */
        'swl'   { RET_INSN(ldstmem,    "swl"); }    /* store word left */
        'swr'   { RET_INSN(ldstmem,    "swr"); }    /* store word right */
        'sync'  { RET_INSN(ldstmem,    "sync"); }   /* synchronize shared memory */

        /* CPU logical instructions */
        'and'   { RET_INSN(logical,    "and"); }    /* and */
        'andi'  { RET_INSN(logical,    "andi"); }   /* and immediate */
        'lui'   { RET_INSN(logical,    "lui"); }    /* load upper immediate */
        'nor'   { RET_INSN(logical,    "nor"); }    /* not or */
        'or'    { RET_INSN(logical,    "or"); }     /* or */
        'ori'   { RET_INSN(logical,    "ori"); }    /* or immediate */
        'xor'   { RET_INSN(logical,    "xor"); }    /* exclusive or */
        'xori'  { RET_INSN(logical,    "xori"); }   /* exclusive or immediate */

        /* CPU move instructions */
        'mfhi'  { RET_INSN(move,       "mfhi"); }   /* move from HI register */
        'mflo'  { RET_INSN(move,       "mflo"); }   /* move from LO register */
        'movn'  { RET_INSN(move,       "movn"); }   /* move conditional on not zero */
        'movz'  { RET_INSN(move,       "movz"); }   /* move conditional on zero */
        'mthi'  { RET_INSN(move,       "mthi"); }   /* move to HI register */
        'mtlo'  { RET_INSN(move,       "mtlo"); }   /* move to LO register */

        /* CPU shift instructions */
        'sll'   { RET_INSN(shift,      "sll"); }    /* shift word left logical */
        'sllv'  { RET_INSN(shift,      "sllv"); }   /* shift word left logical variable */
        'sra'   { RET_INSN(shift,      "sra"); }    /* shift word right arithmetic */
        'srav'  { RET_INSN(shift,      "srav"); }   /* shift word right arithmetic variable */
        'srl'   { RET_INSN(shift,      "srl"); }    /* shift word right logical */
        'srlv'  { RET_INSN(shift,      "srlv"); }   /* shift word right logical variable */

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
    id_insn->instr = instr;
    id_insn->num_info = nelems;
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
    id_insn->instr = "";
    id_insn->num_info = NELEMS(empty_insn);

    return yasm_bc_create_common(&mips_id_insn_callback, id_insn, line);
}
