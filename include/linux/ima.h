/*
 * Copyright (C) 2008 IBM Corporation
 * Author: Mimi Zohar <zohar@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 */

#ifndef _LINUX_IMA_H
#define _LINUX_IMA_H

#include <linux/fs.h>
struct linux_binprm;

#define IMA_COUNT_UPDATE 1
#define IMA_COUNT_LEAVE 0

#ifdef CONFIG_IMA
extern int ima_bprm_check(struct linux_binprm *bprm);
extern int ima_inode_alloc(struct inode *inode);
extern void ima_inode_free(struct inode *inode);
extern int ima_path_check(struct path *path, int mask, int update_counts);
extern void ima_file_free(struct file *file);
extern int ima_file_mmap(struct file *file, unsigned long prot);
extern void ima_counts_get(struct file *file);

#else
static inline int ima_bprm_check(struct linux_binprm *bprm)
{
	return 0;
}

static inline int ima_inode_alloc(struct inode *inode)
{
	return 0;
}

static inline void ima_inode_free(struct inode *inode)
{
	return;
}

static inline int ima_path_check(struct path *path, int mask, int update_counts)
{
	return 0;
}

static inline void ima_file_free(struct file *file)
{
	return;
}

static inline int ima_file_mmap(struct file *file, unsigned long prot)
{
	return 0;
}

static inline void ima_counts_get(struct file *file)
{
	return;
}
#endif /* CONFIG_IMA_H */
#endif /* _LINUX_IMA_H */
