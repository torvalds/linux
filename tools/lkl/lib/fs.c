#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <lkl_host.h>

#include "virtio.h"

#define MAX_FSTYPE_LEN 50
int lkl_mount_fs(char *fstype)
{
	char dir[MAX_FSTYPE_LEN+2] = "/";
	int flags = 0, ret = 0;

	strncat(dir, fstype, MAX_FSTYPE_LEN);

	/* Create with regular umask */
	ret = lkl_sys_mkdir(dir, 0xff);
	if (ret && ret != -LKL_EEXIST) {
		lkl_perror("mount_fs mkdir", ret);
		return ret;
	}

	/* We have no use for nonzero flags right now */
	ret = lkl_sys_mount("none", dir, fstype, flags, NULL);
	if (ret && ret != -LKL_EBUSY) {
		lkl_sys_rmdir(dir);
		return ret;
	}

	if (ret == -LKL_EBUSY)
		return 1;
	return 0;
}

static uint32_t new_encode_dev(unsigned int major, unsigned int minor)
{
	return (minor & 0xff) | (major << 8) | ((minor & ~0xff) << 12);
}

static int startswith(const char *str, const char *pre)
{
	return strncmp(pre, str, strlen(pre)) == 0;
}

static int get_node_with_prefix(const char *path, const char *prefix,
				char *result, unsigned int result_len)
{
	struct lkl_dir *dir = NULL;
	struct lkl_linux_dirent64 *dirent;
	int ret;

	dir = lkl_opendir(path, &ret);
	if (!dir)
		return ret;

	ret = -LKL_ENOENT;

	while ((dirent = lkl_readdir(dir))) {
		if (startswith(dirent->d_name, prefix)) {
			if (strlen(dirent->d_name) + 1 > result_len) {
				ret = -LKL_ENOMEM;
				break;
			}
			memcpy(result, dirent->d_name, strlen(dirent->d_name));
			result[strlen(dirent->d_name)] = '\0';
			ret = 0;
			break;
		}
	}

	lkl_closedir(dir);

	return ret;
}

static int encode_dev_from_sysfs(const char *sysfs_path, uint32_t *pdevid)
{
	int ret;
	long fd;
	int major, minor;
	char buf[16] = { 0, };

	fd = lkl_sys_open(sysfs_path, LKL_O_RDONLY, 0);
	if (fd < 0)
		return fd;

	ret = lkl_sys_read(fd, buf, sizeof(buf));
	if (ret < 0)
		goto out_close;

	if (ret == sizeof(buf)) {
		ret = -LKL_ENOBUFS;
		goto out_close;
	}

	ret = sscanf(buf, "%d:%d", &major, &minor);
	if (ret != 2) {
		ret = -LKL_EINVAL;
		goto out_close;
	}

	*pdevid = new_encode_dev(major, minor);
	ret = 0;

out_close:
	lkl_sys_close(fd);

	return ret;
}

#define SYSFS_DEV_VIRTIO_PLATFORM_PATH \
	"/sysfs/devices/platform/virtio-mmio.%d.auto"
#define SYSFS_DEV_VIRTIO_CMDLINE_PATH \
	"/sysfs/devices/virtio-mmio-cmdline/virtio-mmio.%d"

struct abuf {
	char *mem, *ptr;
	unsigned int len;
};

static int snprintf_append(struct abuf *buf, const char *fmt, ...)
{
	int ret;
	va_list args;

	if (!buf->ptr)
		buf->ptr = buf->mem;

	va_start(args, fmt);
	ret = vsnprintf(buf->ptr, buf->len - (buf->ptr - buf->mem), fmt, args);
	va_end(args);

	if (ret < 0 || (ret >= (buf->len - (buf->ptr - buf->mem))))
		return -LKL_ENOMEM;

	buf->ptr += ret;

	return 0;
}

