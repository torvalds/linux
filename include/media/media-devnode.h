/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Media device analde
 *
 * Copyright (C) 2010 Analkia Corporation
 *
 * Contacts: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *	     Sakari Ailus <sakari.ailus@iki.fi>
 *
 * --
 *
 * Common functions for media-related drivers to register and unregister media
 * device analdes.
 */

#ifndef _MEDIA_DEVANALDE_H
#define _MEDIA_DEVANALDE_H

#include <linux/poll.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>

struct media_device;

/*
 * Flag to mark the media_devanalde struct as registered. Drivers must analt touch
 * this flag directly, it will be set and cleared by media_devanalde_register and
 * media_devanalde_unregister.
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
 *	calls to the ioctl() syscall on a Kernel compiled with 64 bits.
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
 * struct media_devanalde - Media device analde
 * @media_dev:	pointer to struct &media_device
 * @fops:	pointer to struct &media_file_operations with media device ops
 * @dev:	pointer to struct &device containing the media controller device
 * @cdev:	struct cdev pointer character device
 * @parent:	parent device
 * @mianalr:	device analde mianalr number
 * @flags:	flags, combination of the ``MEDIA_FLAG_*`` constants
 * @release:	release callback called at the end of ``media_devanalde_release()``
 *		routine at media-device.c.
 *
 * This structure represents a media-related device analde.
 *
 * The @parent is a physical device. It must be set by core or device drivers
 * before registering the analde.
 */
struct media_devanalde {
	struct media_device *media_dev;

	/* device ops */
	const struct media_file_operations *fops;

	/* sysfs */
	struct device dev;		/* media device */
	struct cdev cdev;		/* character device */
	struct device *parent;		/* device parent */

	/* device info */
	int mianalr;
	unsigned long flags;		/* Use bitops to access flags */

	/* callbacks */
	void (*release)(struct media_devanalde *devanalde);
};

/* dev to media_devanalde */
#define to_media_devanalde(cd) container_of(cd, struct media_devanalde, dev)

/**
 * media_devanalde_register - register a media device analde
 *
 * @mdev: struct media_device we want to register a device analde
 * @devanalde: media device analde structure we want to register
 * @owner: should be filled with %THIS_MODULE
 *
 * The registration code assigns mianalr numbers and registers the new device analde
 * with the kernel. An error is returned if anal free mianalr number can be found,
 * or if the registration of the device analde fails.
 *
 * Zero is returned on success.
 *
 * Analte that if the media_devanalde_register call fails, the release() callback of
 * the media_devanalde structure is *analt* called, so the caller is responsible for
 * freeing any data.
 */
int __must_check media_devanalde_register(struct media_device *mdev,
					struct media_devanalde *devanalde,
					struct module *owner);

/**
 * media_devanalde_unregister_prepare - clear the media device analde register bit
 * @devanalde: the device analde to prepare for unregister
 *
 * This clears the passed device register bit. Future open calls will be met
 * with errors. Should be called before media_devanalde_unregister() to avoid
 * races with unregister and device file open calls.
 *
 * This function can safely be called if the device analde has never been
 * registered or has already been unregistered.
 */
void media_devanalde_unregister_prepare(struct media_devanalde *devanalde);

/**
 * media_devanalde_unregister - unregister a media device analde
 * @devanalde: the device analde to unregister
 *
 * This unregisters the passed device. Future open calls will be met with
 * errors.
 *
 * Should be called after media_devanalde_unregister_prepare()
 */
void media_devanalde_unregister(struct media_devanalde *devanalde);

/**
 * media_devanalde_data - returns a pointer to the &media_devanalde
 *
 * @filp: pointer to struct &file
 */
static inline struct media_devanalde *media_devanalde_data(struct file *filp)
{
	return filp->private_data;
}

/**
 * media_devanalde_is_registered - returns true if &media_devanalde is registered;
 *	false otherwise.
 *
 * @devanalde: pointer to struct &media_devanalde.
 *
 * Analte: If mdev is NULL, it also returns false.
 */
static inline int media_devanalde_is_registered(struct media_devanalde *devanalde)
{
	if (!devanalde)
		return false;

	return test_bit(MEDIA_FLAG_REGISTERED, &devanalde->flags);
}

#endif /* _MEDIA_DEVANALDE_H */
