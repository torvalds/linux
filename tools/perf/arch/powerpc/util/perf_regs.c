// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <string.h>
#include <regex.h>
#include <linux/zalloc.h>

#include "perf_regs.h"
#include "../../../util/perf_regs.h"
#include "../../../util/debug.h"
#include "../../../util/event.h"
#include "../../../util/header.h"
#include "../../../perf-sys.h"
#include "utils_header.h"

#include <linux/kernel.h>

#define PVR_POWER9		0x004E
#define PVR_POWER10		0x0080
#define PVR_POWER11		0x0082

/* REG or %rREG */
#define SDT_OP_REGEX1  "^(%r)?([1-2]?[0-9]|3[0-1])$"

/* -NUM(REG) or NUM(REG) or -NUM(%rREG) or NUM(%rREG) */
#define SDT_OP_REGEX2  "^(\\-)?([0-9]+)\\((%r)?([1-2]?[0-9]|3[0-1])\\)$"

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
 * Parse OP and convert it into uprobe format, which is, +/-NUM(%gprREG).
 * Possible variants of OP are:
 *	Format		Example
 *	-------------------------
 *	NUM(REG)	48(18)
 *	-NUM(REG)	-48(18)
 *	NUM(%rREG)	48(%r18)
 *	-NUM(%rREG)	-48(%r18)
 *	REG		18
 *	%rREG		%r18
 *	iNUM		i0
 *	i-NUM		i-1
 *
 * SDT marker arguments on Powerpc uses %rREG form with -mregnames flag
 * and REG form with -mno-regnames. Here REG is general purpose register,
 * which is in 0 to 31 range.
 */
int arch_sdt_arg_parse_op(char *old_op, char **new_op)
{
	int ret, new_len;
	regmatch_t rm[5];
	char prefix;

	/* Constant argument. Uprobe does not support it */
	if (old_op[0] == 'i') {
		pr_debug4("Skipping unsupported SDT argument: %s\n", old_op);
		return SDT_ARG_SKIP;
	}

	ret = sdt_init_op_regex();
	if (ret < 0)
		return ret;

	if (!regexec(&sdt_op_regex1, old_op, 3, rm, 0)) {
		/* REG or %rREG --> %gprREG */

		new_len = 5;	/* % g p r NULL */
		new_len += (int)(rm[2].rm_eo - rm[2].rm_so);

		*new_op = zalloc(new_len);
		if (!*new_op)
			return -ENOMEM;

		scnprintf(*new_op, new_len, "%%gpr%.*s",
			(int)(rm[2].rm_eo - rm[2].rm_so), old_op + rm[2].rm_so);
	} else if (!regexec(&sdt_op_regex2, old_op, 5, rm, 0)) {
		/*
		 * -NUM(REG) or NUM(REG) or -NUM(%rREG) or NUM(%rREG) -->
		 *	+/-NUM(%gprREG)
		 */
		prefix = (rm[1].rm_so == -1) ? '+' : '-';

		new_len = 8;	/* +/- ( % g p r ) NULL */
		new_len += (int)(rm[2].rm_eo - rm[2].rm_so);
		new_len += (int)(rm[4].rm_eo - rm[4].rm_so);

		*new_op = zalloc(new_len);
		if (!*new_op)
			return -ENOMEM;

		scnprintf(*new_op, new_len, "%c%.*s(%%gpr%.*s)", prefix,
			(int)(rm[2].rm_eo - rm[2].rm_so), old_op + rm[2].rm_so,
			(int)(rm[4].rm_eo - rm[4].rm_so), old_op + rm[4].rm_so);
	} else {
		pr_debug4("Skipping unsupported SDT argument: %s\n", old_op);
		return SDT_ARG_SKIP;
	}

	return SDT_ARG_VALID;
}
