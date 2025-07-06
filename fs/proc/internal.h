/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Internal procfs definitions
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/proc_fs.h>
#include <linux/proc_ns.h>
#include <linux/refcount.h>
#include <linux/spinlock.h>
#include <linux/atomic.h>
#include <linux/binfmts.h>
#include <linux/sched/coredump.h>
#include <linux/sched/task.h>
#include <linux/mm.h>

struct ctl_table_header;
struct mempolicy;

/*
 * This is not completely implemented yet. The idea is to
 * create an in-memory tree (like the actual /proc filesystem
 * tree) of these proc_dir_entries, so that we can dynamically
 * add new files to /proc.
 *
 * parent/subdir are used for the directory structure (every /proc file has a
 * parent, but "subdir" is empty for all non-directory entries).
 * subdir_node is used to build the rb tree "subdir" of the parent.
 */
struct proc_dir_entry {
	/*
	 * number of callers into module in progress;
	 * negative -> it's going away RSN
	 */
	atomic_t in_use;
	refcount_t refcnt;
	struct list_head pde_openers;	/* who did ->open, but not ->release */
	/* protects ->pde_openers and all struct pde_opener instances */
	spinlock_t pde_unload_lock;
	struct completion *pde_unload_completion;
	const struct inode_operations *proc_iops;
	union {
		const struct proc_ops *proc_ops;
		const struct file_operations *proc_dir_ops;
	};
	const struct dentry_operations *proc_dops;
	union {
		const struct seq_operations *seq_ops;
		int (*single_show)(struct seq_file *, void *);
	};
	proc_write_t write;
	void *data;
	unsigned int state_size;
	unsigned int low_ino;
	nlink_t nlink;
	kuid_t uid;
	kgid_t gid;
	loff_t size;
	struct proc_dir_entry *parent;
	struct rb_root subdir;
	struct rb_node subdir_node;
	char *name;
	umode_t mode;
	u8 flags;
	u8 namelen;
	char inline_name[];
} __randomize_layout;

#define SIZEOF_PDE	(				\
	sizeof(struct proc_dir_entry) < 128 ? 128 :	\
	sizeof(struct proc_dir_entry) < 192 ? 192 :	\
	sizeof(struct proc_dir_entry) < 256 ? 256 :	\
	sizeof(struct proc_dir_entry) < 512 ? 512 :	\
	0)
#define SIZEOF_PDE_INLINE_NAME (SIZEOF_PDE - sizeof(struct proc_dir_entry))

static inline bool pde_is_permanent(const struct proc_dir_entry *pde)
{
	return pde->flags & PROC_ENTRY_PERMANENT;
}

static inline void pde_make_permanent(struct proc_dir_entry *pde)
{
	pde->flags |= PROC_ENTRY_PERMANENT;
}

static inline bool pde_has_proc_read_iter(const struct proc_dir_entry *pde)
{
	return pde->flags & PROC_ENTRY_proc_read_iter;
}

static inline bool pde_has_proc_compat_ioctl(const struct proc_dir_entry *pde)
{
#ifdef CONFIG_COMPAT
	return pde->flags & PROC_ENTRY_proc_compat_ioctl;
#else
	return false;
#endif
}

extern struct kmem_cache *proc_dir_entry_cache;
void pde_free(struct proc_dir_entry *pde);

union proc_op {
	int (*proc_get_link)(struct dentry *, struct path *);
	int (*proc_show)(struct seq_file *m,
		struct pid_namespace *ns, struct pid *pid,
		struct task_struct *task);
	int lsmid;
};

struct proc_inode {
	struct pid *pid;
	unsigned int fd;
	union proc_op op;
	struct proc_dir_entry *pde;
	struct ctl_table_header *sysctl;
	const struct ctl_table *sysctl_entry;
	struct hlist_node sibling_inodes;
	const struct proc_ns_operations *ns_ops;
	struct inode vfs_inode;
} __randomize_layout;

/*
 * General functions
 */
static inline struct proc_inode *PROC_I(const struct inode *inode)
{
	return container_of(inode, struct proc_inode, vfs_inode);
}

static inline struct proc_dir_entry *PDE(const struct inode *inode)
{
	return PROC_I(inode)->pde;
}

static inline struct pid *proc_pid(const struct inode *inode)
{
	return PROC_I(inode)->pid;
}

static inline struct task_struct *get_proc_task(const struct inode *inode)
{
	return get_pid_task(proc_pid(inode), PIDTYPE_PID);
}

void task_dump_owner(struct task_struct *task, umode_t mode,
		     kuid_t *ruid, kgid_t *rgid);

unsigned name_to_int(const struct qstr *qstr);
/*
 * Offset of the first process in the /proc root directory..
 */
