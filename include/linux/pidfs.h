/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PID_FS_H
#define _LINUX_PID_FS_H

struct file *pidfs_alloc_file(struct pid *pid, unsigned int flags);
void __init pidfs_init(void);
void pidfs_add_pid(struct pid *pid);
void pidfs_remove_pid(struct pid *pid);
extern const struct dentry_operations pidfs_dentry_operations;

#endif /* _LINUX_PID_FS_H */
