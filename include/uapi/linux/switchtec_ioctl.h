/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Microsemi Switchtec PCIe Driver
 * Copyright (c) 2017, Microsemi Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#ifndef _UAPI_LINUX_SWITCHTEC_IOCTL_H
#define _UAPI_LINUX_SWITCHTEC_IOCTL_H

#include <linux/types.h>

#define SWITCHTEC_IOCTL_PART_CFG0	0
#define SWITCHTEC_IOCTL_PART_CFG1	1
#define SWITCHTEC_IOCTL_PART_IMG0	2
#define SWITCHTEC_IOCTL_PART_IMG1	3
#define SWITCHTEC_IOCTL_PART_NVLOG	4
#define SWITCHTEC_IOCTL_PART_VENDOR0	5
#define SWITCHTEC_IOCTL_PART_VENDOR1	6
#define SWITCHTEC_IOCTL_PART_VENDOR2	7
#define SWITCHTEC_IOCTL_PART_VENDOR3	8
#define SWITCHTEC_IOCTL_PART_VENDOR4	9
#define SWITCHTEC_IOCTL_PART_VENDOR5	10
#define SWITCHTEC_IOCTL_PART_VENDOR6	11
#define SWITCHTEC_IOCTL_PART_VENDOR7	12
#define SWITCHTEC_IOCTL_PART_BL2_0	13
#define SWITCHTEC_IOCTL_PART_BL2_1	14
#define SWITCHTEC_IOCTL_PART_MAP_0	15
#define SWITCHTEC_IOCTL_PART_MAP_1	16
#define SWITCHTEC_IOCTL_PART_KEY_0	17
#define SWITCHTEC_IOCTL_PART_KEY_1	18

#define SWITCHTEC_NUM_PARTITIONS_GEN3	13
#define SWITCHTEC_NUM_PARTITIONS_GEN4	19

/* obsolete: for compatibility with old userspace software */
#define SWITCHTEC_IOCTL_NUM_PARTITIONS	SWITCHTEC_NUM_PARTITIONS_GEN3

struct switchtec_ioctl_flash_info {
	__u64 flash_length;
	__u32 num_partitions;
	__u32 padding;
};

#define SWITCHTEC_IOCTL_PART_ACTIVE  1
#define SWITCHTEC_IOCTL_PART_RUNNING 2

struct switchtec_ioctl_flash_part_info {
	__u32 flash_partition;
	__u32 address;
	__u32 length;
	__u32 active;
};

struct switchtec_ioctl_event_summary_legacy {
	__u64 global;
	__u64 part_bitmap;
	__u32 local_part;
	__u32 padding;
	__u32 part[48];
	__u32 pff[48];
};

struct switchtec_ioctl_event_summary {
	__u64 global;
	__u64 part_bitmap;
	__u32 local_part;
	__u32 padding;
	__u32 part[48];
	__u32 pff[255];
};

