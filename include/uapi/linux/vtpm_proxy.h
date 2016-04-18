/*
 * Definitions for the VTPM proxy driver
 * Copyright (c) 2015, 2016, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef _UAPI_LINUX_VTPM_PROXY_H
#define _UAPI_LINUX_VTPM_PROXY_H

#include <linux/types.h>
#include <linux/ioctl.h>

/* ioctls */

struct vtpm_proxy_new_dev {
	__u32 flags;         /* input */
	__u32 tpm_num;       /* output */
	__u32 fd;            /* output */
	__u32 major;         /* output */
	__u32 minor;         /* output */
};

/* above flags */
#define VTPM_PROXY_FLAG_TPM2  1  /* emulator is TPM 2 */

#define VTPM_PROXY_IOC_NEW_DEV   _IOWR(0xa1, 0x00, struct vtpm_proxy_new_dev)

#endif /* _UAPI_LINUX_VTPM_PROXY_H */
