/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000 Michael Smith
 * Copyright (c) 2003 Paul Saab
 * Copyright (c) 2003 Vinod Kashyap
 * Copyright (c) 2000 BSDi
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
 *      $FreeBSD$
 */

/* 
 * Register names, bit definitions, structure names and members are
 * identical with those in the Linux driver where possible and sane 
 * for simplicity's sake.  (The TW_ prefix has become TWE_)
 * Some defines that are clearly irrelevant to FreeBSD have been
 * removed.
 */

/* control register bit definitions */
#define TWE_CONTROL_CLEAR_HOST_INTERRUPT	0x00080000
#define TWE_CONTROL_CLEAR_ATTENTION_INTERRUPT	0x00040000
#define TWE_CONTROL_MASK_COMMAND_INTERRUPT	0x00020000
#define TWE_CONTROL_MASK_RESPONSE_INTERRUPT	0x00010000
#define TWE_CONTROL_UNMASK_COMMAND_INTERRUPT	0x00008000
#define TWE_CONTROL_UNMASK_RESPONSE_INTERRUPT	0x00004000
#define TWE_CONTROL_CLEAR_ERROR_STATUS		0x00000200
#define TWE_CONTROL_ISSUE_SOFT_RESET		0x00000100
#define TWE_CONTROL_ENABLE_INTERRUPTS		0x00000080
#define TWE_CONTROL_DISABLE_INTERRUPTS		0x00000040
#define TWE_CONTROL_ISSUE_HOST_INTERRUPT	0x00000020
#define TWE_CONTROL_CLEAR_PARITY_ERROR		0x00800000
#define TWE_CONTROL_CLEAR_PCI_ABORT		0x00100000

#define TWE_SOFT_RESET(sc)	TWE_CONTROL(sc, TWE_CONTROL_ISSUE_SOFT_RESET |		\
					   TWE_CONTROL_CLEAR_HOST_INTERRUPT |		\
					   TWE_CONTROL_CLEAR_ATTENTION_INTERRUPT |	\
					   TWE_CONTROL_MASK_COMMAND_INTERRUPT |		\
					   TWE_CONTROL_MASK_RESPONSE_INTERRUPT |	\
					   TWE_CONTROL_CLEAR_ERROR_STATUS |		\
					   TWE_CONTROL_DISABLE_INTERRUPTS)

/* status register bit definitions */
#define TWE_STATUS_MAJOR_VERSION_MASK		0xF0000000
#define TWE_STATUS_MINOR_VERSION_MASK		0x0F000000
#define TWE_STATUS_PCI_PARITY_ERROR		0x00800000
#define TWE_STATUS_QUEUE_ERROR			0x00400000
#define TWE_STATUS_MICROCONTROLLER_ERROR	0x00200000
#define TWE_STATUS_PCI_ABORT			0x00100000
#define TWE_STATUS_HOST_INTERRUPT		0x00080000
#define TWE_STATUS_ATTENTION_INTERRUPT		0x00040000
#define TWE_STATUS_COMMAND_INTERRUPT		0x00020000
#define TWE_STATUS_RESPONSE_INTERRUPT		0x00010000
#define TWE_STATUS_COMMAND_QUEUE_FULL		0x00008000
#define TWE_STATUS_RESPONSE_QUEUE_EMPTY		0x00004000
#define TWE_STATUS_MICROCONTROLLER_READY	0x00002000
#define TWE_STATUS_COMMAND_QUEUE_EMPTY		0x00001000
#define TWE_STATUS_ALL_INTERRUPTS		0x000F0000
#define TWE_STATUS_CLEARABLE_BITS		0x00D00000
#define TWE_STATUS_EXPECTED_BITS		0x00002000
#define TWE_STATUS_UNEXPECTED_BITS		0x00F80000

/* XXX this is a little harsh, but necessary to chase down firmware problems */
#define TWE_STATUS_PANIC_BITS			(TWE_STATUS_MICROCONTROLLER_ERROR)

/* for use with the %b printf format */
#define TWE_STATUS_BITS_DESCRIPTION \
	"\20\15CQEMPTY\16UCREADY\17RQEMPTY\20CQFULL\21RINTR\22CINTR\23AINTR\24HINTR\25PCIABRT\26MCERR\27QERR\30PCIPERR\n"

