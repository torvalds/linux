/*-
 * Copyright (c) 2016 Cavium
 * All rights reserved.
 *
 * This software was developed by Semihalf.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <sys/param.h>

#include <sys/systm.h>
#include <machine/disassem.h>
#include <machine/armreg.h>
#include <ddb/ddb.h>

#define	ARM64_MAX_TOKEN_LEN	8
#define	ARM64_MAX_TOKEN_CNT	10

#define	ARM_INSN_SIZE_OFFSET	30
#define	ARM_INSN_SIZE_MASK	0x3

/* Special options for instruction printing */
#define	OP_SIGN_EXT	(1UL << 0)	/* Sign-extend immediate value */
#define	OP_LITERAL	(1UL << 1)	/* Use literal (memory offset) */
#define	OP_MULT_4	(1UL << 2)	/* Multiply immediate by 4 */
#define	OP_SF32		(1UL << 3)	/* Force 32-bit access */
#define	OP_SF_INV	(1UL << 6)	/* SF is inverted (1 means 32 bit access) */

static const char *w_reg[] = {
	"w0", "w1", "w2", "w3", "w4", "w5", "w6", "w7",
	"w8", "w9", "w10", "w11", "w12", "w13", "w14", "w15",
	"w16", "w17", "w18", "w19", "w20", "w21", "w22", "w23",
	"w24", "w25", "w26", "w27", "w28", "w29", "w30", "wSP",
};

static const char *x_reg[] = {
	"x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7",
	"x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15",
	"x16", "x17", "x18", "x19", "x20", "x21", "x22", "x23",
	"x24", "x25", "x26", "x27", "x28", "x29", "LR", "SP",
};

static const char *shift_2[] = {
	"LSL", "LSR", "ASR", "RSV"
};

/*
 * Structure representing single token (operand) inside instruction.
 * name   - name of operand
 * pos    - position within the instruction (in bits)
 * len    - operand length (in bits)
 */
struct arm64_insn_token {
	char name[ARM64_MAX_TOKEN_LEN];
	int pos;
	int len;
};

/*
 * Define generic types for instruction printing.
 */
enum arm64_format_type {
	TYPE_01,	/* OP <RD>, <RN>, <RM>{, <shift [LSL, LSR, ASR]> #<imm>} SF32/64
			   OP <RD>, <RN>, #<imm>{, <shift [0, 12]>} SF32/64 */
	TYPE_02,	/* OP <RT>, [<RN>, #<imm>]{!}] SF32/64
			   OP <RT>, [<RN>], #<imm>{!} SF32/64
			   OP <RT>, <RN>, <RM> {, EXTEND AMOUNT } */
	TYPE_03,	/* OP <RT>, #imm SF32/64 */
};

/*
 * Structure representing single parsed instruction format.
 * name   - opcode name
 * format - opcode format in a human-readable way
 * type   - syntax type for printing
 * special_ops  - special options passed to a printer (if any)
 * mask   - bitmask for instruction matching
 * pattern      - pattern to look for
 * tokens - array of tokens (operands) inside instruction
 */
struct arm64_insn {
	char* name;
	char* format;
	enum arm64_format_type type;
	uint64_t special_ops;
	uint32_t mask;
	uint32_t pattern;
	struct arm64_insn_token tokens[ARM64_MAX_TOKEN_CNT];
};

/*
 * Specify instruction opcode format in a human-readable way. Use notation
 * obtained from ARM Architecture Reference Manual for ARMv8-A.
 *
 * Format string description:
 *  Each group must be separated by "|". Group made of 0/1 is used to
 *  generate mask and pattern for instruction matching. Groups containing
 *  an operand token (in format NAME(length_bits)) are used to retrieve any
 *  operand data from the instruction. Names here must be meaningful
 *  and match the one described in the Manual.
 *
 * Token description:
 * SF     - "0" represents 32-bit access, "1" represents 64-bit access
 * SHIFT  - type of shift (instruction dependent)
 * IMM    - immediate value
 * Rx     - register number
 * OPTION - command specific options
 * SCALE  - scaling of immediate value
 */
