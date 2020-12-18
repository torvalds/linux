// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020 Facebook

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>

char _license[] SEC("license") = "GPL";

/* shuffled layout for relocatable (CO-RE) reads */
struct callback_head___shuffled {
	void (*func)(struct callback_head___shuffled *head);
	struct callback_head___shuffled *next;
};

struct callback_head k_probe_in = {};
struct callback_head___shuffled k_core_in = {};

struct callback_head *u_probe_in = 0;
struct callback_head___shuffled *u_core_in = 0;

long k_probe_out = 0;
long u_probe_out = 0;

long k_core_out = 0;
long u_core_out = 0;

int my_pid = 0;

SEC("raw_tracepoint/sys_enter")
int handler(void *ctx)
{
	int pid = bpf_get_current_pid_tgid() >> 32;

	if (my_pid != pid)
		return 0;

	/* next pointers for kernel address space have to be initialized from
	 * BPF side, user-space mmaped addresses are stil user-space addresses
	 */
	k_probe_in.next = &k_probe_in;
	__builtin_preserve_access_index(({k_core_in.next = &k_core_in;}));

	k_probe_out = (long)BPF_PROBE_READ(&k_probe_in, next, next, func);
	k_core_out = (long)BPF_CORE_READ(&k_core_in, next, next, func);
	u_probe_out = (long)BPF_PROBE_READ_USER(u_probe_in, next, next, func);
	u_core_out = (long)BPF_CORE_READ_USER(u_core_in, next, next, func);

	return 0;
}
