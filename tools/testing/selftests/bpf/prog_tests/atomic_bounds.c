// SPDX-License-Identifier: GPL-2.0

#include <test_progs.h>

#include "atomic_bounds.skel.h"

void test_atomic_bounds(void)
{
	struct atomic_bounds *skel;
	__u32 duration = 0;

	skel = atomic_bounds__open_and_load();
	if (CHECK(!skel, "skel_load", "couldn't load program\n"))
		return;

	atomic_bounds__destroy(skel);
}
