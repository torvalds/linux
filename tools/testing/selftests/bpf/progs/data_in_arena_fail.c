// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

/* global data of the object is in arena */
char data_in_arena SEC(".arena.data");

int x = 42;
/* the table is read-only data with pointers. It's not in arena. */
int *const volatile tbl[1] SEC(".data.rel.ro") = { &x };
int *const volatile *pp = tbl;

SEC("syscall")
int ptr_to_map(void *ctx)
{
	return **pp;
}

char _license[] SEC("license") = "GPL";
