// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Red hat */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "hid_bpf_helpers.h"

char _license[] SEC("license") = "GPL";

struct attach_prog_args {
	int prog_fd;
	unsigned int hid;
	int retval;
};

__u64 callback_check = 52;
__u64 callback2_check = 52;

SEC("?fmod_ret/hid_bpf_device_event")
int BPF_PROG(hid_first_event, struct hid_bpf_ctx *hid_ctx)
{
	__u8 *rw_data = hid_bpf_get_data(hid_ctx, 0 /* offset */, 3 /* size */);

	if (!rw_data)
		return 0; /* EPERM check */

	callback_check = rw_data[1];

	rw_data[2] = rw_data[1] + 5;

	return 0;
}

SEC("syscall")
int attach_prog(struct attach_prog_args *ctx)
{
	ctx->retval = hid_bpf_attach_prog(ctx->hid,
					  ctx->prog_fd,
					  0);
	return 0;
}
