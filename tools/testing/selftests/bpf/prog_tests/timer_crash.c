// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include "timer_crash.skel.h"

enum {
	MODE_ARRAY,
	MODE_HASH,
};

static void test_timer_crash_mode(int mode)
{
	struct timer_crash *skel;

	skel = timer_crash__open_and_load();
	if (!ASSERT_OK_PTR(skel, "timer_crash__open_and_load"))
		return;
	skel->bss->pid = getpid();
	skel->bss->crash_map = mode;
	if (!ASSERT_OK(timer_crash__attach(skel), "timer_crash__attach"))
		goto end;
	usleep(1);
end:
	timer_crash__destroy(skel);
}

void test_timer_crash(void)
{
	if (test__start_subtest("array"))
		test_timer_crash_mode(MODE_ARRAY);
	if (test__start_subtest("hash"))
		test_timer_crash_mode(MODE_HASH);
}
