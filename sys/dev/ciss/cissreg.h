/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Michael Smith
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
 * Structure and I/O definitions for the Command Interface for SCSI-3 Support.
 *
 * Data in command CDBs are in big-endian format.  All other data is little-endian.
 * This header only supports little-endian hosts at this time.
 */

union ciss_device_address
{
    struct 				/* MODE_PERIPHERAL and MODE_MASK_PERIPHERAL */
    {
	u_int32_t	target:24;	/* SCSI target */
	u_int32_t	bus:6;		/* SCSI bus */
	u_int32_t	mode:2;		/* CISS_HDR_ADDRESS_MODE_* */
	u_int32_t	extra_address;	/* SCSI-3 level-2 and level-3 address bytes */
    } physical;
    struct 				/* MODE_LOGICAL */
    {
	u_int32_t	lun:30;		/* logical device ID */
	u_int32_t	mode:2;		/* CISS_HDR_ADDRESS_MODE_LOGICAL */
	u_int32_t	:32;		/* reserved */
    } logical;
    struct
    {
	u_int32_t	:30;
	u_int32_t	mode:2;
	u_int32_t	:32;
    } mode;
};
#define CISS_HDR_ADDRESS_MODE_PERIPHERAL	0x0
#define CISS_HDR_ADDRESS_MODE_LOGICAL		0x1
#define CISS_HDR_ADDRESS_MODE_MASK_PERIPHERAL	0x3

#define CISS_EXTRA_MODE2(extra)		((extra & 0xc0000000) >> 30)
#define CISS_EXTRA_BUS2(extra)		((extra & 0x3f000000) >> 24)
#define CISS_EXTRA_TARGET2(extra)	((extra & 0x00ff0000) >> 16)
#define CISS_EXTRA_MODE3(extra)		((extra & 0x0000c000) >> 14)
#define CISS_EXTRA_BUS3(extra)		((extra & 0x00003f00) >> 8)
#define CISS_EXTRA_TARGET3(extra)	((extra & 0x000000ff))

struct ciss_header
{
    u_int8_t	:8;			/* reserved */
    u_int8_t	sg_in_list;		/* SG's in the command structure */
    u_int16_t	sg_total;		/* total count of SGs for this command */
    u_int32_t	host_tag;		/* host identifier, bits 0&1 must be clear */
#define CISS_HDR_HOST_TAG_ERROR	(1<<1)
    u_int32_t	host_tag_zeroes;	/* tag is 64 bits, but interface only supports 32 */
    union ciss_device_address address;
} __packed;

struct ciss_cdb
{
    u_int8_t	cdb_length;		/* valid CDB bytes */
    u_int8_t	type:3;
#define CISS_CDB_TYPE_COMMAND			0
#define CISS_CDB_TYPE_MESSAGE			1
    u_int8_t	attribute:3;
#define CISS_CDB_ATTRIBUTE_UNTAGGED		0
#define CISS_CDB_ATTRIBUTE_SIMPLE		4
#define CISS_CDB_ATTRIBUTE_HEAD_OF_QUEUE	5
#define CISS_CDB_ATTRIBUTE_ORDERED		6
#define CISS_CDB_ATTRIBUTE_AUTO_CONTINGENT	7
    u_int8_t	direction:2;
#define CISS_CDB_DIRECTION_NONE			0
#define CISS_CDB_DIRECTION_WRITE		1
#define CISS_CDB_DIRECTION_READ			2
    u_int16_t	timeout;		/* seconds */
#define CISS_CDB_BUFFER_SIZE	16
    u_int8_t	cdb[CISS_CDB_BUFFER_SIZE];
} __packed;

struct ciss_error_info_pointer
{
    u_int64_t	error_info_address;	/* points to ciss_error_info structure */
    u_int32_t	error_info_length;
} __packed;

struct ciss_error_info
{
    u_int8_t	scsi_status;
#define CISS_SCSI_STATUS_GOOD			0x00	/* these are scsi-standard values */
#define CISS_SCSI_STATUS_CHECK_CONDITION	0x02
#define CISS_SCSI_STATUS_CONDITION_MET		0x04
#define CISS_SCSI_STATUS_BUSY			0x08
#define CISS_SCSI_STATUS_INDETERMINATE		0x10
#define CISS_SCSI_STATUS_INDETERMINATE_CM	0x14
#define CISS_SCSI_STATUS_RESERVATION_CONFLICT	0x18
#define CISS_SCSI_STATUS_COMMAND_TERMINATED	0x22
#define CISS_SCSI_STATUS_QUEUE_FULL		0x28
#define CISS_SCSI_STATUS_ACA_ACTIVE		0x30
    u_int8_t	sense_length;
    u_int16_t	command_status;
#define CISS_CMD_STATUS_SUCCESS			0
#define CISS_CMD_STATUS_TARGET_STATUS		1
#define CISS_CMD_STATUS_DATA_UNDERRUN		2
#define CISS_CMD_STATUS_DATA_OVERRUN		3
#define CISS_CMD_STATUS_INVALID_COMMAND		4
#define CISS_CMD_STATUS_PROTOCOL_ERROR		5
#define CISS_CMD_STATUS_HARDWARE_ERROR		6
#define CISS_CMD_STATUS_CONNECTION_LOST		7
#define CISS_CMD_STATUS_ABORTED			8
#define CISS_CMD_STATUS_ABORT_FAILED		9
#define CISS_CMD_STATUS_UNSOLICITED_ABORT	10
#define CISS_CMD_STATUS_TIMEOUT			11
#define CISS_CMD_STATUS_UNABORTABLE		12
    u_int32_t	residual_count;
    union {
	struct {
	    u_int8_t	res1[3];
	    u_int8_t	type;
	    u_int32_t	error_info;
	} __packed common_info;
	struct {
	    u_int8_t	res1[2];
	    u_int8_t	offense_size;
	    u_int8_t	offense_offset;
	    u_int32_t	offense_value;
	} __packed invalid_command;
    } additional_error_info;
    u_int8_t	sense_info[0];
} __packed;

