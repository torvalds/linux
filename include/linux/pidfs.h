/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PID_FS_H
#define _LINUX_PID_FS_H

struct file *pidfs_alloc_file(struct pid *pid, unsigned int flags);
void __init pidfs_init(void);

#endif /* _LINUX_PID_FS_H */
