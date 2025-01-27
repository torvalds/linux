// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Google */
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_experimental.h"

char _license[] SEC("license") = "GPL";

#define SLAB_NAME_MAX  32

struct kmem_cache_result {
	char name[SLAB_NAME_MAX];
	long obj_size;
};

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(key_size, sizeof(void *));
	__uint(value_size, SLAB_NAME_MAX);
	__uint(max_entries, 1);
} slab_hash SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(key_size, sizeof(int));
	__uint(value_size, sizeof(struct kmem_cache_result));
	__uint(max_entries, 1024);
} slab_result SEC(".maps");

extern struct kmem_cache *bpf_get_kmem_cache(u64 addr) __ksym;

/* Result, will be checked by userspace */
int task_struct_found;
int kmem_cache_seen;
int open_coded_seen;

SEC("iter/kmem_cache")
int slab_info_collector(struct bpf_iter__kmem_cache *ctx)
{
	struct seq_file *seq = ctx->meta->seq;
	struct kmem_cache *s = ctx->s;
	struct kmem_cache_result *r;
	int idx;

	if (s) {
		/* To make sure if the slab_iter implements the seq interface
		 * properly and it's also useful for debugging.
		 */
		BPF_SEQ_PRINTF(seq, "%s: %u\n", s->name, s->size);

		idx = kmem_cache_seen;
		r = bpf_map_lookup_elem(&slab_result, &idx);
		if (r == NULL)
			return 0;

		kmem_cache_seen++;

		/* Save name and size to match /proc/slabinfo */
		bpf_probe_read_kernel_str(r->name, sizeof(r->name), s->name);
		r->obj_size = s->size;

		if (!bpf_strncmp(r->name, 11, "task_struct"))
			bpf_map_update_elem(&slab_hash, &s, r->name, BPF_NOEXIST);
	}

	return 0;
}

SEC("raw_tp/bpf_test_finish")
int BPF_PROG(check_task_struct)
{
	u64 curr = bpf_get_current_task();
	struct kmem_cache *s;
	char *name;

	s = bpf_get_kmem_cache(curr);
	if (s == NULL) {
		task_struct_found = -1;
		return 0;
	}
	name = bpf_map_lookup_elem(&slab_hash, &s);
	if (name && !bpf_strncmp(name, 11, "task_struct"))
		task_struct_found = 1;
	else
		task_struct_found = -2;
	return 0;
}

SEC("syscall")
int open_coded_iter(const void *ctx)
{
	struct kmem_cache *s;

	bpf_for_each(kmem_cache, s) {
		struct kmem_cache_result *r;

		r = bpf_map_lookup_elem(&slab_result, &open_coded_seen);
		if (!r)
			break;

		if (r->obj_size != s->size)
			break;

		open_coded_seen++;
	}
	return 0;
}