struct ciss_sg_entry
{
    u_int64_t	address;
#define CISS_SG_ADDRESS_BITBUCKET	(~(u_int64_t)0)
    u_int32_t	length;
    u_int32_t	:31;
    u_int32_t	extension:1;		/* address points to another s/g chain */
} __packed;

struct ciss_command
{
    struct ciss_header			header;
    struct ciss_cdb			cdb;
    struct ciss_error_info_pointer	error_info;
    struct ciss_sg_entry		sg[0];
} __packed;

#define CISS_OPCODE_REPORT_LOGICAL_LUNS		0xc2
#define CISS_OPCODE_REPORT_PHYSICAL_LUNS	0xc3

struct ciss_lun_report
{
    u_int32_t	list_size;		/* big-endian */
    u_int32_t	:32;
    union ciss_device_address lun[0];
} __packed;

#define	CISS_VPD_LOGICAL_DRIVE_GEOMETRY		0xc1
struct ciss_ldrive_geometry
{
    u_int8_t	periph_qualifier:3;
    u_int8_t	periph_devtype:5;
    u_int8_t	page_code;
    u_int8_t	res1;
    u_int8_t	page_length;
    u_int16_t	cylinders;		/* big-endian */
    u_int8_t	heads;
    u_int8_t	sectors;
    u_int8_t	fault_tolerance;
    u_int8_t	res2[3];
} __attribute__ ((packed));

struct ciss_report_cdb
{
    u_int8_t	opcode;
    u_int8_t	reserved[5];
    u_int32_t	length;			/* big-endian */
    u_int8_t	:8;
    u_int8_t	control;
} __packed;

/*
 * Note that it's not clear whether we have to set the detail field to
 * the tag of the command to be aborted, or the tag field in the command itself;
 * documentation conflicts on this.
 */
#define CISS_OPCODE_MESSAGE_ABORT		0x00
#define CISS_MESSAGE_ABORT_TASK			0x00
#define CISS_MESSAGE_ABORT_TASK_SET		0x01
#define CISS_MESSAGE_ABORT_CLEAR_ACA		0x02
#define CISS_MESSAGE_ABORT_CLEAR_TASK_SET	0x03

#define CISS_OPCODE_MESSAGE_RESET		0x01
#define CISS_MESSAGE_RESET_CONTROLLER		0x00
#define CISS_MESSAGE_RESET_BUS			0x01
#define CISS_MESSAGE_RESET_TARGET		0x03
#define CISS_MESSAGE_RESET_LOGICAL_UNIT		0x04

#define CISS_OPCODE_MESSAGE_SCAN		0x02
#define CISS_MESSAGE_SCAN_CONTROLLER		0x00
#define CISS_MESSAGE_SCAN_BUS			0x01
#define CISS_MESSAGE_SCAN_TARGET		0x03
#define CISS_MESSAGE_SCAN_LOGICAL_UNIT		0x04

#define CISS_OPCODE_MESSAGE_NOP			0x03

struct ciss_message_cdb
{
    u_int8_t	opcode;
    u_int8_t	type;
    u_int16_t	:16;
    u_int32_t	abort_tag;					/* XXX endianness? */
    u_int8_t	reserved[8];
} __packed;

/*
 * CISS vendor-specific commands/messages.
 *
 * Note that while messages and vendor-specific commands are
 * differentiated, they are handled in basically the same way and can
 * be considered to be basically the same thing, as long as the cdb
 * type field is set correctly.
 */
#define CISS_OPCODE_READ		0xc0
#define CISS_OPCODE_WRITE		0xc1
#define CISS_COMMAND_NOTIFY_ON_EVENT	0xd0
#define CISS_COMMAND_ABORT_NOTIFY	0xd1