#define SWITCHTEC_IOCTL_EVENT_STACK_ERROR		0
#define SWITCHTEC_IOCTL_EVENT_PPU_ERROR			1
#define SWITCHTEC_IOCTL_EVENT_ISP_ERROR			2
#define SWITCHTEC_IOCTL_EVENT_SYS_RESET			3
#define SWITCHTEC_IOCTL_EVENT_FW_EXC			4
#define SWITCHTEC_IOCTL_EVENT_FW_NMI			5
#define SWITCHTEC_IOCTL_EVENT_FW_NON_FATAL		6
#define SWITCHTEC_IOCTL_EVENT_FW_FATAL			7
#define SWITCHTEC_IOCTL_EVENT_TWI_MRPC_COMP		8
#define SWITCHTEC_IOCTL_EVENT_TWI_MRPC_COMP_ASYNC	9
#define SWITCHTEC_IOCTL_EVENT_CLI_MRPC_COMP		10
#define SWITCHTEC_IOCTL_EVENT_CLI_MRPC_COMP_ASYNC	11
#define SWITCHTEC_IOCTL_EVENT_GPIO_INT			12
#define SWITCHTEC_IOCTL_EVENT_PART_RESET		13
#define SWITCHTEC_IOCTL_EVENT_MRPC_COMP			14
#define SWITCHTEC_IOCTL_EVENT_MRPC_COMP_ASYNC		15
#define SWITCHTEC_IOCTL_EVENT_DYN_PART_BIND_COMP	16
#define SWITCHTEC_IOCTL_EVENT_AER_IN_P2P		17
#define SWITCHTEC_IOCTL_EVENT_AER_IN_VEP		18
#define SWITCHTEC_IOCTL_EVENT_DPC			19
#define SWITCHTEC_IOCTL_EVENT_CTS			20
#define SWITCHTEC_IOCTL_EVENT_HOTPLUG			21
#define SWITCHTEC_IOCTL_EVENT_IER			22
#define SWITCHTEC_IOCTL_EVENT_THRESH			23
#define SWITCHTEC_IOCTL_EVENT_POWER_MGMT		24
#define SWITCHTEC_IOCTL_EVENT_TLP_THROTTLING		25
#define SWITCHTEC_IOCTL_EVENT_FORCE_SPEED		26
#define SWITCHTEC_IOCTL_EVENT_CREDIT_TIMEOUT		27
#define SWITCHTEC_IOCTL_EVENT_LINK_STATE		28
#define SWITCHTEC_IOCTL_EVENT_GFMS			29
#define SWITCHTEC_IOCTL_EVENT_INTERCOMM_REQ_NOTIFY	30
#define SWITCHTEC_IOCTL_EVENT_UEC			31
#define SWITCHTEC_IOCTL_MAX_EVENTS			32

#define SWITCHTEC_IOCTL_EVENT_LOCAL_PART_IDX -1
#define SWITCHTEC_IOCTL_EVENT_IDX_ALL -2

#define SWITCHTEC_IOCTL_EVENT_FLAG_CLEAR     (1 << 0)
#define SWITCHTEC_IOCTL_EVENT_FLAG_EN_POLL   (1 << 1)
#define SWITCHTEC_IOCTL_EVENT_FLAG_EN_LOG    (1 << 2)
#define SWITCHTEC_IOCTL_EVENT_FLAG_EN_CLI    (1 << 3)
#define SWITCHTEC_IOCTL_EVENT_FLAG_EN_FATAL  (1 << 4)
#define SWITCHTEC_IOCTL_EVENT_FLAG_DIS_POLL  (1 << 5)
#define SWITCHTEC_IOCTL_EVENT_FLAG_DIS_LOG   (1 << 6)
#define SWITCHTEC_IOCTL_EVENT_FLAG_DIS_CLI   (1 << 7)
#define SWITCHTEC_IOCTL_EVENT_FLAG_DIS_FATAL (1 << 8)
#define SWITCHTEC_IOCTL_EVENT_FLAG_UNUSED    (~0x1ff)

struct switchtec_ioctl_event_ctl {
	__u32 event_id;
	__s32 index;
	__u32 flags;
	__u32 occurred;
	__u32 count;
	__u32 data[5];
};

#define SWITCHTEC_IOCTL_PFF_VEP 100
struct switchtec_ioctl_pff_port {
	__u32 pff;
	__u32 partition;
	__u32 port;
};

#define SWITCHTEC_IOCTL_FLASH_INFO \
	_IOR('W', 0x40, struct switchtec_ioctl_flash_info)
#define SWITCHTEC_IOCTL_FLASH_PART_INFO \
	_IOWR('W', 0x41, struct switchtec_ioctl_flash_part_info)
#define SWITCHTEC_IOCTL_EVENT_SUMMARY \
	_IOR('W', 0x42, struct switchtec_ioctl_event_summary)
#define SWITCHTEC_IOCTL_EVENT_SUMMARY_LEGACY \
	_IOR('W', 0x42, struct switchtec_ioctl_event_summary_legacy)
#define SWITCHTEC_IOCTL_EVENT_CTL \
	_IOWR('W', 0x43, struct switchtec_ioctl_event_ctl)
#define SWITCHTEC_IOCTL_PFF_TO_PORT \
	_IOWR('W', 0x44, struct switchtec_ioctl_pff_port)
#define SWITCHTEC_IOCTL_PORT_TO_PFF \
	_IOWR('W', 0x45, struct switchtec_ioctl_pff_port)

#endif
