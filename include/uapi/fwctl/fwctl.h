/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/* Copyright (c) 2024-2025, NVIDIA CORPORATION & AFFILIATES.
 */
#ifndef _UAPI_FWCTL_H
#define _UAPI_FWCTL_H

#include <linux/types.h>
#include <linux/ioctl.h>

#define FWCTL_TYPE 0x9A

/**
 * DOC: General ioctl format
 *
 * The ioctl interface follows a general format to allow for extensibility. Each
 * ioctl is passed a structure pointer as the argument providing the size of
 * the structure in the first u32. The kernel checks that any structure space
 * beyond what it understands is 0. This allows userspace to use the backward
 * compatible portion while consistently using the newer, larger, structures.
 *
 * ioctls use a standard meaning for common errnos:
 *
 *  - ENOTTY: The IOCTL number itself is not supported at all
 *  - E2BIG: The IOCTL number is supported, but the provided structure has
 *    non-zero in a part the kernel does not understand.
 *  - EOPNOTSUPP: The IOCTL number is supported, and the structure is
 *    understood, however a known field has a value the kernel does not
 *    understand or support.
 *  - EINVAL: Everything about the IOCTL was understood, but a field is not
 *    correct.
 *  - ENOMEM: Out of memory.
 *  - ENODEV: The underlying device has been hot-unplugged and the FD is
 *            orphaned.
 *
 * As well as additional errnos, within specific ioctls.
 */
enum {
	FWCTL_CMD_BASE = 0,
	FWCTL_CMD_INFO = 0,
};

enum fwctl_device_type {
	FWCTL_DEVICE_TYPE_ERROR = 0,
};

/**
 * struct fwctl_info - ioctl(FWCTL_INFO)
 * @size: sizeof(struct fwctl_info)
 * @flags: Must be 0
 * @out_device_type: Returns the type of the device from enum fwctl_device_type
 * @device_data_len: On input the length of the out_device_data memory. On
 *	output the size of the kernel's device_data which may be larger or
 *	smaller than the input. Maybe 0 on input.
 * @out_device_data: Pointer to a memory of device_data_len bytes. Kernel will
 *	fill the entire memory, zeroing as required.
 *
 * Returns basic information about this fwctl instance, particularly what driver
 * is being used to define the device_data format.
 */
struct fwctl_info {
	__u32 size;
	__u32 flags;
	__u32 out_device_type;
	__u32 device_data_len;
	__aligned_u64 out_device_data;
};
#define FWCTL_INFO _IO(FWCTL_TYPE, FWCTL_CMD_INFO)

#endif
