// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Facebook */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

const volatile int my_pid;

bool abc1_called;
bool abc2_called;
bool custom1_called;
bool custom2_called;
bool kprobe1_called;
bool xyz_called;

SEC("abc")
int abc1(void *ctx)
{
	abc1_called = true;
	return 0;
}

SEC("abc/whatever")
int abc2(void *ctx)
{
	abc2_called = true;
	return 0;
}

SEC("custom")
int custom1(void *ctx)
{
	custom1_called = true;
	return 0;
}

SEC("custom/something")
int custom2(void *ctx)
{
	custom2_called = true;
	return 0;
}

SEC("kprobe")
int kprobe1(void *ctx)
{
	kprobe1_called = true;
	return 0;
}

SEC("xyz/blah")
int xyz(void *ctx)
{
	int whatever;

	/* use sleepable helper, custom handler should set sleepable flag */
	bpf_copy_from_user(&whatever, sizeof(whatever), NULL);
	xyz_called = true;
	return 0;
}

char _license[] SEC("license") = "GPL";