struct ciss_notify_cdb
{
    u_int8_t	opcode;
    u_int8_t	command;
    u_int8_t	res1[2];
    u_int16_t	timeout;		/* seconds, little-endian */
    u_int8_t	res2;			/* reserved */
    u_int8_t	synchronous:1;		/* return immediately */
    u_int8_t	ordered:1;		/* return events in recorded order */
    u_int8_t	seek_to_oldest:1;	/* reset read counter to oldest event */
    u_int8_t	new_only:1;		/* ignore any queued events */
    u_int8_t	:4;
    u_int32_t	length;			/* must be 512, little-endian */
#define CISS_NOTIFY_DATA_SIZE	512
    u_int8_t	control;
} __packed;

#define CISS_NOTIFY_NOTIFIER		0
#define CISS_NOTIFY_NOTIFIER_STATUS		0
#define CISS_NOTIFY_NOTIFIER_PROTOCOL		1

#define CISS_NOTIFY_HOTPLUG		1
#define CISS_NOTIFY_HOTPLUG_PHYSICAL		0
#define CISS_NOTIFY_HOTPLUG_POWERSUPPLY		1
#define CISS_NOTIFY_HOTPLUG_FAN			2
#define CISS_NOTIFY_HOTPLUG_POWER		3
#define CISS_NOTIFY_HOTPLUG_REDUNDANT		4
#define CISS_NOTIFY_HOTPLUG_NONDISK		5

#define CISS_NOTIFY_HARDWARE		2
#define CISS_NOTIFY_HARDWARE_CABLES		0
#define CISS_NOTIFY_HARDWARE_MEMORY		1
#define CISS_NOTIFY_HARDWARE_FAN		2
#define CISS_NOTIFY_HARDWARE_VRM		3

#define CISS_NOTIFY_ENVIRONMENT		3
#define CISS_NOTIFY_ENVIRONMENT_TEMPERATURE	0
#define CISS_NOTIFY_ENVIRONMENT_POWERSUPPLY	1
#define CISS_NOTIFY_ENVIRONMENT_CHASSIS		2
#define CISS_NOTIFY_ENVIRONMENT_POWER		3

#define CISS_NOTIFY_PHYSICAL		4
#define CISS_NOTIFY_PHYSICAL_STATE		0

#define CISS_NOTIFY_LOGICAL		5
#define CISS_NOTIFY_LOGICAL_STATUS		0
#define CISS_NOTIFY_LOGICAL_ERROR		1
#define CISS_NOTIFY_LOGICAL_SURFACE		2

#define CISS_NOTIFY_REDUNDANT		6
#define CISS_NOTIFY_REDUNDANT_STATUS		0

#define CISS_NOTIFY_CISS		8
#define CISS_NOTIFY_CISS_REDUNDANT_CHANGE	0
#define CISS_NOTIFY_CISS_PATH_STATUS		1
#define CISS_NOTIFY_CISS_HARDWARE_ERROR		2
#define CISS_NOTIFY_CISS_LOGICAL		3

struct ciss_notify_drive
{
    u_int16_t	physical_drive_number;
    u_int8_t	configured_drive_flag;
    u_int8_t	spare_drive_flag;
    u_int8_t	big_physical_drive_number;
    u_int8_t	enclosure_bay_number;
} __packed;

struct ciss_notify_locator
{
    u_int16_t	port;
    u_int16_t	id;
    u_int16_t	box;
} __packed;

struct ciss_notify_redundant_controller
{
    u_int16_t	slot;
} __packed;

struct ciss_notify_logical_status
{
    u_int16_t	logical_drive;
    u_int8_t	previous_state;
    u_int8_t	new_state;
    u_int8_t	spare_state;
} __packed;

struct ciss_notify_rebuild_aborted
{
    u_int16_t	logical_drive;
    u_int8_t	replacement_drive;
    u_int8_t	error_drive;
    u_int8_t	big_replacement_drive;
    u_int8_t	big_error_drive;
} __packed;

struct ciss_notify_io_error
{
    u_int16_t	logical_drive;
    u_int32_t	lba;
    u_int16_t	block_count;
    u_int8_t	command;
    u_int8_t	failure_bus;
    u_int8_t	failure_drive;
    u_int64_t	big_lba;
} __packed;

struct ciss_notify_consistency_completed
{
    u_int16_t	logical_drive;
} __packed;

struct ciss_notify
{
    u_int32_t	timestamp;		/* seconds since controller power-on */
    u_int16_t	class;
    u_int16_t	subclass;
    u_int16_t	detail;
    union
    {
	struct ciss_notify_drive		drive;
	struct ciss_notify_locator		location;
	struct ciss_notify_redundant_controller	redundant_controller;
	struct ciss_notify_logical_status	logical_status;
	struct ciss_notify_rebuild_aborted	rebuild_aborted;
	struct ciss_notify_io_error		io_error;
	struct ciss_notify_consistency_completed consistency_completed;
	u_int8_t	data[64];
    } data;
    char	message[80];
    u_int32_t	tag;
    u_int16_t	date;
    u_int16_t	year;
    u_int32_t	time;
    u_int16_t	pre_power_up_time;
    union ciss_device_address	device;
    /* XXX pads to 512 bytes */
} __packed;

