// SPDX-License-Identifier: GPL-2.0
#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>
#include "../bpf_testmod/bpf_testmod_kfunc.h"

struct map_value {
	struct prog_test_ref_kfunc __kptr *ptr;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__type(key, int);
	__type(value, struct map_value);
	__uint(max_entries, 16);
} array_map SEC(".maps");

static __noinline int cb1(void *map, void *key, void *value, void *ctx)
{
	void *p = *(void **)ctx;
	bpf_kfunc_call_test_release(p);
	/* Without the fix this would cause underflow */
	return 0;
}

SEC("?tc")
int underflow_prog(void *ctx)
{
	struct prog_test_ref_kfunc *p;
	unsigned long sl = 0;

	p = bpf_kfunc_call_test_acquire(&sl);
	if (!p)
		return 0;
	bpf_for_each_map_elem(&array_map, cb1, &p, 0);
	return 0;
}

static __always_inline int cb2(void *map, void *key, void *value, void *ctx)
{
	unsigned long sl = 0;

	*(void **)ctx = bpf_kfunc_call_test_acquire(&sl);
	/* Without the fix this would leak memory */
	return 0;
}

SEC("?tc")
int leak_prog(void *ctx)
{
	struct prog_test_ref_kfunc *p;
	struct map_value *v;

	v = bpf_map_lookup_elem(&array_map, &(int){0});
	if (!v)
		return 0;

	p = NULL;
	bpf_for_each_map_elem(&array_map, cb2, &p, 0);
	p = bpf_kptr_xchg(&v->ptr, p);
	if (p)
		bpf_kfunc_call_test_release(p);
	return 0;
}

static __always_inline int cb(void *map, void *key, void *value, void *ctx)
{
	return 0;
}

static __always_inline int cb3(void *map, void *key, void *value, void *ctx)
{
	unsigned long sl = 0;
	void *p;

	bpf_kfunc_call_test_acquire(&sl);
	bpf_for_each_map_elem(&array_map, cb, &p, 0);
	/* It should only complain here, not in cb. This is why we need
	 * callback_ref to be set to frameno.
	 */
	return 0;
}

SEC("?tc")
int nested_cb(void *ctx)
{
	struct prog_test_ref_kfunc *p;
	unsigned long sl = 0;
	int sp = 0;

	p = bpf_kfunc_call_test_acquire(&sl);
	if (!p)
		return 0;
	bpf_for_each_map_elem(&array_map, cb3, &sp, 0);
	bpf_kfunc_call_test_release(p);
	return 0;
}

SEC("?tc")
int non_cb_transfer_ref(void *ctx)
{
	struct prog_test_ref_kfunc *p;
	unsigned long sl = 0;

	p = bpf_kfunc_call_test_acquire(&sl);
	if (!p)
		return 0;
	cb1(NULL, NULL, NULL, &p);
	bpf_kfunc_call_test_acquire(&sl);
	return 0;
}

char _license[] SEC("license") = "GPL";
