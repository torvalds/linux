/*
 * Copyright (C) 2008 IBM Corporation
 * Author: Mimi Zohar <zohar@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 */

#include <linux/fs.h>

#ifndef _LINUX_IMA_H
#define _LINUX_IMA_H

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

static inline int ima_path_check(struct path *path, int mask)
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
#endif /* _LINUX_IMA_H */
