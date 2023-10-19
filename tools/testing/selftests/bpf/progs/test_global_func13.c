// SPDX-License-Identifier: GPL-2.0-only
#include <stddef.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

struct S {
	int x;
};

__noinline int foo(const struct S *s)
{
	if (s)
		return bpf_get_prandom_u32() < s->x;

	return 0;
}

SEC("cgroup_skb/ingress")
int test_cls(struct __sk_buff *skb)
{
	const struct S *s = (const struct S *)(0xbedabeda);

	return foo(s);
}
