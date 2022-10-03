// SPDX-License-Identifier: GPL-2.0
#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>

struct map_value {
	char buf[8];
	struct prog_test_ref_kfunc __kptr *unref_ptr;
	struct prog_test_ref_kfunc __kptr_ref *ref_ptr;
	struct prog_test_member __kptr_ref *ref_memb_ptr;
};

struct array_map {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__type(key, int);
	__type(value, struct map_value);
	__uint(max_entries, 1);
} array_map SEC(".maps");

extern struct prog_test_ref_kfunc *bpf_kfunc_call_test_acquire(unsigned long *sp) __ksym;
extern struct prog_test_ref_kfunc *
bpf_kfunc_call_test_kptr_get(struct prog_test_ref_kfunc **p, int a, int b) __ksym;

SEC("?tc")
int size_not_bpf_dw(struct __sk_buff *ctx)
{
	struct map_value *v;
	int key = 0;

	v = bpf_map_lookup_elem(&array_map, &key);
	if (!v)
		return 0;

	*(u32 *)&v->unref_ptr = 0;
	return 0;
}

SEC("?tc")
int non_const_var_off(struct __sk_buff *ctx)
{
	struct map_value *v;
	int key = 0, id;

	v = bpf_map_lookup_elem(&array_map, &key);
	if (!v)
		return 0;

	id = ctx->protocol;
	if (id < 4 || id > 12)
		return 0;
	*(u64 *)((void *)v + id) = 0;

	return 0;
}

SEC("?tc")
int non_const_var_off_kptr_xchg(struct __sk_buff *ctx)
{
	struct map_value *v;
	int key = 0, id;

	v = bpf_map_lookup_elem(&array_map, &key);
	if (!v)
		return 0;

	id = ctx->protocol;
	if (id < 4 || id > 12)
		return 0;
	bpf_kptr_xchg((void *)v + id, NULL);

	return 0;
}

SEC("?tc")
int misaligned_access_write(struct __sk_buff *ctx)
{
	struct map_value *v;
	int key = 0;

	v = bpf_map_lookup_elem(&array_map, &key);
	if (!v)
		return 0;

	*(void **)((void *)v + 7) = NULL;

	return 0;
}

SEC("?tc")
int misaligned_access_read(struct __sk_buff *ctx)
{
	struct map_value *v;
	int key = 0;

	v = bpf_map_lookup_elem(&array_map, &key);
	if (!v)
		return 0;

	return *(u64 *)((void *)v + 1);
}

SEC("?tc")
int reject_var_off_store(struct __sk_buff *ctx)
{
	struct prog_test_ref_kfunc *unref_ptr;
	struct map_value *v;
	int key = 0, id;

	v = bpf_map_lookup_elem(&array_map, &key);
	if (!v)
		return 0;

	unref_ptr = v->unref_ptr;
	if (!unref_ptr)
		return 0;
	id = ctx->protocol;
	if (id < 4 || id > 12)
		return 0;
	unref_ptr += id;
	v->unref_ptr = unref_ptr;

	return 0;
}

SEC("?tc")
int reject_bad_type_match(struct __sk_buff *ctx)
{
	struct prog_test_ref_kfunc *unref_ptr;
	struct map_value *v;
	int key = 0;

	v = bpf_map_lookup_elem(&array_map, &key);
	if (!v)
		return 0;

	unref_ptr = v->unref_ptr;
	if (!unref_ptr)
		return 0;
	unref_ptr = (void *)unref_ptr + 4;
	v->unref_ptr = unref_ptr;

	return 0;
}

SEC("?tc")
int marked_as_untrusted_or_null(struct __sk_buff *ctx)
{
	struct map_value *v;
	int key = 0;

	v = bpf_map_lookup_elem(&array_map, &key);
	if (!v)
		return 0;

	bpf_this_cpu_ptr(v->unref_ptr);
	return 0;
}

SEC("?tc")
int correct_btf_id_check_size(struct __sk_buff *ctx)
{
	struct prog_test_ref_kfunc *p;
	struct map_value *v;
	int key = 0;

	v = bpf_map_lookup_elem(&array_map, &key);
	if (!v)
		return 0;

	p = v->unref_ptr;
	if (!p)
		return 0;
	return *(int *)((void *)p + bpf_core_type_size(struct prog_test_ref_kfunc));
}

SEC("?tc")
int inherit_untrusted_on_walk(struct __sk_buff *ctx)
{
	struct prog_test_ref_kfunc *unref_ptr;
	struct map_value *v;
	int key = 0;

	v = bpf_map_lookup_elem(&array_map, &key);
	if (!v)
		return 0;

	unref_ptr = v->unref_ptr;
	if (!unref_ptr)
		return 0;
	unref_ptr = unref_ptr->next;
	bpf_this_cpu_ptr(unref_ptr);
	return 0;
}

SEC("?tc")
int reject_kptr_xchg_on_unref(struct __sk_buff *ctx)
{
	struct map_value *v;
	int key = 0;

	v = bpf_map_lookup_elem(&array_map, &key);
	if (!v)
		return 0;

	bpf_kptr_xchg(&v->unref_ptr, NULL);
	return 0;
}

SEC("?tc")
int reject_kptr_get_no_map_val(struct __sk_buff *ctx)
{
	bpf_kfunc_call_test_kptr_get((void *)&ctx, 0, 0);
	return 0;
}