/* detect inconsistencies in the status register */
#define TWE_STATUS_ERRORS(x)				\
	(((x & TWE_STATUS_PCI_ABORT) 		||	\
	  (x & TWE_STATUS_PCI_PARITY_ERROR) 	||	\
	  (x & TWE_STATUS_QUEUE_ERROR)		||	\
	  (x & TWE_STATUS_MICROCONTROLLER_ERROR)) &&	\
	 (x & TWE_STATUS_MICROCONTROLLER_READY))

/* Response queue bit definitions */
#define TWE_RESPONSE_ID_MASK		0x00000FF0

/* PCI related defines */
#define TWE_IO_CONFIG_REG		0x10
#define TWE_DEVICE_NAME			"3ware Storage Controller"
#define TWE_VENDOR_ID			0x13C1
#define TWE_DEVICE_ID			0x1000
#define TWE_DEVICE_ID_ASIC		0x1001
#define TWE_PCI_CLEAR_PARITY_ERROR	0xc100
#define TWE_PCI_CLEAR_PCI_ABORT		0x2000

/* command packet opcodes */
#define TWE_OP_NOP			0x00
#define TWE_OP_INIT_CONNECTION		0x01
#define TWE_OP_READ			0x02
#define TWE_OP_WRITE			0x03
#define TWE_OP_READVERIFY		0x04
#define TWE_OP_VERIFY			0x05
#define TWE_OP_ZEROUNIT			0x08
#define TWE_OP_REPLACEUNIT		0x09
#define TWE_OP_HOTSWAP			0x0a
#define TWE_OP_SETATAFEATURE		0x0c
#define TWE_OP_FLUSH			0x0e
#define TWE_OP_ABORT			0x0f
#define TWE_OP_CHECKSTATUS		0x10
#define TWE_OP_ATA_PASSTHROUGH		0x11
#define TWE_OP_GET_PARAM		0x12
#define TWE_OP_SET_PARAM		0x13
#define TWE_OP_CREATEUNIT		0x14
#define TWE_OP_DELETEUNIT		0x15
#define TWE_OP_REBUILDUNIT		0x17
#define TWE_OP_SECTOR_INFO		0x1a
#define TWE_OP_AEN_LISTEN		0x1c
#define TWE_OP_CMD_PACKET		0x1d
#define TWE_OP_CMD_WITH_DATA		0x1f

/* command status values */
#define TWE_STATUS_RESET		0xff	/* controller requests reset */
#define TWE_STATUS_FATAL		0xc0	/* fatal errors not requiring reset */
#define TWE_STATUS_WARNING		0x80	/* warnings */
#define TWE_STAUS_INFO			0x40	/* informative status */

/* misc defines */
#define TWE_ALIGNMENT			0x200
#define TWE_MAX_UNITS			16
#define TWE_COMMAND_ALIGNMENT_MASK	0x1ff
#define TWE_INIT_MESSAGE_CREDITS	0xff	/* older firmware has issues with 256 commands */
#define TWE_SHUTDOWN_MESSAGE_CREDITS	0x001
#define TWE_INIT_COMMAND_PACKET_SIZE	0x3
#define TWE_MAX_SGL_LENGTH		62
#define TWE_MAX_ATA_SGL_LENGTH		60
#define TWE_MAX_PASSTHROUGH		4096
#define TWE_Q_LENGTH			TWE_INIT_MESSAGE_CREDITS
#define TWE_Q_START			0
#define TWE_MAX_RESET_TRIES		3
#define TWE_BLOCK_SIZE			0x200	/* 512-byte blocks */
#define TWE_SECTOR_SIZE			0x200	/* generic I/O bufffer */
#define TWE_IOCTL			0x80
#define TWE_MAX_AEN_TRIES		100
#define TWE_UNIT_ONLINE			1

/* scatter/gather list entry */
typedef struct
{
    u_int32_t	address;
    u_int32_t	length;
} __packed TWE_SG_Entry;

