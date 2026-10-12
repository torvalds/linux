// SPDX-License-Identifier: GPL-2.0

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>

#include "bpf_experimental.h"
#include "bpf_misc.h"

struct test_empty_event {};

struct test_flex_batch {
	int nr;
	struct test_empty_event events[];
};

struct map_value {
	struct test_flex_batch __kptr *batch;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__type(key, int);
	__type(value, struct map_value);
	__uint(max_entries, 1);
} batches SEC(".maps");

SEC("syscall")
__description("btf walk into flexible array of zero-sized elements")
__failure __msg("access beyond struct test_flex_batch at off 4 size 1")
int stash_and_peek(void *ctx)
{
	struct test_flex_batch *b, *old;
	struct map_value *v;
	int key = 0;

	v = bpf_map_lookup_elem(&batches, &key);
	if (!v)
		return 0;

	b = bpf_obj_new(struct test_flex_batch);
	if (!b)
		return 0;
	b->nr = 1;

	old = bpf_kptr_xchg(&v->batch, b);
	if (old)
		bpf_obj_drop(old);

	b = v->batch;
	if (!b)
		return 0;

	return b->nr + *(char *)&b->events[0];
}

char _license[] SEC("license") = "GPL";
