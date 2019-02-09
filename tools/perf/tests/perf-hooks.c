// SPDX-License-Identifier: GPL-2.0
#include <signal.h>
#include <stdlib.h>

#include "tests.h"
#include "debug.h"
#include "util.h"
#include "perf-hooks.h"

static void sigsegv_handler(int sig __maybe_unused)
{
	pr_debug("SIGSEGV is observed as expected, try to recover.\n");
	perf_hooks__recover();
	signal(SIGSEGV, SIG_DFL);
	raise(SIGSEGV);
	exit(-1);
}


static void the_hook(void *_hook_flags)
{
	int *hook_flags = _hook_flags;
	int *p = NULL;

	*hook_flags = 1234;

	/* Generate a segfault, test perf_hooks__recover */
	*p = 0;
}

int test__perf_hooks(struct test *test __maybe_unused, int subtest __maybe_unused)
{
	int hook_flags = 0;

	signal(SIGSEGV, sigsegv_handler);
	perf_hooks__set_hook("test", the_hook, &hook_flags);
	perf_hooks__invoke_test();

	/* hook is triggered? */
	if (hook_flags != 1234) {
		pr_debug("Setting failed: %d (%p)\n", hook_flags, &hook_flags);
		return TEST_FAIL;
	}

	/* the buggy hook is removed? */
	if (perf_hooks__get_hook("test"))
		return TEST_FAIL;
	return TEST_OK;
}
