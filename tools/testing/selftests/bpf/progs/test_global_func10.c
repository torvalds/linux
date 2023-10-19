// SPDX-License-Identifier: GPL-2.0-only
#include <stddef.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

struct Small {
	int x;
};

struct Big {
	int x;
	int y;
};

__noinline int foo(const struct Big *big)
{
	if (!big)
		return 0;

	return bpf_get_prandom_u32() < big->y;
}

SEC("cgroup_skb/ingress")
int test_cls(struct __sk_buff *skb)
{
	const struct Small small = {.x = skb->len };

	return foo((struct Big *)&small) ? 1 : 0;
}
