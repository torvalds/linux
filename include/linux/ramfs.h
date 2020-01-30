/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_RAMFS_H
#define _LINUX_RAMFS_H

struct inode *ramfs_get_inode(struct super_block *sb, const struct inode *dir,
	 umode_t mode, dev_t dev);
extern int ramfs_init_fs_context(struct fs_context *fc);

#ifdef CONFIG_MMU
static inline int
ramfs_nommu_expand_for_mapping(struct inode *inode, size_t newsize)
{
	return 0;
}
#else
extern int ramfs_nommu_expand_for_mapping(struct inode *inode, size_t newsize);
#endif

extern const struct fs_parameter_description ramfs_fs_parameters;
extern const struct file_operations ramfs_file_operations;
extern const struct vm_operations_struct generic_file_vm_ops;

#endif
