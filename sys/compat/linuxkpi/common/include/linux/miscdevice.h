/*-
 * Copyright (c) 2010 Isilon Systems, Inc.
 * Copyright (c) 2010 iX Systems, Inc.
 * Copyright (c) 2010 Panasas, Inc.
 * Copyright (c) 2013, 2014 Mellanox Technologies, Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef	_LINUX_MISCDEVICE_H_
#define	_LINUX_MISCDEVICE_H_

#define	MISC_DYNAMIC_MINOR	-1

#include <linux/device.h>
#include <linux/cdev.h>

struct miscdevice  {
	const char	*name;
	struct device	*this_device;
	const struct file_operations *fops;
	struct cdev	*cdev;
	int		minor;
	const char *nodename;
	umode_t mode;
};

extern struct class linux_class_misc;

static inline int
misc_register(struct miscdevice *misc)
{
	misc->this_device = device_create(&linux_class_misc,
	    &linux_root_device, 0, misc, misc->name);
	misc->cdev = cdev_alloc();
	if (misc->cdev == NULL)
		return -ENOMEM;
	misc->cdev->owner = THIS_MODULE;
	misc->cdev->ops = misc->fops;
	kobject_set_name(&misc->cdev->kobj, misc->name);
	if (cdev_add(misc->cdev, misc->this_device->devt, 1))
		return -EINVAL;
	return (0);
}

static inline int
misc_deregister(struct miscdevice *misc)
{
	device_destroy(&linux_class_misc, misc->this_device->devt);
	cdev_del(misc->cdev);

	return (0);
}

#endif	/* _LINUX_MISCDEVICE_H_ */
