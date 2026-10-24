// SPDX-License-Identifier: GPL-2.0

#include "vmlinux.h"
#include "bpf_experimental.h"
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"
#include "../test_kmods/bpf_testmod_kfunc.h"

char _license[] SEC("license") = "GPL";

struct bpf_iter_testmod_current {
	u64 __opaque;
};

SEC("raw_tp/sys_enter")
__success
int iter_independent_lifetime_after_next(const void *ctx)
{
	struct bpf_iter_testmod_current it;
	struct task_struct *task;

	bpf_iter_testmod_current_new(&it);
	task = bpf_iter_testmod_current_next(&it);
	if (!task)
		goto out;

	bpf_iter_testmod_current_next(&it);
	bpf_kfunc_trusted_task_test(task);
out:
	bpf_iter_testmod_current_destroy(&it);
	return 0;
}

SEC("raw_tp/sys_enter")
__success
int iter_independent_lifetime_after_destroy(const void *ctx)
{
	struct bpf_iter_testmod_current it;
	struct task_struct *task;

	bpf_iter_testmod_current_new(&it);
	task = bpf_iter_testmod_current_next(&it);
	bpf_iter_testmod_current_destroy(&it);
	if (task)
		bpf_kfunc_trusted_task_test(task);
	return 0;
}

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
__failure __msg("invalid mem access 'scalar'")
int iter_next_trusted_after_destroy(const void *ctx)
{
	struct task_struct *cur_task = bpf_get_current_task_btf();
	struct bpf_iter_task_vma vma_it;
	struct vm_area_struct *vma_ptr;

	bpf_iter_task_vma_new(&vma_it, cur_task, 0);

	vma_ptr = bpf_iter_task_vma_next(&vma_it);
	if (!vma_ptr)
		goto out;

	bpf_iter_task_vma_destroy(&vma_it);
	return vma_ptr->vm_start;
out:
	bpf_iter_task_vma_destroy(&vma_it);
	return 0;
}

SEC("raw_tp/sys_enter")
__failure __msg("invalid mem access 'scalar'")
int iter_next_trusted_after_next(const void *ctx)
{
	struct task_struct *cur_task = bpf_get_current_task_btf();
	struct bpf_iter_task_vma vma_it;
	struct vm_area_struct *vma_ptr, *next_vma_ptr;
	u64 vm_start;

	bpf_iter_task_vma_new(&vma_it, cur_task, 0);

	vma_ptr = bpf_iter_task_vma_next(&vma_it);
	if (!vma_ptr)
		goto out;

	next_vma_ptr = bpf_iter_task_vma_next(&vma_it);
	if (!next_vma_ptr)
		goto out;

	vm_start = vma_ptr->vm_start;
	bpf_iter_task_vma_destroy(&vma_it);
	return vm_start;
out:
	bpf_iter_task_vma_destroy(&vma_it);
	return 0;
}

SEC("raw_tp/sys_enter")
__failure __msg("invalid mem access 'scalar'")
int iter_next_trusted_after_next_null(const void *ctx)
{
	struct task_struct *cur_task = bpf_get_current_task_btf();
	struct bpf_iter_task_vma vma_it;
	struct vm_area_struct *vma_ptr, *next_vma_ptr;
	u64 vm_start;

	bpf_iter_task_vma_new(&vma_it, cur_task, 0);

	vma_ptr = bpf_iter_task_vma_next(&vma_it);
	if (!vma_ptr)
		goto out;

	next_vma_ptr = bpf_iter_task_vma_next(&vma_it);
	if (next_vma_ptr)
		goto out;

	vm_start = vma_ptr->vm_start;
	bpf_iter_task_vma_destroy(&vma_it);
	return vm_start;
out:
	bpf_iter_task_vma_destroy(&vma_it);
	return 0;
}

SEC("raw_tp/sys_enter")
__failure __msg("invalid mem access 'scalar'")
int iter_next_trusted_field_after_destroy(const void *ctx)
{
	struct task_struct *cur_task = bpf_get_current_task_btf();
	struct bpf_iter_task_vma vma_it;
	struct vm_area_struct *vma_ptr;
	struct file *file_ptr;

	bpf_iter_task_vma_new(&vma_it, cur_task, 0);

	vma_ptr = bpf_iter_task_vma_next(&vma_it);
	if (!vma_ptr)
		goto out;

	file_ptr = vma_ptr->vm_file;
	bpf_iter_task_vma_destroy(&vma_it);
	if (file_ptr)
		return file_ptr->f_mode;
	return 0;
out:
	bpf_iter_task_vma_destroy(&vma_it);
	return 0;
}

