// SPDX-License-Identifier: GPL-2.0-only
#include <stddef.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

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
__failure __msg("Caller passes invalid args into func#1")
int global_func13(struct __sk_buff *skb)
{
	const struct S *s = (const struct S *)(0xbedabeda);

	return foo(s);
}
