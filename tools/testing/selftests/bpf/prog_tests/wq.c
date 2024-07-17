// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Benjamin Tissoires */
#include <test_progs.h>
#include "wq.skel.h"
#include "wq_failures.skel.h"

void serial_test_wq(void)
{
	struct wq *wq_skel = NULL;
	int err, prog_fd;

	LIBBPF_OPTS(bpf_test_run_opts, topts);

	RUN_TESTS(wq);

	/* re-run the success test to check if the timer was actually executed */

	wq_skel = wq__open_and_load();
	if (!ASSERT_OK_PTR(wq_skel, "wq_skel_load"))
		return;

	err = wq__attach(wq_skel);
	if (!ASSERT_OK(err, "wq_attach"))
		return;

	prog_fd = bpf_program__fd(wq_skel->progs.test_syscall_array_sleepable);
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "test_run");
	ASSERT_EQ(topts.retval, 0, "test_run");

	usleep(50); /* 10 usecs should be enough, but give it extra */

	ASSERT_EQ(wq_skel->bss->ok_sleepable, (1 << 1), "ok_sleepable");
	wq__destroy(wq_skel);
}

void serial_test_failures_wq(void)
{
	RUN_TESTS(wq_failures);
}
