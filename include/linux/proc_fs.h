#ifndef _LINUX_PROC_FS_H
#define _LINUX_PROC_FS_H

#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/spinlock.h>
#include <linux/magic.h>
#include <linux/atomic.h>
#include <linux/proc_ns.h>

struct net;
struct completion;
struct mm_struct;

/*
 * The proc filesystem constants/structures
 */

/*
 * Offset of the first process in the /proc root directory..
 */
#define FIRST_PROCESS_ENTRY 256

/* Worst case buffer size needed for holding an integer. */
#define PROC_NUMBUF 13

/*
 * This is not completely implemented yet. The idea is to
 * create an in-memory tree (like the actual /proc filesystem
 * tree) of these proc_dir_entries, so that we can dynamically
 * add new files to /proc.
 *
 * The "next" pointer creates a linked list of one /proc directory,
 * while parent/subdir create the directory structure (every
 * /proc file has a parent, but "subdir" is NULL for all
 * non-directory entries).
 */

struct proc_dir_entry {
	unsigned int low_ino;
	umode_t mode;
	nlink_t nlink;
	kuid_t uid;
	kgid_t gid;
	loff_t size;
	const struct inode_operations *proc_iops;
	const struct file_operations *proc_fops;
	struct proc_dir_entry *next, *parent, *subdir;
	void *data;
	atomic_t count;		/* use count */
	atomic_t in_use;	/* number of callers into module in progress; */
			/* negative -> it's going away RSN */
	struct completion *pde_unload_completion;
	struct list_head pde_openers;	/* who did ->open, but not ->release */
	spinlock_t pde_unload_lock; /* proc_fops checks and pde_users bumps */
	u8 namelen;
	char name[];
};

#ifdef CONFIG_PROC_FS

extern void proc_root_init(void);

void proc_flush_task(struct task_struct *task);

struct proc_dir_entry *proc_create_data(const char *name, umode_t mode,
				struct proc_dir_entry *parent,
				const struct file_operations *proc_fops,
				void *data);
extern void remove_proc_entry(const char *name, struct proc_dir_entry *parent);
extern int remove_proc_subtree(const char *name, struct proc_dir_entry *parent);

extern struct proc_dir_entry *proc_symlink(const char *,
		struct proc_dir_entry *, const char *);
extern struct proc_dir_entry *proc_mkdir(const char *,struct proc_dir_entry *);
extern struct proc_dir_entry *proc_mkdir_mode(const char *name, umode_t mode,
			struct proc_dir_entry *parent);

static inline struct proc_dir_entry *proc_create(const char *name, umode_t mode,
	struct proc_dir_entry *parent, const struct file_operations *proc_fops)
{
	return proc_create_data(name, mode, parent, proc_fops, NULL);
}
 
extern struct proc_dir_entry *proc_net_mkdir(struct net *net, const char *name,
	struct proc_dir_entry *parent);

extern void proc_set_size(struct proc_dir_entry *, loff_t);
extern void proc_set_user(struct proc_dir_entry *, kuid_t, kgid_t);
#else

static inline void proc_flush_task(struct task_struct *task)
{
}

#define proc_create(name, mode, parent, fops)  ({ (void)(mode), NULL; })

static inline struct proc_dir_entry *proc_create_data(const char *name,
	umode_t mode, struct proc_dir_entry *parent,
	const struct file_operations *proc_fops, void *data)
{
	return NULL;
}
#define remove_proc_entry(name, parent) do {} while (0)
#define remove_proc_subtree(name, parent) do {} while (0)

static inline struct proc_dir_entry *proc_symlink(const char *name,
		struct proc_dir_entry *parent,const char *dest) {return NULL;}
static inline struct proc_dir_entry *proc_mkdir(const char *name,
	struct proc_dir_entry *parent) {return NULL;}
static inline struct proc_dir_entry *proc_mkdir_mode(const char *name,
	umode_t mode, struct proc_dir_entry *parent) { return NULL; }
static inline void proc_set_size(struct proc_dir_entry *de, loff_t size) {}
static inline void proc_set_user(struct proc_dir_entry *de, kuid_t uid, kgid_t gid) {}

#endif /* CONFIG_PROC_FS */


union proc_op {
	int (*proc_get_link)(struct dentry *, struct path *);
	int (*proc_read)(struct task_struct *task, char *page);
	int (*proc_show)(struct seq_file *m,
		struct pid_namespace *ns, struct pid *pid,
		struct task_struct *task);
};

struct ctl_table_header;
struct ctl_table;

struct proc_inode {
	struct pid *pid;
	int fd;
	union proc_op op;
	struct proc_dir_entry *pde;
	struct ctl_table_header *sysctl;
	struct ctl_table *sysctl_entry;
	struct proc_ns ns;
	struct inode vfs_inode;
};

static inline struct proc_inode *PROC_I(const struct inode *inode)
{
	return container_of(inode, struct proc_inode, vfs_inode);
}

static inline struct proc_dir_entry *PDE(const struct inode *inode)
{
	return PROC_I(inode)->pde;
}

static inline void *PDE_DATA(const struct inode *inode)
{
	return PROC_I(inode)->pde->data;
}

#endif /* _LINUX_PROC_FS_H */
