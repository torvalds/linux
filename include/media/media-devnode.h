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

struct media_device;

/*
 * Flag to mark the media_devnode struct as registered. Drivers must not touch
 * this flag directly, it will be set and cleared by media_devnode_register and
 * media_devnode_unregister.
 */
#define MEDIA_FLAG_REGISTERED	0

/**
 * struct media_file_operations - Media device file operations
 *
 * @owner: should be filled with %THIS_MODULE
 * @read: pointer to the function that implements read() syscall
 * @write: pointer to the function that implements write() syscall
 * @poll: pointer to the function that implements poll() syscall
 * @ioctl: pointer to the function that implements ioctl() syscall
 * @compat_ioctl: pointer to the function that will handle 32 bits userspace
 *	calls to the the ioctl() syscall on a Kernel compiled with 64 bits.
 * @open: pointer to the function that implements open() syscall
 * @release: pointer to the function that will release the resources allocated
 *	by the @open function.
 */
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
 * @media_dev:	pointer to struct &media_device
 * @fops:	pointer to struct &media_file_operations with media device ops
 * @dev:	pointer to struct &device containing the media controller device
 * @cdev:	struct cdev pointer character device
 * @parent:	parent device
 * @minor:	device node minor number
 * @flags:	flags, combination of the ``MEDIA_FLAG_*`` constants
 * @release:	release callback called at the end of ``media_devnode_release()``
 *		routine at media-device.c.
 *
 * This structure represents a media-related device node.
 *
 * The @parent is a physical device. It must be set by core or device drivers
 * before registering the node.
 */
struct media_devnode {
	struct media_device *media_dev;

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
	void (*release)(struct media_devnode *devnode);
};

/* dev to media_devnode */
#define to_media_devnode(cd) container_of(cd, struct media_devnode, dev)

/**
 * media_devnode_register - register a media device node
 *
 * @mdev: struct media_device we want to register a device node
 * @devnode: media device node structure we want to register
 * @owner: should be filled with %THIS_MODULE
 *
 * The registration code assigns minor numbers and registers the new device node
 * with the kernel. An error is returned if no free minor number can be found,
 * or if the registration of the device node fails.
 *
 * Zero is returned on success.
 *
 * Note that if the media_devnode_register call fails, the release() callback of
 * the media_devnode structure is *not* called, so the caller is responsible for
 * freeing any data.
 */
int __must_check media_devnode_register(struct media_device *mdev,
					struct media_devnode *devnode,
					struct module *owner);

/**
 * media_devnode_unregister_prepare - clear the media device node register bit
 * @devnode: the device node to prepare for unregister
 *
 * This clears the passed device register bit. Future open calls will be met
 * with errors. Should be called before media_devnode_unregister() to avoid
 * races with unregister and device file open calls.
 *
 * This function can safely be called if the device node has never been
 * registered or has already been unregistered.
 */
void media_devnode_unregister_prepare(struct media_devnode *devnode);

/**
 * media_devnode_unregister - unregister a media device node
 * @devnode: the device node to unregister
 *
 * This unregisters the passed device. Future open calls will be met with
 * errors.
 *
 * Should be called after media_devnode_unregister_prepare()
 */
void media_devnode_unregister(struct media_devnode *devnode);

/**
 * media_devnode_data - returns a pointer to the &media_devnode
 *
 * @filp: pointer to struct &file
 */
static inline struct media_devnode *media_devnode_data(struct file *filp)
{
	return filp->private_data;
}

/**
 * media_devnode_is_registered - returns true if &media_devnode is registered;
 *	false otherwise.
 *
 * @devnode: pointer to struct &media_devnode.
 *
 * Note: If mdev is NULL, it also returns false.
 */
static inline int media_devnode_is_registered(struct media_devnode *devnode)
{
	if (!devnode)
		return false;

	return test_bit(MEDIA_FLAG_REGISTERED, &devnode->flags);
}

#endif /* _MEDIA_DEVNODE_H */
