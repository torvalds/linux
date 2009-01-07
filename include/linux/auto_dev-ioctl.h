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

#include <linux/string.h>
#include <linux/types.h>

#define AUTOFS_DEVICE_NAME		"autofs"

#define AUTOFS_DEV_IOCTL_VERSION_MAJOR	1
#define AUTOFS_DEV_IOCTL_VERSION_MINOR	0

#define AUTOFS_DEVID_LEN		16

#define AUTOFS_DEV_IOCTL_SIZE		sizeof(struct autofs_dev_ioctl)

/*
 * An ioctl interface for autofs mount point control.
 */

struct args_protover {
	__u32	version;
};

struct args_protosubver {
	__u32	sub_version;
};

struct args_openmount {
	__u32	devid;
};

struct args_ready {
	__u32	token;
};

struct args_fail {
	__u32	token;
	__s32	status;
};

struct args_setpipefd {
	__s32	pipefd;
};

struct args_timeout {
	__u64	timeout;
};

struct args_requester {
	__u32	uid;
	__u32	gid;
};

struct args_expire {
	__u32	how;
};

struct args_askumount {
	__u32	may_umount;
};

struct args_ismountpoint {
	union {
		struct args_in {
			__u32	type;
		} in;
		struct args_out {
			__u32	devid;
			__u32	magic;
		} out;
	};
};

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

	/* Command parameters */

	union {
		struct args_protover		protover;
		struct args_protosubver		protosubver;
		struct args_openmount		openmount;
		struct args_ready		ready;
		struct args_fail		fail;
		struct args_setpipefd		setpipefd;
		struct args_timeout		timeout;
		struct args_requester		requester;
		struct args_expire		expire;
		struct args_askumount		askumount;
		struct args_ismountpoint	ismountpoint;
	};

	char path[0];
};

static inline void init_autofs_dev_ioctl(struct autofs_dev_ioctl *in)
{
	memset(in, 0, sizeof(struct autofs_dev_ioctl));
	in->ver_major = AUTOFS_DEV_IOCTL_VERSION_MAJOR;
	in->ver_minor = AUTOFS_DEV_IOCTL_VERSION_MINOR;
	in->size = sizeof(struct autofs_dev_ioctl);
	in->ioctlfd = -1;
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