int lkl_get_virtio_blkdev(int disk_id, unsigned int part, uint32_t *pdevid)
{
	char sysfs_path[LKL_PATH_MAX];
	char virtio_name[LKL_PATH_MAX];
	char disk_name[LKL_PATH_MAX];
	struct abuf sysfs_path_buf = {
		.mem = sysfs_path,
		.len = sizeof(sysfs_path),
	};
	char *fmt;
	int ret;

	if (disk_id < 0)
		return -LKL_EINVAL;

	ret = lkl_mount_fs("sysfs");
	if (ret < 0)
		return ret;

	if ((uint32_t) disk_id >= virtio_get_num_bootdevs()) {
		fmt = SYSFS_DEV_VIRTIO_PLATFORM_PATH;
		disk_id -= virtio_get_num_bootdevs();
	} else {
		fmt = SYSFS_DEV_VIRTIO_CMDLINE_PATH;
	}

	ret = snprintf_append(&sysfs_path_buf, fmt, disk_id);
	if (ret)
		return ret;

	ret = get_node_with_prefix(sysfs_path, "virtio", virtio_name,
				   sizeof(virtio_name));
	if (ret)
		return ret;

	ret = snprintf_append(&sysfs_path_buf, "/%s/block", virtio_name);
	if (ret)
		return ret;

	ret = get_node_with_prefix(sysfs_path, "vd", disk_name,
				   sizeof(disk_name));
	if (ret)
		return ret;

	if (!part)
		ret = snprintf_append(&sysfs_path_buf, "/%s/dev", disk_name);
	else
		ret = snprintf_append(&sysfs_path_buf, "/%s/%s%d/dev",
				      disk_name, disk_name, part);
	if (ret)
		return ret;

	return encode_dev_from_sysfs(sysfs_path, pdevid);
}

long lkl_mount_dev(unsigned int disk_id, unsigned int part,
		   const char *fs_type, int flags,
		   const char *data, char *mnt_str, unsigned int mnt_str_len)
{
	char dev_str[] = { "/dev/xxxxxxxx" };
	unsigned int dev;
	int err;
	char _data[4096]; /* FIXME: PAGE_SIZE is not exported by LKL */

	if (mnt_str_len < sizeof(dev_str))
		return -LKL_ENOMEM;

	err = lkl_get_virtio_blkdev(disk_id, part, &dev);
	if (err < 0)
		return err;

	snprintf(dev_str, sizeof(dev_str), "/dev/%08x", dev);
	snprintf(mnt_str, mnt_str_len, "/mnt/%08x", dev);

	err = lkl_sys_access("/dev", LKL_S_IRWXO);
	if (err < 0) {
		if (err == -LKL_ENOENT)
			err = lkl_sys_mkdir("/dev", 0700);
		if (err < 0)
			return err;
	}

	err = lkl_sys_mknod(dev_str, LKL_S_IFBLK | 0600, dev);
	if (err < 0)
		return err;

	err = lkl_sys_access("/mnt", LKL_S_IRWXO);
	if (err < 0) {
		if (err == -LKL_ENOENT)
			err = lkl_sys_mkdir("/mnt", 0700);
		if (err < 0)
			return err;
	}

	err = lkl_sys_mkdir(mnt_str, 0700);
	if (err < 0) {
		lkl_sys_unlink(dev_str);
		return err;
	}

	/* kernel always copies a full page */
	if (data) {
		strncpy(_data, data, sizeof(_data));
		_data[sizeof(_data) - 1] = 0;
	} else {
		_data[0] = 0;
	}

	err = lkl_sys_mount(dev_str, mnt_str, (char *)fs_type, flags, _data);
	if (err < 0) {
		lkl_sys_unlink(dev_str);
		lkl_sys_rmdir(mnt_str);
		return err;
	}

	return 0;
}

