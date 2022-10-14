/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _UAPI_LINUX_GUNYAH
#define _UAPI_LINUX_GUNYAH

/*
 * Userspace interface for /dev/gunyah - gunyah based virtual machine
 */

#include <linux/types.h>
#include <linux/ioctl.h>

#define GH_IOCTL_TYPE			'G'

/*
 * ioctls for /dev/gunyah fds:
 */
#define GH_CREATE_VM			_IO(GH_IOCTL_TYPE, 0x0) /* Returns a Gunyah VM fd */

#endif
