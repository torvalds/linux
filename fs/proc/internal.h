/* internal.h: internal procfs definitions
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

struct vmalloc_info {
	unsigned long	used;
	unsigned long	largest_chunk;
};

#ifdef CONFIG_MMU
#define VMALLOC_TOTAL (VMALLOC_END - VMALLOC_START)
extern void get_vmalloc_info(struct vmalloc_info *vmi);
#else

#define VMALLOC_TOTAL 0UL
#define get_vmalloc_info(vmi)			\
do {						\
	(vmi)->used = 0;			\
	(vmi)->largest_chunk = 0;		\
} while(0)

#endif

extern void create_seq_entry(char *name, mode_t mode, struct file_operations *f);
extern int proc_exe_link(struct inode *, struct dentry **, struct vfsmount **);
extern int proc_tid_stat(struct task_struct *,  char *);
extern int proc_tgid_stat(struct task_struct *, char *);
extern int proc_pid_status(struct task_struct *, char *);
extern int proc_pid_statm(struct task_struct *, char *);

void free_proc_entry(struct proc_dir_entry *de);

int proc_init_inodecache(void);

static inline struct task_struct *proc_task(struct inode *inode)
{
	return PROC_I(inode)->task;
}

static inline int proc_type(struct inode *inode)
{
	return PROC_I(inode)->type;
}
