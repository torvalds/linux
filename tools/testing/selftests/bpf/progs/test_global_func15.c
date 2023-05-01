// SPDX-License-Identifier: GPL-2.0-only
#include <stddef.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

__noinline int foo(unsigned int *v)
{
	if (v)
		*v = bpf_get_prandom_u32();

	return 0;
}

SEC("cgroup_skb/ingress")
__failure __msg("At program exit the register R0 has value")
int global_func15(struct __sk_buff *skb)
{
	unsigned int v = 1;

	foo(&v);

	return v;
}
