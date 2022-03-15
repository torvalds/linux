/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */

#ifndef _UAPI_LINUX_RK_IOMUX_H
#define _UAPI_LINUX_RK_IOMUX_H

#include <linux/ioctl.h>
#include <linux/types.h>

struct iomux_ioctl_data {
	__u32 bank;
	__u32 pin;
	__u32 mux;
};

#define IOMUX_IOC_MAGIC		'P'

#define IOMUX_IOC_MUX_SET	_IOWR(IOMUX_IOC_MAGIC, 0, struct iomux_ioctl_data)
#define IOMUX_IOC_MUX_GET	_IOWR(IOMUX_IOC_MAGIC, 1, struct iomux_ioctl_data)

#endif
