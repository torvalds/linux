// SPDX-License-Identifier: GPL-2.0-only
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

__noinline int foo(int *p)
{
	return p ? (*p = 42) : 0;
}

const volatile int i;

SEC("tc")
__failure __msg("Caller passes invalid args into func#1")
int global_func17(struct __sk_buff *skb)
{
	return foo((int *)&i);
}
