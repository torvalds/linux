// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2023 Yafang Shao <laoar.shao@gmail.com> */

#include <string.h>
#include <linux/bpf.h>
#include <test_progs.h>
#include "test_ptr_untrusted.skel.h"

#define TP_NAME "sched_switch"

void serial_test_ptr_untrusted(void)
{
	struct test_ptr_untrusted *skel;
	int err;

	skel = test_ptr_untrusted__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		goto cleanup;

	/* First, attach lsm prog */
	skel->links.lsm_run = bpf_program__attach_lsm(skel->progs.lsm_run);
	if (!ASSERT_OK_PTR(skel->links.lsm_run, "lsm_attach"))
		goto cleanup;

	/* Second, attach raw_tp prog. The lsm prog will be triggered. */
	skel->links.raw_tp_run = bpf_program__attach_raw_tracepoint(skel->progs.raw_tp_run,
								    TP_NAME);
	if (!ASSERT_OK_PTR(skel->links.raw_tp_run, "raw_tp_attach"))
		goto cleanup;

	err = strncmp(skel->bss->tp_name, TP_NAME, strlen(TP_NAME));
	ASSERT_EQ(err, 0, "cmp_tp_name");

cleanup:
	test_ptr_untrusted__destroy(skel);
}