/*
 * CISS config table, which describes the controller's
 * supported interface(s) and capabilities.
 *
 * This is mapped directly via PCI.
 */
struct ciss_config_table
{
    char	signature[4];		/* "CISS" */
    u_int32_t	valence;
    u_int32_t	supported_methods;
#define CISS_TRANSPORT_METHOD_READY	(1<<0)
#define CISS_TRANSPORT_METHOD_SIMPLE	(1<<1)
#define CISS_TRANSPORT_METHOD_PERF	(1<<2)
    u_int32_t	active_method;
    u_int32_t	requested_method;
    u_int32_t	command_physlimit;
    u_int32_t	interrupt_coalesce_delay;
    u_int32_t	interrupt_coalesce_count;
    u_int32_t	max_outstanding_commands;
    u_int32_t	bus_types;
#define CISS_TRANSPORT_BUS_TYPE_ULTRA2	(1<<0)
#define CISS_TRANSPORT_BUS_TYPE_ULTRA3	(1<<1)
#define CISS_TRANSPORT_BUS_TYPE_FIBRE1	(1<<8)
#define CISS_TRANSPORT_BUS_TYPE_FIBRE2	(1<<9)
    u_int32_t	transport_offset;
    char	server_name[16];
    u_int32_t	heartbeat;
    u_int32_t	host_driver;
#define CISS_DRIVER_SUPPORT_UNIT_ATTENTION	(1<<0)
#define CISS_DRIVER_QUICK_INIT			(1<<1)
#define CISS_DRIVER_INTERRUPT_ON_LOCKUP		(1<<2)
#define CISS_DRIVER_SUPPORT_MIXED_Q_TAGS	(1<<3)
#define CISS_DRIVER_HOST_IS_ALPHA		(1<<4)
#define CISS_DRIVER_MULTI_LUN_SUPPORT		(1<<5)
#define CISS_DRIVER_MESSAGE_REQUESTS_SUPPORTED	(1<<7)
#define CISS_DRIVER_DAUGHTER_ATTACHED		(1<<8)
#define CISS_DRIVER_SCSI_PREFETCH		(1<<9)
    u_int32_t	max_sg_length;		/* 31 in older firmware */
/*
 * these fields appear in OpenCISS Spec 1.06
 * http://cciss.sourceforge.net/#docs
 */
    u_int32_t	max_logical_supported;
    u_int32_t	max_physical_supported;
    u_int32_t	max_physical_per_logical;
    u_int32_t	max_perfomant_mode_cmds;
    u_int32_t	max_block_fetch_count;
} __packed;

/*
 * Configuration table for the Performant transport.  Only 4 request queues
 * are mentioned in this table, though apparently up to 256 can exist.
 */
struct ciss_perf_config {
    uint32_t	fetch_count[8];
#define CISS_SG_FETCH_MAX	0
#define CISS_SG_FETCH_1		1
#define CISS_SG_FETCH_2		2
#define CISS_SG_FETCH_4		3
#define CISS_SG_FETCH_8		4
#define CISS_SG_FETCH_16	5
#define CISS_SG_FETCH_32	6
#define CISS_SG_FETCH_NONE	7
    uint32_t	rq_size;
    uint32_t	rq_count;
    uint32_t	rq_bank_lo;
    uint32_t	rq_bank_hi;
    struct {
	uint32_t	rq_addr_lo;
	uint32_t	rq_addr_hi;
    } __packed rq[4];
} __packed;

/*
 * In a flagrant violation of what CISS seems to be meant to be about,
 * Compaq recycle a goodly portion of their previous generation's
 * command set (and all the legacy baggage related to a design
 * originally aimed at narrow SCSI) through the Array Controller Read
 * and Array Controller Write interface.
 *
 * Command ID values here can be looked up for in the
 * publically-available documentation for the older controllers; note
 * that the command layout is necessarily different to fit within the
 * CDB.
 */
#define CISS_ARRAY_CONTROLLER_READ	0x26
#define CISS_ARRAY_CONTROLLER_WRITE	0x27

#define CISS_BMIC_ID_LDRIVE		0x10
#define CISS_BMIC_ID_CTLR		0x11
#define CISS_BMIC_ID_LSTATUS		0x12
#define CISS_BMIC_ID_PDRIVE		0x15
#define CISS_BMIC_BLINK_PDRIVE		0x16
#define CISS_BMIC_SENSE_BLINK_PDRIVE	0x17
#define CISS_BMIC_SOFT_RESET		0x40
#define CISS_BMIC_FLUSH_CACHE		0xc2
#define CISS_BMIC_ACCEPT_MEDIA		0xe0

