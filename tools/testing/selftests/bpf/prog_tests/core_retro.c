// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020 Facebook
#define _GNU_SOURCE
#include <test_progs.h>
#include "test_core_retro.skel.h"

void test_core_retro(void)
{
	int err, zero = 0, res, duration = 0, my_pid = getpid();
	struct test_core_retro *skel;

	/* load program */
	skel = test_core_retro__open_and_load();
	if (CHECK(!skel, "skel_load", "skeleton open/load failed\n"))
		goto out_close;

	err = bpf_map_update_elem(bpf_map__fd(skel->maps.exp_tgid_map), &zero, &my_pid, 0);
	if (CHECK(err, "map_update", "failed to set expected PID: %d\n", errno))
		goto out_close;

	/* attach probe */
	err = test_core_retro__attach(skel);
	if (CHECK(err, "attach_kprobe", "err %d\n", err))
		goto out_close;

	/* trigger */
	usleep(1);

	err = bpf_map_lookup_elem(bpf_map__fd(skel->maps.results), &zero, &res);
	if (CHECK(err, "map_lookup", "failed to lookup result: %d\n", errno))
		goto out_close;

	CHECK(res != my_pid, "pid_check", "got %d != exp %d\n", res, my_pid);

out_close:
	test_core_retro__destroy(skel);
}
