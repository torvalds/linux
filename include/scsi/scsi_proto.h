/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This header file contains public constants and structures used by
 * both the SCSI initiator and the SCSI target code.
 *
 * For documentation on the OPCODES, MESSAGES, and SENSE values,
 * please consult the SCSI standard.
 */

#ifndef _SCSI_PROTO_H_
#define _SCSI_PROTO_H_

#include <linux/build_bug.h>
#include <linux/types.h>

/*
 *      SCSI opcodes
 */

#define TEST_UNIT_READY       0x00
#define REZERO_UNIT           0x01
#define REQUEST_SENSE         0x03
#define FORMAT_UNIT           0x04
#define READ_BLOCK_LIMITS     0x05
#define REASSIGN_BLOCKS       0x07
#define INITIALIZE_ELEMENT_STATUS 0x07
#define READ_6                0x08
#define WRITE_6               0x0a
#define SEEK_6                0x0b
#define READ_REVERSE          0x0f
#define WRITE_FILEMARKS       0x10
#define SPACE                 0x11
#define INQUIRY               0x12
#define RECOVER_BUFFERED_DATA 0x14
#define MODE_SELECT           0x15
#define RESERVE_6             0x16
#define RELEASE_6             0x17
#define COPY                  0x18
#define ERASE                 0x19
#define MODE_SENSE            0x1a
#define START_STOP            0x1b
#define RECEIVE_DIAGNOSTIC    0x1c
#define SEND_DIAGNOSTIC       0x1d
#define ALLOW_MEDIUM_REMOVAL  0x1e

#define READ_FORMAT_CAPACITIES 0x23
#define SET_WINDOW            0x24
#define READ_CAPACITY         0x25
#define READ_10               0x28
#define WRITE_10              0x2a
#define SEEK_10               0x2b
#define POSITION_TO_ELEMENT   0x2b
#define WRITE_VERIFY          0x2e
#define VERIFY                0x2f
#define SEARCH_HIGH           0x30
#define SEARCH_EQUAL          0x31
#define SEARCH_LOW            0x32
#define SET_LIMITS            0x33
#define PRE_FETCH             0x34
#define READ_POSITION         0x34
#define SYNCHRONIZE_CACHE     0x35
#define LOCK_UNLOCK_CACHE     0x36
#define READ_DEFECT_DATA      0x37
#define MEDIUM_SCAN           0x38
#define COMPARE               0x39
#define COPY_VERIFY           0x3a
#define WRITE_BUFFER          0x3b
#define READ_BUFFER           0x3c
#define UPDATE_BLOCK          0x3d
#define READ_LONG             0x3e
#define WRITE_LONG            0x3f
#define CHANGE_DEFINITION     0x40
#define WRITE_SAME            0x41
#define UNMAP		      0x42
#define READ_TOC              0x43
#define READ_HEADER           0x44
#define GET_EVENT_STATUS_NOTIFICATION 0x4a
#define LOG_SELECT            0x4c
#define LOG_SENSE             0x4d
#define XDWRITEREAD_10        0x53
#define MODE_SELECT_10        0x55
#define RESERVE_10            0x56
#define RELEASE_10            0x57
#define MODE_SENSE_10         0x5a
#define PERSISTENT_RESERVE_IN 0x5e
#define PERSISTENT_RESERVE_OUT 0x5f
#define VARIABLE_LENGTH_CMD   0x7f
#define REPORT_LUNS           0xa0
#define SECURITY_PROTOCOL_IN  0xa2
#define MAINTENANCE_IN        0xa3
#define MAINTENANCE_OUT       0xa4
#define MOVE_MEDIUM           0xa5
#define EXCHANGE_MEDIUM       0xa6
#define READ_12               0xa8
#define SERVICE_ACTION_OUT_12 0xa9
#define WRITE_12              0xaa
#define READ_MEDIA_SERIAL_NUMBER 0xab /* Obsolete with SPC-2 */
#define SERVICE_ACTION_IN_12  0xab
#define WRITE_VERIFY_12       0xae
#define VERIFY_12	      0xaf
#define SEARCH_HIGH_12        0xb0
#define SEARCH_EQUAL_12       0xb1
#define SEARCH_LOW_12         0xb2
#define SECURITY_PROTOCOL_OUT 0xb5
#define READ_ELEMENT_STATUS   0xb8
#define SEND_VOLUME_TAG       0xb6
#define WRITE_LONG_2          0xea
#define EXTENDED_COPY         0x83
#define RECEIVE_COPY_RESULTS  0x84
#define ACCESS_CONTROL_IN     0x86
#define ACCESS_CONTROL_OUT    0x87
#define READ_16               0x88
#define COMPARE_AND_WRITE     0x89
#define WRITE_16              0x8a
#define READ_ATTRIBUTE        0x8c
#define WRITE_ATTRIBUTE	      0x8d
#define WRITE_VERIFY_16	      0x8e
#define VERIFY_16	      0x8f
#define SYNCHRONIZE_CACHE_16  0x91
#define WRITE_SAME_16	      0x93
#define ZBC_OUT		      0x94
#define ZBC_IN		      0x95
#define WRITE_ATOMIC_16	0x9c
#define SERVICE_ACTION_BIDIRECTIONAL 0x9d
#define SERVICE_ACTION_IN_16  0x9e
#define SERVICE_ACTION_OUT_16 0x9f
/* values for service action in */
#define	SAI_READ_CAPACITY_16  0x10
#define SAI_GET_LBA_STATUS    0x12
#define SAI_REPORT_REFERRALS  0x13
#define SAI_GET_STREAM_STATUS 0x16
/* values for maintenance in */
#define MI_REPORT_IDENTIFYING_INFORMATION 0x05
#define MI_REPORT_TARGET_PGS  0x0a
#define MI_REPORT_ALIASES     0x0b
#define MI_REPORT_SUPPORTED_OPERATION_CODES 0x0c
#define MI_REPORT_SUPPORTED_TASK_MANAGEMENT_FUNCTIONS 0x0d
#define MI_REPORT_PRIORITY    0x0e
#define MI_REPORT_TIMESTAMP   0x0f
#define MI_MANAGEMENT_PROTOCOL_IN 0x10
/* value for MI_REPORT_TARGET_PGS ext header */
#define MI_EXT_HDR_PARAM_FMT  0x20
/* values for maintenance out */
#define MO_SET_IDENTIFYING_INFORMATION 0x06
#define MO_SET_TARGET_PGS     0x0a
#define MO_CHANGE_ALIASES     0x0b
#define MO_SET_PRIORITY       0x0e
#define MO_SET_TIMESTAMP      0x0f
#define MO_MANAGEMENT_PROTOCOL_OUT 0x10
/* values for ZBC_IN */
#define ZI_REPORT_ZONES	      0x00
/* values for ZBC_OUT */
#define ZO_CLOSE_ZONE	      0x01
#define ZO_FINISH_ZONE	      0x02
#define ZO_OPEN_ZONE	      0x03
#define ZO_RESET_WRITE_POINTER 0x04
/* values for PR in service action */
#define READ_KEYS             0x00
#define READ_RESERVATION      0x01
#define REPORT_CAPABILITES    0x02
#define READ_FULL_STATUS      0x03
/* values for variable length command */
#define XDREAD_32	      0x03
#define XDWRITE_32	      0x04
#define XPWRITE_32	      0x06
#define XDWRITEREAD_32	      0x07
#define READ_32		      0x09
#define VERIFY_32	      0x0a
#define WRITE_32	      0x0b
#define WRITE_VERIFY_32	      0x0c
#define WRITE_SAME_32	      0x0d
#define ATA_32		      0x1ff0

