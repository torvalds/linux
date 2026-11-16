// SPDX-License-Identifier: GPL-2.0
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"
#include "bpf_experimental.h"

char _license[] SEC("license") = "GPL";

static __noinline int spins(void)
{
	__u64 t0 = bpf_ktime_get_ns();

	while (can_loop)
		;
	return bpf_ktime_get_ns() - t0 > 100 * 1000 * 1000;
}

/* Each call of an extension prog is an invocation: one of its two calls spins. */
SEC("freplace")
int replacement(int i)
{
	return spins() + spins() + i;
}
