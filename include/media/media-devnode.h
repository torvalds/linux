/*
 * Media device node
 *
 * Copyright (C) 2010 Nokia Corporation
 *
 * Contacts: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *	     Sakari Ailus <sakari.ailus@iki.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * --
 *
 * Common functions for media-related drivers to register and unregister media
 * device nodes.
 */

#ifndef _MEDIA_DEVNODE_H
#define _MEDIA_DEVNODE_H

#include <linux/poll.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>

/*
 * Flag to mark the media_devnode struct as registered. Drivers must not touch
 * this flag directly, it will be set and cleared by media_devnode_register and
 * media_devnode_unregister.
 */
#define MEDIA_FLAG_REGISTERED	0

struct media_file_operations {
	struct module *owner;
	ssize_t (*read) (struct file *, char __user *, size_t, loff_t *);
	ssize_t (*write) (struct file *, const char __user *, size_t, loff_t *);
	unsigned int (*poll) (struct file *, struct poll_table_struct *);
	long (*ioctl) (struct file *, unsigned int, unsigned long);
	long (*compat_ioctl) (struct file *, unsigned int, unsigned long);
	int (*open) (struct file *);
	int (*release) (struct file *);
};

/**
 * struct media_devnode - Media device node
 * @fops:	pointer to struct media_file_operations with media device ops
 * @dev:	struct device pointer for the media controller device
 * @cdev:	struct cdev pointer character device
 * @parent:	parent device
 * @minor:	device node minor number
 * @flags:	flags, combination of the MEDIA_FLAG_* constants
 * @release:	release callback called at the end of media_devnode_release()
 *
 * This structure represents a media-related device node.
 *
 * The @parent is a physical device. It must be set by core or device drivers
 * before registering the node.
 */
struct media_devnode {
	/* device ops */
	const struct media_file_operations *fops;

	/* sysfs */
	struct device dev;		/* media device */
	struct cdev cdev;		/* character device */
	struct device *parent;		/* device parent */

	/* device info */
	int minor;
	unsigned long flags;		/* Use bitops to access flags */

	/* callbacks */
	void (*release)(struct media_devnode *mdev);
};

/* dev to media_devnode */
#define to_media_devnode(cd) container_of(cd, struct media_devnode, dev)

int __must_check media_devnode_register(struct media_devnode *mdev,
					struct module *owner);
void media_devnode_unregister(struct media_devnode *mdev);

static inline struct media_devnode *media_devnode_data(struct file *filp)
{
	return filp->private_data;
}

static inline int media_devnode_is_registered(struct media_devnode *mdev)
{
	return test_bit(MEDIA_FLAG_REGISTERED, &mdev->flags);
}

#endif /* _MEDIA_DEVNODE_H */
