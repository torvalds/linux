// SPDX-License-Identifier: GPL-2.0
#include <linux/compiler.h>

#include "arch-tests.h"
#include "tests/tests.h"
#include "util/header.h"

int test__cpuid_match(struct test_suite *test __maybe_unused,
			     int subtest __maybe_unused)
{
	/* midr with no leading zeros matches */
	if (strcmp_cpuid_str("0x410fd0c0", "0x00000000410fd0c0"))
		return -1;
	/* Upper case matches */
	if (strcmp_cpuid_str("0x410fd0c0", "0x00000000410FD0C0"))
		return -1;
	/* r0p0 = r0p0 matches */
	if (strcmp_cpuid_str("0x00000000410fd480", "0x00000000410fd480"))
		return -1;
	/* r0p1 > r0p0 matches */
	if (strcmp_cpuid_str("0x00000000410fd480", "0x00000000410fd481"))
		return -1;
	/* r1p0 > r0p0 matches*/
	if (strcmp_cpuid_str("0x00000000410fd480", "0x00000000411fd480"))
		return -1;
	/* r0p0 < r0p1 doesn't match */
	if (!strcmp_cpuid_str("0x00000000410fd481", "0x00000000410fd480"))
		return -1;
	/* r0p0 < r1p0 doesn't match */
	if (!strcmp_cpuid_str("0x00000000411fd480", "0x00000000410fd480"))
		return -1;
	/* Different CPU doesn't match */
	if (!strcmp_cpuid_str("0x00000000410fd4c0", "0x00000000430f0af0"))
		return -1;

	return 0;
}
