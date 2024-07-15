#include "bpf_experimental.h"

struct val_t {
	long b, c, d;
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

void bpf_rcu_read_lock(void) __ksym;
void bpf_rcu_read_unlock(void) __ksym;

const volatile int nr_cpus;

/* Initialize the percpu object */
SEC("?fentry/bpf_fentry_test1")
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

	return 0;
}

/* Update percpu data */
SEC("?fentry/bpf_fentry_test2")
int BPF_PROG(test_array_map_2)
{
	struct val_t __percpu_kptr *p;
	struct val_t *v;
	struct elem *e;
	int index = 0;

	e = bpf_map_lookup_elem(&array, &index);
	if (!e)
		return 0;

	p = e->pc;
	if (!p)
		return 0;

	v = bpf_per_cpu_ptr(p, 0);
	if (!v)
		return 0;
	v->c = 1;
	v->d = 2;

	return 0;
}

int cpu0_field_d, sum_field_c;
int my_pid;

/* Summarize percpu data */
SEC("?fentry/bpf_fentry_test3")
int BPF_PROG(test_array_map_3)
{
	struct val_t __percpu_kptr *p;
	int i, index = 0;
	struct val_t *v;
	struct elem *e;

	if ((bpf_get_current_pid_tgid() >> 32) != my_pid)
		return 0;

	e = bpf_map_lookup_elem(&array, &index);
	if (!e)
		return 0;

	p = e->pc;
	if (!p)
		return 0;

	bpf_for(i, 0, nr_cpus) {
		v = bpf_per_cpu_ptr(p, i);
		if (v) {
			if (i == 0)
				cpu0_field_d = v->d;
			sum_field_c += v->c;
		}
	}

	return 0;
}

/* Explicitly free allocated percpu data */
SEC("?fentry/bpf_fentry_test4")
int BPF_PROG(test_array_map_4)
{
	struct val_t __percpu_kptr *p;
	struct elem *e;
	int index = 0;

	e = bpf_map_lookup_elem(&array, &index);
	if (!e)
		return 0;

	/* delete */
	p = bpf_kptr_xchg(&e->pc, NULL);
	if (p) {
		bpf_percpu_obj_drop(p);
	}

	return 0;
}

SEC("?fentry.s/bpf_fentry_test1")
int BPF_PROG(test_array_map_10)
{
	struct val_t __percpu_kptr *p, *p1;
	int i, index = 0;
	struct val_t *v;
	struct elem *e;

	if ((bpf_get_current_pid_tgid() >> 32) != my_pid)
		return 0;

	e = bpf_map_lookup_elem(&array, &index);
	if (!e)
		return 0;

	bpf_rcu_read_lock();
	p = e->pc;
	if (!p) {
		p = bpf_percpu_obj_new(struct val_t);
		if (!p)
			goto out;

		p1 = bpf_kptr_xchg(&e->pc, p);
		if (p1) {
			/* race condition */
			bpf_percpu_obj_drop(p1);
		}
	}

	v = bpf_this_cpu_ptr(p);
	v->c = 3;
	v = bpf_this_cpu_ptr(p);
	v->c = 0;

	v = bpf_per_cpu_ptr(p, 0);
	if (!v)
		goto out;
	v->c = 1;
	v->d = 2;

	/* delete */
	p1 = bpf_kptr_xchg(&e->pc, NULL);
	if (!p1)
		goto out;

	bpf_for(i, 0, nr_cpus) {
		v = bpf_per_cpu_ptr(p, i);
		if (v) {
			if (i == 0)
				cpu0_field_d = v->d;
			sum_field_c += v->c;
		}
	}

	/* finally release p */
	bpf_percpu_obj_drop(p1);
out:
	bpf_rcu_read_unlock();
	return 0;
}

char _license[] SEC("license") = "GPL";
