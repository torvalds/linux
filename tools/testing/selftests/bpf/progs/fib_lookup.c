// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Meta Platforms, Inc. and affiliates. */

#include <linux/types.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_tracing_net.h"

struct bpf_fib_lookup fib_params = {};
int fib_lookup_ret = 0;
int lookup_flags = 0;

SEC("tc")
int fib_lookup(struct __sk_buff *skb)
{
	fib_lookup_ret = bpf_fib_lookup(skb, &fib_params, sizeof(fib_params),
					lookup_flags);

	return TC_ACT_SHOT;
}

char _license[] SEC("license") = "GPL";
