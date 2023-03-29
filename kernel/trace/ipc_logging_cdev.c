// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/idr.h>
#include <linux/ipc_logging.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#include "ipc_logging_private.h"

#define IPL_CDEV_MAX 255

static dev_t cdev_devt;
static struct class *cdev_class;
static DEFINE_IDA(ipl_minor_ida);

static void dfunc_string(struct encode_context *ectxt, struct decode_context *dctxt)
{
	tsv_timestamp_read(ectxt, dctxt, "");
	tsv_qtimer_read(ectxt, dctxt, " ");
	tsv_byte_array_read(ectxt, dctxt, "");

	/* add trailing \n if necessary */
	if (*(dctxt->buff - 1) != '\n') {
		if (dctxt->size) {
			++dctxt->buff;
			--dctxt->size;
		}
		*(dctxt->buff - 1) = '\n';
	}
}

static int debug_log(struct ipc_log_context *ilctxt, char *buff, int size, int cont)
{
	int i = 0;
	int ret;

	if (size < MAX_MSG_DECODED_SIZE) {
		pr_err("%s: buffer size %d < %d\n", __func__, size, MAX_MSG_DECODED_SIZE);
		return -ENOMEM;
	}
	do {
		i = ipc_log_extract(ilctxt, buff, size - 1);
		if (cont && i == 0) {
			ret = wait_for_completion_interruptible(&ilctxt->read_avail);
			if (ret < 0)
				return ret;
		}
	} while (cont && i == 0);

	return i;
}

static char *ipc_log_cdev_devnode(struct device *dev, umode_t *mode)
{
	return kasprintf(GFP_KERNEL, "ipc_logging/%s", dev_name(dev));
}

static int ipc_log_cdev_open(struct inode *inode, struct file *filp)
{
	struct ipc_log_cdev *ipl_cdev;

	ipl_cdev = container_of(inode->i_cdev, struct ipc_log_cdev, cdev);
	filp->private_data = container_of(ipl_cdev, struct ipc_log_context, cdev);

	return 0;
}

/*
 * VFS Read operation which dispatches the call to the DevFS read command stored in
 * file->private_data.
 *
 * @filp  File structure
 * @buff   user buffer
 * @count size of user buffer
 * @offp  file position to read from (only a value of 0 is accepted)
 *
 * @returns  = 0 end of file
 *           > 0 number of bytes read
 *           < 0 error
 */
static ssize_t ipc_log_cdev_read(struct file *filp, char __user *buff, size_t count, loff_t *offp)
{
	int ret, bsize;
	char *buffer;
	struct ipc_log_context *ilctxt;

	ilctxt = filp->private_data;
	ret = kref_get_unless_zero(&ilctxt->refcount) ? 0 : -EIO;
	if (ret)
		return ret;

	buffer = kmalloc(count, GFP_KERNEL);
	if (!buffer) {
		bsize = -ENOMEM;
		goto done;
	}

	/* only support non-continuous mode */
	bsize = debug_log(ilctxt, buffer, count, 0);

	if (bsize > 0) {
		if (copy_to_user(buff, buffer, bsize)) {
			bsize = -EFAULT;
			kfree(buffer);
			goto done;
		}
		*offp += bsize;
	}
	kfree(buffer);

done:
	ipc_log_context_put(ilctxt);
	return bsize;
}

static const struct file_operations cdev_fops = {
	.owner  = THIS_MODULE,
	.open   = ipc_log_cdev_open,
	.read   = ipc_log_cdev_read,
};

void ipc_log_cdev_remove(struct ipc_log_context *ilctxt)
{
	if (ilctxt->cdev.dev.class) {
		cdev_device_del(&ilctxt->cdev.cdev, &ilctxt->cdev.dev);
		ida_free(&ipl_minor_ida, (unsigned int)MINOR(ilctxt->cdev.dev.devt));
	}
}
EXPORT_SYMBOL(ipc_log_cdev_remove);

void ipc_log_cdev_create(struct ipc_log_context *ilctxt, const char *mod_name)
{
	int ret;
	int minor;
	dev_t devno;

	if (!cdev_class) {
		pr_err("%s: %s no device class created\n", __func__, mod_name);
		return;
	}

	minor = ida_alloc_range(&ipl_minor_ida, 0, IPL_CDEV_MAX, GFP_KERNEL);
	if (minor < 0) {
		pr_err("%s: %s failed to alloc ipl minor number %d\n", __func__, mod_name, minor);
		return;
	}

	devno = MKDEV(MAJOR(cdev_devt), minor);
	device_initialize(&ilctxt->cdev.dev);
	ilctxt->cdev.dev.devt = devno;
	ilctxt->cdev.dev.class = cdev_class;
	dev_set_name(&ilctxt->cdev.dev, "%s", mod_name);

	cdev_init(&ilctxt->cdev.cdev, &cdev_fops);
	ret = cdev_device_add(&ilctxt->cdev.cdev, &ilctxt->cdev.dev);
	if (ret) {
		pr_err("%s: unable to add ipl cdev %s, %d\n", __func__, mod_name, ret);
		ilctxt->cdev.dev.class = NULL;
		ida_free(&ipl_minor_ida, (unsigned int)minor);
		put_device(&ilctxt->cdev.dev);
		return;
	}

	add_deserialization_func((void *)ilctxt, TSV_TYPE_STRING, dfunc_string);
}
EXPORT_SYMBOL(ipc_log_cdev_create);

void ipc_log_cdev_init(void)
{
	int ret;

	cdev_class = NULL;

	ret = alloc_chrdev_region(&cdev_devt, 0, IPL_CDEV_MAX, "ipc_logging");
	if (ret) {
		pr_err("%s: unable to create ipl cdev regoin %d\n", __func__, ret);
		return;
	}

	cdev_class = class_create(THIS_MODULE, "ipc_logging");
	if (IS_ERR(cdev_class)) {
		pr_err("%s: unable to create ipl cdev class %ld\n", __func__, PTR_ERR(cdev_class));
		cdev_class = NULL;
		unregister_chrdev_region(cdev_devt, IPL_CDEV_MAX);
		return;
	}

	cdev_class->devnode = ipc_log_cdev_devnode;
}
EXPORT_SYMBOL(ipc_log_cdev_init);
