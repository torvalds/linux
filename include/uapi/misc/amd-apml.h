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

struct apml_cpuid_msg {
	/*
	 * CPUID input
	 * [0]...[3] cpuid func,
	 * [4][5] cpuid: thread
	 * [6] cpuid: ext function & read eax/ebx or ecx/edx
	 *	[7:0] -> bits [7:4] -> ext function &
	 *	bit [0] read eax/ebx or ecx/edx
	 * CPUID output
	 */
	__u64 cpu_in_out;
	/*
	 * Status code for CPUID read
	 */
	__u32 fw_ret_code;
	__u32 pad;
};

struct apml_mcamsr_msg {
	/*
	 * MCAMSR input
	 * [0]...[3] mca msr func,
	 * [4][5] thread
	 * MCAMSR output
	 */
	__u64 mcamsr_in_out;
	/*
	 * Status code for MCA/MSR access
	 */
	__u32 fw_ret_code;
	__u32 pad;
};

struct apml_reg_xfer_msg {
	/*
	 * RMI register address offset
	 */
	__u16 reg_addr;
	/*
	 * Register data for read/write
	 */
	__u8 data_in_out;
	/*
	 * Register read or write
	 */
	__u8 rflag;
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

/**
 * DOC: SBRMI_IOCTL_CPUID_CMD
 *
 * @Parameters
 *
 * @struct apml_cpuid_msg
 *	Pointer to the &struct apml_cpuid_msg that will contain the protocol
 *	information
 *
 * @Description
 * IOCTL command for APML messages using generic _IOWR
 * The IOCTL provides userspace access to AMD sideband cpuid protocol
 * - CPUID protocol to get CPU details for Function/Ext Function
 * at thread level
 * - returning "-EFAULT" if none of the above
 * "-EPROTOTYPE" error is returned to provide additional error details
 */
#define SBRMI_IOCTL_CPUID_CMD		_IOWR(SB_BASE_IOCTL_NR, 1, struct apml_cpuid_msg)

/**
 * DOC: SBRMI_IOCTL_MCAMSR_CMD
 *
 * @Parameters
 *
 * @struct apml_mcamsr_msg
 *	Pointer to the &struct apml_mcamsr_msg that will contain the protocol
 *	information
 *
 * @Description
 * IOCTL command for APML messages using generic _IOWR
 * The IOCTL provides userspace access to AMD sideband MCAMSR protocol
 * - MCAMSR protocol to get MCA bank details for Function at thread level
 * - returning "-EFAULT" if none of the above
 * "-EPROTOTYPE" error is returned to provide additional error details
 */
#define SBRMI_IOCTL_MCAMSR_CMD		_IOWR(SB_BASE_IOCTL_NR, 2, struct apml_mcamsr_msg)

/**
 * DOC: SBRMI_IOCTL_REG_XFER_CMD
 *
 * @Parameters
 *
 * @struct apml_reg_xfer_msg
 *	Pointer to the &struct apml_reg_xfer_msg that will contain the protocol
 *	information
 *
 * @Description
 * IOCTL command for APML messages using generic _IOWR
 * The IOCTL provides userspace access to AMD sideband register xfer protocol
 * - Register xfer protocol to get/set hardware register for given offset
 */
#define SBRMI_IOCTL_REG_XFER_CMD	_IOWR(SB_BASE_IOCTL_NR, 3, struct apml_reg_xfer_msg)

#endif /*_AMD_APML_H_*/
