#ifndef _LINUX_PROC_FS_H
#define _LINUX_PROC_FS_H

#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/spinlock.h>
#include <linux/magic.h>
#include <linux/atomic.h>

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
 * We always define these enumerators
 */

enum {
	PROC_ROOT_INO		= 1,
	PROC_IPC_INIT_INO	= 0xEFFFFFFFU,
	PROC_UTS_INIT_INO	= 0xEFFFFFFEU,
	PROC_USER_INIT_INO	= 0xEFFFFFFDU,
	PROC_PID_INIT_INO	= 0xEFFFFFFCU,
};

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

typedef	int (read_proc_t)(char *page, char **start, off_t off,
			  int count, int *eof, void *data);
typedef	int (write_proc_t)(struct file *file, const char __user *buffer,
			   unsigned long count, void *data);

struct proc_dir_entry {
	unsigned int low_ino;
	umode_t mode;
	nlink_t nlink;
	kuid_t uid;
	kgid_t gid;
	loff_t size;
	const struct inode_operations *proc_iops;
	/*
	 * NULL ->proc_fops means "PDE is going away RSN" or
	 * "PDE is just created". In either case, e.g. ->read_proc won't be
	 * called because it's too late or too early, respectively.
	 *
	 * If you're allocating ->proc_fops dynamically, save a pointer
	 * somewhere.
	 */
	const struct file_operations *proc_fops;
	struct proc_dir_entry *next, *parent, *subdir;
	void *data;
	read_proc_t *read_proc;
	write_proc_t *write_proc;
	atomic_t count;		/* use count */
	int pde_users;	/* number of callers into module in progress */
	struct completion *pde_unload_completion;
	struct list_head pde_openers;	/* who did ->open, but not ->release */
	spinlock_t pde_unload_lock; /* proc_fops checks and pde_users bumps */
	u8 namelen;
	char name[];
};

enum kcore_type {
	KCORE_TEXT,
	KCORE_VMALLOC,
	KCORE_RAM,
	KCORE_VMEMMAP,
	KCORE_OTHER,
};

struct kcore_list {
	struct list_head list;
	unsigned long addr;
	size_t size;
	int type;
};

struct vmcore {
	struct list_head list;
	unsigned long long paddr;
	unsigned long long size;
	loff_t offset;
};

#ifdef CONFIG_PROC_FS

extern void proc_root_init(void);

void proc_flush_task(struct task_struct *task);

extern struct proc_dir_entry *create_proc_entry(const char *name, umode_t mode,
						struct proc_dir_entry *parent);
struct proc_dir_entry *proc_create_data(const char *name, umode_t mode,
				struct proc_dir_entry *parent,
				const struct file_operations *proc_fops,
				void *data);
extern void remove_proc_entry(const char *name, struct proc_dir_entry *parent);

struct pid_namespace;

extern int pid_ns_prepare_proc(struct pid_namespace *ns);
extern void pid_ns_release_proc(struct pid_namespace *ns);

/*
 * proc_tty.c
 */
struct tty_driver;
extern void proc_tty_init(void);
extern void proc_tty_register_driver(struct tty_driver *driver);
extern void proc_tty_unregister_driver(struct tty_driver *driver);

/*
 * proc_devtree.c
 */
#ifdef CONFIG_PROC_DEVICETREE
struct device_node;
struct property;
extern void proc_device_tree_init(void);
extern void proc_device_tree_add_node(struct device_node *, struct proc_dir_entry *);
extern void proc_device_tree_add_prop(struct proc_dir_entry *pde, struct property *prop);
extern void proc_device_tree_remove_prop(struct proc_dir_entry *pde,
					 struct property *prop);
extern void proc_device_tree_update_prop(struct proc_dir_entry *pde,
					 struct property *newprop,
					 struct property *oldprop);
#endif /* CONFIG_PROC_DEVICETREE */

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

static inline struct proc_dir_entry *create_proc_read_entry(const char *name,
	umode_t mode, struct proc_dir_entry *base, 
	read_proc_t *read_proc, void * data)
{
	struct proc_dir_entry *res=create_proc_entry(name,mode,base);
	if (res) {
		res->read_proc=read_proc;
		res->data=data;
	}
	return res;
}
 
