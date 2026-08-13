// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Isovalent */

#include <unistd.h>

#include "test_progs.h"
#include "ksock_wq.skel.h"

#define CALLBACK_WAIT_RETRIES 1000
#define CALLBACK_WAIT_US 1000

void test_ksock_wq(void)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts);
	struct ksock_wq *skel;
	u32 callback_done;
	int err, i;

	skel = ksock_wq__open_and_load();
	if (!ASSERT_OK_PTR(skel, "ksock_wq open and load"))
		return;

	err = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.ksock_wq_start),
				     &opts);
	if (!ASSERT_OK(err, "run ksock_wq_start"))
		goto out;
	if (!ASSERT_OK(opts.retval, "ksock_wq_start retval"))
		goto out;

	for (i = 0; i < CALLBACK_WAIT_RETRIES; i++) {
		if (__atomic_load_n(&skel->bss->callback_done, __ATOMIC_ACQUIRE))
			break;
		usleep(CALLBACK_WAIT_US);
	}
	callback_done = __atomic_load_n(&skel->bss->callback_done,
					__ATOMIC_ACQUIRE);
	if (!ASSERT_EQ(callback_done, 1, "workqueue callback completed"))
		goto out;

	ASSERT_EQ(skel->bss->create_err, -EOPNOTSUPP,
		  "workqueue create rejected");

out:
	ksock_wq__destroy(skel);
}
