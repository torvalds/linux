// SPDX-License-Identifier: GPL-2.0

#include "vmlinux.h"
#include "bpf_experimental.h"
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"
#include "../test_kmods/bpf_testmod_kfunc.h"

char _license[] SEC("license") = "GPL";

SEC("raw_tp/sys_enter")
__success
int iter_next_trusted(const void *ctx)
{
	struct task_struct *cur_task = bpf_get_current_task_btf();
	struct bpf_iter_task_vma vma_it;
	struct vm_area_struct *vma_ptr;

	bpf_iter_task_vma_new(&vma_it, cur_task, 0);

	vma_ptr = bpf_iter_task_vma_next(&vma_it);
	if (vma_ptr == NULL)
		goto out;

	bpf_kfunc_trusted_vma_test(vma_ptr);
out:
	bpf_iter_task_vma_destroy(&vma_it);
	return 0;
}

SEC("raw_tp/sys_enter")
__failure __msg("Possibly NULL pointer passed to trusted arg0")
int iter_next_trusted_or_null(const void *ctx)
{
	struct task_struct *cur_task = bpf_get_current_task_btf();
	struct bpf_iter_task_vma vma_it;
	struct vm_area_struct *vma_ptr;

	bpf_iter_task_vma_new(&vma_it, cur_task, 0);

	vma_ptr = bpf_iter_task_vma_next(&vma_it);

	bpf_kfunc_trusted_vma_test(vma_ptr);

	bpf_iter_task_vma_destroy(&vma_it);
	return 0;
}

SEC("raw_tp/sys_enter")
__success
int iter_next_rcu(const void *ctx)
{
	struct task_struct *cur_task = bpf_get_current_task_btf();
	struct bpf_iter_task task_it;
	struct task_struct *task_ptr;

	bpf_iter_task_new(&task_it, cur_task, 0);

	task_ptr = bpf_iter_task_next(&task_it);
	if (task_ptr == NULL)
		goto out;

	bpf_kfunc_rcu_task_test(task_ptr);
out:
	bpf_iter_task_destroy(&task_it);
	return 0;
}

SEC("raw_tp/sys_enter")
__failure __msg("Possibly NULL pointer passed to trusted arg0")
int iter_next_rcu_or_null(const void *ctx)
{
	struct task_struct *cur_task = bpf_get_current_task_btf();
	struct bpf_iter_task task_it;
	struct task_struct *task_ptr;

	bpf_iter_task_new(&task_it, cur_task, 0);

	task_ptr = bpf_iter_task_next(&task_it);

	bpf_kfunc_rcu_task_test(task_ptr);

	bpf_iter_task_destroy(&task_it);
	return 0;
}

SEC("raw_tp/sys_enter")
__failure __msg("R1 must be referenced or trusted")
int iter_next_rcu_not_trusted(const void *ctx)
{
	struct task_struct *cur_task = bpf_get_current_task_btf();
	struct bpf_iter_task task_it;
	struct task_struct *task_ptr;

	bpf_iter_task_new(&task_it, cur_task, 0);

	task_ptr = bpf_iter_task_next(&task_it);
	if (task_ptr == NULL)
		goto out;

	bpf_kfunc_trusted_task_test(task_ptr);
out:
	bpf_iter_task_destroy(&task_it);
	return 0;
}

SEC("raw_tp/sys_enter")
__failure __msg("R1 cannot write into rdonly_mem")
/* Message should not be 'R1 cannot write into rdonly_trusted_mem' */
int iter_next_ptr_mem_not_trusted(const void *ctx)
{
	struct bpf_iter_num num_it;
	int *num_ptr;

	bpf_iter_num_new(&num_it, 0, 10);

	num_ptr = bpf_iter_num_next(&num_it);
	if (num_ptr == NULL)
		goto out;

	bpf_kfunc_trusted_num_test(num_ptr);
out:
	bpf_iter_num_destroy(&num_it);
	return 0;
}

SEC("?fentry.s/" SYS_PREFIX "sys_getpgid")
__failure __msg("kernel func bpf_kfunc_ret_rcu_test requires RCU critical section protection")
int iter_ret_rcu_test_protected(const void *ctx)
{
	struct task_struct *p;

	p = bpf_kfunc_ret_rcu_test();
	return p->pid;
}

SEC("?fentry.s/" SYS_PREFIX "sys_getpgid")
__failure __msg("R1 type=rcu_ptr_or_null_ expected=")
int iter_ret_rcu_test_type(const void *ctx)
{
	struct task_struct *p;

	bpf_rcu_read_lock();
	p = bpf_kfunc_ret_rcu_test();
	bpf_this_cpu_ptr(p);
	bpf_rcu_read_unlock();
	return 0;
}

SEC("?fentry.s/" SYS_PREFIX "sys_getpgid")
__failure __msg("kernel func bpf_kfunc_ret_rcu_test_nostruct requires RCU critical section protection")
int iter_ret_rcu_test_protected_nostruct(const void *ctx)
{
	void *p;

	p = bpf_kfunc_ret_rcu_test_nostruct(4);
	return *(int *)p;
}

SEC("?fentry.s/" SYS_PREFIX "sys_getpgid")
__failure __msg("R1 type=rdonly_rcu_mem_or_null expected=")
int iter_ret_rcu_test_type_nostruct(const void *ctx)
{
	void *p;

	bpf_rcu_read_lock();
	p = bpf_kfunc_ret_rcu_test_nostruct(4);
	bpf_this_cpu_ptr(p);
	bpf_rcu_read_unlock();
	return 0;
}