/*
 * When numbering drives, the original design assumed that
 * drives 0-7 are on the first SCSI bus, 8-15 on the second,
 * and so forth.  In order to handle modern SCSI configurations,
 * the MSB is set in the drive ID field, in which case the
 * modulus changes from 8 to the number of supported drives
 * per SCSI bus (as obtained from the ID_CTLR command).
 * This feature is referred to as BIG_MAP support, and we assume
 * that all CISS controllers support it.
 */

#define CISS_BIG_MAP_ID(sc, bus, target)		\
	(0x80 | 					\
	 ((sc)->ciss_id->drives_per_scsi_bus * (bus)) |	\
	 (target))

#define CISS_BIG_MAP_BUS(sc, id)			\
	(((id) & 0x80) ? (((id) & ~0x80) / (sc)->ciss_id->drives_per_scsi_bus) : -1)

#define CISS_BIG_MAP_TARGET(sc, id)			\
	(((id) & 0x80) ? (((id) & ~0x80) % (sc)->ciss_id->drives_per_scsi_bus) : -1)

#define CISS_BIG_MAP_ENTRIES	128	/* number of entries in a BIG_MAP */

/*
 * In the device address of a logical volume, the bus number
 * is encoded into the logical lun volume number starting
 * at the second byte, with the first byte defining the
 * logical drive number.
 */
#define CISS_LUN_TO_BUS(x)    (((x) >> 16) & 0xFF)
#define CISS_LUN_TO_TARGET(x) ((x) & 0xFF)

/*
 * BMIC CDB
 *
 * Note that the phys_drive/res1 field is nominally the 32-bit
 * "block number" field, but the only BMIC command(s) of interest
 * implemented overload the MSB (note big-endian format here)
 * to be the physical drive ID, so we define accordingly.
 */
struct ciss_bmic_cdb {
    u_int8_t	opcode;
    u_int8_t	log_drive;
    u_int8_t	phys_drive;
    u_int8_t	res1[3];
    u_int8_t	bmic_opcode;
    u_int16_t	size;			/* big-endian */
    u_int8_t	res2;
} __packed;

/*
 * BMIC command command/return structures.
 */

/* CISS_BMIC_ID_LDRIVE */
struct ciss_bmic_id_ldrive {
    u_int16_t	block_size;
    u_int32_t	blocks_available;
    u_int8_t	drive_parameter_table[16];	/* XXX define */
    u_int8_t	fault_tolerance;
#define CISS_LDRIVE_RAID0	0
#define CISS_LDRIVE_RAID4	1
#define CISS_LDRIVE_RAID1	2
#define CISS_LDRIVE_RAID5	3
#define CISS_LDRIVE_RAID51	4
#define CISS_LDRIVE_RAIDADG	5
    u_int8_t	res1;
    u_int8_t	bios_disable_flag;
    u_int8_t	res2;
    u_int32_t	logical_drive_identifier;
    char	logical_drive_label[64];
    u_int64_t	big_blocks_available;
    u_int8_t	res3[410];
} __packed;

/* CISS_BMIC_ID_LSTATUS */
struct ciss_bmic_id_lstatus {
    u_int8_t	status;
#define CISS_LSTATUS_OK				0
#define CISS_LSTATUS_FAILED			1
#define CISS_LSTATUS_NOT_CONFIGURED		2
#define CISS_LSTATUS_INTERIM_RECOVERY		3
#define CISS_LSTATUS_READY_RECOVERY		4
#define CISS_LSTATUS_RECOVERING			5
#define CISS_LSTATUS_WRONG_PDRIVE		6
#define CISS_LSTATUS_MISSING_PDRIVE		7
#define CISS_LSTATUS_EXPANDING			10
#define CISS_LSTATUS_BECOMING_READY		11
#define CISS_LSTATUS_QUEUED_FOR_EXPANSION	12
    u_int32_t	deprecated_drive_failure_map;
    u_int8_t	res1[416];
    u_int32_t	blocks_to_recover;
    u_int8_t	deprecated_drive_rebuilding;
    u_int16_t	deprecated_remap_count[32];
    u_int32_t	deprecated_replacement_map;
    u_int32_t	deprecated_active_spare_map;
    u_int8_t	spare_configured:1;
    u_int8_t	spare_rebuilding:1;
    u_int8_t	spare_rebuilt:1;
    u_int8_t	spare_failed:1;
    u_int8_t	spare_switched:1;
    u_int8_t	spare_available:1;
    u_int8_t	res2:2;
    u_int8_t	deprecated_spare_to_replace_map[32];
    u_int32_t	deprecated_replaced_marked_ok_map;
    u_int8_t	media_exchanged;
    u_int8_t	cache_failure;
    u_int8_t	expand_failure;
    u_int8_t	rebuild_read_failure:1;
    u_int8_t	rebuild_write_failure:1;
    u_int8_t	res3:6;
    u_int8_t	drive_failure_map[CISS_BIG_MAP_ENTRIES / 8];
    u_int16_t	remap_count[CISS_BIG_MAP_ENTRIES];
    u_int8_t	replacement_map[CISS_BIG_MAP_ENTRIES / 8];
    u_int8_t	active_spare_map[CISS_BIG_MAP_ENTRIES / 8];
    u_int8_t	spare_to_replace_map[CISS_BIG_MAP_ENTRIES];
    u_int8_t	replaced_marked_ok_map[CISS_BIG_MAP_ENTRIES / 8];
    u_int8_t	drive_rebuilding;
    u_int64_t	big_blocks_to_recover;
    u_int8_t	res4[28];
} __packed;