static struct arm64_insn arm64_i[] = {
	{ "add", "SF(1)|0001011|SHIFT(2)|0|RM(5)|IMM(6)|RN(5)|RD(5)",
	    TYPE_01, 0 },
	{ "mov", "SF(1)|001000100000000000000|RN(5)|RD(5)",
	    TYPE_01, 0 },
	{ "add", "SF(1)|0010001|SHIFT(2)|IMM(12)|RN(5)|RD(5)",
	    TYPE_01, 0 },
	{ "ldr", "1|SF(1)|111000010|IMM(9)|OPTION(2)|RN(5)|RT(5)",
	    TYPE_02, OP_SIGN_EXT },		/* ldr immediate post/pre index */
	{ "ldr", "1|SF(1)|11100101|IMM(12)|RN(5)|RT(5)",
	    TYPE_02, 0 },			/* ldr immediate unsigned */
	{ "ldr", "1|SF(1)|111000011|RM(5)|OPTION(3)|SCALE(1)|10|RN(5)|RT(5)",
	    TYPE_02, 0 },			/* ldr register */
	{ "ldr", "0|SF(1)|011000|IMM(19)|RT(5)",
	    TYPE_03, OP_SIGN_EXT | OP_LITERAL | OP_MULT_4 },	/* ldr literal */
	{ "ldrb", "00|111000010|IMM(9)|OPTION(2)|RN(5)|RT(5)",
	    TYPE_02, OP_SIGN_EXT | OP_SF32 },	/* ldrb immediate post/pre index */
	{ "ldrb", "00|11100101|IMM(12)|RN(5)|RT(5)",
	    TYPE_02, OP_SF32 },			/* ldrb immediate unsigned */
	{ "ldrb", "00|111000011|RM(5)|OPTION(3)|SCALE(1)|10|RN(5)|RT(5)",
	    TYPE_02, OP_SF32  },		/* ldrb register */
	{ "ldrh", "01|111000010|IMM(9)|OPTION(2)|RN(5)|RT(5)", TYPE_02,
	    OP_SIGN_EXT | OP_SF32 },		/* ldrh immediate post/pre index */
	{ "ldrh", "01|11100101|IMM(12)|RN(5)|RT(5)",
	    TYPE_02, OP_SF32 },			/* ldrh immediate unsigned */
	{ "ldrh", "01|111000011|RM(5)|OPTION(3)|SCALE(1)|10|RN(5)|RT(5)",
	    TYPE_02, OP_SF32 },			/* ldrh register */
	{ "ldrsb", "001110001|SF(1)|0|IMM(9)|OPTION(2)|RN(5)|RT(5)",
	    TYPE_02, OP_SIGN_EXT | OP_SF_INV },	/* ldrsb immediate post/pre index */
	{ "ldrsb", "001110011|SF(1)|IMM(12)|RN(5)|RT(5)",\
	    TYPE_02, OP_SF_INV},		/* ldrsb immediate unsigned */
	{ "ldrsb", "001110001|SF(1)|1|RM(5)|OPTION(3)|SCALE(1)|10|RN(5)|RT(5)",
	    TYPE_02,  OP_SF_INV },		/* ldrsb register */
	{ "ldrsh", "011110001|SF(1)|0|IMM(9)|OPTION(2)|RN(5)|RT(5)",
	    TYPE_02, OP_SIGN_EXT | OP_SF_INV },	/* ldrsh immediate post/pre index */
	{ "ldrsh", "011110011|SF(1)|IMM(12)|RN(5)|RT(5)",
	    TYPE_02, OP_SF_INV},		/* ldrsh immediate unsigned */
	{ "ldrsh", "011110001|SF(1)|1|RM(5)|OPTION(3)|SCALE(1)|10|RN(5)|RT(5)",
	    TYPE_02, OP_SF_INV },		/* ldrsh register */
	{ "ldrsw", "10111000100|IMM(9)|OPTION(2)|RN(5)|RT(5)",
	    TYPE_02, OP_SIGN_EXT },		/* ldrsw immediate post/pre index */
	{ "ldrsw", "1011100110|IMM(12)|RN(5)|RT(5)",
	    TYPE_02, 0 },			/* ldrsw immediate unsigned */
	{ "ldrsw", "10111000101|RM(5)|OPTION(3)|SCALE(1)|10|RN(5)|RT(5)",
	    TYPE_02, 0 },			/* ldrsw register */
	{ "ldrsw", "10011000|IMM(19)|RT(5)",
	    TYPE_03, OP_SIGN_EXT | OP_LITERAL | OP_MULT_4 },	/* ldr literal */
	{ NULL, NULL }
};

