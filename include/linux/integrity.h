/*
 * Copyright (C) 2009 IBM Corporation
 * Author: Mimi Zohar <zohar@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 */

#ifndef _LINUX_INTEGRITY_H
#define _LINUX_INTEGRITY_H

#include <linux/fs.h>

#ifdef CONFIG_INTEGRITY
extern int integrity_inode_alloc(struct inode *inode);
extern void integrity_inode_free(struct inode *inode);

#else
static inline int integrity_inode_alloc(struct inode *inode)
{
	return 0;
}

static inline void integrity_inode_free(struct inode *inode)
{
	return;
}
#endif /* CONFIG_INTEGRITY_H */
#endif /* _LINUX_INTEGRITY_H */
