/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The proc filesystem constants/structures
 */
#ifndef _LINUX_PROC_FS_H
#define _LINUX_PROC_FS_H

#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/fs.h>

struct proc_dir_entry;
struct seq_file;
struct seq_operations;

enum {
	/*
	 * All /proc entries using this ->proc_ops instance are never removed.
	 *
	 * If in doubt, ignore this flag.
	 */
#ifdef MODULE
	PROC_ENTRY_PERMANENT = 0U,
#else
	PROC_ENTRY_PERMANENT = 1U << 0,
#endif
};

struct proc_ops {
	unsigned int proc_flags;
	int	(*proc_open)(struct inode *, struct file *);
	ssize_t	(*proc_read)(struct file *, char __user *, size_t, loff_t *);
	ssize_t (*proc_read_iter)(struct kiocb *, struct iov_iter *);
	ssize_t	(*proc_write)(struct file *, const char __user *, size_t, loff_t *);
	/* mandatory unless nonseekable_open() or equivalent is used */
	loff_t	(*proc_lseek)(struct file *, loff_t, int);
	int	(*proc_release)(struct inode *, struct file *);
	__poll_t (*proc_poll)(struct file *, struct poll_table_struct *);
	long	(*proc_ioctl)(struct file *, unsigned int, unsigned long);
#ifdef CONFIG_COMPAT
	long	(*proc_compat_ioctl)(struct file *, unsigned int, unsigned long);
#endif
	int	(*proc_mmap)(struct file *, struct vm_area_struct *);
	unsigned long (*proc_get_unmapped_area)(struct file *, unsigned long, unsigned long, unsigned long, unsigned long);
} __randomize_layout;

/* definitions for hide_pid field */
enum proc_hidepid {
	HIDEPID_OFF	  = 0,
	HIDEPID_NO_ACCESS = 1,
	HIDEPID_INVISIBLE = 2,
	HIDEPID_NOT_PTRACEABLE = 4, /* Limit pids to only ptraceable pids */
};

/* definitions for proc mount option pidonly */
enum proc_pidonly {
	PROC_PIDONLY_OFF = 0,
	PROC_PIDONLY_ON  = 1,
};

struct proc_fs_info {
	struct pid_namespace *pid_ns;
	struct dentry *proc_self;        /* For /proc/self */
	struct dentry *proc_thread_self; /* For /proc/thread-self */
	kgid_t pid_gid;
	enum proc_hidepid hide_pid;
	enum proc_pidonly pidonly;
	struct rcu_head rcu;
};

static inline struct proc_fs_info *proc_sb_info(struct super_block *sb)
{
	return sb->s_fs_info;
}

#ifdef CONFIG_PROC_FS

typedef int (*proc_write_t)(struct file *, char *, size_t);

extern void proc_root_init(void);
extern void proc_flush_pid(struct pid *);

extern struct proc_dir_entry *proc_symlink(const char *,
		struct proc_dir_entry *, const char *);
struct proc_dir_entry *_proc_mkdir(const char *, umode_t, struct proc_dir_entry *, void *, bool);
extern struct proc_dir_entry *proc_mkdir(const char *, struct proc_dir_entry *);
extern struct proc_dir_entry *proc_mkdir_data(const char *, umode_t,
					      struct proc_dir_entry *, void *);
extern struct proc_dir_entry *proc_mkdir_mode(const char *, umode_t,
					      struct proc_dir_entry *);
struct proc_dir_entry *proc_create_mount_point(const char *name);

struct proc_dir_entry *proc_create_seq_private(const char *name, umode_t mode,
		struct proc_dir_entry *parent, const struct seq_operations *ops,
		unsigned int state_size, void *data);
#define proc_create_seq_data(name, mode, parent, ops, data) \
	proc_create_seq_private(name, mode, parent, ops, 0, data)
#define proc_create_seq(name, mode, parent, ops) \
	proc_create_seq_private(name, mode, parent, ops, 0, NULL)
struct proc_dir_entry *proc_create_single_data(const char *name, umode_t mode,
		struct proc_dir_entry *parent,
		int (*show)(struct seq_file *, void *), void *data);
#define proc_create_single(name, mode, parent, show) \
	proc_create_single_data(name, mode, parent, show, NULL)
 
extern struct proc_dir_entry *proc_create_data(const char *, umode_t,
					       struct proc_dir_entry *,
					       const struct proc_ops *,
					       void *);

struct proc_dir_entry *proc_create(const char *name, umode_t mode, struct proc_dir_entry *parent, const struct proc_ops *proc_ops);
extern void proc_set_size(struct proc_dir_entry *, loff_t);
extern void proc_set_user(struct proc_dir_entry *, kuid_t, kgid_t);

/*
 * Obtain the private data passed by user through proc_create_data() or
 * related.
 */
static inline void *pde_data(const struct inode *inode)
{
	return inode->i_private;
}

extern void *proc_get_parent_data(const struct inode *);
extern void proc_remove(struct proc_dir_entry *);
extern void remove_proc_entry(const char *, struct proc_dir_entry *);
extern int remove_proc_subtree(const char *, struct proc_dir_entry *);

struct proc_dir_entry *proc_create_net_data(const char *name, umode_t mode,
		struct proc_dir_entry *parent, const struct seq_operations *ops,
		unsigned int state_size, void *data);