SEC("raw_tp/sys_enter")
__failure __msg("invalid mem access 'scalar'")
int iter_next_trusted_field_after_next(const void *ctx)
{
	struct task_struct *cur_task = bpf_get_current_task_btf();
	struct bpf_iter_task_vma vma_it;
	struct vm_area_struct *vma_ptr, *next_vma_ptr;
	struct file *file_ptr;
	u32 mode;

	bpf_iter_task_vma_new(&vma_it, cur_task, 0);

	vma_ptr = bpf_iter_task_vma_next(&vma_it);
	if (!vma_ptr)
		goto out;

	file_ptr = vma_ptr->vm_file;
	if (!file_ptr)
		goto out;

	next_vma_ptr = bpf_iter_task_vma_next(&vma_it);
	if (!next_vma_ptr)
		goto out;

	mode = file_ptr->f_mode;
	bpf_iter_task_vma_destroy(&vma_it);
	return mode;
out:
	bpf_iter_task_vma_destroy(&vma_it);
	return 0;
}

SEC("raw_tp/sys_enter")
__failure __msg("Leaking reference id=")
int iter_next_file_dynptr_after_next(const void *ctx)
{
	struct task_struct *cur_task = bpf_get_current_task_btf();
	struct bpf_iter_task_vma vma_it;
	struct vm_area_struct *vma_ptr;
	struct bpf_dynptr dynptr;
	struct file *file_ptr;

	bpf_iter_task_vma_new(&vma_it, cur_task, 0);

	vma_ptr = bpf_iter_task_vma_next(&vma_it);
	if (!vma_ptr)
		goto out;

	file_ptr = vma_ptr->vm_file;
	if (!file_ptr)
		goto out;

	bpf_dynptr_from_file(file_ptr, 0, &dynptr);
	bpf_iter_task_vma_next(&vma_it);
	bpf_dynptr_file_discard(&dynptr);
out:
	bpf_iter_task_vma_destroy(&vma_it);
	return 0;
}

SEC("raw_tp/sys_enter")
__failure __msg("Leaking reference id=")
int iter_next_file_dynptr_after_destroy(const void *ctx)
{
	struct task_struct *cur_task = bpf_get_current_task_btf();
	struct bpf_iter_task_vma vma_it;
	struct vm_area_struct *vma_ptr;
	struct bpf_dynptr dynptr;
	struct file *file_ptr;

	bpf_iter_task_vma_new(&vma_it, cur_task, 0);

	vma_ptr = bpf_iter_task_vma_next(&vma_it);
	if (!vma_ptr)
		goto out;

	file_ptr = vma_ptr->vm_file;
	if (!file_ptr)
		goto out;

	bpf_dynptr_from_file(file_ptr, 0, &dynptr);
	bpf_iter_task_vma_destroy(&vma_it);
	bpf_dynptr_file_discard(&dynptr);
	return 0;
out:
	bpf_iter_task_vma_destroy(&vma_it);
	return 0;
}

SEC("raw_tp/sys_enter")
__success
int iter_next_file_dynptr_discard_before_advance(const void *ctx)
{
	struct task_struct *cur_task = bpf_get_current_task_btf();
	struct bpf_iter_task_vma vma_it;
	struct vm_area_struct *vma_ptr;
	struct bpf_dynptr dynptr;
	struct file *file_ptr;

	bpf_iter_task_vma_new(&vma_it, cur_task, 0);

	vma_ptr = bpf_iter_task_vma_next(&vma_it);
	if (!vma_ptr)
		goto out;

	file_ptr = vma_ptr->vm_file;
	if (!file_ptr)
		goto out;

	bpf_dynptr_from_file(file_ptr, 0, &dynptr);
	bpf_dynptr_file_discard(&dynptr);
	bpf_iter_task_vma_next(&vma_it);
out:
	bpf_iter_task_vma_destroy(&vma_it);
	return 0;
}

