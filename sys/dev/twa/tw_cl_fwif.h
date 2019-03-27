/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004-07 Applied Micro Circuits Corporation.
 * Copyright (c) 2004-05 Vinod Kashyap
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	$FreeBSD$
 */

/*
 * AMCC'S 3ware driver for 9000 series storage controllers.
 *
 * Author: Vinod Kashyap
 * Modifications by: Adam Radford
 */



#ifndef TW_CL_FWIF_H

#define TW_CL_FWIF_H


/*
 * Macros and data structures for interfacing with the firmware.
 */


/* Register offsets from base address. */
#define	TWA_CONTROL_REGISTER_OFFSET		0x0
#define	TWA_STATUS_REGISTER_OFFSET		0x4
#define	TWA_COMMAND_QUEUE_OFFSET		0x8
#define	TWA_RESPONSE_QUEUE_OFFSET		0xC
#define	TWA_COMMAND_QUEUE_OFFSET_LOW		0x20
#define	TWA_COMMAND_QUEUE_OFFSET_HIGH		0x24
#define	TWA_LARGE_RESPONSE_QUEUE_OFFSET		0x30


/* Control register bit definitions. */
#define TWA_CONTROL_ISSUE_HOST_INTERRUPT	0x00000020
#define TWA_CONTROL_DISABLE_INTERRUPTS		0x00000040
#define TWA_CONTROL_ENABLE_INTERRUPTS		0x00000080
#define TWA_CONTROL_ISSUE_SOFT_RESET		0x00000100
#define TWA_CONTROL_UNMASK_RESPONSE_INTERRUPT	0x00004000
#define TWA_CONTROL_UNMASK_COMMAND_INTERRUPT	0x00008000
#define TWA_CONTROL_MASK_RESPONSE_INTERRUPT	0x00010000
#define TWA_CONTROL_MASK_COMMAND_INTERRUPT	0x00020000
#define TWA_CONTROL_CLEAR_ATTENTION_INTERRUPT	0x00040000
#define TWA_CONTROL_CLEAR_HOST_INTERRUPT	0x00080000
#define TWA_CONTROL_CLEAR_PCI_ABORT		0x00100000
#define TWA_CONTROL_CLEAR_QUEUE_ERROR		0x00400000
#define TWA_CONTROL_CLEAR_PARITY_ERROR		0x00800000


/* Status register bit definitions. */
#define TWA_STATUS_ROM_BIOS_IN_SBUF		0x00000002
#define TWA_STATUS_COMMAND_QUEUE_EMPTY		0x00001000
#define TWA_STATUS_MICROCONTROLLER_READY	0x00002000
#define TWA_STATUS_RESPONSE_QUEUE_EMPTY		0x00004000
#define TWA_STATUS_COMMAND_QUEUE_FULL		0x00008000
#define TWA_STATUS_RESPONSE_INTERRUPT		0x00010000
#define TWA_STATUS_COMMAND_INTERRUPT		0x00020000
#define TWA_STATUS_ATTENTION_INTERRUPT		0x00040000
#define TWA_STATUS_HOST_INTERRUPT		0x00080000
#define TWA_STATUS_PCI_ABORT_INTERRUPT		0x00100000
#define TWA_STATUS_MICROCONTROLLER_ERROR	0x00200000
#define TWA_STATUS_QUEUE_ERROR_INTERRUPT	0x00400000
#define TWA_STATUS_PCI_PARITY_ERROR_INTERRUPT	0x00800000
#define TWA_STATUS_MINOR_VERSION_MASK		0x0F000000
#define TWA_STATUS_MAJOR_VERSION_MASK		0xF0000000

#define TWA_STATUS_UNEXPECTED_BITS		0x00D00000


/* PCI related defines. */
#define TWA_IO_CONFIG_REG			0x10

#define TWA_PCI_CONFIG_CLEAR_PARITY_ERROR	0xc100
#define TWA_PCI_CONFIG_CLEAR_PCI_ABORT		0x2000