typedef struct {
    u_int8_t	opcode:5;		/* TWE_OP_INITCONNECTION */
    u_int8_t	res1:3;		
    u_int8_t	size;
    u_int8_t	request_id;
    u_int8_t	res2:4;
    u_int8_t	host_id:4;
    u_int8_t	status;
    u_int8_t	flags;
    u_int16_t	message_credits;
    u_int32_t	response_queue_pointer;
} __packed TWE_Command_INITCONNECTION;

typedef struct
{
    u_int8_t	opcode:5;		/* TWE_OP_READ/TWE_OP_WRITE */
    u_int8_t	res1:3;
    u_int8_t	size;
    u_int8_t	request_id;
    u_int8_t	unit:4;
    u_int8_t	host_id:4;
    u_int8_t	status;
    u_int8_t	flags;
    u_int16_t	block_count;
    u_int32_t	lba;
    TWE_SG_Entry sgl[TWE_MAX_SGL_LENGTH];
} __packed TWE_Command_IO;

typedef struct
{
    u_int8_t	opcode:5;		/* TWE_OP_HOTSWAP */
    u_int8_t	res1:3;
    u_int8_t	size;
    u_int8_t	request_id;
    u_int8_t	unit:4;
    u_int8_t	host_id:4;
    u_int8_t	status;
    u_int8_t	flags;
    u_int8_t	action;
#define TWE_OP_HOTSWAP_REMOVE		0x00	/* remove assumed-degraded unit */
#define TWE_OP_HOTSWAP_ADD_CBOD		0x01	/* add CBOD to empty port */
#define TWE_OP_HOTSWAP_ADD_SPARE	0x02	/* add spare to empty port */
    u_int8_t	aport;
} __packed TWE_Command_HOTSWAP;

typedef struct
{
    u_int8_t	opcode:5;		/* TWE_OP_SETATAFEATURE */
    u_int8_t	res1:3;
    u_int8_t	size;
    u_int8_t	request_id;
    u_int8_t	unit:4;
    u_int8_t	host_id:4;
    u_int8_t	status;
    u_int8_t	flags;
    u_int8_t	feature;
#define TWE_OP_SETATAFEATURE_WCE	0x02
#define TWE_OP_SETATAFEATURE_DIS_WCE	0x82
    u_int8_t	feature_mode;
    u_int16_t	all_units;
    u_int16_t	persistence;
} __packed TWE_Command_SETATAFEATURE;

typedef struct
{
    u_int8_t	opcode:5;		/* TWE_OP_CHECKSTATUS */
    u_int8_t	res1:3;
    u_int8_t	size;
    u_int8_t	request_id;
    u_int8_t	unit:4;
    u_int8_t	res2:4;
    u_int8_t	status;
    u_int8_t	flags;
    u_int16_t	target_status;		/* set low byte to target request's ID */
} __packed TWE_Command_CHECKSTATUS;

typedef struct
{
    u_int8_t	opcode:5;		/* TWE_OP_GETPARAM, TWE_OP_SETPARAM */
    u_int8_t	res1:3;
    u_int8_t	size;
    u_int8_t	request_id;
    u_int8_t	unit:4;
    u_int8_t	host_id:4;
    u_int8_t	status;
    u_int8_t	flags;
    u_int16_t	param_count;
    TWE_SG_Entry sgl[TWE_MAX_SGL_LENGTH];
} __packed TWE_Command_PARAM;

typedef struct
{
    u_int8_t	opcode:5;		/* TWE_OP_REBUILDUNIT */
    u_int8_t	res1:3;
    u_int8_t	size;
    u_int8_t	request_id;
    u_int8_t	src_unit:4;
    u_int8_t	host_id:4;
    u_int8_t	status;
    u_int8_t	flags;
    u_int8_t	action:7;
#define TWE_OP_REBUILDUNIT_NOP		0
#define TWE_OP_REBUILDUNIT_STOP		2	/* stop all rebuilds */
#define TWE_OP_REBUILDUNIT_START	4	/* start rebuild with lowest unit */
#define TWE_OP_REBUILDUNIT_STARTUNIT	5	/* rebuild src_unit (not supported) */
    u_int8_t	cs:1;				/* request state change on src_unit */
    u_int8_t	logical_subunit;		/* for RAID10 rebuild of logical subunit */
} __packed TWE_Command_REBUILDUNIT;

