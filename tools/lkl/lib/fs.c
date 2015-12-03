#include <stdio.h>
#include <string.h>
#include <lkl_host.h>

long lkl_mount_sysfs(void)
{
	long ret;
	static int sysfs_mounted;

	if (sysfs_mounted)
		return 0;

	ret = lkl_sys_mkdir("/sys", 0700);
	if (ret)
		return ret;

	ret = lkl_sys_mount("none", "sys", "sysfs", 0, NULL);

	if (ret == 0)
		sysfs_mounted = 1;

	return ret;
}

static long get_virtio_blkdev(int disk_id)
{
	char sysfs_path[] = "/sys/block/vda/dev";
	char buf[16] = { 0, };
	long fd, ret;
	int major, minor;

	ret = lkl_mount_sysfs();
	if (ret)
		return ret;

	sysfs_path[strlen("/sys/block/vd")] += disk_id;

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

	ret = LKL_MKDEV(major, minor);

out_close:
	lkl_sys_close(fd);

	return ret;
}

long lkl_mount_dev(unsigned int disk_id, const char *fs_type, int flags,
		   void *data, char *mnt_str, unsigned int mnt_str_len)
{
	char dev_str[] = { "/dev/xxxxxxxx" };
	unsigned int dev;
	int err;

	if (mnt_str_len < sizeof(dev_str))
		return -LKL_ENOMEM;

	dev = get_virtio_blkdev(disk_id);

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

	err = lkl_sys_mount(dev_str, mnt_str, (char*)fs_type, flags, data);
	if (err < 0) {
		lkl_sys_unlink(dev_str);
		lkl_sys_rmdir(mnt_str);
		return err;
	}

	return 0;
}

long lkl_umount_dev(unsigned int disk_id, int flags, long timeout_ms)
{
	char dev_str[] = { "/dev/xxxxxxxx" };
	char mnt_str[] = { "/mnt/xxxxxxxx" };
	long incr = 10000000; /* 10 ms */
	struct lkl_timespec ts = {
		.tv_sec = 0,
		.tv_nsec = incr,
	};
	unsigned int dev;
	int err;

	dev = get_virtio_blkdev(disk_id);

	snprintf(dev_str, sizeof(dev_str), "/dev/%08x", dev);
	snprintf(mnt_str, sizeof(mnt_str), "/mnt/%08x", dev);

	do {
		err = lkl_sys_umount(mnt_str, flags);
		if (err == -LKL_EBUSY) {
			lkl_sys_nanosleep(&ts, NULL);
			timeout_ms -= incr / 1000000;
		}
	} while (err == -LKL_EBUSY && timeout_ms > 0);

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

struct lkl_dir *lkl_opendir(const char *path, int *err)
{
	struct lkl_dir *dir = lkl_host_ops.mem_alloc(sizeof(struct lkl_dir));

	if (!dir) {
		*err = -LKL_ENOMEM;
		return NULL;
	}

	dir->fd = lkl_sys_open(path, LKL_O_RDONLY | LKL_O_DIRECTORY, 0);
	if (dir->fd < 0) {
		*err = dir->fd;
		lkl_host_ops.mem_free(dir);
		return NULL;
	}

	dir->len = 0;
	dir->pos = NULL;

	return dir;
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
