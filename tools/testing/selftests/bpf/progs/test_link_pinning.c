// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */

#include <stdbool.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

int in = 0;
int out = 0;

SEC("raw_tp/sys_enter")
int raw_tp_prog(const void *ctx)
{
	out = in;
	return 0;
}

SEC("tp_btf/sys_enter")
int tp_btf_prog(const void *ctx)
{
	out = in;
	return 0;
}

char _license[] SEC("license") = "GPL";
