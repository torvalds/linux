/*
 * Copyright 2008 Red Hat, Inc. All rights reserved.
 * Copyright 2008 Ian Kent <raven@themaw.net>
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 */

#ifndef _LINUX_AUTO_DEV_IOCTL_H
#define _LINUX_AUTO_DEV_IOCTL_H

#include <linux/types.h>

#define AUTOFS_DEVICE_NAME		"autofs"

#define AUTOFS_DEV_IOCTL_VERSION_MAJOR	1
#define AUTOFS_DEV_IOCTL_VERSION_MINOR	0

#define AUTOFS_DEVID_LEN		16

#define AUTOFS_DEV_IOCTL_SIZE		sizeof(struct autofs_dev_ioctl)

/*
 * An ioctl interface for autofs mount point control.
 */

/*
 * All the ioctls use this structure.
 * When sending a path size must account for the total length
 * of the chunk of memory otherwise is is the size of the
 * structure.
 */

struct autofs_dev_ioctl {
	__u32 ver_major;
	__u32 ver_minor;
	__u32 size;		/* total size of data passed in
				 * including this struct */
	__s32 ioctlfd;		/* automount command fd */

	__u32 arg1;		/* Command parameters */
	__u32 arg2;

	char path[0];
};

static inline void init_autofs_dev_ioctl(struct autofs_dev_ioctl *in)
{
	in->ver_major = AUTOFS_DEV_IOCTL_VERSION_MAJOR;
	in->ver_minor = AUTOFS_DEV_IOCTL_VERSION_MINOR;
	in->size = sizeof(struct autofs_dev_ioctl);
	in->ioctlfd = -1;
	in->arg1 = 0;
	in->arg2 = 0;
	return;
}

/*
 * If you change this make sure you make the corresponding change
 * to autofs-dev-ioctl.c:lookup_ioctl()
 */
enum {
	/* Get various version info */
	AUTOFS_DEV_IOCTL_VERSION_CMD = 0x71,
	AUTOFS_DEV_IOCTL_PROTOVER_CMD,
	AUTOFS_DEV_IOCTL_PROTOSUBVER_CMD,

	/* Open mount ioctl fd */
	AUTOFS_DEV_IOCTL_OPENMOUNT_CMD,

	/* Close mount ioctl fd */
	AUTOFS_DEV_IOCTL_CLOSEMOUNT_CMD,

	/* Mount/expire status returns */
	AUTOFS_DEV_IOCTL_READY_CMD,
	AUTOFS_DEV_IOCTL_FAIL_CMD,

	/* Activate/deactivate autofs mount */
	AUTOFS_DEV_IOCTL_SETPIPEFD_CMD,
	AUTOFS_DEV_IOCTL_CATATONIC_CMD,

	/* Expiry timeout */
	AUTOFS_DEV_IOCTL_TIMEOUT_CMD,

	/* Get mount last requesting uid and gid */
	AUTOFS_DEV_IOCTL_REQUESTER_CMD,

	/* Check for eligible expire candidates */
	AUTOFS_DEV_IOCTL_EXPIRE_CMD,

	/* Request busy status */
	AUTOFS_DEV_IOCTL_ASKUMOUNT_CMD,

	/* Check if path is a mountpoint */
	AUTOFS_DEV_IOCTL_ISMOUNTPOINT_CMD,
};

#define AUTOFS_IOCTL 0x93

#define AUTOFS_DEV_IOCTL_VERSION \
	_IOWR(AUTOFS_IOCTL, \
	      AUTOFS_DEV_IOCTL_VERSION_CMD, struct autofs_dev_ioctl)

#define AUTOFS_DEV_IOCTL_PROTOVER \
	_IOWR(AUTOFS_IOCTL, \
	      AUTOFS_DEV_IOCTL_PROTOVER_CMD, struct autofs_dev_ioctl)

#define AUTOFS_DEV_IOCTL_PROTOSUBVER \
	_IOWR(AUTOFS_IOCTL, \
	      AUTOFS_DEV_IOCTL_PROTOSUBVER_CMD, struct autofs_dev_ioctl)

#define AUTOFS_DEV_IOCTL_OPENMOUNT \
	_IOWR(AUTOFS_IOCTL, \
	      AUTOFS_DEV_IOCTL_OPENMOUNT_CMD, struct autofs_dev_ioctl)

#define AUTOFS_DEV_IOCTL_CLOSEMOUNT \
	_IOWR(AUTOFS_IOCTL, \
	      AUTOFS_DEV_IOCTL_CLOSEMOUNT_CMD, struct autofs_dev_ioctl)

#define AUTOFS_DEV_IOCTL_READY \
	_IOWR(AUTOFS_IOCTL, \
	      AUTOFS_DEV_IOCTL_READY_CMD, struct autofs_dev_ioctl)

#define AUTOFS_DEV_IOCTL_FAIL \
	_IOWR(AUTOFS_IOCTL, \
	      AUTOFS_DEV_IOCTL_FAIL_CMD, struct autofs_dev_ioctl)

#define AUTOFS_DEV_IOCTL_SETPIPEFD \
	_IOWR(AUTOFS_IOCTL, \
	      AUTOFS_DEV_IOCTL_SETPIPEFD_CMD, struct autofs_dev_ioctl)

#define AUTOFS_DEV_IOCTL_CATATONIC \
	_IOWR(AUTOFS_IOCTL, \
	      AUTOFS_DEV_IOCTL_CATATONIC_CMD, struct autofs_dev_ioctl)

#define AUTOFS_DEV_IOCTL_TIMEOUT \
	_IOWR(AUTOFS_IOCTL, \
	      AUTOFS_DEV_IOCTL_TIMEOUT_CMD, struct autofs_dev_ioctl)

#define AUTOFS_DEV_IOCTL_REQUESTER \
	_IOWR(AUTOFS_IOCTL, \
	      AUTOFS_DEV_IOCTL_REQUESTER_CMD, struct autofs_dev_ioctl)

#define AUTOFS_DEV_IOCTL_EXPIRE \
	_IOWR(AUTOFS_IOCTL, \
	      AUTOFS_DEV_IOCTL_EXPIRE_CMD, struct autofs_dev_ioctl)

#define AUTOFS_DEV_IOCTL_ASKUMOUNT \
	_IOWR(AUTOFS_IOCTL, \
	      AUTOFS_DEV_IOCTL_ASKUMOUNT_CMD, struct autofs_dev_ioctl)

#define AUTOFS_DEV_IOCTL_ISMOUNTPOINT \
	_IOWR(AUTOFS_IOCTL, \
	      AUTOFS_DEV_IOCTL_ISMOUNTPOINT_CMD, struct autofs_dev_ioctl)

#endif	/* _LINUX_AUTO_DEV_IOCTL_H */
