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

static void cssc_sigill(void)
{
	/* CNT x0, x0 */
	asm volatile(".inst 0xdac01c00" : : : "x0");
}

static void rng_sigill(void)
{
	asm volatile("mrs x0, S3_3_C2_C4_0" : : : "x0");
}

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

static void sve2_sigill(void)
{
	/* SQABS Z0.b, P0/M, Z0.B */
	asm volatile(".inst 0x4408A000" : : : "z0");
}

static void sve2p1_sigill(void)
{
	/* BFADD Z0.H, Z0.H, Z0.H */
	asm volatile(".inst 0x65000000" : : : "z0");
}

static void sveaes_sigill(void)
{
	/* AESD z0.b, z0.b, z0.b */
	asm volatile(".inst 0x4522e400" : : : "z0");
}

static void svepmull_sigill(void)
{
	/* PMULLB Z0.Q, Z0.D, Z0.D */
	asm volatile(".inst 0x45006800" : : : "z0");
}

static void svebitperm_sigill(void)
{
	/* BDEP Z0.B, Z0.B, Z0.B */
	asm volatile(".inst 0x4500b400" : : : "z0");
}

static void svesha3_sigill(void)
{
	/* EOR3 Z0.D, Z0.D, Z0.D, Z0.D */
	asm volatile(".inst 0x4203800" : : : "z0");
}

static void svesm4_sigill(void)
{
	/* SM4E Z0.S, Z0.S, Z0.S */
	asm volatile(".inst 0x4523e000" : : : "z0");
}

static void svei8mm_sigill(void)
{
	/* USDOT Z0.S, Z0.B, Z0.B[0] */
	asm volatile(".inst 0x44a01800" : : : "z0");
}

static void svef32mm_sigill(void)
{
	/* FMMLA Z0.S, Z0.S, Z0.S */
	asm volatile(".inst 0x64a0e400" : : : "z0");
}

static void svef64mm_sigill(void)
{
	/* FMMLA Z0.D, Z0.D, Z0.D */
	asm volatile(".inst 0x64e0e400" : : : "z0");
}

static void svebf16_sigill(void)
{
	/* BFCVT Z0.H, P0/M, Z0.S */
	asm volatile(".inst 0x658aa000" : : : "z0");
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
		.name = "CSSC",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_CSSC,
		.cpuinfo = "cssc",
		.sigill_fn = cssc_sigill,
	},
	{
		.name = "RNG",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_RNG,
		.cpuinfo = "rng",
		.sigill_fn = rng_sigill,
	},
	{
		.name = "RPRFM",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_RPRFM,
		.cpuinfo = "rprfm",
	},
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
	{
		.name = "SVE 2",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_SVE2,
		.cpuinfo = "sve2",
		.sigill_fn = sve2_sigill,
	},
	{
		.name = "SVE 2.1",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_SVE2P1,
		.cpuinfo = "sve2p1",
		.sigill_fn = sve2p1_sigill,
	},
	{
		.name = "SVE AES",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_SVEAES,
		.cpuinfo = "sveaes",
		.sigill_fn = sveaes_sigill,
	},
	{
		.name = "SVE2 PMULL",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_SVEPMULL,
		.cpuinfo = "svepmull",
		.sigill_fn = svepmull_sigill,
	},
	{
		.name = "SVE2 BITPERM",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_SVEBITPERM,
		.cpuinfo = "svebitperm",
		.sigill_fn = svebitperm_sigill,
	},
	{
		.name = "SVE2 SHA3",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_SVESHA3,
		.cpuinfo = "svesha3",
		.sigill_fn = svesha3_sigill,
	},
	{
		.name = "SVE2 SM4",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_SVESM4,
		.cpuinfo = "svesm4",
		.sigill_fn = svesm4_sigill,
	},
	{
		.name = "SVE2 I8MM",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_SVEI8MM,
		.cpuinfo = "svei8mm",
		.sigill_fn = svei8mm_sigill,
	},
	{
		.name = "SVE2 F32MM",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_SVEF32MM,
		.cpuinfo = "svef32mm",
		.sigill_fn = svef32mm_sigill,
	},
	{
		.name = "SVE2 F64MM",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_SVEF64MM,
		.cpuinfo = "svef64mm",
		.sigill_fn = svef64mm_sigill,
	},
	{
		.name = "SVE2 BF16",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_SVEBF16,
		.cpuinfo = "svebf16",
		.sigill_fn = svebf16_sigill,
	},
	{
		.name = "SVE2 EBF16",
		.at_hwcap = AT_HWCAP2,
		.hwcap_bit = HWCAP2_SVE_EBF16,
		.cpuinfo = "sveebf16",
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

		have_hwcap = getauxval(hwcap->at_hwcap) & hwcap->hwcap_bit;
		have_cpuinfo = cpuinfo_present(hwcap->cpuinfo);

		if (have_hwcap)
			ksft_print_msg("%s present\n", hwcap->name);

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
