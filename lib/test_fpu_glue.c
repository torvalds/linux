// SPDX-License-Identifier: GPL-2.0+
/*
 * Test cases for using floating point operations inside a kernel module.
 *
 * This tests kernel_fpu_begin() and kernel_fpu_end() functions, especially
 * when userland has modified the floating point control registers. The kernel
 * state might depend on the state set by the userland thread that was active
 * before a syscall.
 *
 * To facilitate the test, this module registers file
 * /sys/kernel/debug/selftest_helpers/test_fpu, which when read causes a
 * sequence of floating point operations. If the operations fail, either the
 * read returns error status or the kernel crashes.
 * If the operations succeed, the read returns "1\n".
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/debugfs.h>
#include <linux/fpu.h>

#include "test_fpu.h"

static int test_fpu_get(void *data, u64 *val)
{
	int status = -EINVAL;

	kernel_fpu_begin();
	status = test_fpu();
	kernel_fpu_end();

	*val = 1;
	return status;
}

DEFINE_DEBUGFS_ATTRIBUTE(test_fpu_fops, test_fpu_get, NULL, "%lld\n");
static struct dentry *selftest_dir;

static int __init test_fpu_init(void)
{
	if (!kernel_fpu_available())
		return -EINVAL;

	selftest_dir = debugfs_create_dir("selftest_helpers", NULL);
	if (!selftest_dir)
		return -ENOMEM;

	debugfs_create_file_unsafe("test_fpu", 0444, selftest_dir, NULL,
				   &test_fpu_fops);

	return 0;
}

static void __exit test_fpu_exit(void)
{
	debugfs_remove(selftest_dir);
}

module_init(test_fpu_init);
module_exit(test_fpu_exit);

MODULE_LICENSE("GPL");
