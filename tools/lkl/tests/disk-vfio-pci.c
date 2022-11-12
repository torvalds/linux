// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <stdint.h>
#include <lkl.h>
#include <lkl_host.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "test.h"
#include "cla.h"

static struct {
	int printk;
	const char *fstype;
	const char *pciname;
} cla;

struct cl_arg args[] = {
	{ "type", 't', "filesystem type", 1, CL_ARG_STR, &cla.fstype },
	{ "pciname", 'n', "PCI device name (as %x:%x:%x.%x format)", 1,
	  CL_ARG_STR, &cla.pciname },
	{ 0 },
};

static char mnt_point[32];
static char bootparams[128];

static int lkl_test_umount_dev(void)
{
	long ret, ret2;

	ret = lkl_sys_chdir("/");

	ret2 = lkl_umount_blkdev(LKL_MKDEV(259, 0), 0, 1000);

	lkl_test_logf("%ld %ld", ret, ret2);

	if (!ret && !ret2)
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

struct lkl_dir *dir;

static int lkl_test_opendir(void)
{
	int err;

	dir = lkl_opendir(mnt_point, &err);

	lkl_test_logf("lkl_opedir(%s) = %d %s\n", mnt_point, err,
		      lkl_strerror(err));

	if (err == 0)
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

static int lkl_test_readdir(void)
{
	struct lkl_linux_dirent64 *de = lkl_readdir(dir);
	int wr = 0;

	while (de) {
		wr += lkl_test_logf("%s ", de->d_name);
		if (wr >= 70) {
			lkl_test_logf("\n");
			wr = 0;
			break;
		}
		de = lkl_readdir(dir);
	}

	if (lkl_errdir(dir) == 0)
		return TEST_SUCCESS;

	return TEST_FAILURE;
}

LKL_TEST_CALL(mount_dev, lkl_mount_blkdev, 0, LKL_MKDEV(259, 0),
	      cla.fstype, 0, NULL, mnt_point, sizeof(mnt_point))
LKL_TEST_CALL(closedir, lkl_closedir, 0, dir);
LKL_TEST_CALL(chdir_mnt_point, lkl_sys_chdir, 0, mnt_point);
LKL_TEST_CALL(start_kernel, lkl_start_kernel, 0, bootparams);
LKL_TEST_CALL(stop_kernel, lkl_sys_halt, 0);

struct lkl_test tests[] = {
	LKL_TEST(start_kernel),	   LKL_TEST(mount_dev),
	LKL_TEST(chdir_mnt_point), LKL_TEST(opendir),
	LKL_TEST(readdir),	   LKL_TEST(closedir),
	LKL_TEST(umount_dev),	   LKL_TEST(stop_kernel),
};

int main(int argc, const char **argv)
{
	int ret;

	if (parse_args(argc, argv, args) < 0)
		return -1;

	snprintf(bootparams, sizeof(bootparams),
		 "mem=16M loglevel=8 lkl_pci=vfio%s", cla.pciname);

	lkl_host_ops.print = lkl_test_log;

	lkl_init(&lkl_host_ops);

	ret = lkl_test_run(tests, sizeof(tests) / sizeof(struct lkl_test),
			"disk-vfio-pci %s", cla.fstype);

	lkl_cleanup();

	return ret;
}
