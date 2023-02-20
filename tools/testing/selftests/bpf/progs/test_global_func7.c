// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2020 Facebook */
#include <stddef.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

__attribute__ ((noinline))
void foo(struct __sk_buff *skb)
{
	skb->tc_index = 0;
}

SEC("tc")
__failure __msg("foo() doesn't return scalar")
int global_func7(struct __sk_buff *skb)
{
	foo(skb);
	return 0;
}