long lkl_umount_timeout(char *path, int flags, long timeout_ms)
{
	long incr = 10000000; /* 10 ms */
	struct lkl_timespec ts = {
		.tv_sec = 0,
		.tv_nsec = incr,
	};
	long err;

	do {
		err = lkl_sys_umount(path, flags);
		if (err == -LKL_EBUSY) {
			lkl_sys_nanosleep(&ts, NULL);
			timeout_ms -= incr / 1000000;
		}
	} while (err == -LKL_EBUSY && timeout_ms > 0);

	return err;
}

long lkl_umount_dev(unsigned int disk_id, unsigned int part, int flags,
		    long timeout_ms)
{
	char dev_str[] = { "/dev/xxxxxxxx" };
	char mnt_str[] = { "/mnt/xxxxxxxx" };
	unsigned int dev;
	int err;

	err = lkl_get_virtio_blkdev(disk_id, part, &dev);
	if (err < 0)
		return err;

	snprintf(dev_str, sizeof(dev_str), "/dev/%08x", dev);
	snprintf(mnt_str, sizeof(mnt_str), "/mnt/%08x", dev);

	err = lkl_umount_timeout(mnt_str, flags, timeout_ms);
	if (err)
		return err;

	err = lkl_sys_unlink(dev_str);
	if (err)
		return err;

	return lkl_sys_rmdir(mnt_str);
}

struct lkl_dir {
	int fd;
	char buf[1024];
	char *pos;
	int len;
};

static struct lkl_dir *lkl_dir_alloc(int *err)
{
	struct lkl_dir *dir = lkl_host_ops.mem_alloc(sizeof(struct lkl_dir));

	if (!dir) {
		*err = -LKL_ENOMEM;
		return NULL;
	}

	dir->len = 0;
	dir->pos = NULL;

	return dir;
}

struct lkl_dir *lkl_opendir(const char *path, int *err)
{
	struct lkl_dir *dir = lkl_dir_alloc(err);

	if (!dir)
		return NULL;

	dir->fd = lkl_sys_open(path, LKL_O_RDONLY | LKL_O_DIRECTORY, 0);
	if (dir->fd < 0) {
		*err = dir->fd;
		lkl_host_ops.mem_free(dir);
		return NULL;
	}

	return dir;
}

struct lkl_dir *lkl_fdopendir(int fd, int *err)
{
	struct lkl_dir *dir = lkl_dir_alloc(err);

	if (!dir)
		return NULL;

	dir->fd = fd;

	return dir;
}

void lkl_rewinddir(struct lkl_dir *dir)
{
	lkl_sys_lseek(dir->fd, 0, SEEK_SET);
	dir->len = 0;
	dir->pos = NULL;
}

int lkl_closedir(struct lkl_dir *dir)
{
	int ret;

	ret = lkl_sys_close(dir->fd);
	lkl_host_ops.mem_free(dir);

	return ret;
}

struct lkl_linux_dirent64 *lkl_readdir(struct lkl_dir *dir)
{
	struct lkl_linux_dirent64 *de;

	if (dir->len < 0)
		return NULL;

	if (!dir->pos || dir->pos - dir->buf >= dir->len)
		goto read_buf;

return_de:
	de = (struct lkl_linux_dirent64 *)dir->pos;
	dir->pos += de->d_reclen;

	return de;

read_buf:
	dir->pos = NULL;
	de = (struct lkl_linux_dirent64 *)dir->buf;
	dir->len = lkl_sys_getdents64(dir->fd, de, sizeof(dir->buf));
	if (dir->len <= 0)
		return NULL;

	dir->pos = dir->buf;
	goto return_de;
}

int lkl_errdir(struct lkl_dir *dir)
{
	if (dir->len >= 0)
		return 0;

	return dir->len;
}

int lkl_dirfd(struct lkl_dir *dir)
{
	return dir->fd;
}

int lkl_set_fd_limit(unsigned int fd_limit)
{
	struct lkl_rlimit rlim = {
		.rlim_cur = fd_limit,
		.rlim_max = fd_limit,
	};
	return lkl_sys_setrlimit(LKL_RLIMIT_NOFILE, &rlim);
}
