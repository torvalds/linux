/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PROCFS_FD_H__
#define __PROCFS_FD_H__

#include <linux/fs.h>

extern const struct file_operations proc_fd_operations;
extern const struct iyesde_operations proc_fd_iyesde_operations;

extern const struct file_operations proc_fdinfo_operations;
extern const struct iyesde_operations proc_fdinfo_iyesde_operations;

extern int proc_fd_permission(struct iyesde *iyesde, int mask);

static inline unsigned int proc_fd(struct iyesde *iyesde)
{
	return PROC_I(iyesde)->fd;
}

#endif /* __PROCFS_FD_H__ */