static void
arm64_disasm_generate_masks(struct arm64_insn *tab)
{
	uint32_t mask, val;
	int a, i;
	int len, ret;
	int token = 0;
	char *format;
	int error;

	while (tab->name != NULL) {
		mask = 0;
		val = 0;
		format = tab->format;
		token = 0;
		error = 0;

		/*
		 * For each entry analyze format strings from the
		 * left (i.e. from the MSB).
		 */
		a = (INSN_SIZE * NBBY) - 1;
		while (*format != '\0' && (a >= 0)) {
			switch(*format) {
			case '0':
				/* Bit is 0, add to mask and pattern */
				mask |= (1 << a);
				a--;
				format++;
				break;
			case '1':
				/* Bit is 1, add to mask and pattern */
				mask |= (1 << a);
				val |= (1 << a);
				a--;
				format++;
				break;
			case '|':
				/* skip */
				format++;
				break;
			default:
				/* Token found, copy the name */
				memset(tab->tokens[token].name, 0,
				    sizeof(tab->tokens[token].name));
				i = 0;
				while (*format != '(') {
					tab->tokens[token].name[i] = *format;
					i++;
					format++;
					if (i >= ARM64_MAX_TOKEN_LEN) {
						printf("ERROR: token too long in op %s\n",
						    tab->name);
						error = 1;
						break;
					}
				}
				if (error != 0)
					break;

				/* Read the length value */
				ret = sscanf(format, "(%d)", &len);
				if (ret == 1) {
					if (token >= ARM64_MAX_TOKEN_CNT) {
						printf("ERROR: to many tokens in op %s\n",
						    tab->name);
						error = 1;
						break;
					}

					a -= len;
					tab->tokens[token].pos = a + 1;
					tab->tokens[token].len = len;
					token++;
				}

				/* Skip to the end of the token */
				while (*format != 0 && *format != '|')
					format++;
			}
		}

		/* Write mask and pattern to the instruction array */
		tab->mask = mask;
		tab->pattern = val;

		/*
		 * If we got here, format string must be parsed and "a"
		 * should point to -1. If it's not, wrong number of bits
		 * in format string. Mark this as invalid and prevent
		 * from being matched.
		 */
		if (*format != 0 || (a != -1) || (error != 0)) {
			tab->mask = 0;
			tab->pattern = 0xffffffff;
			printf("ERROR: skipping instruction op %s\n",
			    tab->name);
		}

		tab++;
	}
}

static int
arm64_disasm_read_token(struct arm64_insn *insn, u_int opcode,
    const char *token, int *val)
{
	int i;

	for (i = 0; i < ARM64_MAX_TOKEN_CNT; i++) {
		if (strcmp(insn->tokens[i].name, token) == 0) {
			*val = (opcode >> insn->tokens[i].pos &
			    ((1 << insn->tokens[i].len) - 1));
			return (0);
		}
	}

	return (EINVAL);
}

