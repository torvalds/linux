/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _FS_CEPH_DEBUGFS_H
#define _FS_CEPH_DEBUGFS_H

#include <linux/ceph/ceph_debug.h>
#include <linux/ceph/types.h>

#define CEPH_DEFINE_SHOW_FUNC(name)					\
static int name##_open(struct inode *inode, struct file *file)		\
{									\
	return single_open(file, name, inode->i_private);		\
}									\
									\
static const struct file_operations name##_fops = {			\
	.open		= name##_open,					\
	.read		= seq_read,					\
	.llseek		= seq_lseek,					\
	.release	= single_release,				\
};

/* debugfs.c */
extern int ceph_debugfs_init(void);
extern void ceph_debugfs_cleanup(void);
extern int ceph_debugfs_client_init(struct ceph_client *client);
extern void ceph_debugfs_client_cleanup(struct ceph_client *client);

#endif

