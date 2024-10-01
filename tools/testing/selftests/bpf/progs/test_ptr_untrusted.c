// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2023 Yafang Shao <laoar.shao@gmail.com> */

#include "vmlinux.h"
#include <bpf/bpf_tracing.h>

char tp_name[128];

SEC("lsm.s/bpf")
int BPF_PROG(lsm_run, int cmd, union bpf_attr *attr, unsigned int size)
{
	switch (cmd) {
	case BPF_RAW_TRACEPOINT_OPEN:
		bpf_copy_from_user(tp_name, sizeof(tp_name) - 1,
				   (void *)attr->raw_tracepoint.name);
		break;
	default:
		break;
	}
	return 0;
}

SEC("raw_tracepoint")
int BPF_PROG(raw_tp_run)
{
	return 0;
}

char _license[] SEC("license") = "GPL";
