// SPDX-License-Identifier: GPL-2.0

#include "test_progs.h"
#include "core_kern_overflow.lskel.h"

void test_core_kern_overflow_lskel(void)
{
	struct core_kern_overflow_lskel *skel;

	skel = core_kern_overflow_lskel__open_and_load();
	if (!ASSERT_NULL(skel, "open_and_load"))
		core_kern_overflow_lskel__destroy(skel);
}