#define TWA_RESET_PHASE1_NOTIFICATION_RESPONSE	0xFFFF
#define TWA_RESET_PHASE1_WAIT_TIME_MS		500


/* Command packet opcodes. */
#define TWA_FW_CMD_NOP				0x00
#define TWA_FW_CMD_INIT_CONNECTION		0x01
#define TWA_FW_CMD_READ				0x02
#define TWA_FW_CMD_WRITE			0x03
#define TWA_FW_CMD_READVERIFY			0x04
#define TWA_FW_CMD_VERIFY			0x05
#define TWA_FW_CMD_ZEROUNIT			0x08
#define TWA_FW_CMD_REPLACEUNIT			0x09
#define TWA_FW_CMD_HOTSWAP			0x0A
#define TWA_FW_CMD_SELFTESTS			0x0B
#define TWA_FW_CMD_SYNC_PARAM			0x0C
#define TWA_FW_CMD_REORDER_UNITS		0x0D

#define TWA_FW_CMD_EXECUTE_SCSI			0x10
#define TWA_FW_CMD_ATA_PASSTHROUGH		0x11
#define TWA_FW_CMD_GET_PARAM			0x12
#define TWA_FW_CMD_SET_PARAM			0x13
#define TWA_FW_CMD_CREATEUNIT			0x14
#define TWA_FW_CMD_DELETEUNIT			0x15
#define TWA_FW_CMD_DOWNLOAD_FIRMWARE		0x16
#define TWA_FW_CMD_REBUILDUNIT			0x17
#define TWA_FW_CMD_POWER_MANAGEMENT		0x18

#define TWA_FW_CMD_REMOTE_PRINT			0x1B
#define TWA_FW_CMD_HARD_RESET_FIRMWARE		0x1C
#define TWA_FW_CMD_DEBUG			0x1D

#define TWA_FW_CMD_DIAGNOSTICS			0x1F


/* Misc defines. */
#define TWA_SHUTDOWN_MESSAGE_CREDITS	0x001
#define TWA_64BIT_SG_ADDRESSES		0x00000001
#define TWA_EXTENDED_INIT_CONNECT	0x00000002
#define TWA_BASE_MODE			1
#define TWA_BASE_FW_SRL			24
#define TWA_BASE_FW_BRANCH		0
#define TWA_BASE_FW_BUILD		1
#define TWA_CURRENT_FW_SRL		41
#define TWA_CURRENT_FW_BRANCH_9K	4
#define TWA_CURRENT_FW_BUILD_9K		8
#define TWA_CURRENT_FW_BRANCH_9K_X	8
#define TWA_CURRENT_FW_BUILD_9K_X	4
#define TWA_MULTI_LUN_FW_SRL		28
#define TWA_ARCH_ID_9K			0x5	/* 9000 PCI controllers */
#define TWA_ARCH_ID_9K_X		0x6	/* 9000 PCI-X controllers */
#define TWA_CTLR_FW_SAME_OR_NEWER	0x00000001
#define TWA_CTLR_FW_COMPATIBLE		0x00000002
#define TWA_SENSE_DATA_LENGTH		18


#define TWA_ARCH_ID(device_id)						\
	(((device_id) == TW_CL_DEVICE_ID_9K) ? TWA_ARCH_ID_9K :		\
	TWA_ARCH_ID_9K_X)
#define TWA_CURRENT_FW_BRANCH(arch_id)					\
	(((arch_id) == TWA_ARCH_ID_9K) ? TWA_CURRENT_FW_BRANCH_9K :	\
	TWA_CURRENT_FW_BRANCH_9K_X)
#define TWA_CURRENT_FW_BUILD(arch_id)					\
	(((arch_id) == TWA_ARCH_ID_9K) ? TWA_CURRENT_FW_BUILD_9K :	\
	TWA_CURRENT_FW_BUILD_9K_X)

