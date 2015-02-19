/*
 * procfs namespace bits
 */
#ifndef _LINUX_PROC_NS_H
#define _LINUX_PROC_NS_H

#include <linux/ns_common.h>

struct pid_namespace;
struct nsproxy;
struct path;

struct proc_ns_operations {
	const char *name;
	int type;
	struct ns_common *(*get)(struct task_struct *task);
	void (*put)(struct ns_common *ns);
	int (*install)(struct nsproxy *nsproxy, struct ns_common *ns);
};

extern const struct proc_ns_operations netns_operations;
extern const struct proc_ns_operations utsns_operations;
extern const struct proc_ns_operations ipcns_operations;
extern const struct proc_ns_operations pidns_operations;
extern const struct proc_ns_operations userns_operations;
extern const struct proc_ns_operations mntns_operations;

/*
 * We always define these enumerators
 */
enum {
	PROC_ROOT_INO		= 1,
	PROC_IPC_INIT_INO	= 0xEFFFFFFFU,
	PROC_UTS_INIT_INO	= 0xEFFFFFFEU,
	PROC_USER_INIT_INO	= 0xEFFFFFFDU,
	PROC_PID_INIT_INO	= 0xEFFFFFFCU,
};

#ifdef CONFIG_PROC_FS

extern int pid_ns_prepare_proc(struct pid_namespace *ns);
extern void pid_ns_release_proc(struct pid_namespace *ns);
extern int proc_alloc_inum(unsigned int *pino);
extern void proc_free_inum(unsigned int inum);

#else /* CONFIG_PROC_FS */

static inline int pid_ns_prepare_proc(struct pid_namespace *ns) { return 0; }
static inline void pid_ns_release_proc(struct pid_namespace *ns) {}

static inline int proc_alloc_inum(unsigned int *inum)
{
	*inum = 1;
	return 0;
}
static inline void proc_free_inum(unsigned int inum) {}

#endif /* CONFIG_PROC_FS */

static inline int ns_alloc_inum(struct ns_common *ns)
{
	atomic_long_set(&ns->stashed, 0);
	return proc_alloc_inum(&ns->inum);
}

#define ns_free_inum(ns) proc_free_inum((ns)->inum)

extern struct file *proc_ns_fget(int fd);
#define get_proc_ns(inode) ((struct ns_common *)(inode)->i_private)
extern void *ns_get_path(struct path *path, struct task_struct *task,
			const struct proc_ns_operations *ns_ops);

extern int ns_get_name(char *buf, size_t size, struct task_struct *task,
			const struct proc_ns_operations *ns_ops);
extern void nsfs_init(void);

#endif /* _LINUX_PROC_NS_H */
