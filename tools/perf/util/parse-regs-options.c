// SPDX-License-Identifier: GPL-2.0
#include "perf.h"
#include "util/util.h"
#include "util/debug.h"
#include <subcmd/parse-options.h>
#include "util/parse-regs-options.h"

int
parse_regs(const struct option *opt, const char *str, int unset)
{
	uint64_t *mode = (uint64_t *)opt->value;
	const struct sample_reg *r;
	char *s, *os = NULL, *p;
	int ret = -1;

	if (unset)
		return 0;

	/*
	 * cannot set it twice
	 */
	if (*mode)
		return -1;

	/* str may be NULL in case no arg is passed to -I */
	if (str) {
		/* because str is read-only */
		s = os = strdup(str);
		if (!s)
			return -1;

		for (;;) {
			p = strchr(s, ',');
			if (p)
				*p = '\0';

			if (!strcmp(s, "?")) {
				fprintf(stderr, "available registers: ");
				for (r = sample_reg_masks; r->name; r++) {
					fprintf(stderr, "%s ", r->name);
				}
				fputc('\n', stderr);
				/* just printing available regs */
				return -1;
			}
			for (r = sample_reg_masks; r->name; r++) {
				if (!strcasecmp(s, r->name))
					break;
			}
			if (!r->name) {
				ui__warning("unknown register %s,"
					    " check man page\n", s);
				goto error;
			}

			*mode |= r->mask;

			if (!p)
				break;

			s = p + 1;
		}
	}
	ret = 0;

	/* default to all possible regs */
	if (*mode == 0)
		*mode = PERF_REGS_MASK;
error:
	free(os);
	return ret;
}
