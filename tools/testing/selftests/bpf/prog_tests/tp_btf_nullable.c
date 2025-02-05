// SPDX-License-Identifier: GPL-2.0

#include <test_progs.h>
#include "test_tp_btf_nullable.skel.h"

void test_tp_btf_nullable(void)
{
	if (!env.has_testmod) {
		test__skip();
		return;
	}

	RUN_TESTS(test_tp_btf_nullable);
}
