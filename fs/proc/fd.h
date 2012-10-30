#ifndef __PROCFS_FD_H__
#define __PROCFS_FD_H__

#include <linux/fs.h>

extern const struct file_operations proc_fd_operations;
extern const struct inode_operations proc_fd_inode_operations;

extern const struct file_operations proc_fdinfo_operations;
extern const struct inode_operations proc_fdinfo_inode_operations;

extern int proc_fd_permission(struct inode *inode, int mask);

#endif /* __PROCFS_FD_H__ */
