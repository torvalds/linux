// SPDX-License-Identifier: GPL-2.0

#define BPF_NO_KFUNC_PROTOTYPES
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include "bpf_experimental.h"
#include <bpf_arena_common.h>

struct {
	__uint(type, BPF_MAP_TYPE_ARENA);
	__uint(map_flags, BPF_F_MMAPABLE);
	__uint(max_entries, 4);
} arena SEC(".maps");

/* global data of the object is in arena */
char data_in_arena SEC(".arena.data");

int counter = 5;
long sum;
const volatile int ro = 7;

#if defined(__BPF_FEATURE_ADDR_SPACE_CAST)
int __arena avar = 11;
#else
int avar = 11;
#endif

SEC("syscall")
int use_data(void *ctx)
{
	sum = counter + ro + avar;
	counter++;
	avar++;
	return sum;
}

char _license[] SEC("license") = "GPL";
