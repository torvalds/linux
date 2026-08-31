/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _NTFS_STREAM_H
#define _NTFS_STREAM_H

#include <linux/fs.h>

struct ntfs_inode;
struct ntfs_volume;

int ntfs_check_stream_name(const __le16 *name, unsigned int name_len);

void ntfs_stream_inode_refresh(struct inode *inode);
int ntfs_stream_inode_validate(struct inode *inode);
void ntfs_stream_update_base_time(struct ntfs_inode *base_ni);
bool ntfs_stream_unlinked(struct ntfs_inode *base_ni,
		const __le16 *name, u32 name_len);
int ntfs_stream_put(struct inode *inode);
int ntfs_remove_named_stream(struct ntfs_inode *ni, __le16 *name,
		u32 name_len, struct inode *expected_inode);

long ntfs_ioctl_stream_read(struct file *file, unsigned long arg);
long ntfs_ioctl_stream_write(struct file *file, unsigned long arg);
long ntfs_ioctl_stream_remove(struct file *file, unsigned long arg);
int ntfs_ioctl_list_streams(struct file *file, unsigned long arg);

int ntfs_file_open(struct inode *inode, struct file *file);
int ntfs_file_release(struct inode *inode, struct file *file);
int ntfs_file_fsync(struct file *file, loff_t start, loff_t end,
		int datasync);
int ntfs_setattr_size(struct inode *inode, struct iattr *attr);
loff_t ntfs_file_llseek(struct file *file, loff_t offset, int whence);
ssize_t ntfs_file_read_iter(struct kiocb *iocb, struct iov_iter *to);
ssize_t ntfs_file_write_iter(struct kiocb *iocb, struct iov_iter *from);
int ntfs_file_mmap_prepare(struct vm_area_desc *desc);
ssize_t ntfs_file_splice_read(struct file *in, loff_t *ppos,
		struct pipe_inode_info *pipe, size_t len, unsigned int flags);
int ntfs_fiemap(struct inode *inode, struct fiemap_extent_info *fieinfo,
		u64 start, u64 len);
long ntfs_fallocate(struct file *file, int mode, loff_t offset, loff_t len);

int ntfs_test_inode_rcu(struct inode *inode, void *data);

#endif /* _NTFS_STREAM_H */