typedef struct
{
    u_int8_t	opcode:5;
    u_int8_t	sgl_offset:3;
    u_int8_t	size;
    u_int8_t	request_id;
    u_int8_t	unit:4;
    u_int8_t	host_id:4;
    u_int8_t	status;
    u_int8_t	flags;
    u_int16_t	param;
    u_int16_t	features;
    u_int16_t	sector_count;
    u_int16_t	sector_num;
    u_int16_t	cylinder_lo;
    u_int16_t	cylinder_hi;
    u_int8_t	drive_head;
    u_int8_t	command;
    TWE_SG_Entry sgl[TWE_MAX_ATA_SGL_LENGTH];
} __packed TWE_Command_ATA;

typedef struct
{
    u_int8_t	opcode:5;
    u_int8_t	sgl_offset:3;
    u_int8_t	size;
    u_int8_t	request_id;
    u_int8_t	unit:4;
    u_int8_t	host_id:4;
    u_int8_t	status;
    u_int8_t	flags;
#define TWE_FLAGS_SUCCESS	0x00
#define TWE_FLAGS_INFORMATIONAL	0x01
#define TWE_FLAGS_WARNING	0x02
#define TWE_FLAGS_FATAL		0x03
#define TWE_FLAGS_PERCENTAGE	(1<<8)	/* bits 0-6 indicate completion percentage */
    u_int16_t	count;			/* block count, parameter count, message credits */
} __packed TWE_Command_Generic;

/* command packet - must be TWE_ALIGNMENT aligned */
typedef union
{
    TWE_Command_INITCONNECTION	initconnection;
    TWE_Command_IO		io;
    TWE_Command_PARAM		param;
    TWE_Command_CHECKSTATUS	checkstatus;
    TWE_Command_REBUILDUNIT	rebuildunit;
    TWE_Command_SETATAFEATURE	setatafeature;
    TWE_Command_ATA		ata;
    TWE_Command_Generic		generic;
    u_int8_t			pad[512];
} TWE_Command;

/* response queue entry */
typedef union
{
    struct 
    {
	u_int32_t	undefined_1:4;
	u_int32_t	response_id:8;
	u_int32_t	undefined_2:20;
    } u;
    u_int32_t	value;
} TWE_Response_Queue;

/*
 * From 3ware's documentation:
 *   All parameters maintained by the controller are grouped into related tables.
 *   Tables are are accessed indirectly via get and set parameter commands.
 *   To access a specific parameter in a table, the table ID and parameter index
 *   are used to uniquely identify a parameter.  Table 0xffff is the directory
 *   table and provides a list of the table IDs and sizes of all other tables.
 *   Index zero in each table specifies the entire table, and index one specifies
 *   the size of the table.  An entire table can be read or set by using index zero.
 */

#define TWE_PARAM_PARAM_ALL	0
#define TWE_PARAM_PARAM_SIZE	1

#define TWE_PARAM_DIRECTORY	0xffff			/* size is 4 * number of tables */
#define TWE_PARAM_DIRECTORY_TABLES		2	/* 16 bits * number of tables */
#define TWE_PARAM_DIRECTORY_SIZES		3	/* 16 bits * number of tables */

#define TWE_PARAM_DRIVESUMMARY	0x0002
#define TWE_PARAM_DRIVESUMMARY_Num		2	/* number of physical drives [2] */
#define TWE_PARAM_DRIVESUMMARY_Status		3	/* array giving drive status per aport */
#define TWE_PARAM_DRIVESTATUS_Missing	0x00
#define TWE_PARAM_DRIVESTATUS_NotSupp	0xfe
#define TWE_PARAM_DRIVESTATUS_Present	0xff

#define TWE_PARAM_UNITSUMMARY	0x0003
#define TWE_PARAM_UNITSUMMARY_Num		2	/* number of logical units [2] */
#define TWE_PARAM_UNITSUMMARY_Status		3	/* array giving unit status [16] */
#define TWE_PARAM_UNITSTATUS_Online		(1<<0)
#define TWE_PARAM_UNITSTATUS_Complete		(1<<1)
#define TWE_PARAM_UNITSTATUS_MASK		0xfc
#define TWE_PARAM_UNITSTATUS_Normal		0xfc
#define TWE_PARAM_UNITSTATUS_Initialising	0xf4	/* cannot be incomplete */
#define TWE_PARAM_UNITSTATUS_Degraded		0xec
#define TWE_PARAM_UNITSTATUS_Rebuilding		0xdc	/* cannot be incomplete */
#define TWE_PARAM_UNITSTATUS_Verifying		0xcc	/* cannot be incomplete */
#define TWE_PARAM_UNITSTATUS_Corrupt		0xbc	/* cannot be complete */
#define TWE_PARAM_UNITSTATUS_Missing		0x00	/* cannot be complete or online */

