/*
 * Copyright (c) 2016-2017, The Linux Foundation. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _QCOM_BAM_DMA_H
#define _QCOM_BAM_DMA_H

#include <asm/byteorder.h>

/*
 * This data type corresponds to the native Command Element
 * supported by BAM DMA Engine.
 *
 * @cmd_and_addr - upper 8 bits command and lower 24 bits register address.
 * @data - for write command: content to be written into peripheral register.
 *	   for read command: dest addr to write peripheral register value.
 * @mask - register mask.
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
 */
static inline void
bam_prep_ce_le32(struct bam_cmd_element *bam_ce, u32 addr,
		 enum bam_command_type cmd, __le32 data)
{
	bam_ce->cmd_and_addr =
		cpu_to_le32((addr & 0xffffff) | ((cmd & 0xff) << 24));
	bam_ce->data = data;
	bam_ce->mask = cpu_to_le32(0xffffffff);
}

/*
 * bam_prep_ce - Wrapper function to prepare a single BAM command element
 * with the data.
 *
 * @bam_ce: BAM command element
 * @addr: target address
 * @cmd: BAM command
 * @data: actual data for write and dest addr for read
 */
static inline void
bam_prep_ce(struct bam_cmd_element *bam_ce, u32 addr,
	    enum bam_command_type cmd, u32 data)
{
	bam_prep_ce_le32(bam_ce, addr, cmd, cpu_to_le32(data));
}
#endif
