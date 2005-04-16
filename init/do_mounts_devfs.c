
#include <linux/kernel.h>
#include <linux/dirent.h>
#include <linux/string.h>

#include "do_mounts.h"

void __init mount_devfs(void)
{
	sys_mount("devfs", "/dev", "devfs", 0, NULL);
}

void __init umount_devfs(char *path)
{
	sys_umount(path, 0);
}

/*
 * If the dir will fit in *buf, return its length.  If it won't fit, return
 * zero.  Return -ve on error.
 */
static int __init do_read_dir(int fd, void *buf, int len)
{
	long bytes, n;
	char *p = buf;
	sys_lseek(fd, 0, 0);

	for (bytes = 0; bytes < len; bytes += n) {
		n = sys_getdents64(fd, (struct linux_dirent64 *)(p + bytes),
					len - bytes);
		if (n < 0)
			return n;
		if (n == 0)
			return bytes;
	}
	return 0;
}

/*
 * Try to read all of a directory.  Returns the contents at *p, which
 * is kmalloced memory.  Returns the number of bytes read at *len.  Returns
 * NULL on error.
 */
static void * __init read_dir(char *path, int *len)
{
	int size;
	int fd = sys_open(path, 0, 0);

	*len = 0;
	if (fd < 0)
		return NULL;

	for (size = 1 << 9; size <= (PAGE_SIZE << MAX_ORDER); size <<= 1) {
		void *p = kmalloc(size, GFP_KERNEL);
		int n;
		if (!p)
			break;
		n = do_read_dir(fd, p, size);
		if (n > 0) {
			sys_close(fd);
			*len = n;
			return p;
		}
		kfree(p);
		if (n == -EINVAL)
			continue;	/* Try a larger buffer */
		if (n < 0)
			break;
	}
	sys_close(fd);
	return NULL;
}

/*
 * recursively scan <path>, looking for a device node of type <dev>
 */
static int __init find_in_devfs(char *path, unsigned dev)
{
	char *end = path + strlen(path);
	int rest = path + 64 - end;
	int size;
	char *p = read_dir(path, &size);
	char *s;

	if (!p)
		return -1;
	for (s = p; s < p + size; s += ((struct linux_dirent64 *)s)->d_reclen) {
		struct linux_dirent64 *d = (struct linux_dirent64 *)s;
		if (strlen(d->d_name) + 2 > rest)
			continue;
		switch (d->d_type) {
			case DT_BLK:
				sprintf(end, "/%s", d->d_name);
				if (bstat(path) != dev)
					break;
				kfree(p);
				return 0;
			case DT_DIR:
				if (strcmp(d->d_name, ".") == 0)
					break;
				if (strcmp(d->d_name, "..") == 0)
					break;
				sprintf(end, "/%s", d->d_name);
				if (find_in_devfs(path, dev) < 0)
					break;
				kfree(p);
				return 0;
		}
	}
	kfree(p);
	return -1;
}

/*
 * create a device node called <name> which points to
 * <devfs_name> if possible, otherwise find a device node
 * which matches <dev> and make <name> a symlink pointing to it.
 */
int __init create_dev(char *name, dev_t dev, char *devfs_name)
{
	char path[64];

	sys_unlink(name);
	if (devfs_name && devfs_name[0]) {
		if (strncmp(devfs_name, "/dev/", 5) == 0)
			devfs_name += 5;
		sprintf(path, "/dev/%s", devfs_name);
		if (sys_access(path, 0) == 0)
			return sys_symlink(devfs_name, name);
	}
	if (!dev)
		return -1;
	strcpy(path, "/dev");
	if (find_in_devfs(path, new_encode_dev(dev)) < 0)
		return -1;
	return sys_symlink(path + 5, name);
}
