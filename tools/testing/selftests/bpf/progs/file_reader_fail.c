// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <string.h>
#include <stdbool.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"

char _license[] SEC("license") = "GPL";

int err;
void *user_ptr;

SEC("lsm/file_open")
__failure
__msg("Unreleased reference id=")
int on_nanosleep_unreleased_ref(void *ctx)
{
	struct task_struct *task = bpf_get_current_task_btf();
	struct file *file = bpf_get_task_exe_file(task);
	struct bpf_dynptr dynptr;

	if (!file)
		return 0;

	err = bpf_dynptr_from_file(file, 0, &dynptr);
	return err ? 1 : 0;
}

SEC("xdp")
__failure
__msg("Expected a dynptr of type file as arg #0")
int xdp_wrong_dynptr_type(struct xdp_md *xdp)
{
	struct bpf_dynptr dynptr;

	bpf_dynptr_from_xdp(xdp, 0, &dynptr);
	bpf_dynptr_file_discard(&dynptr);
	return 0;
}

SEC("xdp")
__failure
__msg("Expected an initialized dynptr as arg #0")
int xdp_no_dynptr_type(struct xdp_md *xdp)
{
	struct bpf_dynptr dynptr;

	bpf_dynptr_file_discard(&dynptr);
	return 0;
}
