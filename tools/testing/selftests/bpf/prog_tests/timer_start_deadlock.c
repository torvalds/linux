// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */
#include <test_progs.h>
#include "timer_start_deadlock.skel.h"

void test_timer_start_deadlock(void)
{
	struct timer_start_deadlock *skel;
	int err, prog_fd;
	LIBBPF_OPTS(bpf_test_run_opts, opts);

	skel = timer_start_deadlock__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open_and_load"))
		return;

	err = timer_start_deadlock__attach(skel);
	if (!ASSERT_OK(err, "skel_attach"))
		goto cleanup;

	prog_fd = bpf_program__fd(skel->progs.start_timer);

	/*
	 * Run the syscall program that attempts to deadlock.
	 * If the kernel deadlocks, this call will never return.
	 */
	err = bpf_prog_test_run_opts(prog_fd, &opts);
	ASSERT_OK(err, "prog_test_run");
	ASSERT_EQ(opts.retval, 0, "prog_retval");

	ASSERT_EQ(skel->bss->tp_called, 1, "tp_called");
cleanup:
	timer_start_deadlock__destroy(skel);
}
