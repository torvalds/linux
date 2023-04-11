// SPDX-License-Identifier: GPL-2.0-only
#include <stddef.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

struct Small {
	long x;
};

struct Big {
	long x;
	long y;
};

__noinline int foo(const struct Big *big)
{
	if (!big)
		return 0;

	return bpf_get_prandom_u32() < big->y;
}

SEC("cgroup_skb/ingress")
__failure __msg("invalid indirect access to stack")
int global_func10(struct __sk_buff *skb)
{
	const struct Small small = {.x = skb->len };

	return foo((struct Big *)&small) ? 1 : 0;
}