extern struct proc_dir_entry *proc_net_fops_create(struct net *net,
	const char *name, umode_t mode, const struct file_operations *fops);
extern void proc_net_remove(struct net *net, const char *name);
extern struct proc_dir_entry *proc_net_mkdir(struct net *net, const char *name,
	struct proc_dir_entry *parent);

extern struct file *proc_ns_fget(int fd);
extern bool proc_ns_inode(struct inode *inode);

extern int proc_alloc_inum(unsigned int *pino);
extern void proc_free_inum(unsigned int inum);
#else

#define proc_net_fops_create(net, name, mode, fops)  ({ (void)(mode), NULL; })
static inline void proc_net_remove(struct net *net, const char *name) {}

static inline void proc_flush_task(struct task_struct *task)
{
}

static inline struct proc_dir_entry *create_proc_entry(const char *name,
	umode_t mode, struct proc_dir_entry *parent) { return NULL; }
static inline struct proc_dir_entry *proc_create(const char *name,
	umode_t mode, struct proc_dir_entry *parent,
	const struct file_operations *proc_fops)
{
	return NULL;
}
static inline struct proc_dir_entry *proc_create_data(const char *name,
	umode_t mode, struct proc_dir_entry *parent,
	const struct file_operations *proc_fops, void *data)
{
	return NULL;
}
#define remove_proc_entry(name, parent) do {} while (0)

static inline struct proc_dir_entry *proc_symlink(const char *name,
		struct proc_dir_entry *parent,const char *dest) {return NULL;}
static inline struct proc_dir_entry *proc_mkdir(const char *name,
	struct proc_dir_entry *parent) {return NULL;}
static inline struct proc_dir_entry *proc_mkdir_mode(const char *name,
	umode_t mode, struct proc_dir_entry *parent) { return NULL; }

static inline struct proc_dir_entry *create_proc_read_entry(const char *name,
	umode_t mode, struct proc_dir_entry *base, 
	read_proc_t *read_proc, void * data) { return NULL; }

struct tty_driver;
static inline void proc_tty_register_driver(struct tty_driver *driver) {};
static inline void proc_tty_unregister_driver(struct tty_driver *driver) {};

static inline int pid_ns_prepare_proc(struct pid_namespace *ns)
{
	return 0;
}

static inline void pid_ns_release_proc(struct pid_namespace *ns)
{
}

static inline struct file *proc_ns_fget(int fd)
{
	return ERR_PTR(-EINVAL);
}

static inline bool proc_ns_inode(struct inode *inode)
{
	return false;
}

static inline int proc_alloc_inum(unsigned int *inum)
{
	*inum = 1;
	return 0;
}
static inline void proc_free_inum(unsigned int inum)
{
}
#endif /* CONFIG_PROC_FS */

#if !defined(CONFIG_PROC_KCORE)
static inline void
kclist_add(struct kcore_list *new, void *addr, size_t size, int type)
{
}
#else
extern void kclist_add(struct kcore_list *, void *, size_t, int type);
#endif

struct nsproxy;
struct proc_ns_operations {
	const char *name;
	int type;
	void *(*get)(struct task_struct *task);
	void (*put)(void *ns);
	int (*install)(struct nsproxy *nsproxy, void *ns);
	unsigned int (*inum)(void *ns);
};
extern const struct proc_ns_operations netns_operations;
extern const struct proc_ns_operations utsns_operations;
extern const struct proc_ns_operations ipcns_operations;
extern const struct proc_ns_operations pidns_operations;
extern const struct proc_ns_operations userns_operations;
extern const struct proc_ns_operations mntns_operations;

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
	void *ns;
	const struct proc_ns_operations *ns_ops;
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

static inline struct net *PDE_NET(struct proc_dir_entry *pde)
{
	return pde->parent->data;
}

#include <linux/signal.h>

void render_sigset_t(struct seq_file *m, const char *header, sigset_t *set);
#endif /* _LINUX_PROC_FS_H */