#define TWE_PARAM_DRIVEINFO	0x0200			/* add drive number 0x00-0x0f XXX docco confused 0x0100 vs 0x0200 */
#define TWE_PARAM_DRIVEINFO_Size		2	/* size in blocks [4] */
#define TWE_PARAM_DRIVEINFO_Model		3	/* drive model string [40] */
#define TWE_PARAM_DRIVEINFO_Serial		4	/* drive serial number [20] */
#define TWE_PARAM_DRIVEINFO_PhysCylNum		5	/* physical geometry [2] */
#define TWE_PARAM_DRIVEINFO_PhysHeadNum		6	/* [2] */
#define TWE_PARAM_DRIVEINFO_PhysSectorNym	7	/* [2] */
#define TWE_PARAM_DRIVEINFO_LogCylNum		8	/* logical geometry [2] */
#define TWE_PARAM_DRIVEINFO_LogHeadNum		9	/* [2] */
#define TWE_PARAM_DRIVEINFO_LogSectorNum	10	/* [2] */
#define TWE_PARAM_DRIVEINFO_UnitNum		11	/* unit number this drive is associated with or 0xff [1] */
#define TWE_PARAM_DRIVEINFO_DriveFlags		12	/* N/A [1] */

#define TWE_PARAM_APORTTIMEOUT	0x02c0			/* add (aport_number * 3) to parameter index */
#define TWE_PARAM_APORTTIMEOUT_READ		2	/* read timeouts last 24hrs [2] */
#define TWE_PARAM_APORTTIMEOUT_WRITE		3	/* write timeouts last 24hrs [2] */
#define TWE_PARAM_APORTTIMEOUT_DEGRADE		4	/* degrade threshold [2] */

#define TWE_PARAM_UNITINFO	0x0300			/* add unit number 0x00-0x0f */
#define TWE_PARAM_UNITINFO_Number		2	/* unit number [1] */
#define TWE_PARAM_UNITINFO_Status		3	/* unit status [1] */
#define TWE_PARAM_UNITINFO_Capacity		4	/* unit capacity in blocks [4] */
#define TWE_PARAM_UNITINFO_DescriptorSize	5	/* unit descriptor size + 3 bytes [2] */
#define TWE_PARAM_UNITINFO_Descriptor		6	/* unit descriptor, TWE_UnitDescriptor or TWE_Array_Descriptor */
#define TWE_PARAM_UNITINFO_Flags		7	/* unit flags [1] */
#define TWE_PARAM_UNITFLAGS_WCE			(1<<0)

#define TWE_PARAM_AEN		0x0401
#define TWE_PARAM_AEN_UnitCode			2	/* (unit number << 8) | AEN code [2] */
#define TWE_AEN_QUEUE_EMPTY		0x00
#define TWE_AEN_SOFT_RESET		0x01
#define TWE_AEN_DEGRADED_MIRROR		0x02	/* reports unit */
#define TWE_AEN_CONTROLLER_ERROR	0x03
#define TWE_AEN_REBUILD_FAIL		0x04	/* reports unit */
#define TWE_AEN_REBUILD_DONE		0x05	/* reports unit */
#define TWE_AEN_INCOMP_UNIT		0x06	/* reports unit */
#define TWE_AEN_INIT_DONE		0x07	/* reports unit */
#define TWE_AEN_UNCLEAN_SHUTDOWN	0x08	/* reports unit */
#define TWE_AEN_APORT_TIMEOUT		0x09	/* reports unit, rate limited to 1 per 2^16 errors */
#define TWE_AEN_DRIVE_ERROR		0x0a	/* reports unit */
#define TWE_AEN_REBUILD_STARTED		0x0b	/* reports unit */
#define TWE_AEN_QUEUE_FULL		0xff
#define TWE_AEN_TABLE_UNDEFINED		0x15
#define TWE_AEN_CODE(x)			((x) & 0xff)
#define TWE_AEN_UNIT(x)			((x) >> 8)

