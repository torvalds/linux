// SPDX-License-Identifier: GPL-2.0-only
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>

__noinline int foo(int *p)
{
	return p ? (*p = 42) : 0;
}

const volatile int i;

SEC("tc")
int test_cls(struct __sk_buff *skb)
{
	return foo((int *)&i);
}
