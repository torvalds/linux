// SPDX-License-Identifier: GPL-2.0
/*
 * Implements pstore backend driver that write to block (or non-block) storage
 * devices, using the pstore/zone API.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/string.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/pstore_blk.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/init_syscalls.h>
#include <linux/mount.h>

static long kmsg_size = CONFIG_PSTORE_BLK_KMSG_SIZE;
module_param(kmsg_size, long, 0400);
MODULE_PARM_DESC(kmsg_size, "kmsg dump record size in kbytes");

static int max_reason = CONFIG_PSTORE_BLK_MAX_REASON;
module_param(max_reason, int, 0400);
MODULE_PARM_DESC(max_reason,
		 "maximum reason for kmsg dump (default 2: Oops and Panic)");

#if IS_ENABLED(CONFIG_PSTORE_PMSG)
static long pmsg_size = CONFIG_PSTORE_BLK_PMSG_SIZE;
#else
static long pmsg_size = -1;
#endif
module_param(pmsg_size, long, 0400);
MODULE_PARM_DESC(pmsg_size, "pmsg size in kbytes");

#if IS_ENABLED(CONFIG_PSTORE_CONSOLE)
static long console_size = CONFIG_PSTORE_BLK_CONSOLE_SIZE;
#else
static long console_size = -1;
#endif
module_param(console_size, long, 0400);
MODULE_PARM_DESC(console_size, "console size in kbytes");

#if IS_ENABLED(CONFIG_PSTORE_FTRACE)
static long ftrace_size = CONFIG_PSTORE_BLK_FTRACE_SIZE;
#else
static long ftrace_size = -1;
#endif
module_param(ftrace_size, long, 0400);
MODULE_PARM_DESC(ftrace_size, "ftrace size in kbytes");

static bool best_effort;
module_param(best_effort, bool, 0400);
MODULE_PARM_DESC(best_effort, "use best effort to write (i.e. do not require storage driver pstore support, default: off)");

/*
 * blkdev - the block device to use for pstore storage
 * See Documentation/admin-guide/pstore-blk.rst for details.
 */
static char blkdev[80] = CONFIG_PSTORE_BLK_BLKDEV;
module_param_string(blkdev, blkdev, 80, 0400);
MODULE_PARM_DESC(blkdev, "block device for pstore storage");

/*
 * All globals must only be accessed under the pstore_blk_lock
 * during the register/unregister functions.
 */
static DEFINE_MUTEX(pstore_blk_lock);
static struct file *psblk_file;
static struct pstore_device_info *pstore_device_info;

#define check_size(name, alignsize) ({				\
	long _##name_ = (name);					\
	_##name_ = _##name_ <= 0 ? 0 : (_##name_ * 1024);	\
	if (_##name_ & ((alignsize) - 1)) {			\
		pr_info(#name " must align to %d\n",		\
				(alignsize));			\
		_##name_ = ALIGN(name, (alignsize));		\
	}							\
	_##name_;						\
})

#define verify_size(name, alignsize, enabled) {			\
	long _##name_;						\
	if (enabled)						\
		_##name_ = check_size(name, alignsize);		\
	else							\
		_##name_ = 0;					\
	/* Synchronize module parameters with resuls. */	\
	name = _##name_ / 1024;					\
	dev->zone.name = _##name_;				\
}

static int __register_pstore_device(struct pstore_device_info *dev)
{
	int ret;

	lockdep_assert_held(&pstore_blk_lock);

	if (!dev) {
		pr_err("NULL device info\n");
		return -EINVAL;
	}
	if (!dev->zone.total_size) {
		pr_err("zero sized device\n");
		return -EINVAL;
	}
	if (!dev->zone.read) {
		pr_err("no read handler for device\n");
		return -EINVAL;
	}
	if (!dev->zone.write) {
		pr_err("no write handler for device\n");
		return -EINVAL;
	}

	/* someone already registered before */
	if (pstore_device_info)
		return -EBUSY;

	/* zero means not limit on which backends to attempt to store. */
	if (!dev->flags)
		dev->flags = UINT_MAX;

	/* Copy in module parameters. */
	verify_size(kmsg_size, 4096, dev->flags & PSTORE_FLAGS_DMESG);
	verify_size(pmsg_size, 4096, dev->flags & PSTORE_FLAGS_PMSG);
	verify_size(console_size, 4096, dev->flags & PSTORE_FLAGS_CONSOLE);
	verify_size(ftrace_size, 4096, dev->flags & PSTORE_FLAGS_FTRACE);
	dev->zone.max_reason = max_reason;

	/* Initialize required zone ownership details. */
	dev->zone.name = KBUILD_MODNAME;
	dev->zone.owner = THIS_MODULE;

	ret = register_pstore_zone(&dev->zone);
	if (ret == 0)
		pstore_device_info = dev;

	return ret;
}
/**
 * register_pstore_device() - register non-block device to pstore/blk
 *
 * @dev: non-block device information
 *
 * Return:
 * * 0		- OK
 * * Others	- something error.
 */