/*
 * All SG addresses and DMA'able memory allocated by the OSL should be
 * TWA_ALIGNMENT bytes aligned, and have a size that is a multiple of
 * TWA_SG_ELEMENT_SIZE_FACTOR.
 */
#define TWA_ALIGNMENT(device_id)			0x4
#define TWA_SG_ELEMENT_SIZE_FACTOR(device_id)		\
	(((device_id) == TW_CL_DEVICE_ID_9K) ? 512 : 4)


/*
 * Some errors of interest (in cmd_hdr->status_block.error) when a command
 * is completed by the firmware with a bad status.
 */
#define TWA_ERROR_LOGICAL_UNIT_NOT_SUPPORTED	0x010a
#define TWA_ERROR_UNIT_OFFLINE			0x0128
#define TWA_ERROR_MORE_DATA			0x0231


/* AEN codes of interest. */
#define TWA_AEN_QUEUE_EMPTY		0x00
#define TWA_AEN_SOFT_RESET		0x01
#define TWA_AEN_SYNC_TIME_WITH_HOST	0x31


/* Table #'s and id's of parameters of interest in firmware's param table. */
#define TWA_PARAM_VERSION_TABLE		0x0402
#define TWA_PARAM_VERSION_FW		3	/* firmware version [16] */
#define TWA_PARAM_VERSION_BIOS		4	/* BIOSs version [16] */
#define TWA_PARAM_CTLR_MODEL		8	/* Controller model [16] */

#define TWA_PARAM_CONTROLLER_TABLE	0x0403
#define TWA_PARAM_CONTROLLER_PORT_COUNT	3	/* number of ports [1] */

#define TWA_PARAM_TIME_TABLE		0x40A
#define TWA_PARAM_TIME_SCHED_TIME	0x3

#define TWA_9K_PARAM_DESCRIPTOR		0x8000


#pragma pack(1)
/* 7000 structures. */
struct tw_cl_command_init_connect {
	TW_UINT8	res1__opcode;	/* 3:5 */
	TW_UINT8	size;
	TW_UINT8	request_id;
	TW_UINT8	res2;
	TW_UINT8	status;
	TW_UINT8	flags;
	TW_UINT16	message_credits;
	TW_UINT32	features;
	TW_UINT16	fw_srl;
	TW_UINT16	fw_arch_id;
	TW_UINT16	fw_branch;
	TW_UINT16	fw_build;
	TW_UINT32	result;
};


/* Structure for downloading firmware onto the controller. */
struct tw_cl_command_download_firmware {
	TW_UINT8	sgl_off__opcode;/* 3:5 */
	TW_UINT8	size;
	TW_UINT8	request_id;
	TW_UINT8	unit;
	TW_UINT8	status;
	TW_UINT8	flags;
	TW_UINT16	param;
	TW_UINT8	sgl[1];
};


/* Structure for hard resetting the controller. */
struct tw_cl_command_reset_firmware {
	TW_UINT8	res1__opcode;	/* 3:5 */
	TW_UINT8	size;
	TW_UINT8	request_id;
	TW_UINT8	unit;
	TW_UINT8	status;
	TW_UINT8	flags;
	TW_UINT8	res2;
	TW_UINT8	param;
};


/* Structure for sending get/set param commands. */
struct tw_cl_command_param {
	TW_UINT8	sgl_off__opcode;/* 3:5 */
	TW_UINT8	size;
	TW_UINT8	request_id;
	TW_UINT8	host_id__unit;	/* 4:4 */
	TW_UINT8	status;
	TW_UINT8	flags;
	TW_UINT16	param_count;
	TW_UINT8	sgl[1];
};


/* Generic command packet. */
struct tw_cl_command_generic {
	TW_UINT8	sgl_off__opcode;/* 3:5 */
	TW_UINT8	size;
	TW_UINT8	request_id;
	TW_UINT8	host_id__unit;	/* 4:4 */
	TW_UINT8	status;
	TW_UINT8	flags;
	TW_UINT16	count;	/* block cnt, parameter cnt, message credits */
};


