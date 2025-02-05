/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Common helpers for stackable filesystems and backing files.
 *
 * Copyright (C) 2023 CTERA Networks.
 */

#ifndef _LINUX_BACKING_FILE_H
#define _LINUX_BACKING_FILE_H

#include <linux/file.h>
#include <linux/uio.h>
#include <linux/fs.h>

struct backing_file_ctx {
	const struct cred *cred;
	void (*accessed)(struct file *file);
	void (*end_write)(struct kiocb *iocb, ssize_t);
};

struct file *backing_file_open(const struct path *user_path, int flags,
			       const struct path *real_path,
			       const struct cred *cred);
struct file *backing_tmpfile_open(const struct path *user_path, int flags,
				  const struct path *real_parentpath,
				  umode_t mode, const struct cred *cred);
ssize_t backing_file_read_iter(struct file *file, struct iov_iter *iter,
			       struct kiocb *iocb, int flags,
			       struct backing_file_ctx *ctx);
ssize_t backing_file_write_iter(struct file *file, struct iov_iter *iter,
				struct kiocb *iocb, int flags,
				struct backing_file_ctx *ctx);
ssize_t backing_file_splice_read(struct file *in, struct kiocb *iocb,
				 struct pipe_inode_info *pipe, size_t len,
				 unsigned int flags,
				 struct backing_file_ctx *ctx);
ssize_t backing_file_splice_write(struct pipe_inode_info *pipe,
				  struct file *out, struct kiocb *iocb,
				  size_t len, unsigned int flags,
				  struct backing_file_ctx *ctx);
int backing_file_mmap(struct file *file, struct vm_area_struct *vma,
		      struct backing_file_ctx *ctx);

#endif /* _LINUX_BACKING_FILE_H */
