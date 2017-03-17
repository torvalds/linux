/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_FS_IOCTL_H
#define _BCACHEFS_FS_IOCTL_H

void bch2_inode_flags_to_vfs(struct bch_inode_info *);

long bch2_fs_file_ioctl(struct file *, unsigned, unsigned long);
long bch2_compat_fs_ioctl(struct file *, unsigned, unsigned long);

#endif /* _BCACHEFS_FS_IOCTL_H */