/* Command packet header. */
struct tw_cl_command_header {
	TW_UINT8	sense_data[TWA_SENSE_DATA_LENGTH];
	struct {
		TW_INT8		reserved[4];
		TW_UINT16	error;
		TW_UINT8	padding;
		TW_UINT8	res__severity;	/* 5:3 */
	} status_block;
	TW_UINT8	err_specific_desc[98];
	struct {
		TW_UINT8	size_header;
		TW_UINT16	reserved;
		TW_UINT8	size_sense;
	} header_desc;
};


/* 7000 Command packet. */
union tw_cl_command_7k {
	struct tw_cl_command_init_connect	init_connect;
	struct tw_cl_command_download_firmware	download_fw;
	struct tw_cl_command_reset_firmware	reset_fw;
	struct tw_cl_command_param		param;
	struct tw_cl_command_generic		generic;
	TW_UINT8	padding[1024 - sizeof(struct tw_cl_command_header)];
};


/* 9000 Command Packet. */
struct tw_cl_command_9k {
	TW_UINT8	res__opcode;	/* 3:5 */
	TW_UINT8	unit;
	TW_UINT16	lun_l4__req_id;	/* 4:12 */
	TW_UINT8	status;
	TW_UINT8	sgl_offset; /* offset (in bytes) to sg_list, from the
					end of sgl_entries */
	TW_UINT16	lun_h4__sgl_entries;
	TW_UINT8	cdb[16];
	TW_UINT8	sg_list[872];/* total struct size =
					1024-sizeof(cmd_hdr) */
};


/* Full command packet. */
struct tw_cl_command_packet {
	struct tw_cl_command_header	cmd_hdr;
	union {
		union tw_cl_command_7k	cmd_pkt_7k;
		struct tw_cl_command_9k cmd_pkt_9k;
	} command;
};


/* Structure describing payload for get/set param commands. */
struct tw_cl_param_9k {
	TW_UINT16	table_id;
	TW_UINT8	parameter_id;
	TW_UINT8	reserved;
	TW_UINT16	parameter_size_bytes;
	TW_UINT16	parameter_actual_size_bytes;
	TW_UINT8	data[1];
};
#pragma pack()


/* Functions to read from, and write to registers */
#define TW_CLI_WRITE_CONTROL_REGISTER(ctlr_handle, value)		\
	tw_osl_write_reg(ctlr_handle, TWA_CONTROL_REGISTER_OFFSET, value, 4)


#define TW_CLI_READ_STATUS_REGISTER(ctlr_handle)			\
	tw_osl_read_reg(ctlr_handle, TWA_STATUS_REGISTER_OFFSET, 4)


#define TW_CLI_WRITE_COMMAND_QUEUE(ctlr_handle, value)	do {		\
	if (ctlr->flags & TW_CL_64BIT_ADDRESSES) {			\
		/* First write the low 4 bytes, then the high 4. */	\
		tw_osl_write_reg(ctlr_handle, TWA_COMMAND_QUEUE_OFFSET_LOW, \
			(TW_UINT32)(value), 4);				\
		tw_osl_write_reg(ctlr_handle, TWA_COMMAND_QUEUE_OFFSET_HIGH,\
			(TW_UINT32)(((TW_UINT64)value)>>32), 4);	\
	} else								\
		tw_osl_write_reg(ctlr_handle, TWA_COMMAND_QUEUE_OFFSET,	\
					(TW_UINT32)(value), 4);		\
} while (0)


#define TW_CLI_READ_RESPONSE_QUEUE(ctlr_handle)				\
	tw_osl_read_reg(ctlr_handle, TWA_RESPONSE_QUEUE_OFFSET, 4)


