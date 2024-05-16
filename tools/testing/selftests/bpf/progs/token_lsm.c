// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

int my_pid;
bool reject_capable;
bool reject_cmd;

SEC("lsm/bpf_token_capable")
int BPF_PROG(token_capable, struct bpf_token *token, int cap)
{
	if (my_pid == 0 || my_pid != (bpf_get_current_pid_tgid() >> 32))
		return 0;
	if (reject_capable)
		return -1;
	return 0;
}

SEC("lsm/bpf_token_cmd")
int BPF_PROG(token_cmd, struct bpf_token *token, enum bpf_cmd cmd)
{
	if (my_pid == 0 || my_pid != (bpf_get_current_pid_tgid() >> 32))
		return 0;
	if (reject_cmd)
		return -1;
	return 0;
}
