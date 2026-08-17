// SPDX-License-Identifier: GPL-2.0
#include <linux/compiler.h>
#include "../tests.h"

int dt_work = 1234;

static void function1(void)
{
	dt_work += 7;
	dt_work += 7;
	dt_work += 7;
}

static void function2(void)
{
	dt_work += 7;
	dt_work += 7;
	dt_work += 7;
}

static int deterministic(int argc __maybe_unused,
			 const char **argv __maybe_unused)
{
	dt_work += 7;
	dt_work += 7;
	dt_work += 7;

	function1();

	dt_work += 7;
	dt_work += 7;
	dt_work += 7;

	function2();

	return 0;
}

DEFINE_WORKLOAD(deterministic);
