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
	FWCTL_CMD_RPC = 1,
};

enum fwctl_device_type {
	FWCTL_DEVICE_TYPE_ERROR = 0,
	FWCTL_DEVICE_TYPE_MLX5 = 1,
	FWCTL_DEVICE_TYPE_CXL = 2,
	FWCTL_DEVICE_TYPE_PDS = 4,
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

/**
 * enum fwctl_rpc_scope - Scope of access for the RPC
 *
 * Refer to fwctl.rst for a more detailed discussion of these scopes.
 */
enum fwctl_rpc_scope {
	/**
	 * @FWCTL_RPC_CONFIGURATION: Device configuration access scope
	 *
	 * Read/write access to device configuration. When configuration
	 * is written to the device it remains in a fully supported state.
	 */
	FWCTL_RPC_CONFIGURATION = 0,
	/**
	 * @FWCTL_RPC_DEBUG_READ_ONLY: Read only access to debug information
	 *
	 * Readable debug information. Debug information is compatible with
	 * kernel lockdown, and does not disclose any sensitive information. For
	 * instance exposing any encryption secrets from this information is
	 * forbidden.
	 */
	FWCTL_RPC_DEBUG_READ_ONLY = 1,
	/**
	 * @FWCTL_RPC_DEBUG_WRITE: Writable access to lockdown compatible debug information
	 *
	 * Allows write access to data in the device which may leave a fully
	 * supported state. This is intended to permit intensive and possibly
	 * invasive debugging. This scope will taint the kernel.
	 */
	FWCTL_RPC_DEBUG_WRITE = 2,
	/**
	 * @FWCTL_RPC_DEBUG_WRITE_FULL: Write access to all debug information
	 *
	 * Allows read/write access to everything. Requires CAP_SYS_RAW_IO, so
	 * it is not required to follow lockdown principals. If in doubt
	 * debugging should be placed in this scope. This scope will taint the
	 * kernel.
	 */
	FWCTL_RPC_DEBUG_WRITE_FULL = 3,
};

/**
 * struct fwctl_rpc - ioctl(FWCTL_RPC)
 * @size: sizeof(struct fwctl_rpc)
 * @scope: One of enum fwctl_rpc_scope, required scope for the RPC
 * @in_len: Length of the in memory
 * @out_len: Length of the out memory
 * @in: Request message in device specific format
 * @out: Response message in device specific format
 *
 * Deliver a Remote Procedure Call to the device FW and return the response. The
 * call's parameters and return are marshaled into linear buffers of memory. Any
 * errno indicates that delivery of the RPC to the device failed. Return status
 * originating in the device during a successful delivery must be encoded into
 * out.
 *
 * The format of the buffers matches the out_device_type from FWCTL_INFO.
 */
struct fwctl_rpc {
	__u32 size;
	__u32 scope;
	__u32 in_len;
	__u32 out_len;
	__aligned_u64 in;
	__aligned_u64 out;
};
#define FWCTL_RPC _IO(FWCTL_TYPE, FWCTL_CMD_RPC)

#endif