/* Values for T10/04-262r7 */
#define	ATA_16		      0x85	/* 16-byte pass-thru */
#define	ATA_12		      0xa1	/* 12-byte pass-thru */

/* Vendor specific CDBs start here */
#define VENDOR_SPECIFIC_CDB 0xc0

/*
 *	SCSI command lengths
 */

#define SCSI_MAX_VARLEN_CDB_SIZE 260

/* defined in T10 SCSI Primary Commands-2 (SPC2) */
struct scsi_varlen_cdb_hdr {
	__u8 opcode;        /* opcode always == VARIABLE_LENGTH_CMD */
	__u8 control;
	__u8 misc[5];
	__u8 additional_cdb_length;         /* total cdb length - 8 */
	__be16 service_action;
	/* service specific data follows */
};

/*
 *  SCSI Architecture Model (SAM) Status codes. Taken from SAM-3 draft
 *  T10/1561-D Revision 4 Draft dated 7th November 2002.
 */
enum sam_status {
	SAM_STAT_GOOD				= 0x00,
	SAM_STAT_CHECK_CONDITION		= 0x02,
	SAM_STAT_CONDITION_MET			= 0x04,
	SAM_STAT_BUSY				= 0x08,
	SAM_STAT_INTERMEDIATE			= 0x10,
	SAM_STAT_INTERMEDIATE_CONDITION_MET	= 0x14,
	SAM_STAT_RESERVATION_CONFLICT		= 0x18,
	SAM_STAT_COMMAND_TERMINATED		= 0x22,	/* obsolete in SAM-3 */
	SAM_STAT_TASK_SET_FULL			= 0x28,
	SAM_STAT_ACA_ACTIVE			= 0x30,
	SAM_STAT_TASK_ABORTED			= 0x40,
};

#define STATUS_MASK         0xfe

/*
 *  SENSE KEYS
 */
