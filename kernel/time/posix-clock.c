// SPDX-License-Identifier: GPL-2.0+
/*
 * Support for dynamic clock devices
 *
 * Copyright (C) 2010 OMICRON electronics GmbH
 */
#include <linux/device.h>
#include <linux/export.h>
#include <linux/file.h>
#include <linux/posix-clock.h>
#include <linux/slab.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>

#include "posix-timers.h"

/*
 * Returns NULL if the posix_clock instance attached to 'fp' is old and stale.
 */
static struct posix_clock *get_posix_clock(struct file *fp)
{
	struct posix_clock_context *pccontext = fp->private_data;
	struct posix_clock *clk = pccontext->clk;

	down_read(&clk->rwsem);

	if (!clk->zombie)
		return clk;

	up_read(&clk->rwsem);

	return NULL;
}

static void put_posix_clock(struct posix_clock *clk)
{
	up_read(&clk->rwsem);
}

static ssize_t posix_clock_read(struct file *fp, char __user *buf,
				size_t count, loff_t *ppos)
{
	struct posix_clock_context *pccontext = fp->private_data;
	struct posix_clock *clk = get_posix_clock(fp);
	int err = -EINVAL;

	if (!clk)
		return -ENODEV;

	if (clk->ops.read)
		err = clk->ops.read(pccontext, fp->f_flags, buf, count);

	put_posix_clock(clk);

	return err;
}

static __poll_t posix_clock_poll(struct file *fp, poll_table *wait)
{
	struct posix_clock_context *pccontext = fp->private_data;
	struct posix_clock *clk = get_posix_clock(fp);
	__poll_t result = 0;

	if (!clk)
		return EPOLLERR;

	if (clk->ops.poll)
		result = clk->ops.poll(pccontext, fp, wait);

	put_posix_clock(clk);

	return result;
}

static long posix_clock_ioctl(struct file *fp,
			      unsigned int cmd, unsigned long arg)
{
	struct posix_clock_context *pccontext = fp->private_data;
	struct posix_clock *clk = get_posix_clock(fp);
	int err = -ENOTTY;

	if (!clk)
		return -ENODEV;

	if (clk->ops.ioctl)
		err = clk->ops.ioctl(pccontext, cmd, arg);

	put_posix_clock(clk);

	return err;
}

#ifdef CONFIG_COMPAT
static long posix_clock_compat_ioctl(struct file *fp,
				     unsigned int cmd, unsigned long arg)
{
	struct posix_clock_context *pccontext = fp->private_data;
	struct posix_clock *clk = get_posix_clock(fp);
	int err = -ENOTTY;

	if (!clk)
		return -ENODEV;

	if (clk->ops.ioctl)
		err = clk->ops.ioctl(pccontext, cmd, arg);

	put_posix_clock(clk);

	return err;
}
#endif

static int posix_clock_open(struct inode *inode, struct file *fp)
{
	int err;
	struct posix_clock *clk =
		container_of(inode->i_cdev, struct posix_clock, cdev);
	struct posix_clock_context *pccontext;

	down_read(&clk->rwsem);

	if (clk->zombie) {
		err = -ENODEV;
		goto out;
	}
	pccontext = kzalloc(sizeof(*pccontext), GFP_KERNEL);
	if (!pccontext) {
		err = -ENOMEM;
		goto out;
	}
	pccontext->clk = clk;
	if (clk->ops.open) {
		err = clk->ops.open(pccontext, fp->f_mode);
		if (err) {
			kfree(pccontext);
			goto out;
		}
	}

	fp->private_data = pccontext;
	get_device(clk->dev);
	err = 0;
out:
	up_read(&clk->rwsem);
	return err;
}

static int posix_clock_release(struct inode *inode, struct file *fp)
{
	struct posix_clock_context *pccontext = fp->private_data;
	struct posix_clock *clk;
	int err = 0;

	if (!pccontext)
		return -ENODEV;
	clk = pccontext->clk;

	if (clk->ops.release)
		err = clk->ops.release(pccontext);

	put_device(clk->dev);

	kfree(pccontext);
	fp->private_data = NULL;

	return err;
}

