// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Google LLC. */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

/* The format string is filled from the userspace such that loading fails */
static const char fmt[10];

SEC("raw_tp/sys_enter")
int handler(const void *ctx)
{
	unsigned long long arg = 42;

	bpf_snprintf(NULL, 0, fmt, &arg, sizeof(arg));

	return 0;
}

char _license[] SEC("license") = "GPL";
