// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <regex.h>
#include <string.h>
#include <linux/kernel.h>
#include <linux/zalloc.h>

#include "../../../util/debug.h"
#include "../../../util/event.h"
#include "../../../util/perf_regs.h"

const struct sample_reg sample_reg_masks[] = {
	SMPL_REG(x0, PERF_REG_ARM64_X0),
	SMPL_REG(x1, PERF_REG_ARM64_X1),
	SMPL_REG(x2, PERF_REG_ARM64_X2),
	SMPL_REG(x3, PERF_REG_ARM64_X3),
	SMPL_REG(x4, PERF_REG_ARM64_X4),
	SMPL_REG(x5, PERF_REG_ARM64_X5),
	SMPL_REG(x6, PERF_REG_ARM64_X6),
	SMPL_REG(x7, PERF_REG_ARM64_X7),
	SMPL_REG(x8, PERF_REG_ARM64_X8),
	SMPL_REG(x9, PERF_REG_ARM64_X9),
	SMPL_REG(x10, PERF_REG_ARM64_X10),
	SMPL_REG(x11, PERF_REG_ARM64_X11),
	SMPL_REG(x12, PERF_REG_ARM64_X12),
	SMPL_REG(x13, PERF_REG_ARM64_X13),
	SMPL_REG(x14, PERF_REG_ARM64_X14),
	SMPL_REG(x15, PERF_REG_ARM64_X15),
	SMPL_REG(x16, PERF_REG_ARM64_X16),
	SMPL_REG(x17, PERF_REG_ARM64_X17),
	SMPL_REG(x18, PERF_REG_ARM64_X18),
	SMPL_REG(x19, PERF_REG_ARM64_X19),
	SMPL_REG(x20, PERF_REG_ARM64_X20),
	SMPL_REG(x21, PERF_REG_ARM64_X21),
	SMPL_REG(x22, PERF_REG_ARM64_X22),
	SMPL_REG(x23, PERF_REG_ARM64_X23),
	SMPL_REG(x24, PERF_REG_ARM64_X24),
	SMPL_REG(x25, PERF_REG_ARM64_X25),
	SMPL_REG(x26, PERF_REG_ARM64_X26),
	SMPL_REG(x27, PERF_REG_ARM64_X27),
	SMPL_REG(x28, PERF_REG_ARM64_X28),
	SMPL_REG(x29, PERF_REG_ARM64_X29),
	SMPL_REG(lr, PERF_REG_ARM64_LR),
	SMPL_REG(sp, PERF_REG_ARM64_SP),
	SMPL_REG(pc, PERF_REG_ARM64_PC),
	SMPL_REG_END
};

/* %xNUM */
#define SDT_OP_REGEX1  "^(x[1-2]?[0-9]|3[0-1])$"

/* [sp], [sp, NUM] */
#define SDT_OP_REGEX2  "^\\[sp(, )?([0-9]+)?\\]$"

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
	return ret;
}

/*
 * SDT marker arguments on Arm64 uses %xREG or [sp, NUM], currently
 * support these two formats.
 */
int arch_sdt_arg_parse_op(char *old_op, char **new_op)
{
	int ret, new_len;
	regmatch_t rm[5];

	ret = sdt_init_op_regex();
	if (ret < 0)
		return ret;

	if (!regexec(&sdt_op_regex1, old_op, 3, rm, 0)) {
		/* Extract xNUM */
		new_len = 2;	/* % NULL */
		new_len += (int)(rm[1].rm_eo - rm[1].rm_so);

		*new_op = zalloc(new_len);
		if (!*new_op)
			return -ENOMEM;

		scnprintf(*new_op, new_len, "%%%.*s",
			(int)(rm[1].rm_eo - rm[1].rm_so), old_op + rm[1].rm_so);
	} else if (!regexec(&sdt_op_regex2, old_op, 5, rm, 0)) {
		/* [sp], [sp, NUM] or [sp,NUM] */
		new_len = 7;	/* + ( % s p ) NULL */

		/* If the argument is [sp], need to fill offset '0' */
		if (rm[2].rm_so == -1)
			new_len += 1;
		else
			new_len += (int)(rm[2].rm_eo - rm[2].rm_so);

		*new_op = zalloc(new_len);
		if (!*new_op)
			return -ENOMEM;

		if (rm[2].rm_so == -1)
			scnprintf(*new_op, new_len, "+0(%%sp)");
		else
			scnprintf(*new_op, new_len, "+%.*s(%%sp)",
				  (int)(rm[2].rm_eo - rm[2].rm_so),
				  old_op + rm[2].rm_so);
	} else {
		pr_debug4("Skipping unsupported SDT argument: %s\n", old_op);
		return SDT_ARG_SKIP;
	}

	return SDT_ARG_VALID;
}
