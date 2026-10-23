// SPDX-License-Identifier: GPL-2.0

#include <test_progs.h>
#include "sysctl_helpers.h"
#include "verifier_arena_scalar.skel.h"

/* The same tests with constants of the programs blinded */
void serial_test_arena_scalar_blinded(void)
{
	const char *harden = "/proc/sys/net/core/bpf_jit_harden";
	char old[16] = {};

	if (!is_jit_enabled()) {
		test__skip();
		return;
	}
	if (sysctl_set_or_fail(harden, old, "2"))
		return;
	RUN_TESTS(verifier_arena_scalar);
	sysctl_set_or_fail(harden, NULL, old);
}