#define proc_create_net(name, mode, parent, ops, state_size) \
	proc_create_net_data(name, mode, parent, ops, state_size, NULL)
struct proc_dir_entry *proc_create_net_single(const char *name, umode_t mode,
		struct proc_dir_entry *parent,
		int (*show)(struct seq_file *, void *), void *data);
struct proc_dir_entry *proc_create_net_data_write(const char *name, umode_t mode,
						  struct proc_dir_entry *parent,
						  const struct seq_operations *ops,
						  proc_write_t write,
						  unsigned int state_size, void *data);
struct proc_dir_entry *proc_create_net_single_write(const char *name, umode_t mode,
						    struct proc_dir_entry *parent,
						    int (*show)(struct seq_file *, void *),
						    proc_write_t write,
						    void *data);
extern struct pid *tgid_pidfd_to_pid(const struct file *file);

struct bpf_iter_aux_info;
extern int bpf_iter_init_seq_net(void *priv_data, struct bpf_iter_aux_info *aux);
extern void bpf_iter_fini_seq_net(void *priv_data);

#ifdef CONFIG_PROC_PID_ARCH_STATUS
/*
 * The architecture which selects CONFIG_PROC_PID_ARCH_STATUS must
 * provide proc_pid_arch_status() definition.
 */
int proc_pid_arch_status(struct seq_file *m, struct pid_namespace *ns,
			struct pid *pid, struct task_struct *task);
#endif /* CONFIG_PROC_PID_ARCH_STATUS */

void arch_report_meminfo(struct seq_file *m);
void arch_proc_pid_thread_features(struct seq_file *m, struct task_struct *task);

#else /* CONFIG_PROC_FS */

static inline void proc_root_init(void)
{
}

static inline void proc_flush_pid(struct pid *pid)
{
}

static inline struct proc_dir_entry *proc_symlink(const char *name,
		struct proc_dir_entry *parent,const char *dest) { return NULL;}
static inline struct proc_dir_entry *proc_mkdir(const char *name,
	struct proc_dir_entry *parent) {return NULL;}
static inline struct proc_dir_entry *proc_create_mount_point(const char *name) { return NULL; }
static inline struct proc_dir_entry *_proc_mkdir(const char *name, umode_t mode,
		struct proc_dir_entry *parent, void *data, bool force_lookup)
{
	return NULL;
}
static inline struct proc_dir_entry *proc_mkdir_data(const char *name,
	umode_t mode, struct proc_dir_entry *parent, void *data) { return NULL; }
static inline struct proc_dir_entry *proc_mkdir_mode(const char *name,
	umode_t mode, struct proc_dir_entry *parent) { return NULL; }
#define proc_create_seq_private(name, mode, parent, ops, size, data) ({NULL;})
#define proc_create_seq_data(name, mode, parent, ops, data) ({NULL;})
#define proc_create_seq(name, mode, parent, ops) ({NULL;})
#define proc_create_single(name, mode, parent, show) ({NULL;})
#define proc_create_single_data(name, mode, parent, show, data) ({NULL;})

static inline struct proc_dir_entry *
proc_create(const char *name, umode_t mode, struct proc_dir_entry *parent,
	    const struct proc_ops *proc_ops)
{ return NULL; }

static inline struct proc_dir_entry *
proc_create_data(const char *name, umode_t mode, struct proc_dir_entry *parent,
		 const struct proc_ops *proc_ops, void *data)
{ return NULL; }

static inline void proc_set_size(struct proc_dir_entry *de, loff_t size) {}
static inline void proc_set_user(struct proc_dir_entry *de, kuid_t uid, kgid_t gid) {}
static inline void *pde_data(const struct inode *inode) {BUG(); return NULL;}
static inline void *proc_get_parent_data(const struct inode *inode) { BUG(); return NULL; }

static inline void proc_remove(struct proc_dir_entry *de) {}
#define remove_proc_entry(name, parent) do {} while (0)
static inline int remove_proc_subtree(const char *name, struct proc_dir_entry *parent) { return 0; }

#define proc_create_net_data(name, mode, parent, ops, state_size, data) ({NULL;})
#define proc_create_net_data_write(name, mode, parent, ops, write, state_size, data) ({NULL;})
#define proc_create_net(name, mode, parent, state_size, ops) ({NULL;})
#define proc_create_net_single(name, mode, parent, show, data) ({NULL;})
#define proc_create_net_single_write(name, mode, parent, show, write, data) ({NULL;})

static inline struct pid *tgid_pidfd_to_pid(const struct file *file)
{
	return ERR_PTR(-EBADF);
}

#endif /* CONFIG_PROC_FS */

struct net;

static inline struct proc_dir_entry *proc_net_mkdir(
	struct net *net, const char *name, struct proc_dir_entry *parent)
{
	return _proc_mkdir(name, 0, parent, net, true);
}

struct ns_common;
int open_related_ns(struct ns_common *ns,
		   struct ns_common *(*get_ns)(struct ns_common *ns));

/* get the associated pid namespace for a file in procfs */
static inline struct pid_namespace *proc_pid_ns(struct super_block *sb)
{
	return proc_sb_info(sb)->pid_ns;
}

bool proc_ns_file(const struct file *file);

#endif /* _LINUX_PROC_FS_H */
