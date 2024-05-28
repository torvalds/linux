#include "bpf_experimental.h"

struct val_t {
	long b, c, d;
};

struct elem {
	long sum;
	struct val_t __percpu_kptr *pc;
};

struct {
	__uint(type, BPF_MAP_TYPE_CGRP_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, struct elem);
} cgrp SEC(".maps");

const volatile int nr_cpus;

/* Initialize the percpu object */
SEC("fentry/bpf_fentry_test1")
int BPF_PROG(test_cgrp_local_storage_1)
{
	struct task_struct *task;
	struct val_t __percpu_kptr *p;
	struct elem *e;

	task = bpf_get_current_task_btf();
	e = bpf_cgrp_storage_get(&cgrp, task->cgroups->dfl_cgrp, 0,
				 BPF_LOCAL_STORAGE_GET_F_CREATE);
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

/* Percpu data collection */
SEC("fentry/bpf_fentry_test2")
int BPF_PROG(test_cgrp_local_storage_2)
{
	struct task_struct *task;
	struct val_t __percpu_kptr *p;
	struct val_t *v;
	struct elem *e;

	task = bpf_get_current_task_btf();
	e = bpf_cgrp_storage_get(&cgrp, task->cgroups->dfl_cgrp, 0, 0);
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

/* Summarize percpu data collection */
SEC("fentry/bpf_fentry_test3")
int BPF_PROG(test_cgrp_local_storage_3)
{
	struct task_struct *task;
	struct val_t __percpu_kptr *p;
	struct val_t *v;
	struct elem *e;
	int i;

	if ((bpf_get_current_pid_tgid() >> 32) != my_pid)
		return 0;

	task = bpf_get_current_task_btf();
	e = bpf_cgrp_storage_get(&cgrp, task->cgroups->dfl_cgrp, 0, 0);
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

char _license[] SEC("license") = "GPL";