/* CISS_BMIC_ID_CTLR */
struct ciss_bmic_id_table {
    u_int8_t	configured_logical_drives;
    u_int32_t	config_signature;
    char	running_firmware_revision[4];
    char	stored_firmware_revision[4];
    u_int8_t	hardware_revision;
    u_int8_t	boot_block_revision[4];
    u_int32_t	deprecated_drive_present_map;
    u_int32_t	deprecated_external_drive_present_map;
    u_int32_t	board_id;
    u_int8_t	swapped_error_cable;
    u_int32_t	deprecated_non_disk_map;
    u_int8_t	bad_host_ram_addr;
    u_int8_t	cpu_revision;
    u_int8_t	res3[3];
    char	marketting_revision;
    u_int8_t	controller_flags;
#define	CONTROLLER_FLAGS_FLASH_ROM_INSTALLED	0x01
#define	CONTROLLER_FLAGS_DIAGS_MODE_BIT		0x02
#define	CONTROLLER_FLAGS_EXPAND_32MB_FX 	0x04
#define	CONTROLLER_FLAGS_MORE_THAN_7_SUPPORT 	0x08
#define	CONTROLLER_FLAGS_DAISY_SUPPORT_BIT 	0x10
#define	CONTROLLER_FLAGS_RES6 			0x20
#define	CONTROLLER_FLAGS_RES7 			0x40
#define	CONTROLLER_FLAGS_BIG_MAP_SUPPORT 	0x80
    u_int8_t	host_flags;
#define HOST_FLAGS_SDB_ASIC_WORK_AROUND 	0x01
#define HOST_FLAGS_PCI_DATA_BUS_PARITY_SUPPORT	0x02
#define HOST_FLAGS_RES3				0x04
#define HOST_FLAGS_RES4				0x08
#define HOST_FLAGS_RES5				0x10
#define HOST_FLAGS_RES6				0x20
#define HOST_FLAGS_RES7				0x30
#define HOST_FLAGS_RES8				0x40
    u_int8_t	expand_disable_code;
#define EXPAND_DISABLE_NOT_NEEDED		0x01
#define EXPAND_DISABLE_MISSING_CACHE_BOARD	0x02
#define EXPAND_DISABLE_WCXC_FATAL_CACHE_BITS	0x04
#define EXPAND_DISABLE_CACHE_PERM_DISABLED	0x08
#define EXPAND_DISABLE_RAM_ALLOCATION_FAILED	0x10
#define EXPAND_DISABLE_BATTEREIS_DISCHARGED	0x20
#define EXPAND_DISABLE_RES7			0x40
#define EXPAND_DISABLE_REBUILD_RUNNING		0x80
    u_int8_t	scsi_chip_count;
    u_int32_t	maximum_blocks;
    u_int32_t	controller_clock;
    u_int8_t	drives_per_scsi_bus;
    u_int8_t	big_drive_present_map[CISS_BIG_MAP_ENTRIES / 8];
    u_int8_t	big_external_drive_present_map[CISS_BIG_MAP_ENTRIES / 8];
    u_int8_t	big_non_disk_map[CISS_BIG_MAP_ENTRIES / 8];

    u_int16_t	task_flags;		/* used for FW debugging */
    u_int8_t	ICL_bus_map;		/* Bitmap used for ICL between controllers */
    u_int8_t	redund_ctlr_modes_support;	/* See REDUNDANT MODE VALUES */
    u_int8_t	curr_redund_ctlr_mode;
    u_int8_t	redund_ctlr_status;
    u_int8_t	redund_op_failure_code;

    u_int8_t	unsupported_nile_bus;
    u_int8_t	host_i2c_autorev;
    u_int8_t	cpld_revision;
    u_int8_t	fibre_chip_count;
    u_int8_t	daughterboard_type;
    u_int8_t	more_swapped_config_cable_error;

