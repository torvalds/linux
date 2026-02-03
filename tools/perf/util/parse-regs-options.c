// SPDX-License-Identifier: GPL-2.0
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "util/debug.h"
#include <dwarf-regs.h>
#include <subcmd/parse-options.h>
#include "util/perf_regs.h"
#include "util/parse-regs-options.h"

static void list_perf_regs(FILE *fp, uint64_t mask)
{
	const char *last_name = NULL;

	fprintf(fp, "available registers: ");
	for (int reg = 0; reg < 64; reg++) {
		const char *name;

		if (((1ULL << reg) & mask) == 0)
			continue;

		name = perf_reg_name(reg, EM_HOST, EF_HOST);
		if (name && (!last_name || strcmp(last_name, name)))
			fprintf(fp, "%s%s", reg > 0 ? " " : "", name);
		last_name = name;
	}
	fputc('\n', fp);
}

static uint64_t name_to_perf_reg_mask(const char *to_match, uint64_t mask)
{
	uint64_t reg_mask = 0;

	for (int reg = 0; reg < 64; reg++) {
		const char *name;

		if (((1ULL << reg) & mask) == 0)
			continue;

		name = perf_reg_name(reg, EM_HOST, EF_HOST);
		if (!name)
			continue;

		if (!strcasecmp(to_match, name))
			reg_mask |= 1ULL << reg;
	}
	return reg_mask;
}

static int
__parse_regs(const struct option *opt, const char *str, int unset, bool intr)
{
	uint64_t *mode = (uint64_t *)opt->value;
	char *s, *os = NULL, *p;
	int ret = -1;
	uint64_t mask;

	if (unset)
		return 0;

	/*
	 * cannot set it twice
	 */
	if (*mode)
		return -1;

	mask = intr ? perf_intr_reg_mask(EM_HOST) : perf_user_reg_mask(EM_HOST);

	/* str may be NULL in case no arg is passed to -I */
	if (!str) {
		*mode = mask;
		return 0;
	}

	/* because str is read-only */
	s = os = strdup(str);
	if (!s)
		return -1;

	for (;;) {
		uint64_t reg_mask;

		p = strchr(s, ',');
		if (p)
			*p = '\0';

		if (!strcmp(s, "?")) {
			list_perf_regs(stderr, mask);
			goto error;
		}

		reg_mask = name_to_perf_reg_mask(s, mask);
		if (reg_mask == 0) {
			ui__warning("Unknown register \"%s\", check man page or run \"perf record %s?\"\n",
				s, intr ? "-I" : "--user-regs=");
			goto error;
		}
		*mode |= reg_mask;

		if (!p)
			break;

		s = p + 1;
	}
	ret = 0;

error:
	free(os);
	return ret;
}

int
parse_user_regs(const struct option *opt, const char *str, int unset)
{
	return __parse_regs(opt, str, unset, false);
}

int
parse_intr_regs(const struct option *opt, const char *str, int unset)
{
	return __parse_regs(opt, str, unset, true);
}
