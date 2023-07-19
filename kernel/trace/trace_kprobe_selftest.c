// SPDX-License-Identifier: GPL-2.0

#include "trace_kprobe_selftest.h"

/*
 * Function used during the kprobe self test. This function is in a separate
 * compile unit so it can be compile with CC_FLAGS_FTRACE to ensure that it
 * can be probed by the selftests.
 */
int kprobe_trace_selftest_target(int a1, int a2, int a3, int a4, int a5, int a6)
{
	return a1 + a2 + a3 + a4 + a5 + a6;
}
