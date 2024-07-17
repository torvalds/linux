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
#ifdef __TARGET_ARCH_arm64
	__ulong(map_extra, 0x1ull << 32); /* start of mmap() region */
#else
	__ulong(map_extra, 0x1ull << 44); /* start of mmap() region */
#endif
} arena SEC(".maps");

#include "bpf_arena_alloc.h"
#include "bpf_arena_list.h"

struct elem {
	struct arena_list_node node;
	__u64 value;
};

struct arena_list_head __arena *list_head;
int list_sum;
int cnt;
bool skip = false;

#ifdef __BPF_FEATURE_ADDR_SPACE_CAST
long __arena arena_sum;
int __arena test_val = 1;
struct arena_list_head __arena global_head;
#else
long arena_sum SEC(".addr_space.1");
int test_val SEC(".addr_space.1");
#endif

int zero;

SEC("syscall")
int arena_list_add(void *ctx)
{
#ifdef __BPF_FEATURE_ADDR_SPACE_CAST
	__u64 i;

	list_head = &global_head;

	for (i = zero; i < cnt && can_loop; i++) {
		struct elem __arena *n = bpf_alloc(sizeof(*n));

		test_val++;
		n->value = i;
		arena_sum += i;
		list_add_head(&n->node, list_head);
	}
#else
	skip = true;
#endif
	return 0;
}

SEC("syscall")
int arena_list_del(void *ctx)
{
#ifdef __BPF_FEATURE_ADDR_SPACE_CAST
	struct elem __arena *n;
	int sum = 0;

	arena_sum = 0;
	list_for_each_entry(n, list_head, node) {
		sum += n->value;
		arena_sum += n->value;
		list_del(&n->node);
		bpf_free(n);
	}
	list_sum = sum;
#else
	skip = true;
#endif
	return 0;
}

char _license[] SEC("license") = "GPL";
