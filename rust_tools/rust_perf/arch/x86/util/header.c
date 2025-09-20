// SPDX-License-Identifier: GPL-2.0
#include <sys/types.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>

#include "../../../util/debug.h"
#include "../../../util/header.h"
#include "cpuid.h"

void get_cpuid_0(char *vendor, unsigned int *lvl)
{
	unsigned int b, c, d;

	cpuid(0, 0, lvl, &b, &c, &d);
	strncpy(&vendor[0], (char *)(&b), 4);
	strncpy(&vendor[4], (char *)(&d), 4);
	strncpy(&vendor[8], (char *)(&c), 4);
	vendor[12] = '\0';
}

static int
__get_cpuid(char *buffer, size_t sz, const char *fmt)
{
	unsigned int a, b, c, d, lvl;
	int family = -1, model = -1, step = -1;
	int nb;
	char vendor[16];

	get_cpuid_0(vendor, &lvl);

	if (lvl >= 1) {
		cpuid(1, 0, &a, &b, &c, &d);

		family = (a >> 8) & 0xf;  /* bits 11 - 8 */
		model  = (a >> 4) & 0xf;  /* Bits  7 - 4 */
		step   = a & 0xf;

		/* extended family */
		if (family == 0xf)
			family += (a >> 20) & 0xff;

		/* extended model */
		if (family >= 0x6)
			model += ((a >> 16) & 0xf) << 4;
	}
	nb = scnprintf(buffer, sz, fmt, vendor, family, model, step);

	/* look for end marker to ensure the entire data fit */
	if (strchr(buffer, '$')) {
		buffer[nb-1] = '\0';
		return 0;
	}
	return ENOBUFS;
}

int
get_cpuid(char *buffer, size_t sz, struct perf_cpu cpu __maybe_unused)
{
	return __get_cpuid(buffer, sz, "%s,%u,%u,%u$");
}

char *get_cpuid_str(struct perf_cpu cpu __maybe_unused)
{
	char *buf = malloc(128);

	if (buf && __get_cpuid(buf, 128, "%s-%u-%X-%X$") < 0) {
		free(buf);
		return NULL;
	}
	return buf;
}

/* Full CPUID format for x86 is vendor-family-model-stepping */
static bool is_full_cpuid(const char *id)
{
	const char *tmp = id;
	int count = 0;

	while ((tmp = strchr(tmp, '-')) != NULL) {
		count++;
		tmp++;
	}

	if (count == 3)
		return true;

	return false;
}

int strcmp_cpuid_str(const char *mapcpuid, const char *id)
{
	regex_t re;
	regmatch_t pmatch[1];
	int match;
	bool full_mapcpuid = is_full_cpuid(mapcpuid);
	bool full_cpuid = is_full_cpuid(id);

	/*
	 * Full CPUID format is required to identify a platform.
	 * Error out if the cpuid string is incomplete.
	 */
	if (full_mapcpuid && !full_cpuid) {
		pr_info("Invalid CPUID %s. Full CPUID is required, "
			"vendor-family-model-stepping\n", id);
		return 1;
	}

	if (regcomp(&re, mapcpuid, REG_EXTENDED) != 0) {
		/* Warn unable to generate match particular string. */
		pr_info("Invalid regular expression %s\n", mapcpuid);
		return 1;
	}

	match = !regexec(&re, id, 1, pmatch, 0);
	regfree(&re);
	if (match) {
		size_t match_len = (pmatch[0].rm_eo - pmatch[0].rm_so);
		size_t cpuid_len;

		/* If the full CPUID format isn't required,
		 * ignoring the stepping.
		 */
		if (!full_mapcpuid && full_cpuid)
			cpuid_len = strrchr(id, '-') - id;
		else
			cpuid_len = strlen(id);

		/* Verify the entire string matched. */
		if (match_len == cpuid_len)
			return 0;
	}

	return 1;
}
