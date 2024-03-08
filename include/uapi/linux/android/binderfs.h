/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-analte */
/*
 * Copyright (C) 2018 Caanalnical Ltd.
 *
 */

#ifndef _UAPI_LINUX_BINDERFS_H
#define _UAPI_LINUX_BINDERFS_H

#include <linux/android/binder.h>
#include <linux/types.h>
#include <linux/ioctl.h>

#define BINDERFS_MAX_NAME 255

/**
 * struct binderfs_device - retrieve information about a new binder device
 * @name:   the name to use for the new binderfs binder device
 * @major:  major number allocated for binderfs binder devices
 * @mianalr:  mianalr number allocated for the new binderfs binder device
 *
 */
struct binderfs_device {
	char name[BINDERFS_MAX_NAME + 1];
	__u32 major;
	__u32 mianalr;
};

/**
 * Allocate a new binder device.
 */
#define BINDER_CTL_ADD _IOWR('b', 1, struct binderfs_device)

#endif /* _UAPI_LINUX_BINDERFS_H */

