// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020 Facebook
#define _GNU_SOURCE
#include <test_progs.h>
#include "test_core_retro.skel.h"

void test_core_retro(void)
{
	int err, zero = 0, res, my_pid = getpid();
	struct test_core_retro *skel;

	/* load program */
	skel = test_core_retro__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_load"))
		goto out_close;

	err = bpf_map__update_elem(skel->maps.exp_tgid_map, &zero, sizeof(zero),
				   &my_pid, sizeof(my_pid), 0);
	if (!ASSERT_OK(err, "map_update"))
		goto out_close;

	/* attach probe */
	err = test_core_retro__attach(skel);
	if (!ASSERT_OK(err, "attach_kprobe"))
		goto out_close;

	/* trigger */
	usleep(1);

	err = bpf_map__lookup_elem(skel->maps.results, &zero, sizeof(zero), &res, sizeof(res), 0);
	if (!ASSERT_OK(err, "map_lookup"))
		goto out_close;

	ASSERT_EQ(res, my_pid, "pid_check");

out_close:
	test_core_retro__destroy(skel);
}
