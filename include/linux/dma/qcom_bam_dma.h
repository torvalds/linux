/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016-2017, The Linux Foundation. All rights reserved.
 */

#ifndef _QCOM_BAM_DMA_H
#define _QCOM_BAM_DMA_H

#include <asm/byteorder.h>

/*
 * This data type corresponds to the native Command Element
 * supported by BAM DMA Engine.
 *
 * @cmd_and_addr - upper 8 bits command and lower 24 bits register address.
 * @data - For write command: content to be written into peripheral register.
 *	   For read command: lower 32 bits of destination address.
 * @mask - For write command: register write mask.
 *	   For read command on BAM v1.6.0+: upper 4 bits of destination address.
 *	   For read command on BAM < v1.6.0: ignored by hardware.
 *	   Setting to 0 ensures 32-bit addressing compatibility.
 * @reserved - for future usage.
 *
 */
struct bam_cmd_element {
	__le32 cmd_and_addr;
	__le32 data;
	__le32 mask;
	__le32 reserved;
};

/*
 * This enum indicates the command type in a command element
 */
enum bam_command_type {
	BAM_WRITE_COMMAND = 0,
	BAM_READ_COMMAND,
};

/*
 * prep_bam_ce_le32 - Wrapper function to prepare a single BAM command
 * element with the data already in le32 format.
 *
 * @bam_ce: bam command element
 * @addr: target address
 * @cmd: BAM command
 * @data: actual data for write and dest addr for read in le32
 *
 * For BAM v1.6.0+, the mask field behavior depends on command type:
 * - Write commands: mask = write mask (typically 0xffffffff)
 * - Read commands: mask = upper 4 bits of destination address (0 for 32-bit)
 */
static inline void
bam_prep_ce_le32(struct bam_cmd_element *bam_ce, u32 addr,
		 enum bam_command_type cmd, __le32 data)
{
	bam_ce->cmd_and_addr =
		cpu_to_le32((addr & 0xffffff) | ((cmd & 0xff) << 24));
	bam_ce->data = data;
	if (cmd == BAM_READ_COMMAND)
		bam_ce->mask = cpu_to_le32(0x0); /* 32-bit addressing */
	else
		bam_ce->mask = cpu_to_le32(0xffffffff); /* Write mask */
	bam_ce->reserved = 0;
}

/*
 * bam_prep_ce - Wrapper function to prepare a single BAM command element
 * with the data.
 *
 * @bam_ce: BAM command element
 * @addr: target address
 * @cmd: BAM command
 * @data: actual data for write and destination address for read
 */
static inline void
bam_prep_ce(struct bam_cmd_element *bam_ce, u32 addr,
	    enum bam_command_type cmd, u32 data)
{
	bam_prep_ce_le32(bam_ce, addr, cmd, cpu_to_le32(data));
}
#endif
