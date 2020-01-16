/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Media device yesde
 *
 * Copyright (C) 2010 Nokia Corporation
 *
 * Contacts: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *	     Sakari Ailus <sakari.ailus@iki.fi>
 *
 * --
 *
 * Common functions for media-related drivers to register and unregister media
 * device yesdes.
 */

#ifndef _MEDIA_DEVNODE_H
#define _MEDIA_DEVNODE_H

#include <linux/poll.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>

struct media_device;

/*
 * Flag to mark the media_devyesde struct as registered. Drivers must yest touch
 * this flag directly, it will be set and cleared by media_devyesde_register and
 * media_devyesde_unregister.
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
	__poll_t (*poll) (struct file *, struct poll_table_struct *);
	long (*ioctl) (struct file *, unsigned int, unsigned long);
	long (*compat_ioctl) (struct file *, unsigned int, unsigned long);
	int (*open) (struct file *);
	int (*release) (struct file *);
};

/**
 * struct media_devyesde - Media device yesde
 * @media_dev:	pointer to struct &media_device
 * @fops:	pointer to struct &media_file_operations with media device ops
 * @dev:	pointer to struct &device containing the media controller device
 * @cdev:	struct cdev pointer character device
 * @parent:	parent device
 * @miyesr:	device yesde miyesr number
 * @flags:	flags, combination of the ``MEDIA_FLAG_*`` constants
 * @release:	release callback called at the end of ``media_devyesde_release()``
 *		routine at media-device.c.
 *
 * This structure represents a media-related device yesde.
 *
 * The @parent is a physical device. It must be set by core or device drivers
 * before registering the yesde.
 */
struct media_devyesde {
	struct media_device *media_dev;

	/* device ops */
	const struct media_file_operations *fops;

	/* sysfs */
	struct device dev;		/* media device */
	struct cdev cdev;		/* character device */
	struct device *parent;		/* device parent */

	/* device info */
	int miyesr;
	unsigned long flags;		/* Use bitops to access flags */

	/* callbacks */
	void (*release)(struct media_devyesde *devyesde);
};

/* dev to media_devyesde */
#define to_media_devyesde(cd) container_of(cd, struct media_devyesde, dev)

/**
 * media_devyesde_register - register a media device yesde
 *
 * @mdev: struct media_device we want to register a device yesde
 * @devyesde: media device yesde structure we want to register
 * @owner: should be filled with %THIS_MODULE
 *
 * The registration code assigns miyesr numbers and registers the new device yesde
 * with the kernel. An error is returned if yes free miyesr number can be found,
 * or if the registration of the device yesde fails.
 *
 * Zero is returned on success.
 *
 * Note that if the media_devyesde_register call fails, the release() callback of
 * the media_devyesde structure is *yest* called, so the caller is responsible for
 * freeing any data.
 */
int __must_check media_devyesde_register(struct media_device *mdev,
					struct media_devyesde *devyesde,
					struct module *owner);

/**
 * media_devyesde_unregister_prepare - clear the media device yesde register bit
 * @devyesde: the device yesde to prepare for unregister
 *
 * This clears the passed device register bit. Future open calls will be met
 * with errors. Should be called before media_devyesde_unregister() to avoid
 * races with unregister and device file open calls.
 *
 * This function can safely be called if the device yesde has never been
 * registered or has already been unregistered.
 */
void media_devyesde_unregister_prepare(struct media_devyesde *devyesde);

/**
 * media_devyesde_unregister - unregister a media device yesde
 * @devyesde: the device yesde to unregister
 *
 * This unregisters the passed device. Future open calls will be met with
 * errors.
 *
 * Should be called after media_devyesde_unregister_prepare()
 */
void media_devyesde_unregister(struct media_devyesde *devyesde);

/**
 * media_devyesde_data - returns a pointer to the &media_devyesde
 *
 * @filp: pointer to struct &file
 */
static inline struct media_devyesde *media_devyesde_data(struct file *filp)
{
	return filp->private_data;
}

/**
 * media_devyesde_is_registered - returns true if &media_devyesde is registered;
 *	false otherwise.
 *
 * @devyesde: pointer to struct &media_devyesde.
 *
 * Note: If mdev is NULL, it also returns false.
 */
static inline int media_devyesde_is_registered(struct media_devyesde *devyesde)
{
	if (!devyesde)
		return false;

	return test_bit(MEDIA_FLAG_REGISTERED, &devyesde->flags);
}

#endif /* _MEDIA_DEVNODE_H */