#define FIRST_PROCESS_ENTRY 256

/* Worst case buffer size needed for holding an integer. */
#define PROC_NUMBUF 13

#ifdef CONFIG_PAGE_MAPCOUNT
/**
 * folio_precise_page_mapcount() - Number of mappings of this folio page.
 * @folio: The folio.
 * @page: The page.
 *
 * The number of present user page table entries that reference this page
 * as tracked via the RMAP: either referenced directly (PTE) or as part of
 * a larger area that covers this page (e.g., PMD).
 *
 * Use this function only for the calculation of existing statistics
 * (USS, PSS, mapcount_max) and for debugging purposes (/proc/kpagecount).
 *
 * Do not add new users.
 *
 * Returns: The number of mappings of this folio page. 0 for
 * folios that are not mapped to user space or are not tracked via the RMAP
 * (e.g., shared zeropage).
 */
static inline int folio_precise_page_mapcount(struct folio *folio,
		struct page *page)
{
	int mapcount = atomic_read(&page->_mapcount) + 1;

	if (page_mapcount_is_type(mapcount))
		mapcount = 0;
	if (folio_test_large(folio))
		mapcount += folio_entire_mapcount(folio);

	return mapcount;
}
#else /* !CONFIG_PAGE_MAPCOUNT */
static inline int folio_precise_page_mapcount(struct folio *folio,
		struct page *page)
{
	BUILD_BUG();
}
#endif /* CONFIG_PAGE_MAPCOUNT */

/**
 * folio_average_page_mapcount() - Average number of mappings per page in this
 *				   folio
 * @folio: The folio.
 *
 * The average number of user page table entries that reference each page in
 * this folio as tracked via the RMAP: either referenced directly (PTE) or
 * as part of a larger area that covers this page (e.g., PMD).
 *
 * The average is calculated by rounding to the nearest integer; however,
 * to avoid duplicated code in current callers, the average is at least
 * 1 if any page of the folio is mapped.
 *
 * Returns: The average number of mappings per page in this folio.
 */
static inline int folio_average_page_mapcount(struct folio *folio)
{
	int mapcount, entire_mapcount, avg;

	if (!folio_test_large(folio))
		return atomic_read(&folio->_mapcount) + 1;

	mapcount = folio_large_mapcount(folio);
	if (unlikely(mapcount <= 0))
		return 0;
	entire_mapcount = folio_entire_mapcount(folio);
	if (mapcount <= entire_mapcount)
		return entire_mapcount;
	mapcount -= entire_mapcount;

	/* Round to closest integer ... */
	avg = ((unsigned int)mapcount + folio_large_nr_pages(folio) / 2) >> folio_large_order(folio);
	/* ... but return at least 1. */
	return max_t(int, avg + entire_mapcount, 1);
}
/*
 * array.c
 */
extern const struct file_operations proc_tid_children_operations;

extern void proc_task_name(struct seq_file *m, struct task_struct *p,
			   bool escape);
extern int proc_tid_stat(struct seq_file *, struct pid_namespace *,
			 struct pid *, struct task_struct *);
extern int proc_tgid_stat(struct seq_file *, struct pid_namespace *,
			  struct pid *, struct task_struct *);
extern int proc_pid_status(struct seq_file *, struct pid_namespace *,
			   struct pid *, struct task_struct *);
extern int proc_pid_statm(struct seq_file *, struct pid_namespace *,
			  struct pid *, struct task_struct *);

/*
 * base.c
 */
extern const struct dentry_operations pid_dentry_operations;
extern int pid_getattr(struct mnt_idmap *, const struct path *,
		       struct kstat *, u32, unsigned int);
extern int proc_setattr(struct mnt_idmap *, struct dentry *,
			struct iattr *);
extern void proc_pid_evict_inode(struct proc_inode *);
extern struct inode *proc_pid_make_inode(struct super_block *, struct task_struct *, umode_t);
extern void pid_update_inode(struct task_struct *, struct inode *);
extern int pid_delete_dentry(const struct dentry *);
extern int proc_pid_readdir(struct file *, struct dir_context *);
struct dentry *proc_pid_lookup(struct dentry *, unsigned int);
extern loff_t mem_lseek(struct file *, loff_t, int);

/* Lookups */
typedef struct dentry *instantiate_t(struct dentry *,
				     struct task_struct *, const void *);
bool proc_fill_cache(struct file *, struct dir_context *, const char *, unsigned int,
			   instantiate_t, struct task_struct *, const void *);

/*
 * generic.c
 */
struct proc_dir_entry *proc_create_reg(const char *name, umode_t mode,
		struct proc_dir_entry **parent, void *data);
