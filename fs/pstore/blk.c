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
 *
 * Usually, this will be a partition of a block device.
 *
 * blkdev accepts the following variants, when built as a module:
 * 1) /dev/<disk_name> represents the device number of disk
 * 2) /dev/<disk_name><decimal> represents the device number
 *    of partition - device number of disk plus the partition number
 * 3) /dev/<disk_name>p<decimal> - same as the above, that form is
 *    used when disk name of partitioned disk ends on a digit.
 *
 * blkdev accepts the following variants when built into the kernel:
 * 1) <hex_major><hex_minor> device number in hexadecimal representation,
 *    with no leading 0x, for example b302.
 * 2) PARTUUID=00112233-4455-6677-8899-AABBCCDDEEFF representing the
 *    unique id of a partition if the partition table provides it.
 *    The UUID may be either an EFI/GPT UUID, or refer to an MSDOS
 *    partition using the format SSSSSSSS-PP, where SSSSSSSS is a zero-
 *    filled hex representation of the 32-bit "NT disk signature", and PP
 *    is a zero-filled hex representation of the 1-based partition number.
 * 3) PARTUUID=<UUID>/PARTNROFF=<int> to select a partition in relation to
 *    a partition with a known unique id.
 * 4) <major>:<minor> major and minor number of the device separated by
 *    a colon.
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
static struct pstore_zone_info *pstore_zone_info;

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
	pstore_zone_info->name = _##name_;			\
}

static int __register_pstore_device(struct pstore_device_info *dev)
{
	int ret;

	lockdep_assert_held(&pstore_blk_lock);

	if (!dev) {
		pr_err("NULL device info\n");
		return -EINVAL;
	}
	if (!dev->total_size) {
		pr_err("zero sized device\n");
		return -EINVAL;
	}
	if (!dev->read) {
		pr_err("no read handler for device\n");
		return -EINVAL;
	}
	if (!dev->write) {
		pr_err("no write handler for device\n");
		return -EINVAL;
	}

	/* someone already registered before */
	if (pstore_zone_info)
		return -EBUSY;

	pstore_zone_info = kzalloc(sizeof(struct pstore_zone_info), GFP_KERNEL);
	if (!pstore_zone_info)
		return -ENOMEM;

	/* zero means not limit on which backends to attempt to store. */
	if (!dev->flags)
		dev->flags = UINT_MAX;

	verify_size(kmsg_size, 4096, dev->flags & PSTORE_FLAGS_DMESG);
	verify_size(pmsg_size, 4096, dev->flags & PSTORE_FLAGS_PMSG);
	verify_size(console_size, 4096, dev->flags & PSTORE_FLAGS_CONSOLE);
	verify_size(ftrace_size, 4096, dev->flags & PSTORE_FLAGS_FTRACE);

	pstore_zone_info->total_size = dev->total_size;
	pstore_zone_info->max_reason = max_reason;
	pstore_zone_info->read = dev->read;
	pstore_zone_info->write = dev->write;
	pstore_zone_info->erase = dev->erase;
	pstore_zone_info->panic_write = dev->panic_write;
	pstore_zone_info->name = KBUILD_MODNAME;
	pstore_zone_info->owner = THIS_MODULE;

	ret = register_pstore_zone(pstore_zone_info);
	if (ret) {
		kfree(pstore_zone_info);
		pstore_zone_info = NULL;
	}
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
	if (pstore_zone_info && pstore_zone_info->read == dev->read) {
		unregister_pstore_zone(pstore_zone_info);
		kfree(pstore_zone_info);
		pstore_zone_info = NULL;
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
static int __register_pstore_blk(const char *devpath)
{
	struct pstore_device_info dev = {
		.read = psblk_generic_blk_read,
		.write = psblk_generic_blk_write,
	};
	struct inode *inode;
	int ret = -ENODEV;

	lockdep_assert_held(&pstore_blk_lock);

	psblk_file = filp_open(devpath, O_RDWR | O_DSYNC | O_NOATIME | O_EXCL, 0);
	if (IS_ERR(psblk_file)) {
		ret = PTR_ERR(psblk_file);
		pr_err("failed to open '%s': %d!\n", devpath, ret);
		goto err;
	}

	inode = file_inode(psblk_file);
	if (!S_ISBLK(inode->i_mode)) {
		pr_err("'%s' is not block device!\n", devpath);
		goto err_fput;
	}

	inode = I_BDEV(psblk_file->f_mapping->host)->bd_inode;
	dev.total_size = i_size_read(inode);

	ret = __register_pstore_device(&dev);
	if (ret)
		goto err_fput;

	return 0;

err_fput:
	fput(psblk_file);
err:
	psblk_file = NULL;

	return ret;
}

static void __unregister_pstore_blk(struct file *device)
{
	struct pstore_device_info dev = { .read = psblk_generic_blk_read };

	lockdep_assert_held(&pstore_blk_lock);
	if (psblk_file && psblk_file == device) {
		__unregister_pstore_device(&dev);
		fput(psblk_file);
		psblk_file = NULL;
	}
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
	dev_t dev = name_to_dev_t(initial_devname);

	if (!dev) {
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

static int __init pstore_blk_init(void)
{
	int ret = 0;

	mutex_lock(&pstore_blk_lock);
	if (!pstore_zone_info && best_effort && blkdev[0]) {
		ret = __register_pstore_blk(early_boot_devpath(blkdev));
		if (ret == 0 && pstore_zone_info)
			pr_info("attached %s:%s (%zu) (no dedicated panic_write!)\n",
				pstore_zone_info->name, blkdev,
				pstore_zone_info->total_size);
	}
	mutex_unlock(&pstore_blk_lock);

	return ret;
}
late_initcall(pstore_blk_init);

static void __exit pstore_blk_exit(void)
{
	mutex_lock(&pstore_blk_lock);
	if (psblk_file)
		__unregister_pstore_blk(psblk_file);
	else {
		struct pstore_device_info dev = { };

		if (pstore_zone_info)
			dev.read = pstore_zone_info->read;
		__unregister_pstore_device(&dev);
	}
	mutex_unlock(&pstore_blk_lock);
}
module_exit(pstore_blk_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("WeiXiong Liao <liaoweixiong@allwinnertech.com>");
MODULE_AUTHOR("Kees Cook <keescook@chromium.org>");
MODULE_DESCRIPTION("pstore backend for block devices");
