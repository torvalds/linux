#ifndef _LINUX_RAMFS_H
#define _LINUX_RAMFS_H

struct inode *ramfs_get_inode(struct super_block *sb, int mode, dev_t dev);
struct super_block *ramfs_get_sb(struct file_system_type *fs_type,
	 int flags, const char *dev_name, void *data);

extern struct file_operations ramfs_file_operations;
extern struct vm_operations_struct generic_file_vm_ops;

#endif
