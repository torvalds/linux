/* process_kobject.h, (C) 2002 Milan Pikula */

#ifndef _TASK_KOBJECT_H
#define _TASK_KOBJECT_H

/* TASK kobject: this file defines the kobject structure for task, e.g.
 * the data, which we want to pass to the authorization server.
 *
 * The structure contains some data from ordinary task_struct
 * (such as pid etc.), and some data from medusa_l1_task_s, which is
 * defined in medusa/l1/task.h.
 */

// #include <linux/medusa/l1/task.h>
#include <linux/sched.h>	/* contains all includes we need ;) */
#include <linux/medusa/l3/kobject.h>
#include <linux/medusa/l1/task.h>
#include <linux/medusa/l1/process_handlers.h>

#define task_security(task) (*((struct medusa_l1_task_s*)task_cred_xxx((task), security)))
#define task_suid(task) (task_cred_xxx((task), suid))
#define task_fsuid(task) (task_cred_xxx((task), fsuid))
#define task_gid(task) (task_cred_xxx((task), gid))
#define task_egid(task) (task_cred_xxx((task), egid))
#define task_sgid(task) (task_cred_xxx((task), sgid))
#define task_fsgid(task) (task_cred_xxx((task), fsgid))
#define task_cap_inheritable(task) (task_cred_xxx((task), cap_inheritable))
#define task_cap_effective(task) (task_cred_xxx((task), cap_effective))
#define task_cap_permitted(task) (task_cred_xxx((task), cap_permitted))

struct process_kobject { /* was: m_proc_inf */
	MEDUSA_KOBJECT_HEADER;

	pid_t pid, parent_pid, child_pid, sibling_pid;
	struct pid* pgrp;
	kuid_t uid, euid, suid, fsuid;
	kgid_t gid, egid, sgid, fsgid;

	kuid_t luid;
	kernel_cap_t ecap, icap, pcap;
	MEDUSA_SUBJECT_VARS;
	MEDUSA_OBJECT_VARS;
	__u32 user;
#ifdef CONFIG_MEDUSA_SYSCALL
	/* FIXME: this is wrong on non-i386 architectures */

		/* bitmap of syscalls, which are reported */
	unsigned char med_syscall[NR_syscalls / (sizeof(unsigned char) * 8)];
#endif
};
extern MED_DECLARE_KCLASSOF(process_kobject);

int process_kobj2kern(struct process_kobject * tk, struct task_struct * ts);
int process_kern2kobj(struct process_kobject * tk, struct task_struct * ts);

#endif
