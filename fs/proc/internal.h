/* Internal procfs definitions
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/proc_fs.h>
#include <linux/proc_ns.h>
#include <linux/spinlock.h>
#include <linux/atomic.h>
#include <linux/binfmts.h>

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
	unsigned int low_ino;
	umode_t mode;
	nlink_t nlink;
	kuid_t uid;
	kgid_t gid;
	loff_t size;
	const struct inode_operations *proc_iops;
	const struct file_operations *proc_fops;
	struct proc_dir_entry *parent;
	struct rb_root subdir;
	struct rb_node subdir_node;
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

union proc_op {
	int (*proc_get_link)(struct dentry *, struct path *);
	int (*proc_show)(struct seq_file *m,
		struct pid_namespace *ns, struct pid *pid,
		struct task_struct *task);
};

struct proc_inode {
	struct pid *pid;
	unsigned int fd;
	union proc_op op;
	struct proc_dir_entry *pde;
	struct ctl_table_header *sysctl;
	struct ctl_table *sysctl_entry;
	struct list_head sysctl_inodes;
	const struct proc_ns_operations *ns_ops;
	struct inode vfs_inode;
};

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

static inline void *__PDE_DATA(const struct inode *inode)
{
	return PDE(inode)->data;
}

static inline struct pid *proc_pid(struct inode *inode)
{
	return PROC_I(inode)->pid;
}

static inline struct task_struct *get_proc_task(struct inode *inode)
{
	return get_pid_task(proc_pid(inode), PIDTYPE_PID);
}

void task_dump_owner(struct task_struct *task, mode_t mode,
		     kuid_t *ruid, kgid_t *rgid);

static inline unsigned name_to_int(const struct qstr *qstr)
{
	const char *name = qstr->name;
	int len = qstr->len;
	unsigned n = 0;

	if (len > 1 && *name == '0')
		goto out;
	while (len-- > 0) {
		unsigned c = *name++ - '0';
		if (c > 9)
			goto out;
		if (n >= (~0U-9)/10)
			goto out;
		n *= 10;
		n += c;
	}
	return n;
out:
	return ~0U;
}

/*
 * Offset of the first process in the /proc root directory..
 */
#define FIRST_PROCESS_ENTRY 256

/* Worst case buffer size needed for holding an integer. */
#define PROC_NUMBUF 13

/*
 * array.c
 */
extern const struct file_operations proc_tid_children_operations;

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
extern int pid_getattr(const struct path *, struct kstat *, u32, unsigned int);
extern int proc_setattr(struct dentry *, struct iattr *);
extern struct inode *proc_pid_make_inode(struct super_block *, struct task_struct *, umode_t);
extern int pid_revalidate(struct dentry *, unsigned int);
extern int pid_delete_dentry(const struct dentry *);
extern int proc_pid_readdir(struct file *, struct dir_context *);
extern struct dentry *proc_pid_lookup(struct inode *, struct dentry *, unsigned int);
extern loff_t mem_lseek(struct file *, loff_t, int);

/* Lookups */
typedef int instantiate_t(struct inode *, struct dentry *,
				     struct task_struct *, const void *);
extern bool proc_fill_cache(struct file *, struct dir_context *, const char *, int,
			   instantiate_t, struct task_struct *, const void *);

/*
 * generic.c
 */
extern struct dentry *proc_lookup(struct inode *, struct dentry *, unsigned int);
extern struct dentry *proc_lookup_de(struct proc_dir_entry *, struct inode *,
				     struct dentry *);
extern int proc_readdir(struct file *, struct dir_context *);
extern int proc_readdir_de(struct proc_dir_entry *, struct file *, struct dir_context *);

static inline struct proc_dir_entry *pde_get(struct proc_dir_entry *pde)
{
	atomic_inc(&pde->count);
	return pde;
}
extern void pde_put(struct proc_dir_entry *);

static inline bool is_empty_pde(const struct proc_dir_entry *pde)
{
	return S_ISDIR(pde->mode) && !pde->proc_iops;
}

/*
 * inode.c
 */
struct pde_opener {
	struct file *file;
	struct list_head lh;
	bool closing;
	struct completion *c;
};
extern const struct inode_operations proc_link_inode_operations;

extern const struct inode_operations proc_pid_link_inode_operations;

extern void proc_init_inodecache(void);
void set_proc_pid_nlink(void);
extern struct inode *proc_get_inode(struct super_block *, struct proc_dir_entry *);
extern int proc_fill_super(struct super_block *, void *data, int flags);
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
extern int proc_parse_options(char *options, struct pid_namespace *pid);

extern void proc_self_init(void);
extern int proc_remount(struct super_block *, int *, char *);

/*
 * task_[no]mmu.c
 */
struct proc_maps_private {
	struct inode *inode;
	struct task_struct *task;
	struct mm_struct *mm;
#ifdef CONFIG_MMU
	struct vm_area_struct *tail_vma;
#endif
#ifdef CONFIG_NUMA
	struct mempolicy *task_mempolicy;
#endif
};

struct mm_struct *proc_mem_open(struct inode *inode, unsigned int mode);

extern const struct file_operations proc_pid_maps_operations;
extern const struct file_operations proc_tid_maps_operations;
extern const struct file_operations proc_pid_numa_maps_operations;
extern const struct file_operations proc_tid_numa_maps_operations;
extern const struct file_operations proc_pid_smaps_operations;
extern const struct file_operations proc_tid_smaps_operations;
extern const struct file_operations proc_clear_refs_operations;
extern const struct file_operations proc_pagemap_operations;

extern unsigned long task_vsize(struct mm_struct *);
extern unsigned long task_statm(struct mm_struct *,
				unsigned long *, unsigned long *,
				unsigned long *, unsigned long *);
extern void task_mem(struct seq_file *, struct mm_struct *);
