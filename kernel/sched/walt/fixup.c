// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <trace/hooks/cpufreq.h>

#include "walt.h"

unsigned int cpuinfo_max_freq_cached;

char sched_lib_name[LIB_PATH_LENGTH];
unsigned int sched_lib_mask_force;

static bool is_sched_lib_based_app(pid_t pid)
{
	const char *name = NULL;
	char *libname, *lib_list;
	struct vm_area_struct *vma;
	char path_buf[LIB_PATH_LENGTH];
	char *tmp_lib_name;
	bool found = false;
	struct task_struct *p;
	struct mm_struct *mm;

	if (strnlen(sched_lib_name, LIB_PATH_LENGTH) == 0)
		return false;

	tmp_lib_name = kmalloc(LIB_PATH_LENGTH, GFP_KERNEL);
	if (!tmp_lib_name)
		return false;

	rcu_read_lock();
	p = pid ? get_pid_task(find_vpid(pid), PIDTYPE_PID) : get_task_struct(current);
	rcu_read_unlock();
	if (!p) {
		kfree(tmp_lib_name);
		return false;
	}

	mm = get_task_mm(p);
	if (mm) {
		MA_STATE(mas, &mm->mm_mt, 0, 0);
		down_read(&mm->mmap_lock);

		mas_for_each(&mas, vma, ULONG_MAX) {
			if (vma->vm_file && vma->vm_flags & VM_EXEC) {
				name = d_path(&vma->vm_file->f_path,
						path_buf, LIB_PATH_LENGTH);
				if (IS_ERR(name))
					goto release_sem;

				strlcpy(tmp_lib_name, sched_lib_name, LIB_PATH_LENGTH);
				lib_list = tmp_lib_name;
				while ((libname = strsep(&lib_list, ","))) {
					libname = skip_spaces(libname);
					if (strnstr(name, libname,
						strnlen(name, LIB_PATH_LENGTH))) {
						found = true;
						goto release_sem;
					}
				}
			}
		}

release_sem:
		up_read(&mm->mmap_lock);
		mmput(mm);

	}
	put_task_struct(p);
	kfree(tmp_lib_name);
	return found;
}

static void android_rvh_show_max_freq(void *unused, struct cpufreq_policy *policy,
				     unsigned int *max_freq)
{
	if (!cpuinfo_max_freq_cached)
		return;

	if (!(BIT(policy->cpu) & sched_lib_mask_force))
		return;

	if (is_sched_lib_based_app(current->pid))
		*max_freq = cpuinfo_max_freq_cached << 1;
}

void walt_fixup_init(void)
{
	register_trace_android_rvh_show_max_freq(android_rvh_show_max_freq, NULL);
}
