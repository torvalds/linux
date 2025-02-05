// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include <network_helpers.h>
#include <preempt_lock.skel.h>

void test_preempt_lock(void)
{
	RUN_TESTS(preempt_lock);
}
