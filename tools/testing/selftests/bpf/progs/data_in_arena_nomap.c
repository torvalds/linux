// SPDX-License-Identifier: GPL-2.0

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include "bpf_arena_common.h"

/* global data of the object is in arena */
char data_in_arena SEC(".arena.data");

int counter = 5;
/* needs an arena map that is declared. The one that libbpf creates won't do. */
int __arena_global avar = 11;

SEC("syscall")
int arena_var(void *ctx)
{
	return avar + counter;
}

char _license[] SEC("license") = "GPL";