#define NO_SENSE            0x00
#define RECOVERED_ERROR     0x01
#define NOT_READY           0x02
#define MEDIUM_ERROR        0x03
#define HARDWARE_ERROR      0x04
#define ILLEGAL_REQUEST     0x05
#define UNIT_ATTENTION      0x06
#define DATA_PROTECT        0x07
#define BLANK_CHECK         0x08
#define VENDOR_SPECIFIC     0x09
#define COPY_ABORTED        0x0a
#define ABORTED_COMMAND     0x0b
#define VOLUME_OVERFLOW     0x0d
#define MISCOMPARE          0x0e
#define COMPLETED	    0x0f

/*
 *  DEVICE TYPES
 *  Please keep them in 0x%02x format for $MODALIAS to work
 */

#define TYPE_DISK           0x00
#define TYPE_TAPE           0x01
#define TYPE_PRINTER        0x02
#define TYPE_PROCESSOR      0x03    /* HP scanners use this */
#define TYPE_WORM           0x04    /* Treated as ROM by our system */
#define TYPE_ROM            0x05
#define TYPE_SCANNER        0x06
#define TYPE_MOD            0x07    /* Magneto-optical disk -
				     * - treated as TYPE_DISK */
#define TYPE_MEDIUM_CHANGER 0x08
#define TYPE_COMM           0x09    /* Communications device */
#define TYPE_RAID           0x0c
#define TYPE_ENCLOSURE      0x0d    /* Enclosure Services Device */
#define TYPE_RBC	    0x0e
#define TYPE_OSD            0x11
#define TYPE_ZBC            0x14
#define TYPE_WLUN           0x1e    /* well-known logical unit */
#define TYPE_NO_LUN         0x7f

/* SCSI protocols; these are taken from SPC-3 section 7.5 */
enum scsi_protocol {
	SCSI_PROTOCOL_FCP = 0,	/* Fibre Channel */
	SCSI_PROTOCOL_SPI = 1,	/* parallel SCSI */
	SCSI_PROTOCOL_SSA = 2,	/* Serial Storage Architecture - Obsolete */
	SCSI_PROTOCOL_SBP = 3,	/* firewire */
	SCSI_PROTOCOL_SRP = 4,	/* Infiniband RDMA */
	SCSI_PROTOCOL_ISCSI = 5,
	SCSI_PROTOCOL_SAS = 6,
	SCSI_PROTOCOL_ADT = 7,	/* Media Changers */
	SCSI_PROTOCOL_ATA = 8,
	SCSI_PROTOCOL_UNSPEC = 0xf, /* No specific protocol */
};

/*
 * ScsiLun: 8 byte LUN.
 */
struct scsi_lun {
	__u8 scsi_lun[8];
};

/* SBC-5 IO advice hints group descriptor */
struct scsi_io_group_descriptor {
#if defined(__BIG_ENDIAN)
	u8 io_advice_hints_mode: 2;
	u8 reserved1: 3;
	u8 st_enble: 1;
	u8 cs_enble: 1;
	u8 ic_enable: 1;
#elif defined(__LITTLE_ENDIAN)
	u8 ic_enable: 1;
	u8 cs_enble: 1;
	u8 st_enble: 1;
	u8 reserved1: 3;
	u8 io_advice_hints_mode: 2;
#else
#error
#endif
	u8 reserved2[3];
	/* Logical block markup descriptor */
#if defined(__BIG_ENDIAN)
	u8 acdlu: 1;
	u8 reserved3: 1;
	u8 rlbsr: 2;
	u8 lbm_descriptor_type: 4;
#elif defined(__LITTLE_ENDIAN)
	u8 lbm_descriptor_type: 4;
	u8 rlbsr: 2;
	u8 reserved3: 1;
	u8 acdlu: 1;
#else
#error
#endif
	u8 params[2];
	u8 reserved4;
	u8 reserved5[8];
};

static_assert(sizeof(struct scsi_io_group_descriptor) == 16);

/* SCSI stream status descriptor */
struct scsi_stream_status {
#if defined(__BIG_ENDIAN)
	u8 perm: 1;
	u8 reserved1: 7;
#elif defined(__LITTLE_ENDIAN)
	u8 reserved1: 7;
	u8 perm: 1;
#else
#error
#endif
	u8 reserved2;
	__be16 stream_identifier;
#if defined(__BIG_ENDIAN)
	u8 reserved3: 2;
	u8 rel_lifetime: 6;
#elif defined(__LITTLE_ENDIAN)
	u8 rel_lifetime: 6;
	u8 reserved3: 2;
#else
#error
#endif
	u8 reserved4[3];
};

static_assert(sizeof(struct scsi_stream_status) == 8);

/* GET STREAM STATUS parameter data */
struct scsi_stream_status_header {
	__be32 len;	/* length in bytes of following payload */
	u16 reserved;
	__be16 number_of_open_streams;
};

