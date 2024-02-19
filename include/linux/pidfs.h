/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PID_FS_H
#define _LINUX_PID_FS_H

struct file *pidfs_alloc_file(struct pid *pid, unsigned int flags);
void __init pidfs_init(void);
bool is_pidfs_sb(const struct super_block *sb);

#endif /* _LINUX_PID_FS_H */
