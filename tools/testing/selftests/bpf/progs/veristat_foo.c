// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

/*
 * Programs below exist only to exercise veristat's -f name filters,
 * their bodies are irrelevant, only the names matter.
 * This file is also included by veristat_bar.c, so that the same set of
 * program names is available in two differently named object files.
 */

SEC("socket")
int foo(void *ctx)
{
	return 0;
}

SEC("socket")
int bar(void *ctx)
{
	return 0;
}

SEC("socket")
int buz(void *ctx)
{
	return 0;
}

char _license[] SEC("license") = "GPL";
