// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */
#define BPF_NO_KFUNC_PROTOTYPES
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include "bpf_experimental.h"

struct {
	__uint(type, BPF_MAP_TYPE_ARENA);
	__uint(map_flags, BPF_F_MMAPABLE);
	__uint(max_entries, 100); /* number of pages */
} arena SEC(".maps");

#include "bpf_arena_htab.h"

void __arena *htab_for_user;
bool skip = false;

int zero = 0;
char __arena arr1[100000];
char arr2[1000];

SEC("syscall")
int arena_htab_llvm(void *ctx)
{
#if defined(__BPF_FEATURE_ADDR_SPACE_CAST) || defined(BPF_ARENA_FORCE_ASM)
	struct htab __arena *htab;
	char __arena *arr = arr1;
	__u64 i;

	htab = bpf_alloc(sizeof(*htab));
	cast_kern(htab);
	htab_init(htab);

	cast_kern(arr);

	/* first run. No old elems in the table */
	for (i = zero; i < 100000 && can_loop; i++) {
		htab_update_elem(htab, i, i);
		arr[i] = i;
	}

	/* should replace some elems with new ones */
	for (i = zero; i < 1000 && can_loop; i++) {
		htab_update_elem(htab, i, i);
		/* Access mem to make the verifier use bounded loop logic */
		arr2[i] = i;
	}
	cast_user(htab);
	htab_for_user = htab;
#else
	skip = true;
#endif
	return 0;
}

char _license[] SEC("license") = "GPL";