    u_int8_t	license_key_status;
    u_int8_t	access_module_status;
    u_int8_t	features_supported[12];
    u_int8_t	rec_rom_inact_rev[4];    /* Recovery ROM inactive f/w revision  */
    u_int8_t	rec_rom_act_status;      /* Recovery ROM flags                  */
    u_int8_t	pci_to_pci_status;       /* PCI to PCI bridge status            */
    u_int32_t	redundant_server_info;   /* Reserved for future use             */
    u_int8_t	percent_write_cache;     /* Percent of memory allocated to write cache */
    u_int16_t	daughterboard_size_mb;   /* Total size (MB) of cache board      */
    u_int8_t	cache_batter_count;      /* Number of cache batteries           */
    u_int16_t	total_controller_mem_mb; /* Total size (MB) of atttached memory */
    u_int8_t	more_controller_flags;   /* Additional controller flags byte    */
    u_int8_t	x_board_host_i2c_rev;    /* 2nd byte of 3 byte autorev field    */
    u_int8_t	battery_pic_rev;         /* BBWC PIC revision                   */
/*
 * Below here I have no documentation on the rest of this data structure.  It is
 * inferred from the opensource cciss_vol_status application.  I assume that this 
 * data strucutre is 512 bytes in total size, do not exceed it.
 */
    u_int8_t	bDdffVersion[4];         /* DDFF update engine version          */
    u_int16_t	usMaxLogicalUnits;       /* Maximum logical units supported */
    u_int16_t	usExtLogicalUnitCount;   /* Big num configured logical units */
    u_int16_t	usMaxPhysicalDevices;    /* Maximum physical devices supported */
    u_int16_t	usMaxPhyDrvPerLogicalUnit; /* Max physical drive per logical unit */
    u_int8_t	bEnclosureCount;         /* Number of attached enclosures */
    u_int8_t	bExpanderCount;          /* Number of expanders detected */
    u_int16_t	usOffsetToEDPbitmap;     /* Offset to extended drive present map*/
    u_int16_t	usOffsetToEEDPbitmap;    /* Offset to extended external drive present map */
    u_int16_t	usOffsetToENDbitmap;     /* Offset to extended non-disk map */
    u_int8_t	bInternalPortStatus[8];  /* Internal port status bytes */
    u_int8_t	bExternalPortStatus[8];  /* External port status bytes */
    u_int32_t	uiYetMoreControllerFlags;/* Yet More Controller flags  */
#define YMORE_CONTROLLER_FLAGS_JBOD_SUPPORTED \
	( 1 << 25 )			 /* Controller has JBOD support */

    u_int8_t	bLastLockup;              /* Last lockup code */
    u_int8_t	bSlot;                    /* PCI slot according to option ROM*/
    u_int16_t	usBuildNum;               /* Build number */
    u_int32_t	uiMaxSafeFullStripeSize;  /* Maximum safe full stripe size */
    u_int32_t	uiTotalLength;            /* Total structure length */
    u_int8_t	bVendorID[8];             /* Vendor ID */
    u_int8_t	bProductID[16];           /* Product ID */
/*
 * These are even more obscure as they seem to only be available in cciss_vol_status
 */
    u_int32_t	ExtendedLastLockupCode;
    u_int16_t	MaxRaid;
    u_int16_t	MaxParity;
    u_int16_t	MaxADGStripSize;
    u_int16_t	YetMoreSwappedCables;
    u_int8_t	MaxDevicePaths;
    u_int8_t	PowerUPNvramFlags;
#define PWR_UP_FLAG_JBOD_ENABLED	0x08	/*JBOD mode is enabled, all RAID features off */

    u_int16_t	ZonedOffset;
    u_int32_t   FixedFieldsLength;
    u_int8_t	FWCompileTimeStamp[24];
    u_int32_t	EvenMoreControllerFlags;
    u_int8_t	padding[240];
} __packed;

/* CISS_BMIC_ID_PDRIVE */
struct ciss_bmic_id_pdrive {
    u_int8_t	scsi_bus;
    u_int8_t	scsi_id;
    u_int16_t	block_size;
    u_int32_t	total_blocks;
    u_int32_t	reserved_blocks;
    char	model[40];
    char	serial[40];
    char	revision[8];
    u_int8_t	inquiry_bits;
    u_int8_t	res1[2];
    u_int8_t	drive_present:1;
    u_int8_t	non_disk:1;
    u_int8_t	wide:1;
    u_int8_t	synchronous:1;
    u_int8_t	narrow:1;
    u_int8_t	wide_downgraded_to_narrow:1;
    u_int8_t	ultra:1;
    u_int8_t	ultra2:1;
    u_int8_t	SMART:1;
    u_int8_t	SMART_errors_recorded:1;
    u_int8_t	SMART_errors_enabled:1;
    u_int8_t	SMART_errors_detected:1;
    u_int8_t	external:1;
    u_int8_t	configured:1;
    u_int8_t	configured_spare:1;
    u_int8_t	cache_saved_enabled:1;
    u_int8_t	res2;
    u_int8_t	res3:6;
    u_int8_t	cache_currently_enabled:1;
    u_int8_t	cache_safe:1;
    u_int8_t	res4[5];
    char	connector[2];
    u_int8_t	res5;
    u_int8_t	bay;
    u_int16_t	rpm;
    u_int8_t	drive_type;
    u_int8_t	res6[393];
} __packed;

/* CISS_BMIC_BLINK_PDRIVE */
/* CISS_BMIC_SENSE_BLINK_PDRIVE */
struct ciss_bmic_blink_pdrive {
    u_int32_t	blink_duration;		/* 10ths of a second */
    u_int32_t	duration_elapsed;	/* only for sense command  */
    u_int8_t	blinktab[256];
#define CISS_BMIC_BLINK_ALL	1
#define CISS_BMIC_BLINK_TIMED	2
    u_int8_t	res2[248];
} __packed;

