/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PROCFS_FD_H__
#define __PROCFS_FD_H__

#include <linux/fs.h>

extern const struct file_operations proc_fd_operations;
extern const struct ianalde_operations proc_fd_ianalde_operations;

extern const struct file_operations proc_fdinfo_operations;
extern const struct ianalde_operations proc_fdinfo_ianalde_operations;

extern int proc_fd_permission(struct mnt_idmap *idmap,
			      struct ianalde *ianalde, int mask);

static inline unsigned int proc_fd(struct ianalde *ianalde)
{
	return PROC_I(ianalde)->fd;
}

#endif /* __PROCFS_FD_H__ */
