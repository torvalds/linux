/*
 * procfs namespace bits
 */
#ifndef _LINUX_PROC_NS_H
#define _LINUX_PROC_NS_H

struct pid_namespace;
struct nsproxy;
struct ns_common;

struct proc_ns_operations {
	const char *name;
	int type;
	struct ns_common *(*get)(struct task_struct *task);
	void (*put)(struct ns_common *ns);
	int (*install)(struct nsproxy *nsproxy, struct ns_common *ns);
};

struct proc_ns {
	struct ns_common *ns;
	const struct proc_ns_operations *ns_ops;
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
extern struct file *proc_ns_fget(int fd);
extern struct proc_ns *get_proc_ns(struct inode *);
extern int proc_alloc_inum(unsigned int *pino);
extern void proc_free_inum(unsigned int inum);
extern bool proc_ns_inode(struct inode *inode);

#else /* CONFIG_PROC_FS */

static inline int pid_ns_prepare_proc(struct pid_namespace *ns) { return 0; }
static inline void pid_ns_release_proc(struct pid_namespace *ns) {}

static inline struct file *proc_ns_fget(int fd)
{
	return ERR_PTR(-EINVAL);
}

static inline struct proc_ns *get_proc_ns(struct inode *inode) { return NULL; }

static inline int proc_alloc_inum(unsigned int *inum)
{
	*inum = 1;
	return 0;
}
static inline void proc_free_inum(unsigned int inum) {}
static inline bool proc_ns_inode(struct inode *inode) { return false; }

#endif /* CONFIG_PROC_FS */

#endif /* _LINUX_PROC_NS_H */
