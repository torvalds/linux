// SPDX-License-Identifier: GPL-2.0-only
#include <stddef.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

__noinline int foo(int (*arr)[10])
{
	if (arr)
		return (*arr)[9];

	return 0;
}

SEC("cgroup_skb/ingress")
int test_cls(struct __sk_buff *skb)
{
	int array[10];

	const int rv = foo(&array);

	return rv ? 1 : 0;
}
