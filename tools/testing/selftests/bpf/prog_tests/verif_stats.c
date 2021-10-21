// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */

#include <test_progs.h>

#include "trace_vprintk.lskel.h"

void test_verif_stats(void)
{
	__u32 len = sizeof(struct bpf_prog_info);
	struct bpf_prog_info info = {};
	struct trace_vprintk *skel;
	int err;

	skel = trace_vprintk__open_and_load();
	if (!ASSERT_OK_PTR(skel, "trace_vprintk__open_and_load"))
		goto cleanup;

	err = bpf_obj_get_info_by_fd(skel->progs.sys_enter.prog_fd, &info, &len);
	if (!ASSERT_OK(err, "bpf_obj_get_info_by_fd"))
		goto cleanup;

	if (!ASSERT_GT(info.verified_insns, 0, "verified_insns"))
		goto cleanup;

cleanup:
	trace_vprintk__destroy(skel);
}
