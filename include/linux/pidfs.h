/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PID_FS_H
#define _LINUX_PID_FS_H

struct coredump_params;

struct file *pidfs_alloc_file(struct pid *pid, unsigned int flags);
void __init pidfs_init(void);
void pidfs_add_pid(struct pid *pid);
void pidfs_remove_pid(struct pid *pid);
void pidfs_exit(struct task_struct *tsk);
#ifdef CONFIG_COREDUMP
void pidfs_coredump(const struct coredump_params *cprm);
#endif
extern const struct dentry_operations pidfs_dentry_operations;
int pidfs_register_pid(struct pid *pid);
void pidfs_free_pid(struct pid *pid);

#endif /* _LINUX_PID_FS_H */
