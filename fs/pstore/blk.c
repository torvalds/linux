// SPDX-License-Identifier: GPL-2.0
/*
 * Implements pstore backend driver that write to block (or non-block) storage
 * devices, using the pstore/zone API.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include "../../block/blk.h"
#include <linux/blkdev.h>
#include <linux/string.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/pstore_blk.h>
#include <linux/mount.h>
#include <linux/uio.h>

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
 * blkdev accepts the following variants:
 * 1) <hex_major><hex_minor> device number in hexadecimal representation,
 *    with no leading 0x, for example b302.
 * 2) /dev/<disk_name> represents the device number of disk
 * 3) /dev/<disk_name><decimal> represents the device number
 *    of partition - device number of disk plus the partition number
 * 4) /dev/<disk_name>p<decimal> - same as the above, that form is
 *    used when disk name of partitioned disk ends on a digit.
 * 5) PARTUUID=00112233-4455-6677-8899-AABBCCDDEEFF representing the
 *    unique id of a partition if the partition table provides it.
 *    The UUID may be either an EFI/GPT UUID, or refer to an MSDOS
 *    partition using the format SSSSSSSS-PP, where SSSSSSSS is a zero-
 *    filled hex representation of the 32-bit "NT disk signature", and PP
 *    is a zero-filled hex representation of the 1-based partition number.
 * 6) PARTUUID=<UUID>/PARTNROFF=<int> to select a partition in relation to
 *    a partition with a known unique id.
 * 7) <major>:<minor> major and minor number of the device separated by
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
static struct block_device *psblk_bdev;
static struct pstore_zone_info *pstore_zone_info;

struct bdev_info {
	dev_t devt;
	sector_t nr_sects;
	sector_t start_sect;
};

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

static int __register_pstore_device(struct pstore_device_info *dev)
{
	int ret;

	lockdep_assert_held(&pstore_blk_lock);

	if (!dev || !dev->total_size || !dev->read || !dev->write)
		return -EINVAL;

	/* someone already registered before */
	if (pstore_zone_info)
		return -EBUSY;

	pstore_zone_info = kzalloc(sizeof(struct pstore_zone_info), GFP_KERNEL);
	if (!pstore_zone_info)
		return -ENOMEM;

	/* zero means not limit on which backends to attempt to store. */
	if (!dev->flags)
		dev->flags = UINT_MAX;

#define verify_size(name, alignsize, enabled) {				\
		long _##name_;						\
		if (enabled)						\
			_##name_ = check_size(name, alignsize);		\
		else							\
			_##name_ = 0;					\
		name = _##name_ / 1024;					\
		pstore_zone_info->name = _##name_;			\
	}

	verify_size(kmsg_size, 4096, dev->flags & PSTORE_FLAGS_DMESG);
	verify_size(pmsg_size, 4096, dev->flags & PSTORE_FLAGS_PMSG);
	verify_size(console_size, 4096, dev->flags & PSTORE_FLAGS_CONSOLE);
	verify_size(ftrace_size, 4096, dev->flags & PSTORE_FLAGS_FTRACE);
#undef verify_size

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

/**
 * psblk_get_bdev() - open block device
 *
 * @holder:	Exclusive holder identifier
 * @info:	Information about bdev to fill in
 *
 * Return: pointer to block device on success and others on error.
 *
 * On success, the returned block_device has reference count of one.
 */
static struct block_device *psblk_get_bdev(void *holder,
					   struct bdev_info *info)
{
	struct block_device *bdev = ERR_PTR(-ENODEV);
	fmode_t mode = FMODE_READ | FMODE_WRITE;
	sector_t nr_sects;

	lockdep_assert_held(&pstore_blk_lock);

	if (pstore_zone_info)
		return ERR_PTR(-EBUSY);

	if (!blkdev[0])
		return ERR_PTR(-ENODEV);

	if (holder)
		mode |= FMODE_EXCL;
	bdev = blkdev_get_by_path(blkdev, mode, holder);
	if (IS_ERR(bdev)) {
		dev_t devt;

		devt = name_to_dev_t(blkdev);
		if (devt == 0)
			return ERR_PTR(-ENODEV);
		bdev = blkdev_get_by_dev(devt, mode, holder);
		if (IS_ERR(bdev))
			return bdev;
	}

	nr_sects = bdev_nr_sectors(bdev);
	if (!nr_sects) {
		pr_err("not enough space for '%s'\n", blkdev);
		blkdev_put(bdev, mode);
		return ERR_PTR(-ENOSPC);
	}

	if (info) {
		info->devt = bdev->bd_dev;
		info->nr_sects = nr_sects;
		info->start_sect = get_start_sect(bdev);
	}

	return bdev;
}

static void psblk_put_bdev(struct block_device *bdev, void *holder)
{
	fmode_t mode = FMODE_READ | FMODE_WRITE;

	lockdep_assert_held(&pstore_blk_lock);

	if (!bdev)
		return;

	if (holder)
		mode |= FMODE_EXCL;
	blkdev_put(bdev, mode);
}

