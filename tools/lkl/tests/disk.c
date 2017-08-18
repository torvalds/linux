#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <stdint.h>
#include <lkl.h>
#include <lkl_host.h>
#ifndef __MINGW32__
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/epoll.h>
#else
#include <windows.h>
#endif

#include "test.h"
#include "cla.h"

static struct {
	int printk;
	const char *disk;
	const char *fstype;
	int partition;
} cla;

struct cl_arg args[] = {
	{"printk", 'p', "show Linux printks", 0, CL_ARG_BOOL, &cla.printk},
	{"disk", 'd', "disk file to use", 1, CL_ARG_STR, &cla.disk},
	{"partition", 'P', "partition to mount", 1, CL_ARG_INT, &cla.partition},
	{"type", 't', "filesystem type", 1, CL_ARG_STR, &cla.fstype},
	{0},
};


static struct lkl_disk disk;
static int disk_id = -1;

int test_disk_add(char *str, int len)
{
#ifdef __MINGW32__
	disk.handle = CreateFile(cla.disk, GENERIC_READ | GENERIC_WRITE,
			       0, NULL, OPEN_EXISTING, 0, NULL);
	if (!disk.handle)
#else
	disk.fd = open(cla.disk, O_RDWR);
	if (disk.fd < 0)
#endif
		goto out_unlink;

	disk.ops = NULL;

	disk_id = lkl_disk_add(&disk);
	if (disk_id < 0)
		goto out_close;

	goto out;

out_close:
#ifdef __MINGW32__
	CloseHandle(disk.handle);
#else
	close(disk.fd);
#endif

out_unlink:
#ifdef __MINGW32__
	DeleteFile(cla.disk);
#else
	unlink(cla.disk);
#endif

out:
	snprintf(str, len, "%x %d", disk.fd, disk_id);

	if (disk_id >= 0)
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

int test_disk_remove(char *str, int len)
{
	int ret;

	ret = lkl_disk_remove(disk);

#ifdef __MINGW32__
	CloseHandle(disk.handle);
#else
	close(disk.fd);
#endif

	if (ret == 0)
		return TEST_SUCCESS;

	return TEST_FAILURE;
}


static char mnt_point[32];

static int test_mount_dev(char *str, int len)
{
	long ret;

	ret = lkl_mount_dev(disk_id, cla.partition, cla.fstype, 0, NULL,
			mnt_point, sizeof(mnt_point));

	snprintf(str, len, "%ld", ret);

	if (ret == 0)
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

static int test_umount_dev(char *str, int len)
{
	long ret, ret2;

	ret = lkl_sys_chdir("/");

	ret2 = lkl_umount_dev(disk_id, cla.partition, 0, 1000);

	snprintf(str, len, "%ld %ld", ret, ret2);

	if (!ret && !ret2)
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

struct lkl_dir *dir;

static int test_opendir(char *str, int len)
{
	int err;

	dir = lkl_opendir(mnt_point, &err);

	snprintf(str, len, "%s %d", mnt_point, err);

	if (err == 0)
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

static int test_readdir(char *str, int len)
{
	struct lkl_linux_dirent64 *de = lkl_readdir(dir);
	int wr = 0;

	while (de) {
		wr += snprintf(str + wr, len - wr, "%s ", de->d_name);
		if (wr >= len)
			break;
		de = lkl_readdir(dir);
	}

	if (lkl_errdir(dir) == 0)
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

static int test_closedir(char *str, int len)
{
	long ret;

	ret = lkl_closedir(dir);

	if (ret == 0)
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

static int test_chdir(char *str, int len, const char *path)
{
	long ret;

	ret = lkl_sys_chdir(path);

	snprintf(str, len, "%ld", ret);

	if (ret == 0)
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

int main(int argc, const char **argv)
{
	if (parse_args(argc, argv, args) < 0)
		return -1;

	if (!cla.printk)
		lkl_host_ops.print = NULL;

	TEST(disk_add);

	lkl_start_kernel(&lkl_host_ops, "mem=16M loglevel=8");

	TEST(mount_dev);
	TEST(chdir, mnt_point);
	TEST(opendir);
	TEST(readdir);
	TEST(closedir);
	TEST(umount_dev);
	TEST(disk_remove);
}