static const struct file_operations posix_clock_file_operations = {
	.owner		= THIS_MODULE,
	.read		= posix_clock_read,
	.poll		= posix_clock_poll,
	.unlocked_ioctl	= posix_clock_ioctl,
	.open		= posix_clock_open,
	.release	= posix_clock_release,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= posix_clock_compat_ioctl,
#endif
};

int posix_clock_register(struct posix_clock *clk, struct device *dev)
{
	int err;

	init_rwsem(&clk->rwsem);

	cdev_init(&clk->cdev, &posix_clock_file_operations);
	err = cdev_device_add(&clk->cdev, dev);
	if (err) {
		pr_err("%s unable to add device %d:%d\n",
			dev_name(dev), MAJOR(dev->devt), MINOR(dev->devt));
		return err;
	}
	clk->cdev.owner = clk->ops.owner;
	clk->dev = dev;

	return 0;
}
EXPORT_SYMBOL_GPL(posix_clock_register);

void posix_clock_unregister(struct posix_clock *clk)
{
	cdev_device_del(&clk->cdev, clk->dev);

	down_write(&clk->rwsem);
	clk->zombie = true;
	up_write(&clk->rwsem);

	put_device(clk->dev);
}
EXPORT_SYMBOL_GPL(posix_clock_unregister);

struct posix_clock_desc {
	struct file *fp;
	struct posix_clock *clk;
};

static int get_clock_desc(const clockid_t id, struct posix_clock_desc *cd)
{
	struct file *fp = fget(clockid_to_fd(id));
	int err = -EINVAL;

	if (!fp)
		return err;

	if (fp->f_op->open != posix_clock_open || !fp->private_data)
		goto out;

	cd->fp = fp;
	cd->clk = get_posix_clock(fp);

	err = cd->clk ? 0 : -ENODEV;
out:
	if (err)
		fput(fp);
	return err;
}

static void put_clock_desc(struct posix_clock_desc *cd)
{
	put_posix_clock(cd->clk);
	fput(cd->fp);
}

static int pc_clock_adjtime(clockid_t id, struct __kernel_timex *tx)
{
	struct posix_clock_desc cd;
	int err;

	err = get_clock_desc(id, &cd);
	if (err)
		return err;

	if ((cd.fp->f_mode & FMODE_WRITE) == 0) {
		err = -EACCES;
		goto out;
	}

	if (cd.clk->ops.clock_adjtime)
		err = cd.clk->ops.clock_adjtime(cd.clk, tx);
	else
		err = -EOPNOTSUPP;
out:
	put_clock_desc(&cd);

	return err;
}

static int pc_clock_gettime(clockid_t id, struct timespec64 *ts)
{
	struct posix_clock_desc cd;
	int err;

	err = get_clock_desc(id, &cd);
	if (err)
		return err;

	if (cd.clk->ops.clock_gettime)
		err = cd.clk->ops.clock_gettime(cd.clk, ts);
	else
		err = -EOPNOTSUPP;

	put_clock_desc(&cd);

	return err;
}

static int pc_clock_getres(clockid_t id, struct timespec64 *ts)
{
	struct posix_clock_desc cd;
	int err;

	err = get_clock_desc(id, &cd);
	if (err)
		return err;

	if (cd.clk->ops.clock_getres)
		err = cd.clk->ops.clock_getres(cd.clk, ts);
	else
		err = -EOPNOTSUPP;

	put_clock_desc(&cd);

	return err;
}

static int pc_clock_settime(clockid_t id, const struct timespec64 *ts)
{
	struct posix_clock_desc cd;
	int err;

	if (!timespec64_valid_strict(ts))
		return -EINVAL;

	err = get_clock_desc(id, &cd);
	if (err)
		return err;

	if ((cd.fp->f_mode & FMODE_WRITE) == 0) {
		err = -EACCES;
		goto out;
	}

	if (cd.clk->ops.clock_settime)
		err = cd.clk->ops.clock_settime(cd.clk, ts);
	else
		err = -EOPNOTSUPP;
out:
	put_clock_desc(&cd);

	return err;
}

const struct k_clock clock_posix_dynamic = {
	.clock_getres		= pc_clock_getres,
	.clock_set		= pc_clock_settime,
	.clock_get_timespec	= pc_clock_gettime,
	.clock_adj		= pc_clock_adjtime,
};