static int
arm64_disasm_read_token_sign_ext(struct arm64_insn *insn, u_int opcode,
    const char *token, int *val)
{
	int i;
	int msk;

	for (i = 0; i < ARM64_MAX_TOKEN_CNT; i++) {
		if (strcmp(insn->tokens[i].name, token) == 0) {
			msk = (1 << insn->tokens[i].len) - 1;
			*val = ((opcode >> insn->tokens[i].pos) & msk);

			/* If last bit is 1, sign-extend the value */
			if (*val & (1 << (insn->tokens[i].len - 1)))
				*val |= ~msk;

			return (0);
		}
	}

	return (EINVAL);
}

static const char *
arm64_reg(int b64, int num)
{

	if (b64 != 0)
		return (x_reg[num]);

	return (w_reg[num]);
}

vm_offset_t
disasm(const struct disasm_interface *di, vm_offset_t loc, int altfmt)
{
	struct arm64_insn *i_ptr = arm64_i;
	uint32_t insn;
	int matchp;
	int ret;
	int shift, rm, rt, rd, rn, imm, sf, idx, option, scale, amount;
	int sign_ext;
	int rm_absent;
	/* Indicate if immediate should be outside or inside brackets */
	int inside;
	/* Print exclamation mark if pre-incremented */
	int pre;

	/* Initialize defaults, all are 0 except SF indicating 64bit access */
	shift = rd = rm = rn = imm = idx = option = amount = scale = 0;
	sign_ext = 0;
	sf = 1;

	matchp = 0;
	insn = di->di_readword(loc);
	while (i_ptr->name) {
		/* If mask is 0 then the parser was not initialized yet */
		if ((i_ptr->mask != 0) &&
		    ((insn & i_ptr->mask) ==  i_ptr->pattern)) {
			matchp = 1;
			break;
		}
		i_ptr++;
	}
	if (matchp == 0)
		goto undefined;

	/* Global options */
	if (i_ptr->special_ops & OP_SF32)
		sf = 0;

	/* Global optional tokens */
	arm64_disasm_read_token(i_ptr, insn, "SF", &sf);
	if (i_ptr->special_ops & OP_SF_INV)
		sf = 1 - sf;
	if (arm64_disasm_read_token(i_ptr, insn, "SIGN", &sign_ext) == 0)
		sign_ext = 1 - sign_ext;
	if (i_ptr->special_ops & OP_SIGN_EXT)
		sign_ext = 1;
	if (sign_ext != 0)
		arm64_disasm_read_token_sign_ext(i_ptr, insn, "IMM", &imm);
	else
		arm64_disasm_read_token(i_ptr, insn, "IMM", &imm);
	if (i_ptr->special_ops & OP_MULT_4)
		imm <<= 2;

	/* Print opcode by type */
	switch (i_ptr->type) {
	case TYPE_01:
		/* OP <RD>, <RN>, <RM>{, <shift [LSL, LSR, ASR]> #<imm>} SF32/64
		   OP <RD>, <RN>, #<imm>{, <shift [0, 12]>} SF32/64 */

		/* Mandatory tokens */
		ret = arm64_disasm_read_token(i_ptr, insn, "RD", &rd);
		ret |= arm64_disasm_read_token(i_ptr, insn, "RN", &rn);
		if (ret != 0) {
			printf("ERROR: Missing mandatory token for op %s type %d\n",
			    i_ptr->name, i_ptr->type);
			goto undefined;
		}

		/* Optional tokens */
		arm64_disasm_read_token(i_ptr, insn, "SHIFT", &shift);
		rm_absent = arm64_disasm_read_token(i_ptr, insn, "RM", &rm);

		di->di_printf("%s\t%s, %s", i_ptr->name, arm64_reg(sf, rd),
		    arm64_reg(sf, rn));

		/* If RM is present use it, otherwise use immediate notation */
		if (rm_absent == 0) {
			di->di_printf(", %s", arm64_reg(sf, rm));
			if (imm != 0)
				di->di_printf(", %s #%d", shift_2[shift], imm);
		} else {
			if (imm != 0 || shift != 0)
				di->di_printf(", #0x%x", imm);
			if (shift != 0)
				di->di_printf(" LSL #12");
		}
		break;
	case TYPE_02:
		/* OP <RT>, [<RN>, #<imm>]{!}] SF32/64
		   OP <RT>, [<RN>], #<imm>{!} SF32/64
		   OP <RT>, <RN>, <RM> {, EXTEND AMOUNT } */

		/* Mandatory tokens */
		ret = arm64_disasm_read_token(i_ptr, insn, "RT", &rt);
		ret |= arm64_disasm_read_token(i_ptr, insn, "RN", &rn);
		if (ret != 0) {
			printf("ERROR: Missing mandatory token for op %s type %d\n",
			    i_ptr->name, i_ptr->type);
			goto undefined;
		}

		/* Optional tokens */
		arm64_disasm_read_token(i_ptr, insn, "OPTION", &option);
		arm64_disasm_read_token(i_ptr, insn, "SCALE", &scale);
		rm_absent = arm64_disasm_read_token(i_ptr, insn, "RM", &rm);

		if (rm_absent) {
			/*
			 * In unsigned operation, shift immediate value
			 * and reset options to default.
			 */
			if (sign_ext == 0) {
				imm = imm << ((insn >> ARM_INSN_SIZE_OFFSET) &
				    ARM_INSN_SIZE_MASK);
				option = 0;
			}
			switch (option) {
			case 0x0:
				pre = 0;
				inside = 1;
				break;
			case 0x1:
				pre = 0;
				inside = 0;
				break;
			case 0x2:
			default:
				pre = 1;
				inside = 1;
				break;
			}

			di->di_printf("%s\t%s, ", i_ptr->name, arm64_reg(sf, rt));
			if (inside != 0) {
				di->di_printf("[%s", arm64_reg(1, rn));
				if (imm != 0)
					di->di_printf(", #%d", imm);
				di->di_printf("]");
			} else {
				di->di_printf("[%s]", arm64_reg(1, rn));
				if (imm != 0)
					di->di_printf(", #%d", imm);
			}
			if (pre != 0)
				di->di_printf("!");
		} else {
			/* Last bit of option field determines 32/64 bit offset */
			di->di_printf("%s\t%s, [%s, %s", i_ptr->name,
			    arm64_reg(sf, rt), arm64_reg(1, rn),
			    arm64_reg(option & 1, rm));

			/* Calculate amount, it's op(31:30) */
			amount = (insn >> ARM_INSN_SIZE_OFFSET) &
			    ARM_INSN_SIZE_MASK;

			switch (option) {
			case 0x2:
				di->di_printf(", uxtw #%d", amount);
				break;
			case 0x3:
				if (scale != 0)
					di->di_printf(", lsl #%d", amount);
				break;
			case 0x6:
				di->di_printf(", sxtw #%d", amount);
				break;
			case 0x7:
				di->di_printf(", sxts #%d", amount);
				break;
			default:
				di->di_printf(", RSVD");
				break;
			}
			di->di_printf("]");
		}

		break;

	case TYPE_03:
		/* OP <RT>, #imm SF32/64 */

		/* Mandatory tokens */
		ret = arm64_disasm_read_token(i_ptr, insn, "RT", &rt);
		if (ret != 0) {
			printf("ERROR: Missing mandatory token for op %s type %d\n",
			    i_ptr->name, i_ptr->type);
			goto undefined;
		}

		di->di_printf("%s\t%s, ", i_ptr->name, arm64_reg(sf, rt));
		if (i_ptr->special_ops & OP_LITERAL)
			di->di_printf("0x%lx", loc + imm);
		else
			di->di_printf("#%d", imm);

		break;
	default:
		goto undefined;
	}

	di->di_printf("\n");
	return(loc + INSN_SIZE);

undefined:
	di->di_printf("undefined\t%08x\n", insn);
	return(loc + INSN_SIZE);
}

/* Parse format strings at the very beginning */
SYSINIT(arm64_disasm_generate_masks, SI_SUB_DDB_SERVICES,
    SI_ORDER_FIRST, arm64_disasm_generate_masks, arm64_i);
