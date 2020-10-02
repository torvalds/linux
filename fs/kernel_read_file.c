// SPDX-License-Identifier: GPL-2.0-only
#include <linux/fs.h>
#include <linux/fs_struct.h>
#include <linux/kernel_read_file.h>
#include <linux/security.h>
#include <linux/vmalloc.h>

int kernel_read_file(struct file *file, void **buf, loff_t *size,
		     loff_t max_size, enum kernel_read_file_id id)
{
	loff_t i_size, pos;
	ssize_t bytes = 0;
	void *allocated = NULL;
	int ret;

	if (!S_ISREG(file_inode(file)->i_mode) || max_size < 0)
		return -EINVAL;

	ret = deny_write_access(file);
	if (ret)
		return ret;

	ret = security_kernel_read_file(file, id);
	if (ret)
		goto out;

	i_size = i_size_read(file_inode(file));
	if (i_size <= 0) {
		ret = -EINVAL;
		goto out;
	}
	if (i_size > SIZE_MAX || (max_size > 0 && i_size > max_size)) {
		ret = -EFBIG;
		goto out;
	}

	if (!*buf)
		*buf = allocated = vmalloc(i_size);
	if (!*buf) {
		ret = -ENOMEM;
		goto out;
	}

	pos = 0;
	while (pos < i_size) {
		bytes = kernel_read(file, *buf + pos, i_size - pos, &pos);
		if (bytes < 0) {
			ret = bytes;
			goto out_free;
		}

		if (bytes == 0)
			break;
	}

	if (pos != i_size) {
		ret = -EIO;
		goto out_free;
	}

	ret = security_kernel_post_read_file(file, *buf, i_size, id);
	if (!ret)
		*size = pos;

out_free:
	if (ret < 0) {
		if (allocated) {
			vfree(*buf);
			*buf = NULL;
		}
	}

out:
	allow_write_access(file);
	return ret;
}
EXPORT_SYMBOL_GPL(kernel_read_file);

int kernel_read_file_from_path(const char *path, void **buf, loff_t *size,
			       loff_t max_size, enum kernel_read_file_id id)
{
	struct file *file;
	int ret;

	if (!path || !*path)
		return -EINVAL;

	file = filp_open(path, O_RDONLY, 0);
	if (IS_ERR(file))
		return PTR_ERR(file);

	ret = kernel_read_file(file, buf, size, max_size, id);
	fput(file);
	return ret;
}
EXPORT_SYMBOL_GPL(kernel_read_file_from_path);

int kernel_read_file_from_path_initns(const char *path, void **buf,
				      loff_t *size, loff_t max_size,
				      enum kernel_read_file_id id)
{
	struct file *file;
	struct path root;
	int ret;

	if (!path || !*path)
		return -EINVAL;

	task_lock(&init_task);
	get_fs_root(init_task.fs, &root);
	task_unlock(&init_task);

	file = file_open_root(root.dentry, root.mnt, path, O_RDONLY, 0);
	path_put(&root);
	if (IS_ERR(file))
		return PTR_ERR(file);

	ret = kernel_read_file(file, buf, size, max_size, id);
	fput(file);
	return ret;
}
EXPORT_SYMBOL_GPL(kernel_read_file_from_path_initns);

int kernel_read_file_from_fd(int fd, void **buf, loff_t *size, loff_t max_size,
			     enum kernel_read_file_id id)
{
	struct fd f = fdget(fd);
	int ret = -EBADF;

	if (!f.file)
		goto out;

	ret = kernel_read_file(f.file, buf, size, max_size, id);
out:
	fdput(f);
	return ret;
}
EXPORT_SYMBOL_GPL(kernel_read_file_from_fd);
