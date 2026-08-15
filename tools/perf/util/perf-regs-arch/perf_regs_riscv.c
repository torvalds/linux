// SPDX-License-Identifier: GPL-2.0

#include <errno.h>
#include <regex.h>
#include <string.h>
#include <linux/kernel.h>
#include <linux/zalloc.h>

#include "../debug.h"
#include "../perf_regs.h"
#include "../../arch/riscv/include/perf_regs.h"

/*
 * RISC-V SDT argument formats (GCC 'nor' constraint):
 *
 * Register:  REG        e.g. a0, t1, s0, sp
 * Memory:    NUM(REG)   e.g. 8(a0), -20(s0)
 * Constant:  NUM        e.g. 99  (not supported by uprobe, skip)
 *
 * Note: 'zero' (x0) is hardwired to 0 and not in pt_regs; skip it.
 *
 * Uprobe target format:
 *   Register: %REG       e.g. %a0
 *   Memory:   +NUM(%REG) or -NUM(%REG)
 */

/* RISC-V register ABI names: ra, sp, gp, tp, t0-t6, s0-s11, a0-a7 */
#define SDT_OP_REGEX1  "^(ra|sp|gp|tp|t[0-6]|s[0-9]|s1[01]|a[0-7])$"

/* RISC-V memory operand: [-]NUM(REG) */
#define SDT_OP_REGEX2  "^(\\-)?([0-9]+)\\((ra|sp|gp|tp|t[0-6]|s[0-9]|s1[01]|a[0-7])\\)$"

static regex_t sdt_op_regex1, sdt_op_regex2;

static int sdt_init_op_regex(void)
{
	static int initialized;
	int ret = 0;

	if (initialized)
		return 0;

	ret = regcomp(&sdt_op_regex1, SDT_OP_REGEX1, REG_EXTENDED);
	if (ret)
		goto error;

	ret = regcomp(&sdt_op_regex2, SDT_OP_REGEX2, REG_EXTENDED);
	if (ret)
		goto free_regex1;

	initialized = 1;
	return 0;

free_regex1:
	regfree(&sdt_op_regex1);
error:
	pr_debug4("Regex compilation error.\n");
	return -ret;
}

/*
 * Parse OP and convert it into uprobe format.
 * Possible variants of OP (RISC-V, GCC 'nor' constraint):
 *
 *   Format         Example       Uprobe
 *   ----------------------------------------
 *   REG            a0            %a0
 *   NUM(REG)       8(a0)         +8(%a0)
 *   -NUM(REG)      -20(s0)       -20(%s0)
 *   NUM            99            (skip, constant not supported)
 */
int __perf_sdt_arg_parse_op_riscv(char *old_op, char **new_op)
{
	int ret, new_len;
	regmatch_t rm[4];
	char prefix;

	/*
	 * Constant argument: pure integer with no trailing '(' (e.g. "99", "-1").
	 * uprobe does not support immediate values, so skip them.
	 * Memory operands like "8(a0)" or "-20(s0)" contain '(' so are NOT
	 * treated as constants here; they will be matched by REGEX2 below.
	 */
	if (strchr(old_op, '(') == NULL &&
	    ((*old_op >= '0' && *old_op <= '9') ||
	     (*old_op == '-' && old_op[1] >= '0' && old_op[1] <= '9'))) {
		pr_debug4("Skipping unsupported SDT argument: %s\n", old_op);
		return SDT_ARG_SKIP;
	}

	ret = sdt_init_op_regex();
	if (ret < 0)
		return ret;

	if (!regexec(&sdt_op_regex1, old_op, 2, rm, 0)) {
		/* REG --> %REG */
		new_len = 2;	/* % NULL */
		new_len += (int)(rm[1].rm_eo - rm[1].rm_so);

		*new_op = zalloc(new_len);
		if (!*new_op)
			return -ENOMEM;

		scnprintf(*new_op, new_len, "%%%.*s",
			(int)(rm[1].rm_eo - rm[1].rm_so), old_op + rm[1].rm_so);
	} else if (!regexec(&sdt_op_regex2, old_op, 4, rm, 0)) {
		/*
		 * NUM(REG) or -NUM(REG) --> +NUM(%REG) or -NUM(%REG)
		 * rm[1]: optional '-'
		 * rm[2]: decimal offset
		 * rm[3]: register name
		 */
		prefix = (rm[1].rm_so == -1) ? '+' : '-';

		new_len = 5;	/* sign ( % ) NULL */
		new_len += (int)(rm[2].rm_eo - rm[2].rm_so);
		new_len += (int)(rm[3].rm_eo - rm[3].rm_so);

		*new_op = zalloc(new_len);
		if (!*new_op)
			return -ENOMEM;

		scnprintf(*new_op, new_len, "%c%.*s(%%%.*s)", prefix,
			(int)(rm[2].rm_eo - rm[2].rm_so), old_op + rm[2].rm_so,
			(int)(rm[3].rm_eo - rm[3].rm_so), old_op + rm[3].rm_so);
	} else {
		pr_debug4("Skipping unsupported SDT argument: %s\n", old_op);
		return SDT_ARG_SKIP;
	}

	return SDT_ARG_VALID;
}

uint64_t __perf_reg_mask_riscv(bool intr __maybe_unused)
{
	return PERF_REGS_MASK;
}

const char *__perf_reg_name_riscv(int id)
{
	switch (id) {
	case PERF_REG_RISCV_PC:
		return "pc";
	case PERF_REG_RISCV_RA:
		return "ra";
	case PERF_REG_RISCV_SP:
		return "sp";
	case PERF_REG_RISCV_GP:
		return "gp";
	case PERF_REG_RISCV_TP:
		return "tp";
	case PERF_REG_RISCV_T0:
		return "t0";
	case PERF_REG_RISCV_T1:
		return "t1";
	case PERF_REG_RISCV_T2:
		return "t2";
	case PERF_REG_RISCV_S0:
		return "s0";
	case PERF_REG_RISCV_S1:
		return "s1";
	case PERF_REG_RISCV_A0:
		return "a0";
	case PERF_REG_RISCV_A1:
		return "a1";
	case PERF_REG_RISCV_A2:
		return "a2";
	case PERF_REG_RISCV_A3:
		return "a3";
	case PERF_REG_RISCV_A4:
		return "a4";
	case PERF_REG_RISCV_A5:
		return "a5";
	case PERF_REG_RISCV_A6:
		return "a6";
	case PERF_REG_RISCV_A7:
		return "a7";
	case PERF_REG_RISCV_S2:
		return "s2";
	case PERF_REG_RISCV_S3:
		return "s3";
	case PERF_REG_RISCV_S4:
		return "s4";
	case PERF_REG_RISCV_S5:
		return "s5";
	case PERF_REG_RISCV_S6:
		return "s6";
	case PERF_REG_RISCV_S7:
		return "s7";
	case PERF_REG_RISCV_S8:
		return "s8";
	case PERF_REG_RISCV_S9:
		return "s9";
	case PERF_REG_RISCV_S10:
		return "s10";
	case PERF_REG_RISCV_S11:
		return "s11";
	case PERF_REG_RISCV_T3:
		return "t3";
	case PERF_REG_RISCV_T4:
		return "t4";
	case PERF_REG_RISCV_T5:
		return "t5";
	case PERF_REG_RISCV_T6:
		return "t6";
	default:
		return NULL;
	}

	return NULL;
}

uint64_t __perf_reg_ip_riscv(void)
{
	return PERF_REG_RISCV_PC;
}

uint64_t __perf_reg_sp_riscv(void)
{
	return PERF_REG_RISCV_SP;
}
