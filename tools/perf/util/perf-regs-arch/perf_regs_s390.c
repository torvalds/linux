// SPDX-License-Identifier: GPL-2.0

#include <errno.h>
#include <regex.h>
#include "../perf_regs.h"
#include "../../arch/s390/include/perf_regs.h"
#include "debug.h"

#include <linux/zalloc.h>
#include <linux/kernel.h>

uint64_t __perf_reg_mask_s390(bool intr __maybe_unused)
{
	return PERF_REGS_MASK;
}

const char *__perf_reg_name_s390(int id)
{
	switch (id) {
	case PERF_REG_S390_R0:
		return "R0";
	case PERF_REG_S390_R1:
		return "R1";
	case PERF_REG_S390_R2:
		return "R2";
	case PERF_REG_S390_R3:
		return "R3";
	case PERF_REG_S390_R4:
		return "R4";
	case PERF_REG_S390_R5:
		return "R5";
	case PERF_REG_S390_R6:
		return "R6";
	case PERF_REG_S390_R7:
		return "R7";
	case PERF_REG_S390_R8:
		return "R8";
	case PERF_REG_S390_R9:
		return "R9";
	case PERF_REG_S390_R10:
		return "R10";
	case PERF_REG_S390_R11:
		return "R11";
	case PERF_REG_S390_R12:
		return "R12";
	case PERF_REG_S390_R13:
		return "R13";
	case PERF_REG_S390_R14:
		return "R14";
	case PERF_REG_S390_R15:
		return "R15";
	case PERF_REG_S390_FP0:
		return "FP0";
	case PERF_REG_S390_FP1:
		return "FP1";
	case PERF_REG_S390_FP2:
		return "FP2";
	case PERF_REG_S390_FP3:
		return "FP3";
	case PERF_REG_S390_FP4:
		return "FP4";
	case PERF_REG_S390_FP5:
		return "FP5";
	case PERF_REG_S390_FP6:
		return "FP6";
	case PERF_REG_S390_FP7:
		return "FP7";
	case PERF_REG_S390_FP8:
		return "FP8";
	case PERF_REG_S390_FP9:
		return "FP9";
	case PERF_REG_S390_FP10:
		return "FP10";
	case PERF_REG_S390_FP11:
		return "FP11";
	case PERF_REG_S390_FP12:
		return "FP12";
	case PERF_REG_S390_FP13:
		return "FP13";
	case PERF_REG_S390_FP14:
		return "FP14";
	case PERF_REG_S390_FP15:
		return "FP15";
	case PERF_REG_S390_MASK:
		return "MASK";
	case PERF_REG_S390_PC:
		return "PC";
	default:
		return NULL;
	}

	return NULL;
}

uint64_t __perf_reg_ip_s390(void)
{
	return PERF_REG_S390_PC;
}

uint64_t __perf_reg_sp_s390(void)
{
	return PERF_REG_S390_R15;
}

/* %rXX */
#define SDT_OP_REGEX1  "^(%r([0-9]|1[0-5]))$"
/* +-###(%rXX) */
#define SDT_OP_REGEX2  "^([+-]?[0-9]+\\(%r([0-9]|1[0-5])\\))$"
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
	initialized = 1;

	ret = regcomp(&sdt_op_regex2, SDT_OP_REGEX2, REG_EXTENDED);
	if (ret)
		goto free_regex1;
	initialized = 2;

	return 0;

free_regex1:
	regfree(&sdt_op_regex1);
error:
	pr_debug4("Regex compilation error, initialized %d\n", initialized);
	initialized = 0;
	return ret;
}

/*
 * Parse OP and convert it into uprobe format, which is, +/-NUM(%gprREG).
 * Possible variants of OP are:
 *	Format		Example
 *	-------------------------
 *	NUM(%rREG)	48(%r1)
 *	-NUM(%rREG)	-48(%r1)
 *	+NUM(%rREG)	+48(%r1)
 *	%rREG		%r1
 */
int __perf_sdt_arg_parse_op_s390(char *old_op, char **new_op)
{
	int ret, new_len;
	regmatch_t rm[6];

	*new_op = NULL;
	ret = sdt_init_op_regex();
	if (ret)
		return -EINVAL;

	if (!regexec(&sdt_op_regex1, old_op, ARRAY_SIZE(rm), rm, 0) ||
	    !regexec(&sdt_op_regex2, old_op, ARRAY_SIZE(rm), rm, 0)) {
		new_len = 1;    /* NULL byte */
		new_len += (int)(rm[1].rm_eo - rm[1].rm_so);
		*new_op = zalloc(new_len);
		if (!*new_op)
			return -ENOMEM;

		scnprintf(*new_op, new_len, "%.*s",
			  (int)(rm[1].rm_eo - rm[1].rm_so), old_op + rm[1].rm_so);
	} else {
		pr_debug4("Skipping unsupported SDT argument: %s\n", old_op);
		return SDT_ARG_SKIP;
	}

	return SDT_ARG_VALID;
}
