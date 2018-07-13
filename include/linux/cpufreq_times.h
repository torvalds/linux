/* drivers/cpufreq/cpufreq_times.c
 *
 * Copyright (C) 2018 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _LINUX_CPUFREQ_TIMES_H
#define _LINUX_CPUFREQ_TIMES_H

#include <linux/cpufreq.h>
#include <linux/cputime.h>
#include <linux/pid.h>

#ifdef CONFIG_CPU_FREQ_TIMES
void cpufreq_task_times_init(struct task_struct *p);
void cpufreq_task_times_exit(struct task_struct *p);
int proc_time_in_state_show(struct seq_file *m, struct pid_namespace *ns,
			    struct pid *pid, struct task_struct *p);
void cpufreq_acct_update_power(struct task_struct *p, cputime_t cputime);
void cpufreq_times_create_policy(struct cpufreq_policy *policy);
void cpufreq_times_record_transition(struct cpufreq_freqs *freq);
void cpufreq_task_times_remove_uids(uid_t uid_start, uid_t uid_end);
int single_uid_time_in_state_open(struct inode *inode, struct file *file);
#else
static inline void cpufreq_task_times_init(struct task_struct *p) {}
static inline void cpufreq_task_times_exit(struct task_struct *p) {}
static inline void cpufreq_acct_update_power(struct task_struct *p,
					     u64 cputime) {}
static inline void cpufreq_times_create_policy(struct cpufreq_policy *policy) {}
static inline void cpufreq_times_record_transition(
	struct cpufreq_freqs *freq) {}
static inline void cpufreq_task_times_remove_uids(uid_t uid_start,
						  uid_t uid_end) {}
#endif /* CONFIG_CPU_FREQ_TIMES */
#endif /* _LINUX_CPUFREQ_TIMES_H */