static_assert(sizeof(struct scsi_stream_status_header) == 8);

/* SPC asymmetric access states */
#define SCSI_ACCESS_STATE_OPTIMAL     0x00
#define SCSI_ACCESS_STATE_ACTIVE      0x01
#define SCSI_ACCESS_STATE_STANDBY     0x02
#define SCSI_ACCESS_STATE_UNAVAILABLE 0x03
#define SCSI_ACCESS_STATE_LBA         0x04
#define SCSI_ACCESS_STATE_OFFLINE     0x0e
#define SCSI_ACCESS_STATE_TRANSITIONING 0x0f

/* Values for REPORT TARGET GROUP STATES */
#define SCSI_ACCESS_STATE_MASK        0x0f
#define SCSI_ACCESS_STATE_PREFERRED   0x80

/* Reporting options for REPORT ZONES */
enum zbc_zone_reporting_options {
	ZBC_ZONE_REPORTING_OPTION_ALL		= 0x00,
	ZBC_ZONE_REPORTING_OPTION_EMPTY		= 0x01,
	ZBC_ZONE_REPORTING_OPTION_IMPLICIT_OPEN	= 0x02,
	ZBC_ZONE_REPORTING_OPTION_EXPLICIT_OPEN	= 0x03,
	ZBC_ZONE_REPORTING_OPTION_CLOSED	= 0x04,
	ZBC_ZONE_REPORTING_OPTION_FULL		= 0x05,
	ZBC_ZONE_REPORTING_OPTION_READONLY	= 0x06,
	ZBC_ZONE_REPORTING_OPTION_OFFLINE	= 0x07,
	/* 0x08 to 0x0f are reserved */
	ZBC_ZONE_REPORTING_OPTION_NEED_RESET_WP	= 0x10,
	ZBC_ZONE_REPORTING_OPTION_NON_SEQWRITE	= 0x11,
	/* 0x12 to 0x3e are reserved */
	ZBC_ZONE_REPORTING_OPTION_NON_WP	= 0x3f,
};

#define ZBC_REPORT_ZONE_PARTIAL 0x80

/* Zone types of REPORT ZONES zone descriptors */
enum zbc_zone_type {
	ZBC_ZONE_TYPE_CONV		= 0x1,
	ZBC_ZONE_TYPE_SEQWRITE_REQ	= 0x2,
	ZBC_ZONE_TYPE_SEQWRITE_PREF	= 0x3,
	ZBC_ZONE_TYPE_SEQ_OR_BEFORE_REQ	= 0x4,
	ZBC_ZONE_TYPE_GAP		= 0x5,
	/* 0x6 to 0xf are reserved */
};

/* Zone conditions of REPORT ZONES zone descriptors */
enum zbc_zone_cond {
	ZBC_ZONE_COND_NO_WP		= 0x0,
	ZBC_ZONE_COND_EMPTY		= 0x1,
	ZBC_ZONE_COND_IMP_OPEN		= 0x2,
	ZBC_ZONE_COND_EXP_OPEN		= 0x3,
	ZBC_ZONE_COND_CLOSED		= 0x4,
	/* 0x5 to 0xc are reserved */
	ZBC_ZONE_COND_READONLY		= 0xd,
	ZBC_ZONE_COND_FULL		= 0xe,
	ZBC_ZONE_COND_OFFLINE		= 0xf,
};

enum zbc_zone_alignment_method {
	ZBC_CONSTANT_ZONE_LENGTH	= 0x1,
	ZBC_CONSTANT_ZONE_START_OFFSET	= 0x8,
};

/* Version descriptor values for INQUIRY */
enum scsi_version_descriptor {
	SCSI_VERSION_DESCRIPTOR_FCP4	= 0x0a40,
	SCSI_VERSION_DESCRIPTOR_ISCSI	= 0x0960,
	SCSI_VERSION_DESCRIPTOR_SAM5	= 0x00a0,
	SCSI_VERSION_DESCRIPTOR_SAS3	= 0x0c60,
	SCSI_VERSION_DESCRIPTOR_SBC3	= 0x04c0,
	SCSI_VERSION_DESCRIPTOR_SBP3	= 0x0980,
	SCSI_VERSION_DESCRIPTOR_SPC4	= 0x0460,
	SCSI_VERSION_DESCRIPTOR_SRP	= 0x0940
};

enum scsi_support_opcode {
	SCSI_SUPPORT_NO_INFO		= 0,
	SCSI_SUPPORT_NOT_SUPPORTED	= 1,
	SCSI_SUPPORT_FULL		= 3,
	SCSI_SUPPORT_VENDOR		= 5,
};

#define SCSI_CONTROL_MASK 0
#define SCSI_GROUP_NUMBER_MASK 0

#endif /* _SCSI_PROTO_H_ */
