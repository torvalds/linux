/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _FS_CEPH_DEFS_H
#define _FS_CEPH_DEFS_H

#include <linux/ceph/ceph_de.h>
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

/* defs.c */
extern int ceph_defs_init(void);
extern void ceph_defs_cleanup(void);
extern int ceph_defs_client_init(struct ceph_client *client);
extern void ceph_defs_client_cleanup(struct ceph_client *client);

#endif

