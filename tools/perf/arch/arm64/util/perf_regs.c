// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <regex.h>
#include <string.h>
#include <sys/auxv.h>
#include <linux/kernel.h>
#include <linux/zalloc.h>

#include "perf_regs.h"
#include "../../../perf-sys.h"
#include "../../../util/debug.h"
#include "../../../util/event.h"
#include "../../../util/perf_regs.h"

#define SMPL_REG_MASK(b) (1ULL << (b))

#ifndef HWCAP_SVE
#define HWCAP_SVE	(1 << 22)
#endif

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