struct proc_dir_entry *proc_register(struct proc_dir_entry *dir,
		struct proc_dir_entry *dp);
extern struct dentry *proc_lookup(struct inode *, struct dentry *, unsigned int);
struct dentry *proc_lookup_de(struct inode *, struct dentry *, struct proc_dir_entry *);
extern int proc_readdir(struct file *, struct dir_context *);
int proc_readdir_de(struct file *, struct dir_context *, struct proc_dir_entry *);

static inline void pde_get(struct proc_dir_entry *pde)
{
	refcount_inc(&pde->refcnt);
}
extern void pde_put(struct proc_dir_entry *);

static inline bool is_empty_pde(const struct proc_dir_entry *pde)
{
	return S_ISDIR(pde->mode) && !pde->proc_iops;
}
extern ssize_t proc_simple_write(struct file *, const char __user *, size_t, loff_t *);

/*
 * inode.c
 */
struct pde_opener {
	struct list_head lh;
	struct file *file;
	bool closing;
	struct completion *c;
} __randomize_layout;
extern const struct inode_operations proc_link_inode_operations;
extern const struct inode_operations proc_pid_link_inode_operations;
extern const struct super_operations proc_sops;

void proc_init_kmemcache(void);
void proc_invalidate_siblings_dcache(struct hlist_head *inodes, spinlock_t *lock);
void set_proc_pid_nlink(void);
extern struct inode *proc_get_inode(struct super_block *, struct proc_dir_entry *);
extern void proc_entry_rundown(struct proc_dir_entry *);

/*
 * proc_namespaces.c
 */
extern const struct inode_operations proc_ns_dir_inode_operations;
extern const struct file_operations proc_ns_dir_operations;

/*
 * proc_net.c
 */
extern const struct file_operations proc_net_operations;
extern const struct inode_operations proc_net_inode_operations;

#ifdef CONFIG_NET
extern int proc_net_init(void);
#else
static inline int proc_net_init(void) { return 0; }
#endif

/*
 * proc_self.c
 */
extern int proc_setup_self(struct super_block *);

/*
 * proc_thread_self.c
 */
extern int proc_setup_thread_self(struct super_block *);
extern void proc_thread_self_init(void);

/*
 * proc_sysctl.c
 */
#ifdef CONFIG_PROC_SYSCTL
extern int proc_sys_init(void);
extern void proc_sys_evict_inode(struct inode *inode,
				 struct ctl_table_header *head);
#else
static inline void proc_sys_init(void) { }
static inline void proc_sys_evict_inode(struct  inode *inode,
					struct ctl_table_header *head) { }
#endif

/*
 * proc_tty.c
 */
#ifdef CONFIG_TTY
extern void proc_tty_init(void);
#else
static inline void proc_tty_init(void) {}
#endif

/*
 * root.c
 */
extern struct proc_dir_entry proc_root;

extern void proc_self_init(void);

/*
 * task_[no]mmu.c
 */
struct mem_size_stats;
struct proc_maps_private {
	struct inode *inode;
	struct task_struct *task;
	struct mm_struct *mm;
	struct vma_iterator iter;
#ifdef CONFIG_NUMA
	struct mempolicy *task_mempolicy;
#endif
} __randomize_layout;

struct mm_struct *proc_mem_open(struct inode *inode, unsigned int mode);

extern const struct file_operations proc_pid_maps_operations;
extern const struct file_operations proc_pid_numa_maps_operations;
extern const struct file_operations proc_pid_smaps_operations;
extern const struct file_operations proc_pid_smaps_rollup_operations;
extern const struct file_operations proc_clear_refs_operations;
extern const struct file_operations proc_pagemap_operations;

extern unsigned long task_vsize(struct mm_struct *);
extern unsigned long task_statm(struct mm_struct *,
				unsigned long *, unsigned long *,
				unsigned long *, unsigned long *);
extern void task_mem(struct seq_file *, struct mm_struct *);

extern const struct dentry_operations proc_net_dentry_ops;
static inline void pde_force_lookup(struct proc_dir_entry *pde)
{
	/* /proc/net/ entries can be changed under us by setns(CLONE_NEWNET) */
	pde->proc_dops = &proc_net_dentry_ops;
}

/*
 * Add a new procfs dentry that can't serve as a mountpoint. That should
 * encompass anything that is ephemeral and can just disappear while the
 * process is still around.
 */
static inline struct dentry *proc_splice_unmountable(struct inode *inode,
		struct dentry *dentry, const struct dentry_operations *d_ops)
{
	d_set_d_op(dentry, d_ops);
	dont_mount(dentry);
	return d_splice_alias(inode, dentry);
}
