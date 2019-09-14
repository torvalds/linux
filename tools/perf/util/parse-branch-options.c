// SPDX-License-Identifier: GPL-2.0
#include "perf.h"
#include "util/debug.h"
#include <subcmd/parse-options.h>
#include "util/parse-branch-options.h"
#include <stdlib.h>

#define BRANCH_OPT(n, m) \
	{ .name = n, .mode = (m) }

#define BRANCH_END { .name = NULL }

struct branch_mode {
	const char *name;
	int mode;
};

static const struct branch_mode branch_modes[] = {
	BRANCH_OPT("u", PERF_SAMPLE_BRANCH_USER),
	BRANCH_OPT("k", PERF_SAMPLE_BRANCH_KERNEL),
	BRANCH_OPT("hv", PERF_SAMPLE_BRANCH_HV),
	BRANCH_OPT("any", PERF_SAMPLE_BRANCH_ANY),
	BRANCH_OPT("any_call", PERF_SAMPLE_BRANCH_ANY_CALL),
	BRANCH_OPT("any_ret", PERF_SAMPLE_BRANCH_ANY_RETURN),
	BRANCH_OPT("ind_call", PERF_SAMPLE_BRANCH_IND_CALL),
	BRANCH_OPT("abort_tx", PERF_SAMPLE_BRANCH_ABORT_TX),
	BRANCH_OPT("in_tx", PERF_SAMPLE_BRANCH_IN_TX),
	BRANCH_OPT("no_tx", PERF_SAMPLE_BRANCH_NO_TX),
	BRANCH_OPT("cond", PERF_SAMPLE_BRANCH_COND),
	BRANCH_OPT("ind_jmp", PERF_SAMPLE_BRANCH_IND_JUMP),
	BRANCH_OPT("call", PERF_SAMPLE_BRANCH_CALL),
	BRANCH_OPT("save_type", PERF_SAMPLE_BRANCH_TYPE_SAVE),
	BRANCH_END
};

int parse_branch_str(const char *str, __u64 *mode)
{
#define ONLY_PLM \
	(PERF_SAMPLE_BRANCH_USER	|\
	 PERF_SAMPLE_BRANCH_KERNEL	|\
	 PERF_SAMPLE_BRANCH_HV)

	int ret = 0;
	char *p, *s;
	char *os = NULL;
	const struct branch_mode *br;

	if (str == NULL) {
		*mode = PERF_SAMPLE_BRANCH_ANY;
		return 0;
	}

	/* because str is read-only */
	s = os = strdup(str);
	if (!s)
		return -1;

	for (;;) {
		p = strchr(s, ',');
		if (p)
			*p = '\0';

		for (br = branch_modes; br->name; br++) {
			if (!strcasecmp(s, br->name))
				break;
		}
		if (!br->name) {
			ret = -1;
			pr_warning("unknown branch filter %s,"
				    " check man page\n", s);
			goto error;
		}

		*mode |= br->mode;

		if (!p)
			break;

		s = p + 1;
	}

	/* default to any branch */
	if ((*mode & ~ONLY_PLM) == 0) {
		*mode = PERF_SAMPLE_BRANCH_ANY;
	}
error:
	free(os);
	return ret;
}

int
parse_branch_stack(const struct option *opt, const char *str, int unset)
{
	__u64 *mode = (__u64 *)opt->value;

	if (unset)
		return 0;

	/*
	 * cannot set it twice, -b + --branch-filter for instance
	 */
	if (*mode)
		return -1;

	return parse_branch_str(str, mode);
}
