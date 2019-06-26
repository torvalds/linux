/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Intel Speed Select Interface: OS to hardware Interface
 * Copyright (c) 2019, Intel Corporation.
 * All rights reserved.
 *
 * Author: Srinivas Pandruvada <srinivas.pandruvada@linux.intel.com>
 */

#ifndef __ISST_IF_H
#define __ISST_IF_H

#include <linux/types.h>

/**
 * struct isst_if_platform_info - Define platform information
 * @api_version:	Version of the firmware document, which this driver
 *			can communicate
 * @driver_version:	Driver version, which will help user to send right
 *			commands. Even if the firmware is capable, driver may
 *			not be ready
 * @max_cmds_per_ioctl:	Returns the maximum number of commands driver will
 *			accept in a single ioctl
 * @mbox_supported:	Support of mail box interface
 * @mmio_supported:	Support of mmio interface for core-power feature
 *
 * Used to return output of IOCTL ISST_IF_GET_PLATFORM_INFO. This
 * information can be used by the user space, to get the driver, firmware
 * support and also number of commands to send in a single IOCTL request.
 */
struct isst_if_platform_info {
	__u16 api_version;
	__u16 driver_version;
	__u16 max_cmds_per_ioctl;
	__u8 mbox_supported;
	__u8 mmio_supported;
};

/**
 * struct isst_if_cpu_map - CPU mapping between logical and physical CPU
 * @logical_cpu:	Linux logical CPU number
 * @physical_cpu:	PUNIT CPU number
 *
 * Used to convert from Linux logical CPU to PUNIT CPU numbering scheme.
 * The PUNIT CPU number is different than APIC ID based CPU numbering.
 */
struct isst_if_cpu_map {
	__u32 logical_cpu;
	__u32 physical_cpu;
};

/**
 * struct isst_if_cpu_maps - structure for CPU map IOCTL
 * @cmd_count:	Number of CPU mapping command in cpu_map[]
 * @cpu_map[]:	Holds one or more CPU map data structure
 *
 * This structure used with ioctl ISST_IF_GET_PHY_ID to send
 * one or more CPU mapping commands. Here IOCTL return value indicates
 * number of commands sent or error number if no commands have been sent.
 */
struct isst_if_cpu_maps {
	__u32 cmd_count;
	struct isst_if_cpu_map cpu_map[1];
};

/**
 * struct isst_if_io_reg - Read write PUNIT IO register
 * @read_write:		Value 0: Read, 1: Write
 * @logical_cpu:	Logical CPU number to get target PCI device.
 * @reg:		PUNIT register offset
 * @value:		For write operation value to write and for
 *			for read placeholder read value
 *
 * Structure to specify read/write data to PUNIT registers.
 */
struct isst_if_io_reg {
	__u32 read_write; /* Read:0, Write:1 */
	__u32 logical_cpu;
	__u32 reg;
	__u32 value;
};

/**
 * struct isst_if_io_regs - structure for IO register commands
 * @cmd_count:	Number of io reg commands in io_reg[]
 * @io_reg[]:	Holds one or more io_reg command structure
 *
 * This structure used with ioctl ISST_IF_IO_CMD to send
 * one or more read/write commands to PUNIT. Here IOCTL return value
 * indicates number of requests sent or error number if no requests have
 * been sent.
 */
struct isst_if_io_regs {
	__u32 req_count;
	struct isst_if_io_reg io_reg[1];
};

/**
 * struct isst_if_mbox_cmd - Structure to define mail box command
 * @logical_cpu:	Logical CPU number to get target PCI device
 * @parameter:		Mailbox parameter value
 * @req_data:		Request data for the mailbox
 * @resp_data:		Response data for mailbox command response
 * @command:		Mailbox command value
 * @sub_command:	Mailbox sub command value
 * @reserved:		Unused, set to 0
 *
 * Structure to specify mailbox command to be sent to PUNIT.
 */
struct isst_if_mbox_cmd {
	__u32 logical_cpu;
	__u32 parameter;
	__u32 req_data;
	__u32 resp_data;
	__u16 command;
	__u16 sub_command;
	__u32 reserved;
};

/**
 * struct isst_if_mbox_cmds - structure for mailbox commands
 * @cmd_count:	Number of mailbox commands in mbox_cmd[]
 * @mbox_cmd[]:	Holds one or more mbox commands
 *
 * This structure used with ioctl ISST_IF_MBOX_COMMAND to send
 * one or more mailbox commands to PUNIT. Here IOCTL return value
 * indicates number of commands sent or error number if no commands have
 * been sent.
 */
struct isst_if_mbox_cmds {
	__u32 cmd_count;
	struct isst_if_mbox_cmd mbox_cmd[1];
};

/**
 * struct isst_if_msr_cmd - Structure to define msr command
 * @read_write:		Value 0: Read, 1: Write
 * @logical_cpu:	Logical CPU number
 * @msr:		MSR number
 * @data:		For write operation, data to write, for read
 *			place holder
 *
 * Structure to specify MSR command related to PUNIT.
 */
struct isst_if_msr_cmd {
	__u32 read_write; /* Read:0, Write:1 */
	__u32 logical_cpu;
	__u64 msr;
	__u64 data;
};

/**
 * struct isst_if_msr_cmds - structure for msr commands
 * @cmd_count:	Number of mailbox commands in msr_cmd[]
 * @msr_cmd[]:	Holds one or more msr commands
 *
 * This structure used with ioctl ISST_IF_MSR_COMMAND to send
 * one or more MSR commands. IOCTL return value indicates number of
 * commands sent or error number if no commands have been sent.
 */
struct isst_if_msr_cmds {
	__u32 cmd_count;
	struct isst_if_msr_cmd msr_cmd[1];
};

#define ISST_IF_MAGIC			0xFE
#define ISST_IF_GET_PLATFORM_INFO	_IOR(ISST_IF_MAGIC, 0, struct isst_if_platform_info *)
#define ISST_IF_GET_PHY_ID		_IOWR(ISST_IF_MAGIC, 1, struct isst_if_cpu_map *)
#define ISST_IF_IO_CMD		_IOW(ISST_IF_MAGIC, 2, struct isst_if_io_regs *)
#define ISST_IF_MBOX_COMMAND	_IOWR(ISST_IF_MAGIC, 3, struct isst_if_mbox_cmds *)
#define ISST_IF_MSR_COMMAND	_IOWR(ISST_IF_MAGIC, 4, struct isst_if_msr_cmds *)
#endif
