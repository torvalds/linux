/*
 * LIRC base driver
 *
 * by Artur Lipowski <alipowski@interia.pl>
 *        This code is licensed under GNU GPL
 *
 */

#ifndef _LINUX_LIRC_DEV_H
#define _LINUX_LIRC_DEV_H

#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/poll.h>
#include <linux/kfifo.h>
#include <media/lirc.h>
#include <linux/device.h>
#include <linux/cdev.h>

/**
 * struct lirc_dev - represents a LIRC device
 *
 * @name:		used for logging
 * @minor:		the minor device (/dev/lircX) number for the device
 * @rdev:		&struct rc_dev associated with the device
 * @fops:		&struct file_operations for the device
 * @owner:		the module owning this struct
 * @open:		open count for the device's chardev
 * @mutex:		serialises file_operations calls
 * @dev:		&struct device assigned to the device
 * @cdev:		&struct cdev assigned to the device
 */
struct lirc_dev {
	char name[40];
	unsigned int minor;

	struct rc_dev *rdev;
	const struct file_operations *fops;
	struct module *owner;

	int open;

	struct mutex mutex; /* protect from simultaneous accesses */

	struct device dev;
	struct cdev cdev;
};

struct lirc_dev *lirc_allocate_device(void);

void lirc_free_device(struct lirc_dev *d);

int lirc_register_device(struct lirc_dev *d);

void lirc_unregister_device(struct lirc_dev *d);

/* default file operations
 * used by drivers if they override only some operations
 */
int lirc_dev_fop_open(struct inode *inode, struct file *file);
int lirc_dev_fop_close(struct inode *inode, struct file *file);
#endif
