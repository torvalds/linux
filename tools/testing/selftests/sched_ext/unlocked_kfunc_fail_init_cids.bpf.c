// SPDX-License-Identifier: GPL-2.0
/*
 * Verify that the BPF verifier rejects scx_bpf_create_dsq() from
 * ops.init_cids().
 *
 * Copyright (c) 2026 Google LLC.
 */

#include <scx/common.bpf.h>

#define SHARED_DSQ	0

char _license[] SEC("license") = "GPL";

s32 BPF_STRUCT_OPS_SLEEPABLE(unlocked_kfunc_fail_init_cids_cb)
{
	return scx_bpf_create_dsq(SHARED_DSQ, -1);
}

SCX_OPS_CID_DEFINE(unlocked_kfunc_fail_init_cids_ops,
		   .init_cids		= (void *)unlocked_kfunc_fail_init_cids_cb,
		   .name		= "unlocked_kfunc_fail_init_cids");
