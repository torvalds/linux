// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

bool prog1_called = false;
bool prog2_called = false;
bool prog3_called = false;

SEC("raw_tp/sys_enter")
int prog1(const void *ctx)
{
	prog1_called = true;
	return 0;
}

SEC("raw_tp/sys_exit")
int prog2(const void *ctx)
{
	prog2_called = true;
	return 0;
}

struct fake_kernel_struct {
	int whatever;
} __attribute__((preserve_access_index));

SEC("fentry/unexisting-kprobe-will-fail-if-loaded")
int prog3(const void *ctx)
{
	struct fake_kernel_struct *fake = (void *)ctx;
	fake->whatever = 123;
	prog3_called = true;
	return 0;
}

char _license[] SEC("license") = "GPL";