SEC("raw_tp/sys_enter")
__success
int iter_next_trusted_current_after_next(const void *ctx)
{
	struct task_struct *cur_task = bpf_get_current_task_btf();
	struct bpf_iter_task_vma vma_it;
	struct vm_area_struct *vma_ptr;

	bpf_iter_task_vma_new(&vma_it, cur_task, 0);

	vma_ptr = bpf_iter_task_vma_next(&vma_it);
	if (!vma_ptr)
		goto out;

	vma_ptr = bpf_iter_task_vma_next(&vma_it);
	if (vma_ptr)
		bpf_kfunc_trusted_vma_test(vma_ptr);
out:
	bpf_iter_task_vma_destroy(&vma_it);
	return 0;
}

SEC("raw_tp/sys_enter")
__success
int iter_next_trusted_rcu_field_after_destroy(const void *ctx)
{
	struct task_struct *cur_task = bpf_get_current_task_btf();
	struct bpf_iter_task_vma vma_it;
	struct vm_area_struct *vma_ptr;
	struct mm_struct *mm_ptr;
	struct file *file_ptr = NULL;

	bpf_iter_task_vma_new(&vma_it, cur_task, 0);

	vma_ptr = bpf_iter_task_vma_next(&vma_it);
	if (vma_ptr) {
		mm_ptr = vma_ptr->vm_mm;
		/* exe_file has RCU protection independent of the iterator. */
		if (mm_ptr)
			file_ptr = mm_ptr->exe_file;
	}

	bpf_iter_task_vma_destroy(&vma_it);
	if (file_ptr)
		return file_ptr->f_mode;
	return 0;
}

SEC("raw_tp/sys_enter")
__success
int iter_next_trusted_rcu_field_after_next(const void *ctx)
{
	struct task_struct *cur_task = bpf_get_current_task_btf();
	struct bpf_iter_task_vma vma_it;
	struct vm_area_struct *vma_ptr;
	struct mm_struct *mm_ptr;
	struct file *file_ptr = NULL;
	u32 mode = 0;

	bpf_iter_task_vma_new(&vma_it, cur_task, 0);

	vma_ptr = bpf_iter_task_vma_next(&vma_it);
	if (vma_ptr) {
		mm_ptr = vma_ptr->vm_mm;
		if (mm_ptr)
			file_ptr = mm_ptr->exe_file;
	}

	bpf_iter_task_vma_next(&vma_it);
	if (file_ptr)
		mode = file_ptr->f_mode;
	bpf_iter_task_vma_destroy(&vma_it);
	return mode;
}

SEC("raw_tp/sys_enter")
__failure __msg("Possibly NULL pointer passed to trusted R1")
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

SEC("syscall")
__success
int trusted_nullable_kfunc(const void *ctx)
{
	struct task_struct *task;

	task = bpf_kfunc_trusted_null_test();
	bpf_kfunc_trusted_nullable_test(task);
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
__success
int iter_next_rcu_after_destroy(const void *ctx)
{
	struct task_struct *cur_task = bpf_get_current_task_btf();
	struct bpf_iter_task task_it;
	struct task_struct *task_ptr;

	bpf_iter_task_new(&task_it, cur_task, 0);

	task_ptr = bpf_iter_task_next(&task_it);
	bpf_iter_task_destroy(&task_it);
	if (!task_ptr)
		return 0;

	bpf_kfunc_rcu_task_test(task_ptr);
	return 0;
}

SEC("raw_tp/sys_enter")
__success
int iter_next_rcu_after_next(const void *ctx)
{
	struct task_struct *cur_task = bpf_get_current_task_btf();
	struct bpf_iter_task task_it;
	struct task_struct *task_ptr;

	bpf_iter_task_new(&task_it, cur_task, 0);

	task_ptr = bpf_iter_task_next(&task_it);
	if (!task_ptr)
		goto out;

	bpf_iter_task_next(&task_it);
	bpf_kfunc_rcu_task_test(task_ptr);
out:
	bpf_iter_task_destroy(&task_it);
	return 0;
}

SEC("raw_tp/sys_enter")
__failure __msg("Possibly NULL pointer passed to trusted R1")
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
__failure __msg("R1 type=rdonly_mem expected=fp")
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
__failure __msg("Possibly NULL pointer passed to trusted R1")
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
__failure __msg("Possibly NULL pointer passed to trusted R1")
int iter_ret_rcu_test_type_nostruct(const void *ctx)
{
	void *p;

	bpf_rcu_read_lock();
	p = bpf_kfunc_ret_rcu_test_nostruct(4);
	bpf_this_cpu_ptr(p);
	bpf_rcu_read_unlock();
	return 0;
}