#define TW_CLI_READ_LARGE_RESPONSE_QUEUE(ctlr_handle)			\
	tw_osl_read_reg(ctlr_handle, TWA_LARGE_RESPONSE_QUEUE_OFFSET, 4)


#define TW_CLI_SOFT_RESET(ctlr)					\
	TW_CLI_WRITE_CONTROL_REGISTER(ctlr,			\
		TWA_CONTROL_ISSUE_SOFT_RESET |			\
		TWA_CONTROL_CLEAR_HOST_INTERRUPT |		\
		TWA_CONTROL_CLEAR_ATTENTION_INTERRUPT |		\
		TWA_CONTROL_MASK_COMMAND_INTERRUPT |		\
		TWA_CONTROL_MASK_RESPONSE_INTERRUPT |		\
		TWA_CONTROL_DISABLE_INTERRUPTS)

/* Detect inconsistencies in the status register. */
#define TW_CLI_STATUS_ERRORS(x)					\
	((x & TWA_STATUS_UNEXPECTED_BITS) &&			\
	 (x & TWA_STATUS_MICROCONTROLLER_READY))


/*
 * Functions for making transparent, the bit fields in firmware
 * interface structures.
 */
#define BUILD_SGL_OFF__OPCODE(sgl_off, opcode)	\
	((sgl_off << 5) & 0xE0) | (opcode & 0x1F)	/* 3:5 */

#define BUILD_RES__OPCODE(res, opcode)		\
	((res << 5) & 0xE0) | (opcode & 0x1F)		/* 3:5 */

#define BUILD_HOST_ID__UNIT(host_id, unit)	\
	((host_id << 4) & 0xF0) | (unit & 0xF)		/* 4:4 */

#define BUILD_RES__SEVERITY(res, severity)	\
	((res << 3) & 0xF8) | (severity & 0x7)		/* 5:3 */

#define BUILD_LUN_L4__REQ_ID(lun, req_id)	\
	(((lun << 12) & 0xF000) | (req_id & 0xFFF))	/* 4:12 */

#define BUILD_LUN_H4__SGL_ENTRIES(lun, sgl_entries)	\
	(((lun << 8) & 0xF000) | (sgl_entries & 0xFFF))	/* 4:12 */


#define GET_OPCODE(sgl_off__opcode)	\
	(sgl_off__opcode & 0x1F)		/* 3:5 */

#define GET_SGL_OFF(sgl_off__opcode)	\
	((sgl_off__opcode >> 5) & 0x7)		/* 3:5 */

#define GET_UNIT(host_id__unit)		\
	(host_id__unit & 0xF)			/* 4:4 */

#define GET_HOST_ID(host_id__unit)	\
	((host_id__unit >> 4) & 0xF)		/* 4:4 */

#define GET_SEVERITY(res__severity)	\
	(res__severity & 0x7)			/* 5:3 */

#define GET_RESP_ID(undef2__resp_id__undef1)	\
	((undef2__resp_id__undef1 >> 4) & 0xFF)	/* 20:8:4 */

#define GET_RESP_ID_9K_X(undef2__resp_id)	\
	((undef2__resp_id) & 0xFFF)		/* 20:12 */

#define GET_LARGE_RESP_ID(misc__large_resp_id)	\
	((misc__large_resp_id) & 0xFFFF)	/* 16:16 */

#define GET_REQ_ID(lun_l4__req_id)	\
	(lun_l4__req_id & 0xFFF)		/* 4:12 */

#define GET_LUN_L4(lun_l4__req_id)	\
	((lun_l4__req_id >> 12) & 0xF)		/* 4:12 */

#define GET_SGL_ENTRIES(lun_h4__sgl_entries)	\
	(lun_h4__sgl_entries & 0xFFF)		/* 4:12 */

#define GET_LUN_H4(lun_h4__sgl_entries)	\
	((lun_h4__sgl_entries >> 12) & 0xF)	/* 4:12 */



#endif /* TW_CL_FWIF_H */
