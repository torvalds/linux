/* SPDX-License-Identifier: GPL-2.0 */
/*
 * procfs namespace bits
 */
#ifndef _LINUX_PROC_NS_H
#define _LINUX_PROC_NS_H

#include <linux/nsfs.h>
#include <uapi/linux/nsfs.h>

struct pid_namespace;
struct nsset;
struct path;
struct task_struct;
struct inode;

struct proc_ns_operations {
	const char *name;
	const char *real_ns_name;
	struct ns_common *(*get)(struct task_struct *task);
	void (*put)(struct ns_common *ns);
	int (*install)(struct nsset *nsset, struct ns_common *ns);
	struct user_namespace *(*owner)(struct ns_common *ns);
	struct ns_common *(*get_parent)(struct ns_common *ns);
} __randomize_layout;

extern const struct proc_ns_operations netns_operations;
extern const struct proc_ns_operations utsns_operations;
extern const struct proc_ns_operations ipcns_operations;
extern const struct proc_ns_operations pidns_operations;
extern const struct proc_ns_operations pidns_for_children_operations;
extern const struct proc_ns_operations userns_operations;
extern const struct proc_ns_operations mntns_operations;
extern const struct proc_ns_operations cgroupns_operations;
extern const struct proc_ns_operations timens_operations;
extern const struct proc_ns_operations timens_for_children_operations;

/*
 * We always define these enumerators
 */
enum {
	PROC_IPC_INIT_INO	= IPC_NS_INIT_INO,
	PROC_UTS_INIT_INO	= UTS_NS_INIT_INO,
	PROC_USER_INIT_INO	= USER_NS_INIT_INO,
	PROC_PID_INIT_INO	= PID_NS_INIT_INO,
	PROC_CGROUP_INIT_INO	= CGROUP_NS_INIT_INO,
	PROC_TIME_INIT_INO	= TIME_NS_INIT_INO,
	PROC_NET_INIT_INO	= NET_NS_INIT_INO,
	PROC_MNT_INIT_INO	= MNT_NS_INIT_INO,
};

#ifdef CONFIG_PROC_FS

extern int proc_alloc_inum(unsigned int *pino);
extern void proc_free_inum(unsigned int inum);

#else /* CONFIG_PROC_FS */

static inline int proc_alloc_inum(unsigned int *inum)
{
	*inum = 1;
	return 0;
}
static inline void proc_free_inum(unsigned int inum) {}

#endif /* CONFIG_PROC_FS */

#define get_proc_ns(inode) ((struct ns_common *)(inode)->i_private)

#endif /* _LINUX_PROC_NS_H */
