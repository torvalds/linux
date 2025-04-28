/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (C) 2021-2024 Advanced Micro Devices, Inc.
 */
#ifndef _AMD_APML_H_
#define _AMD_APML_H_

#include <linux/types.h>

/* Mailbox data size for data_in and data_out */
#define AMD_SBI_MB_DATA_SIZE		4

struct apml_mbox_msg {
	/*
	 * Mailbox Message ID
	 */
	__u32 cmd;
	/*
	 * [0]...[3] mailbox 32bit input/output data
	 */
	__u32 mb_in_out;
	/*
	 * Error code is returned in case of soft mailbox error
	 */
	__u32 fw_ret_code;
};

/*
 * AMD sideband interface base IOCTL
 */
#define SB_BASE_IOCTL_NR	0xF9

/**
 * DOC: SBRMI_IOCTL_MBOX_CMD
 *
 * @Parameters
 *
 * @struct apml_mbox_msg
 *	Pointer to the &struct apml_mbox_msg that will contain the protocol
 *	information
 *
 * @Description
 * IOCTL command for APML messages using generic _IOWR
 * The IOCTL provides userspace access to AMD sideband mailbox protocol
 * - Mailbox message read/write(0x0~0xFF)
 * - returning "-EFAULT" if none of the above
 * "-EPROTOTYPE" error is returned to provide additional error details
 */
#define SBRMI_IOCTL_MBOX_CMD		_IOWR(SB_BASE_IOCTL_NR, 0, struct apml_mbox_msg)

#endif /*_AMD_APML_H_*/
