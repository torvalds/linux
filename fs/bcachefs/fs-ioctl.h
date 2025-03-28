/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_FS_IOCTL_H
#define _BCACHEFS_FS_IOCTL_H

long bch2_fs_file_ioctl(struct file *, unsigned, unsigned long);
long bch2_compat_fs_ioctl(struct file *, unsigned, unsigned long);

#endif /* _BCACHEFS_FS_IOCTL_H */
