// SPDX-License-Identifier: GPL-2.0-only
#include <linux/fs.h>
#include <linux/fs_struct.h>
#include <linux/kernel_read_file.h>
#include <linux/security.h>
#include <linux/vmalloc.h>

/**
 * kernel_read_file() - read file contents into a kernel buffer
 *
 * @file	file to read from
 * @offset	where to start reading from (see below).
 * @buf		pointer to a "void *" buffer for reading into (if
 *		*@buf is NULL, a buffer will be allocated, and
 *		@buf_size will be ignored)
 * @buf_size	size of buf, if already allocated. If @buf not
 *		allocated, this is the largest size to allocate.
 * @file_size	if non-NULL, the full size of @file will be
 *		written here.
 * @id		the kernel_read_file_id identifying the type of
 *		file contents being read (for LSMs to examine)
 *
 * @offset must be 0 unless both @buf and @file_size are non-NULL
 * (i.e. the caller must be expecting to read partial file contents
 * via an already-allocated @buf, in at most @buf_size chunks, and
 * will be able to determine when the entire file was read by
 * checking @file_size). This isn't a recommended way to read a
 * file, though, since it is possible that the contents might
 * change between calls to kernel_read_file().
 *
 * Returns number of bytes read (no single read will be bigger
 * than SSIZE_MAX), or negative on error.
 *
 */
ssize_t kernel_read_file(struct file *file, loff_t offset, void **buf,
			 size_t buf_size, size_t *file_size,
			 enum kernel_read_file_id id)
{
	loff_t i_size, pos;
	ssize_t copied;
	void *allocated = NULL;
	bool whole_file;
	int ret;

	if (offset != 0 && (!*buf || !file_size))
		return -EINVAL;

	if (!S_ISREG(file_inode(file)->i_mode))
		return -EINVAL;

	ret = deny_write_access(file);
	if (ret)
		return ret;

	i_size = i_size_read(file_inode(file));
	if (i_size <= 0) {
		ret = -EINVAL;
		goto out;
	}
	/* The file is too big for sane activities. */
	if (i_size > SSIZE_MAX) {
		ret = -EFBIG;
		goto out;
	}
	/* The entire file cannot be read in one buffer. */
	if (!file_size && offset == 0 && i_size > buf_size) {
		ret = -EFBIG;
		goto out;
	}

	whole_file = (offset == 0 && i_size <= buf_size);
	ret = security_kernel_read_file(file, id, whole_file);
	if (ret)
		goto out;

	if (file_size)
		*file_size = i_size;

	if (!*buf)
		*buf = allocated = vmalloc(i_size);
	if (!*buf) {
		ret = -ENOMEM;
		goto out;
	}

	pos = offset;
	copied = 0;
	while (copied < buf_size) {
		ssize_t bytes;
		size_t wanted = min_t(size_t, buf_size - copied,
					      i_size - pos);

		bytes = kernel_read(file, *buf + copied, wanted, &pos);
		if (bytes < 0) {
			ret = bytes;
			goto out_free;
		}

		if (bytes == 0)
			break;
		copied += bytes;
	}

	if (whole_file) {
		if (pos != i_size) {
			ret = -EIO;
			goto out_free;
		}

		ret = security_kernel_post_read_file(file, *buf, i_size, id);
	}

out_free:
	if (ret < 0) {
		if (allocated) {
			vfree(*buf);
			*buf = NULL;
		}
	}

out:
	allow_write_access(file);
	return ret == 0 ? copied : ret;
}
EXPORT_SYMBOL_GPL(kernel_read_file);

ssize_t kernel_read_file_from_path(const char *path, loff_t offset, void **buf,
				   size_t buf_size, size_t *file_size,
				   enum kernel_read_file_id id)
{
	struct file *file;
	ssize_t ret;

	if (!path || !*path)
		return -EINVAL;

	file = filp_open(path, O_RDONLY, 0);
	if (IS_ERR(file))
		return PTR_ERR(file);

	ret = kernel_read_file(file, offset, buf, buf_size, file_size, id);
	fput(file);
	return ret;
}
EXPORT_SYMBOL_GPL(kernel_read_file_from_path);

ssize_t kernel_read_file_from_path_initns(const char *path, loff_t offset,
					  void **buf, size_t buf_size,
					  size_t *file_size,
					  enum kernel_read_file_id id)
{
	struct file *file;
	struct path root;
	ssize_t ret;

	if (!path || !*path)
		return -EINVAL;

	task_lock(&init_task);
	get_fs_root(init_task.fs, &root);
	task_unlock(&init_task);

	file = file_open_root(&root, path, O_RDONLY, 0);
	path_put(&root);
	if (IS_ERR(file))
		return PTR_ERR(file);

	ret = kernel_read_file(file, offset, buf, buf_size, file_size, id);
	fput(file);
	return ret;
}
EXPORT_SYMBOL_GPL(kernel_read_file_from_path_initns);

ssize_t kernel_read_file_from_fd(int fd, loff_t offset, void **buf,
				 size_t buf_size, size_t *file_size,
				 enum kernel_read_file_id id)
{
	struct fd f = fdget(fd);
	ssize_t ret = -EBADF;

	if (!f.file || !(f.file->f_mode & FMODE_READ))
		goto out;

	ret = kernel_read_file(f.file, offset, buf, buf_size, file_size, id);
out:
	fdput(f);
	return ret;
}
EXPORT_SYMBOL_GPL(kernel_read_file_from_fd);