static ssize_t psblk_generic_blk_read(char *buf, size_t bytes, loff_t pos)
{
	struct block_device *bdev = psblk_bdev;
	struct file file;
	struct kiocb kiocb;
	struct iov_iter iter;
	struct kvec iov = {.iov_base = buf, .iov_len = bytes};

	if (!bdev)
		return -ENODEV;

	memset(&file, 0, sizeof(struct file));
	file.f_mapping = bdev->bd_inode->i_mapping;
	file.f_flags = O_DSYNC | __O_SYNC | O_NOATIME;
	file.f_inode = bdev->bd_inode;
	file_ra_state_init(&file.f_ra, file.f_mapping);

	init_sync_kiocb(&kiocb, &file);
	kiocb.ki_pos = pos;
	iov_iter_kvec(&iter, READ, &iov, 1, bytes);

	return generic_file_read_iter(&kiocb, &iter);
}

static ssize_t psblk_generic_blk_write(const char *buf, size_t bytes,
		loff_t pos)
{
	struct block_device *bdev = psblk_bdev;
	struct iov_iter iter;
	struct kiocb kiocb;
	struct file file;
	ssize_t ret;
	struct kvec iov = {.iov_base = (void *)buf, .iov_len = bytes};

	if (!bdev)
		return -ENODEV;

	/* Console/Ftrace backend may handle buffer until flush dirty zones */
	if (in_interrupt() || irqs_disabled())
		return -EBUSY;

	memset(&file, 0, sizeof(struct file));
	file.f_mapping = bdev->bd_inode->i_mapping;
	file.f_flags = O_DSYNC | __O_SYNC | O_NOATIME;
	file.f_inode = bdev->bd_inode;

	init_sync_kiocb(&kiocb, &file);
	kiocb.ki_pos = pos;
	iov_iter_kvec(&iter, WRITE, &iov, 1, bytes);

	inode_lock(bdev->bd_inode);
	ret = generic_write_checks(&kiocb, &iter);
	if (ret > 0)
		ret = generic_perform_write(&file, &iter, pos);
	inode_unlock(bdev->bd_inode);

	if (likely(ret > 0)) {
		const struct file_operations f_op = {.fsync = blkdev_fsync};

		file.f_op = &f_op;
		kiocb.ki_pos += ret;
		ret = generic_write_sync(&kiocb, ret);
	}
	return ret;
}

/*
 * This takes its configuration only from the module parameters now.
 * See psblk_get_bdev() and blkdev.
 */
static int __register_pstore_blk(void)
{
	char bdev_name[BDEVNAME_SIZE];
	struct block_device *bdev;
	struct pstore_device_info dev;
	struct bdev_info binfo;
	void *holder = blkdev;
	int ret = -ENODEV;

	lockdep_assert_held(&pstore_blk_lock);

	/* hold bdev exclusively */
	memset(&binfo, 0, sizeof(binfo));
	bdev = psblk_get_bdev(holder, &binfo);
	if (IS_ERR(bdev)) {
		pr_err("failed to open '%s'!\n", blkdev);
		return PTR_ERR(bdev);
	}

	/* only allow driver matching the @blkdev */
	if (!binfo.devt) {
		pr_debug("no major\n");
		ret = -ENODEV;
		goto err_put_bdev;
	}

	/* psblk_bdev must be assigned before register to pstore/blk */
	psblk_bdev = bdev;

	memset(&dev, 0, sizeof(dev));
	dev.total_size = binfo.nr_sects << SECTOR_SHIFT;
	dev.read = psblk_generic_blk_read;
	dev.write = psblk_generic_blk_write;

	ret = __register_pstore_device(&dev);
	if (ret)
		goto err_put_bdev;

	bdevname(bdev, bdev_name);
	pr_info("attached %s (no dedicated panic_write!)\n", bdev_name);
	return 0;

err_put_bdev:
	psblk_bdev = NULL;
	psblk_put_bdev(bdev, holder);
	return ret;
}

static void __unregister_pstore_blk(unsigned int major)
{
	struct pstore_device_info dev = { .read = psblk_generic_blk_read };
	void *holder = blkdev;

	lockdep_assert_held(&pstore_blk_lock);
	if (psblk_bdev && MAJOR(psblk_bdev->bd_dev) == major) {
		__unregister_pstore_device(&dev);
		psblk_put_bdev(psblk_bdev, holder);
		psblk_bdev = NULL;
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

static int __init pstore_blk_init(void)
{
	int ret = 0;

	mutex_lock(&pstore_blk_lock);
	if (!pstore_zone_info && best_effort && blkdev[0])
		ret = __register_pstore_blk();
	mutex_unlock(&pstore_blk_lock);

	return ret;
}
late_initcall(pstore_blk_init);

static void __exit pstore_blk_exit(void)
{
	mutex_lock(&pstore_blk_lock);
	if (psblk_bdev)
		__unregister_pstore_blk(MAJOR(psblk_bdev->bd_dev));
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
