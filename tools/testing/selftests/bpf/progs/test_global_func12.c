// SPDX-License-Identifier: GPL-2.0-only
#include <stddef.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

struct S {
	int x;
};

__noinline int foo(const struct S *s)
{
	return bpf_get_prandom_u32() < s->x;
}

SEC("cgroup_skb/ingress")
int test_cls(struct __sk_buff *skb)
{
	const struct S s = {.x = skb->len };

	return foo(&s);
}
