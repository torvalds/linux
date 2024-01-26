#include "bpf_experimental.h"
#include "bpf_misc.h"

struct val_t {
	long b, c, d;
};

struct val2_t {
	long b;
};

struct val_with_ptr_t {
	char *p;
};

struct val_with_rb_root_t {
	struct bpf_spin_lock lock;
};

struct val_600b_t {
	char b[600];
};

struct elem {
	long sum;
	struct val_t __percpu_kptr *pc;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, struct elem);
} array SEC(".maps");

long ret;

SEC("?fentry/bpf_fentry_test1")
__failure __msg("store to referenced kptr disallowed")
int BPF_PROG(test_array_map_1)
{
	struct val_t __percpu_kptr *p;
	struct elem *e;
	int index = 0;

	e = bpf_map_lookup_elem(&array, &index);
	if (!e)
		return 0;

	p = bpf_percpu_obj_new(struct val_t);
	if (!p)
		return 0;

	p = bpf_kptr_xchg(&e->pc, p);
	if (p)
		bpf_percpu_obj_drop(p);

	e->pc = (struct val_t __percpu_kptr *)ret;
	return 0;
}

SEC("?fentry/bpf_fentry_test1")
__failure __msg("invalid kptr access, R2 type=percpu_ptr_val2_t expected=ptr_val_t")
int BPF_PROG(test_array_map_2)
{
	struct val2_t __percpu_kptr *p2;
	struct val_t __percpu_kptr *p;
	struct elem *e;
	int index = 0;

	e = bpf_map_lookup_elem(&array, &index);
	if (!e)
		return 0;

	p2 = bpf_percpu_obj_new(struct val2_t);
	if (!p2)
		return 0;

	p = bpf_kptr_xchg(&e->pc, p2);
	if (p)
		bpf_percpu_obj_drop(p);

	return 0;
}

SEC("?fentry.s/bpf_fentry_test1")
__failure __msg("R1 type=scalar expected=percpu_ptr_, percpu_rcu_ptr_, percpu_trusted_ptr_")
int BPF_PROG(test_array_map_3)
{
	struct val_t __percpu_kptr *p, *p1;
	struct val_t *v;
	struct elem *e;
	int index = 0;

	e = bpf_map_lookup_elem(&array, &index);
	if (!e)
		return 0;

	p = bpf_percpu_obj_new(struct val_t);
	if (!p)
		return 0;

	p1 = bpf_kptr_xchg(&e->pc, p);
	if (p1)
		bpf_percpu_obj_drop(p1);

	v = bpf_this_cpu_ptr(p);
	ret = v->b;
	return 0;
}

SEC("?fentry.s/bpf_fentry_test1")
__failure __msg("arg#0 expected for bpf_percpu_obj_drop_impl()")
int BPF_PROG(test_array_map_4)
{
	struct val_t __percpu_kptr *p;

	p = bpf_percpu_obj_new(struct val_t);
	if (!p)
		return 0;

	bpf_obj_drop(p);
	return 0;
}

SEC("?fentry.s/bpf_fentry_test1")
__failure __msg("arg#0 expected for bpf_obj_drop_impl()")
int BPF_PROG(test_array_map_5)
{
	struct val_t *p;

	p = bpf_obj_new(struct val_t);
	if (!p)
		return 0;

	bpf_percpu_obj_drop(p);
	return 0;
}

SEC("?fentry.s/bpf_fentry_test1")
__failure __msg("bpf_percpu_obj_new type ID argument must be of a struct of scalars")
int BPF_PROG(test_array_map_6)
{
	struct val_with_ptr_t __percpu_kptr *p;

	p = bpf_percpu_obj_new(struct val_with_ptr_t);
	if (!p)
		return 0;

	bpf_percpu_obj_drop(p);
	return 0;
}

SEC("?fentry.s/bpf_fentry_test1")
__failure __msg("bpf_percpu_obj_new type ID argument must not contain special fields")
int BPF_PROG(test_array_map_7)
{
	struct val_with_rb_root_t __percpu_kptr *p;

	p = bpf_percpu_obj_new(struct val_with_rb_root_t);
	if (!p)
		return 0;

	bpf_percpu_obj_drop(p);
	return 0;
}

SEC("?fentry.s/bpf_fentry_test1")
__failure __msg("bpf_percpu_obj_new type size (600) is greater than 512")
int BPF_PROG(test_array_map_8)
{
	struct val_600b_t __percpu_kptr *p;

	p = bpf_percpu_obj_new(struct val_600b_t);
	if (!p)
		return 0;

	bpf_percpu_obj_drop(p);
	return 0;
}

char _license[] SEC("license") = "GPL";