/* CISS_BMIC_FLUSH_CACHE */
struct ciss_bmic_flush_cache {
    u_int16_t	flag;
#define CISS_BMIC_FLUSH_AND_ENABLE	0
#define CISS_BMIC_FLUSH_AND_DISABLE	1
    u_int8_t	res1[510];
} __packed;

#ifdef _KERNEL
/*
 * CISS "simple" transport layer.
 *
 * Note that there are two slightly different versions of this interface
 * with different interrupt mask bits.  There's nothing like consistency...
 */
#define CISS_TL_SIMPLE_BAR_REGS	0x10	/* BAR pointing to register space */
#define CISS_TL_SIMPLE_BAR_CFG	0x14	/* BAR pointing to space containing config table */

#define CISS_TL_SIMPLE_IDBR	0x20	/* inbound doorbell register */
#define CISS_TL_SIMPLE_IDBR_CFG_TABLE	(1<<0)	/* notify controller of config table update */

#define CISS_TL_SIMPLE_ISR	0x30	/* interrupt status register */
#define CISS_TL_SIMPLE_IMR	0x34	/* interrupt mask register */
#define CISS_TL_SIMPLE_INTR_OPQ_SA5	(1<<3)	/* OPQ not empty interrupt, SA5 boards */
#define CISS_TL_SIMPLE_INTR_OPQ_SA5B	(1<<2)	/* OPQ not empty interrupt, SA5B boards */

#define CISS_TL_SIMPLE_IPQ	0x40	/* inbound post queue */
#define CISS_TL_SIMPLE_OPQ	0x44	/* outbound post queue */
#define CISS_TL_SIMPLE_OPQ_EMPTY	(~(u_int32_t)0)

#define CISS_TL_SIMPLE_OSR	0x9c	/* outbound status register */
#define CISS_TL_SIMPLE_ODC	0xa0	/* outbound doorbell clear register */
#define CISS_TL_SIMPLE_ODC_CLEAR	(0x1)

#define CISS_TL_SIMPLE_CFG_BAR	0xb4	/* should be 0x14 */
#define CISS_TL_SIMPLE_CFG_OFF	0xb8	/* offset in BAR at which config table is located */

/*
 * Register access primitives.
 */
#define CISS_TL_SIMPLE_READ(sc, ofs) \
	bus_space_read_4(sc->ciss_regs_btag, sc->ciss_regs_bhandle, ofs)
#define CISS_TL_SIMPLE_WRITE(sc, ofs, val) \
	bus_space_write_4(sc->ciss_regs_btag, sc->ciss_regs_bhandle, ofs, val)

#define CISS_TL_SIMPLE_POST_CMD(sc, phys)	CISS_TL_SIMPLE_WRITE(sc, CISS_TL_SIMPLE_IPQ, phys)
#define CISS_TL_SIMPLE_FETCH_CMD(sc)		CISS_TL_SIMPLE_READ(sc, CISS_TL_SIMPLE_OPQ)

#define CISS_TL_PERF_INTR_OPQ	(CISS_TL_SIMPLE_INTR_OPQ_SA5 | CISS_TL_SIMPLE_INTR_OPQ_SA5B)
#define CISS_TL_PERF_INTR_MSI	0x01

#define CISS_TL_PERF_POST_CMD(sc, cr)		CISS_TL_SIMPLE_WRITE(sc, CISS_TL_SIMPLE_IPQ, cr->cr_ccphys | (cr)->cr_sg_tag)
#define CISS_TL_PERF_FLUSH_INT(sc)		CISS_TL_SIMPLE_READ(sc, CISS_TL_SIMPLE_OSR)
#define CISS_TL_PERF_CLEAR_INT(sc)		CISS_TL_SIMPLE_WRITE(sc, CISS_TL_SIMPLE_ODC, CISS_TL_SIMPLE_ODC_CLEAR)
#define CISS_CYCLE_MASK		0x00000001

/* Only need one MSI/MSI-X vector */
#define CISS_MSI_COUNT	1

#define CISS_TL_SIMPLE_DISABLE_INTERRUPTS(sc) \
	CISS_TL_SIMPLE_WRITE(sc, CISS_TL_SIMPLE_IMR, \
			     CISS_TL_SIMPLE_READ(sc, CISS_TL_SIMPLE_IMR) | (sc)->ciss_interrupt_mask)
#define CISS_TL_SIMPLE_ENABLE_INTERRUPTS(sc) \
	CISS_TL_SIMPLE_WRITE(sc, CISS_TL_SIMPLE_IMR, \
			     CISS_TL_SIMPLE_READ(sc, CISS_TL_SIMPLE_IMR) & ~(sc)->ciss_interrupt_mask)



#endif /* _KERNEL */