#define TWE_PARAM_VERSION	0x0402
#define TWE_PARAM_VERSION_Mon			2	/* monitor version [16] */
#define TWE_PARAM_VERSION_FW			3	/* firmware version [16] */
#define TWE_PARAM_VERSION_BIOS			4	/* BIOSs version [16] */
#define TWE_PARAM_VERSION_PCB			5	/* PCB version [8] */
#define TWE_PARAM_VERSION_ATA			6	/* A-chip version [8] */
#define TWE_PARAM_VERSION_PCI			7	/* P-chip version [8] */
#define TWE_PARAM_VERSION_CtrlModel		8	/* N/A */
#define TWE_PARAM_VERSION_CtrlSerial		9	/* N/A */
#define TWE_PARAM_VERSION_SBufSize		10	/* N/A */
#define TWE_PARAM_VERSION_CompCode		11	/* compatibility code [4] */

#define TWE_PARAM_CONTROLLER	0x0403
#define TWE_PARAM_CONTROLLER_DCBSectors		2	/* # sectors reserved for DCB per drive [2] */
#define TWE_PARAM_CONTROLLER_PortCount		3	/* number of drive ports [1] */

#define TWE_PARAM_FEATURES	0x404
#define TWE_PARAM_FEATURES_DriverShutdown	2	/* set to 1 if driver supports shutdown notification [1] */

typedef struct
{
    u_int8_t		num_subunits;	/* must be zero */
    u_int8_t		configuration;
#define TWE_UD_CONFIG_CBOD	0x0c	/* JBOD with DCB, used for mirrors */
#define TWE_UD_CONFIG_SPARE	0x0d	/* same as CBOD, but firmware will use as spare */
#define TWE_UD_CONFIG_SUBUNIT	0x0e	/* drive is a subunit in an array */
#define TWE_UD_CONFIG_JBOD	0x0f	/* plain drive */
    u_int8_t		phys_drv_num;	/* may be 0xff if port can't be determined at runtime */
    u_int8_t		log_drv_num;	/* must be zero for configuration == 0x0f */
    u_int32_t		start_lba;
    u_int32_t		block_count;	/* actual drive size if configuration == 0x0f, otherwise less DCB size */
} __packed TWE_Unit_Descriptor;

typedef struct
{
    u_int8_t		flag;			/* must be 0xff */
    u_int8_t		res1;
    u_int8_t		mirunit_status[4];	/* bitmap of functional subunits in each mirror */
    u_int8_t		res2[6];
} __packed TWE_Mirror_Descriptor;

typedef struct
{
    u_int8_t		num_subunits;	/* number of subunits, or number of mirror units in RAID10 */
    u_int8_t		configuration;
#define TWE_UD_CONFIG_RAID0	0x00
#define TWE_UD_CONFIG_RAID1	0x01
#define TWE_UD_CONFIG_TwinStor	0x02
#define TWE_UD_CONFIG_RAID5	0x05
#define TWE_UD_CONFIG_RAID10	0x06
    u_int8_t		stripe_size;
#define TWE_UD_STRIPE_4k	0x03
#define TWE_UD_STRIPE_8k	0x04
#define TWE_UD_STRIPE_16k	0x05
#define TWE_UD_STRIPE_32k	0x06
#define TWE_UD_STRIPE_64k	0x07
    u_int8_t		log_drv_status;	/* bitmap of functional subunits, or mirror units in RAID10 */
    u_int32_t		start_lba;
    u_int32_t		block_count;	/* actual drive size if configuration == 0x0f, otherwise less DCB size */
    TWE_Unit_Descriptor	subunit[0];	/* subunit descriptors, in RAID10 mode is [mirunit][subunit] */
} __packed TWE_Array_Descriptor;

typedef struct
{
    u_int16_t	table_id;
    u_int8_t	parameter_id;
    u_int8_t	parameter_size_bytes;
    u_int8_t	data[0];
} __packed TWE_Param;

