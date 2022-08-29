// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 ARM Limited.
 */

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/auxv.h>
#include <sys/prctl.h>
#include <asm/hwcap.h>
#include <asm/sigcontext.h>
#include <asm/unistd.h>

#include "../../kselftest.h"

#define TESTS_PER_HWCAP 2

/*
 * Function expected to generate SIGILL when the feature is not
 * supported and return when it is supported. If SIGILL is generated
 * then the handler must be able to skip over the instruction safely.
 *
 * Note that it is expected that for many architecture extensions
 * there are no specific traps due to no architecture state being
 * added so we may not fault if running on a kernel which doesn't know
 * to add the hwcap.
 */
typedef void (*sigill_fn)(void);

static void sme_sigill(void)
{
	/* RDSVL x0, #0 */
	asm volatile(".inst 0x04bf5800" : : : "x0");
}

static void sve_sigill(void)
{
	/* RDVL x0, #0 */
	asm volatile(".inst 0x04bf5000" : : : "x0");
}

static const struct hwcap_data {
	const char *name;
	unsigned long at_hwcap;
	unsigned long hwcap_bit;
	const char *cpuinfo;
	sigill_fn sigill_fn;
	bool sigill_reliable;
} hwcaps[] = {
	{
		.name = "SME",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_SME,
		.cpuinfo = "sme",
		.sigill_fn = sme_sigill,
		.sigill_reliable = true,
	},
	{
		.name = "SVE",
		.at_hwcap = AT_HWCAP,
		.hwcap_bit = HWCAP_SVE,
		.cpuinfo = "sve",
		.sigill_fn = sve_sigill,
		.sigill_reliable = true,
	},
};

static bool seen_sigill;

static void handle_sigill(int sig, siginfo_t *info, void *context)
{
	ucontext_t *uc = context;

	seen_sigill = true;

	/* Skip over the offending instruction */
	uc->uc_mcontext.pc += 4;
}

bool cpuinfo_present(const char *name)
{
	FILE *f;
	char buf[2048], name_space[30], name_newline[30];
	char *s;

	/*
	 * The feature should appear with a leading space and either a
	 * trailing space or a newline.
	 */
	snprintf(name_space, sizeof(name_space), " %s ", name);
	snprintf(name_newline, sizeof(name_newline), " %s\n", name);

	f = fopen("/proc/cpuinfo", "r");
	if (!f) {
		ksft_print_msg("Failed to open /proc/cpuinfo\n");
		return false;
	}

	while (fgets(buf, sizeof(buf), f)) {
		/* Features: line? */
		if (strncmp(buf, "Features\t:", strlen("Features\t:")) != 0)
			continue;

		/* All CPUs should be symmetric, don't read any more */
		fclose(f);

		s = strstr(buf, name_space);
		if (s)
			return true;
		s = strstr(buf, name_newline);
		if (s)
			return true;

		return false;
	}

	ksft_print_msg("Failed to find Features in /proc/cpuinfo\n");
	fclose(f);
	return false;
}

int main(void)
{
	const struct hwcap_data *hwcap;
	int i, ret;
	bool have_cpuinfo, have_hwcap;
	struct sigaction sa;

	ksft_print_header();
	ksft_set_plan(ARRAY_SIZE(hwcaps) * TESTS_PER_HWCAP);

	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = handle_sigill;
	sa.sa_flags = SA_RESTART | SA_SIGINFO;
	sigemptyset(&sa.sa_mask);
	ret = sigaction(SIGILL, &sa, NULL);
	if (ret < 0)
		ksft_exit_fail_msg("Failed to install SIGILL handler: %s (%d)\n",
				   strerror(errno), errno);

	for (i = 0; i < ARRAY_SIZE(hwcaps); i++) {
		hwcap = &hwcaps[i];

		have_hwcap = getauxval(hwcaps->at_hwcap) & hwcap->hwcap_bit;
		have_cpuinfo = cpuinfo_present(hwcap->cpuinfo);

		if (have_hwcap)
			ksft_print_msg("%s present", hwcap->name);

		ksft_test_result(have_hwcap == have_cpuinfo,
				 "cpuinfo_match_%s\n", hwcap->name);

		if (hwcap->sigill_fn) {
			seen_sigill = false;
			hwcap->sigill_fn();

			if (have_hwcap) {
				/* Should be able to use the extension */
				ksft_test_result(!seen_sigill, "sigill_%s\n",
						 hwcap->name);
			} else if (hwcap->sigill_reliable) {
				/* Guaranteed a SIGILL */
				ksft_test_result(seen_sigill, "sigill_%s\n",
						 hwcap->name);
			} else {
				/* Missing SIGILL might be fine */
				ksft_print_msg("SIGILL %sreported for %s\n",
					       seen_sigill ? "" : "not ",
					       hwcap->name);
				ksft_test_result_skip("sigill_%s\n",
						      hwcap->name);
			}
		} else {
			ksft_test_result_skip("sigill_%s\n",
					      hwcap->name);
		}
	}

	ksft_print_cnts();

	return 0;
}