int register_pstore_device(struct pstore_device_info *dev)
{
	int ret;

	mutex_lock(&pstore_blk_lock);
	ret = __register_pstore_device(dev);
	mutex_unlock(&pstore_blk_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(register_pstore_device);

static void __unregister_pstore_device(struct pstore_device_info *dev)
{
	lockdep_assert_held(&pstore_blk_lock);
	if (pstore_device_info && pstore_device_info == dev) {
		unregister_pstore_zone(&dev->zone);
		pstore_device_info = NULL;
	}
}

/**
 * unregister_pstore_device() - unregister non-block device from pstore/blk
 *
 * @dev: non-block device information
 */
void unregister_pstore_device(struct pstore_device_info *dev)
{
	mutex_lock(&pstore_blk_lock);
	__unregister_pstore_device(dev);
	mutex_unlock(&pstore_blk_lock);
}
EXPORT_SYMBOL_GPL(unregister_pstore_device);

static ssize_t psblk_generic_blk_read(char *buf, size_t bytes, loff_t pos)
{
	return kernel_read(psblk_file, buf, bytes, &pos);
}

static ssize_t psblk_generic_blk_write(const char *buf, size_t bytes,
		loff_t pos)
{
	/* Console/Ftrace backend may handle buffer until flush dirty zones */
	if (in_interrupt() || irqs_disabled())
		return -EBUSY;
	return kernel_write(psblk_file, buf, bytes, &pos);
}

/*
 * This takes its configuration only from the module parameters now.
 */
static int __register_pstore_blk(struct pstore_device_info *dev,
				 const char *devpath)
{
	int ret = -ENODEV;

	lockdep_assert_held(&pstore_blk_lock);

	psblk_file = filp_open(devpath, O_RDWR | O_DSYNC | O_NOATIME | O_EXCL, 0);
	if (IS_ERR(psblk_file)) {
		ret = PTR_ERR(psblk_file);
		pr_err("failed to open '%s': %d!\n", devpath, ret);
		goto err;
	}

	if (!S_ISBLK(file_inode(psblk_file)->i_mode)) {
		pr_err("'%s' is not block device!\n", devpath);
		goto err_fput;
	}

	dev->zone.total_size =
		bdev_nr_bytes(I_BDEV(psblk_file->f_mapping->host));

	ret = __register_pstore_device(dev);
	if (ret)
		goto err_fput;

	return 0;

err_fput:
	fput(psblk_file);
err:
	psblk_file = NULL;

	return ret;
}

/* get information of pstore/blk */
int pstore_blk_get_config(struct pstore_blk_config *info)
{
	strncpy(info->device, blkdev, 80);
	info->max_reason = max_reason;
	info->kmsg_size = check_size(kmsg_size, 4096);
	info->pmsg_size = check_size(pmsg_size, 4096);
	info->ftrace_size = check_size(ftrace_size, 4096);
	info->console_size = check_size(console_size, 4096);

	return 0;
}
EXPORT_SYMBOL_GPL(pstore_blk_get_config);


#ifndef MODULE
static const char devname[] = "/dev/pstore-blk";
static __init const char *early_boot_devpath(const char *initial_devname)
{
	/*
	 * During early boot the real root file system hasn't been
	 * mounted yet, and no device nodes are present yet. Use the
	 * same scheme to find the device that we use for mounting
	 * the root file system.
	 */
	dev_t dev;

	if (early_lookup_bdev(initial_devname, &dev)) {
		pr_err("failed to resolve '%s'!\n", initial_devname);
		return initial_devname;
	}

	init_unlink(devname);
	init_mknod(devname, S_IFBLK | 0600, new_encode_dev(dev));

	return devname;
}
#else
static inline const char *early_boot_devpath(const char *initial_devname)
{
	return initial_devname;
}
#endif

static int __init __best_effort_init(void)
{
	struct pstore_device_info *best_effort_dev;
	int ret;

	/* No best-effort mode requested. */
	if (!best_effort)
		return 0;

	/* Reject an empty blkdev. */
	if (!blkdev[0]) {
		pr_err("blkdev empty with best_effort=Y\n");
		return -EINVAL;
	}

	best_effort_dev = kzalloc(sizeof(*best_effort_dev), GFP_KERNEL);
	if (!best_effort_dev)
		return -ENOMEM;

	best_effort_dev->zone.read = psblk_generic_blk_read;
	best_effort_dev->zone.write = psblk_generic_blk_write;

	ret = __register_pstore_blk(best_effort_dev,
				    early_boot_devpath(blkdev));
	if (ret)
		kfree(best_effort_dev);
	else
		pr_info("attached %s (%lu) (no dedicated panic_write!)\n",
			blkdev, best_effort_dev->zone.total_size);

	return ret;
}

static void __exit __best_effort_exit(void)
{
	/*
	 * Currently, the only user of psblk_file is best_effort, so
	 * we can assume that pstore_device_info is associated with it.
	 * Once there are "real" blk devices, there will need to be a
	 * dedicated pstore_blk_info, etc.
	 */
	if (psblk_file) {
		struct pstore_device_info *dev = pstore_device_info;

		__unregister_pstore_device(dev);
		kfree(dev);
		fput(psblk_file);
		psblk_file = NULL;
	}
}

static int __init pstore_blk_init(void)
{
	int ret;

	mutex_lock(&pstore_blk_lock);
	ret = __best_effort_init();
	mutex_unlock(&pstore_blk_lock);

	return ret;
}
late_initcall(pstore_blk_init);

static void __exit pstore_blk_exit(void)
{
	mutex_lock(&pstore_blk_lock);
	__best_effort_exit();
	/* If we've been asked to unload, unregister any remaining device. */
	__unregister_pstore_device(pstore_device_info);
	mutex_unlock(&pstore_blk_lock);
}
module_exit(pstore_blk_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("WeiXiong Liao <liaoweixiong@allwinnertech.com>");
MODULE_AUTHOR("Kees Cook <keescook@chromium.org>");
MODULE_DESCRIPTION("pstore backend for block devices");