SEC("?tc")
int reject_kptr_get_no_null_map_val(struct __sk_buff *ctx)
{
	bpf_kfunc_call_test_kptr_get(bpf_map_lookup_elem(&array_map, &(int){0}), 0, 0);
	return 0;
}

SEC("?tc")
int reject_kptr_get_no_kptr(struct __sk_buff *ctx)
{
	struct map_value *v;
	int key = 0;

	v = bpf_map_lookup_elem(&array_map, &key);
	if (!v)
		return 0;

	bpf_kfunc_call_test_kptr_get((void *)v, 0, 0);
	return 0;
}

SEC("?tc")
int reject_kptr_get_on_unref(struct __sk_buff *ctx)
{
	struct map_value *v;
	int key = 0;

	v = bpf_map_lookup_elem(&array_map, &key);
	if (!v)
		return 0;

	bpf_kfunc_call_test_kptr_get(&v->unref_ptr, 0, 0);
	return 0;
}

SEC("?tc")
int reject_kptr_get_bad_type_match(struct __sk_buff *ctx)
{
	struct map_value *v;
	int key = 0;

	v = bpf_map_lookup_elem(&array_map, &key);
	if (!v)
		return 0;

	bpf_kfunc_call_test_kptr_get((void *)&v->ref_memb_ptr, 0, 0);
	return 0;
}

SEC("?tc")
int mark_ref_as_untrusted_or_null(struct __sk_buff *ctx)
{
	struct map_value *v;
	int key = 0;

	v = bpf_map_lookup_elem(&array_map, &key);
	if (!v)
		return 0;

	bpf_this_cpu_ptr(v->ref_ptr);
	return 0;
}

SEC("?tc")
int reject_untrusted_store_to_ref(struct __sk_buff *ctx)
{
	struct prog_test_ref_kfunc *p;
	struct map_value *v;
	int key = 0;

	v = bpf_map_lookup_elem(&array_map, &key);
	if (!v)
		return 0;

	p = v->ref_ptr;
	if (!p)
		return 0;
	/* Checkmate, clang */
	*(struct prog_test_ref_kfunc * volatile *)&v->ref_ptr = p;
	return 0;
}

SEC("?tc")
int reject_untrusted_xchg(struct __sk_buff *ctx)
{
	struct prog_test_ref_kfunc *p;
	struct map_value *v;
	int key = 0;

	v = bpf_map_lookup_elem(&array_map, &key);
	if (!v)
		return 0;

	p = v->ref_ptr;
	if (!p)
		return 0;
	bpf_kptr_xchg(&v->ref_ptr, p);
	return 0;
}

SEC("?tc")
int reject_bad_type_xchg(struct __sk_buff *ctx)
{
	struct prog_test_ref_kfunc *ref_ptr;
	struct map_value *v;
	int key = 0;

	v = bpf_map_lookup_elem(&array_map, &key);
	if (!v)
		return 0;

	ref_ptr = bpf_kfunc_call_test_acquire(&(unsigned long){0});
	if (!ref_ptr)
		return 0;
	bpf_kptr_xchg(&v->ref_memb_ptr, ref_ptr);
	return 0;
}

SEC("?tc")
int reject_member_of_ref_xchg(struct __sk_buff *ctx)
{
	struct prog_test_ref_kfunc *ref_ptr;
	struct map_value *v;
	int key = 0;

	v = bpf_map_lookup_elem(&array_map, &key);
	if (!v)
		return 0;

	ref_ptr = bpf_kfunc_call_test_acquire(&(unsigned long){0});
	if (!ref_ptr)
		return 0;
	bpf_kptr_xchg(&v->ref_memb_ptr, &ref_ptr->memb);
	return 0;
}

SEC("?syscall")
int reject_indirect_helper_access(struct __sk_buff *ctx)
{
	struct map_value *v;
	int key = 0;

	v = bpf_map_lookup_elem(&array_map, &key);
	if (!v)
		return 0;

	bpf_get_current_comm(v, sizeof(v->buf) + 1);
	return 0;
}

__noinline
int write_func(int *p)
{
	return p ? *p = 42 : 0;
}

SEC("?tc")
int reject_indirect_global_func_access(struct __sk_buff *ctx)
{
	struct map_value *v;
	int key = 0;

	v = bpf_map_lookup_elem(&array_map, &key);
	if (!v)
		return 0;

	return write_func((void *)v + 5);
}

SEC("?tc")
int kptr_xchg_ref_state(struct __sk_buff *ctx)
{
	struct prog_test_ref_kfunc *p;
	struct map_value *v;
	int key = 0;

	v = bpf_map_lookup_elem(&array_map, &key);
	if (!v)
		return 0;

	p = bpf_kfunc_call_test_acquire(&(unsigned long){0});
	if (!p)
		return 0;
	bpf_kptr_xchg(&v->ref_ptr, p);
	return 0;
}

SEC("?tc")
int kptr_get_ref_state(struct __sk_buff *ctx)
{
	struct map_value *v;
	int key = 0;

	v = bpf_map_lookup_elem(&array_map, &key);
	if (!v)
		return 0;

	bpf_kfunc_call_test_kptr_get(&v->ref_ptr, 0, 0);
	return 0;
}

char _license[] SEC("license") = "GPL";
