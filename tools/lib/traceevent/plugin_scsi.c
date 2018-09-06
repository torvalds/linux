// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "event-parse.h"
#include "trace-seq.h"

typedef unsigned long sector_t;
typedef uint64_t u64;
typedef unsigned int u32;

/*
 *      SCSI opcodes
 */
#define TEST_UNIT_READY			0x00
#define REZERO_UNIT			0x01
#define REQUEST_SENSE			0x03
#define FORMAT_UNIT			0x04
#define READ_BLOCK_LIMITS		0x05
#define REASSIGN_BLOCKS			0x07
#define INITIALIZE_ELEMENT_STATUS	0x07
#define READ_6				0x08
#define WRITE_6				0x0a
#define SEEK_6				0x0b
#define READ_REVERSE			0x0f
#define WRITE_FILEMARKS			0x10
#define SPACE				0x11
#define INQUIRY				0x12
#define RECOVER_BUFFERED_DATA		0x14
#define MODE_SELECT			0x15
#define RESERVE				0x16
#define RELEASE				0x17
#define COPY				0x18
#define ERASE				0x19
#define MODE_SENSE			0x1a
#define START_STOP			0x1b
#define RECEIVE_DIAGNOSTIC		0x1c
#define SEND_DIAGNOSTIC			0x1d
#define ALLOW_MEDIUM_REMOVAL		0x1e

#define READ_FORMAT_CAPACITIES		0x23
#define SET_WINDOW			0x24
#define READ_CAPACITY			0x25
#define READ_10				0x28
#define WRITE_10			0x2a
#define SEEK_10				0x2b
#define POSITION_TO_ELEMENT		0x2b
#define WRITE_VERIFY			0x2e
#define VERIFY				0x2f
#define SEARCH_HIGH			0x30
#define SEARCH_EQUAL			0x31
#define SEARCH_LOW			0x32
#define SET_LIMITS			0x33
#define PRE_FETCH			0x34
#define READ_POSITION			0x34
#define SYNCHRONIZE_CACHE		0x35
#define LOCK_UNLOCK_CACHE		0x36
#define READ_DEFECT_DATA		0x37
#define MEDIUM_SCAN			0x38
#define COMPARE				0x39
#define COPY_VERIFY			0x3a
#define WRITE_BUFFER			0x3b
#define READ_BUFFER			0x3c
#define UPDATE_BLOCK			0x3d
#define READ_LONG			0x3e
#define WRITE_LONG			0x3f
#define CHANGE_DEFINITION		0x40
#define WRITE_SAME			0x41
#define UNMAP				0x42
#define READ_TOC			0x43
#define READ_HEADER			0x44
#define GET_EVENT_STATUS_NOTIFICATION	0x4a
#define LOG_SELECT			0x4c
#define LOG_SENSE			0x4d
#define XDWRITEREAD_10			0x53
#define MODE_SELECT_10			0x55
#define RESERVE_10			0x56
#define RELEASE_10			0x57
#define MODE_SENSE_10			0x5a
#define PERSISTENT_RESERVE_IN		0x5e
#define PERSISTENT_RESERVE_OUT		0x5f
#define VARIABLE_LENGTH_CMD		0x7f
#define REPORT_LUNS			0xa0
#define SECURITY_PROTOCOL_IN		0xa2
#define MAINTENANCE_IN			0xa3
#define MAINTENANCE_OUT			0xa4
#define MOVE_MEDIUM			0xa5
#define EXCHANGE_MEDIUM			0xa6
#define READ_12				0xa8
#define SERVICE_ACTION_OUT_12		0xa9
#define WRITE_12			0xaa
#define SERVICE_ACTION_IN_12		0xab
#define WRITE_VERIFY_12			0xae
#define VERIFY_12			0xaf
#define SEARCH_HIGH_12			0xb0
#define SEARCH_EQUAL_12			0xb1
#define SEARCH_LOW_12			0xb2
#define SECURITY_PROTOCOL_OUT		0xb5
#define READ_ELEMENT_STATUS		0xb8
#define SEND_VOLUME_TAG			0xb6
#define WRITE_LONG_2			0xea
#define EXTENDED_COPY			0x83
#define RECEIVE_COPY_RESULTS		0x84
#define ACCESS_CONTROL_IN		0x86
#define ACCESS_CONTROL_OUT		0x87
#define READ_16				0x88
#define WRITE_16			0x8a
#define READ_ATTRIBUTE			0x8c
#define WRITE_ATTRIBUTE			0x8d
#define VERIFY_16			0x8f
#define SYNCHRONIZE_CACHE_16		0x91
#define WRITE_SAME_16			0x93
#define SERVICE_ACTION_BIDIRECTIONAL	0x9d
#define SERVICE_ACTION_IN_16		0x9e
#define SERVICE_ACTION_OUT_16		0x9f
/* values for service action in */
#define	SAI_READ_CAPACITY_16		0x10
#define SAI_GET_LBA_STATUS		0x12
/* values for VARIABLE_LENGTH_CMD service action codes
 * see spc4r17 Section D.3.5, table D.7 and D.8 */
#define VLC_SA_RECEIVE_CREDENTIAL	0x1800
/* values for maintenance in */
#define MI_REPORT_IDENTIFYING_INFORMATION		0x05
#define MI_REPORT_TARGET_PGS				0x0a
#define MI_REPORT_ALIASES				0x0b
#define MI_REPORT_SUPPORTED_OPERATION_CODES		0x0c
#define MI_REPORT_SUPPORTED_TASK_MANAGEMENT_FUNCTIONS	0x0d
#define MI_REPORT_PRIORITY				0x0e
#define MI_REPORT_TIMESTAMP				0x0f
#define MI_MANAGEMENT_PROTOCOL_IN			0x10
/* value for MI_REPORT_TARGET_PGS ext header */
#define MI_EXT_HDR_PARAM_FMT		0x20
/* values for maintenance out */
#define MO_SET_IDENTIFYING_INFORMATION	0x06
#define MO_SET_TARGET_PGS		0x0a
#define MO_CHANGE_ALIASES		0x0b
#define MO_SET_PRIORITY			0x0e
#define MO_SET_TIMESTAMP		0x0f
#define MO_MANAGEMENT_PROTOCOL_OUT	0x10
/* values for variable length command */
#define XDREAD_32			0x03
#define XDWRITE_32			0x04
#define XPWRITE_32			0x06
#define XDWRITEREAD_32			0x07
#define READ_32				0x09
#define VERIFY_32			0x0a
#define WRITE_32			0x0b
#define WRITE_SAME_32			0x0d

#define SERVICE_ACTION16(cdb) (cdb[1] & 0x1f)
#define SERVICE_ACTION32(cdb) ((cdb[8] << 8) | cdb[9])

static const char *
scsi_trace_misc(struct trace_seq *, unsigned char *, int);

static const char *
scsi_trace_rw6(struct trace_seq *p, unsigned char *cdb, int len)
{
	const char *ret = p->buffer + p->len;
	sector_t lba = 0, txlen = 0;

	lba |= ((cdb[1] & 0x1F) << 16);
	lba |=  (cdb[2] << 8);
	lba |=   cdb[3];
	txlen = cdb[4];

	trace_seq_printf(p, "lba=%llu txlen=%llu",
			 (unsigned long long)lba, (unsigned long long)txlen);
	trace_seq_putc(p, 0);
	return ret;
}

static const char *
scsi_trace_rw10(struct trace_seq *p, unsigned char *cdb, int len)
{
	const char *ret = p->buffer + p->len;
	sector_t lba = 0, txlen = 0;

	lba |= (cdb[2] << 24);
	lba |= (cdb[3] << 16);
	lba |= (cdb[4] << 8);
	lba |=  cdb[5];
	txlen |= (cdb[7] << 8);
	txlen |=  cdb[8];

	trace_seq_printf(p, "lba=%llu txlen=%llu protect=%u",
			 (unsigned long long)lba, (unsigned long long)txlen,
			 cdb[1] >> 5);

	if (cdb[0] == WRITE_SAME)
		trace_seq_printf(p, " unmap=%u", cdb[1] >> 3 & 1);

	trace_seq_putc(p, 0);
	return ret;
}

static const char *
scsi_trace_rw12(struct trace_seq *p, unsigned char *cdb, int len)
{
	const char *ret = p->buffer + p->len;
	sector_t lba = 0, txlen = 0;

	lba |= (cdb[2] << 24);
	lba |= (cdb[3] << 16);
	lba |= (cdb[4] << 8);
	lba |=  cdb[5];
	txlen |= (cdb[6] << 24);
	txlen |= (cdb[7] << 16);
	txlen |= (cdb[8] << 8);
	txlen |=  cdb[9];

	trace_seq_printf(p, "lba=%llu txlen=%llu protect=%u",
			 (unsigned long long)lba, (unsigned long long)txlen,
			 cdb[1] >> 5);
	trace_seq_putc(p, 0);
	return ret;
}

static const char *
scsi_trace_rw16(struct trace_seq *p, unsigned char *cdb, int len)
{
	const char *ret = p->buffer + p->len;
	sector_t lba = 0, txlen = 0;

	lba |= ((u64)cdb[2] << 56);
	lba |= ((u64)cdb[3] << 48);
	lba |= ((u64)cdb[4] << 40);
	lba |= ((u64)cdb[5] << 32);
	lba |= (cdb[6] << 24);
	lba |= (cdb[7] << 16);
	lba |= (cdb[8] << 8);
	lba |=  cdb[9];
	txlen |= (cdb[10] << 24);
	txlen |= (cdb[11] << 16);
	txlen |= (cdb[12] << 8);
	txlen |=  cdb[13];

	trace_seq_printf(p, "lba=%llu txlen=%llu protect=%u",
			 (unsigned long long)lba, (unsigned long long)txlen,
			 cdb[1] >> 5);

	if (cdb[0] == WRITE_SAME_16)
		trace_seq_printf(p, " unmap=%u", cdb[1] >> 3 & 1);

	trace_seq_putc(p, 0);
	return ret;
}

static const char *
scsi_trace_rw32(struct trace_seq *p, unsigned char *cdb, int len)
{
	const char *ret = p->buffer + p->len, *cmd;
	sector_t lba = 0, txlen = 0;
	u32 ei_lbrt = 0;

	switch (SERVICE_ACTION32(cdb)) {
	case READ_32:
		cmd = "READ";
		break;
	case VERIFY_32:
		cmd = "VERIFY";
		break;
	case WRITE_32:
		cmd = "WRITE";
		break;
	case WRITE_SAME_32:
		cmd = "WRITE_SAME";
		break;
	default:
		trace_seq_printf(p, "UNKNOWN");
		goto out;
	}

	lba |= ((u64)cdb[12] << 56);
	lba |= ((u64)cdb[13] << 48);
	lba |= ((u64)cdb[14] << 40);
	lba |= ((u64)cdb[15] << 32);
	lba |= (cdb[16] << 24);
	lba |= (cdb[17] << 16);
	lba |= (cdb[18] << 8);
	lba |=  cdb[19];
	ei_lbrt |= (cdb[20] << 24);
	ei_lbrt |= (cdb[21] << 16);
	ei_lbrt |= (cdb[22] << 8);
	ei_lbrt |=  cdb[23];
	txlen |= (cdb[28] << 24);
	txlen |= (cdb[29] << 16);
	txlen |= (cdb[30] << 8);
	txlen |=  cdb[31];

	trace_seq_printf(p, "%s_32 lba=%llu txlen=%llu protect=%u ei_lbrt=%u",
			 cmd, (unsigned long long)lba,
			 (unsigned long long)txlen, cdb[10] >> 5, ei_lbrt);

	if (SERVICE_ACTION32(cdb) == WRITE_SAME_32)
		trace_seq_printf(p, " unmap=%u", cdb[10] >> 3 & 1);

out:
	trace_seq_putc(p, 0);
	return ret;
}

static const char *
scsi_trace_unmap(struct trace_seq *p, unsigned char *cdb, int len)
{
	const char *ret = p->buffer + p->len;
	unsigned int regions = cdb[7] << 8 | cdb[8];

	trace_seq_printf(p, "regions=%u", (regions - 8) / 16);
	trace_seq_putc(p, 0);
	return ret;
}

static const char *
scsi_trace_service_action_in(struct trace_seq *p, unsigned char *cdb, int len)
{
	const char *ret = p->buffer + p->len, *cmd;
	sector_t lba = 0;
	u32 alloc_len = 0;

	switch (SERVICE_ACTION16(cdb)) {
	case SAI_READ_CAPACITY_16:
		cmd = "READ_CAPACITY_16";
		break;
	case SAI_GET_LBA_STATUS:
		cmd = "GET_LBA_STATUS";
		break;
	default:
		trace_seq_printf(p, "UNKNOWN");
		goto out;
	}

	lba |= ((u64)cdb[2] << 56);
	lba |= ((u64)cdb[3] << 48);
	lba |= ((u64)cdb[4] << 40);
	lba |= ((u64)cdb[5] << 32);
	lba |= (cdb[6] << 24);
	lba |= (cdb[7] << 16);
	lba |= (cdb[8] << 8);
	lba |=  cdb[9];
	alloc_len |= (cdb[10] << 24);
	alloc_len |= (cdb[11] << 16);
	alloc_len |= (cdb[12] << 8);
	alloc_len |=  cdb[13];

	trace_seq_printf(p, "%s lba=%llu alloc_len=%u", cmd,
			 (unsigned long long)lba, alloc_len);

out:
	trace_seq_putc(p, 0);
	return ret;
}

static const char *
scsi_trace_varlen(struct trace_seq *p, unsigned char *cdb, int len)
{
	switch (SERVICE_ACTION32(cdb)) {
	case READ_32:
	case VERIFY_32:
	case WRITE_32:
	case WRITE_SAME_32:
		return scsi_trace_rw32(p, cdb, len);
	default:
		return scsi_trace_misc(p, cdb, len);
	}
}

static const char *
scsi_trace_misc(struct trace_seq *p, unsigned char *cdb, int len)
{
	const char *ret = p->buffer + p->len;

	trace_seq_printf(p, "-");
	trace_seq_putc(p, 0);
	return ret;
}

const char *
scsi_trace_parse_cdb(struct trace_seq *p, unsigned char *cdb, int len)
{
	switch (cdb[0]) {
	case READ_6:
	case WRITE_6:
		return scsi_trace_rw6(p, cdb, len);
	case READ_10:
	case VERIFY:
	case WRITE_10:
	case WRITE_SAME:
		return scsi_trace_rw10(p, cdb, len);
	case READ_12:
	case VERIFY_12:
	case WRITE_12:
		return scsi_trace_rw12(p, cdb, len);
	case READ_16:
	case VERIFY_16:
	case WRITE_16:
	case WRITE_SAME_16:
		return scsi_trace_rw16(p, cdb, len);
	case UNMAP:
		return scsi_trace_unmap(p, cdb, len);
	case SERVICE_ACTION_IN_16:
		return scsi_trace_service_action_in(p, cdb, len);
	case VARIABLE_LENGTH_CMD:
		return scsi_trace_varlen(p, cdb, len);
	default:
		return scsi_trace_misc(p, cdb, len);
	}
}

unsigned long long process_scsi_trace_parse_cdb(struct trace_seq *s,
						unsigned long long *args)
{
	scsi_trace_parse_cdb(s, (unsigned char *) (unsigned long) args[1], args[2]);
	return 0;
}

int TEP_PLUGIN_LOADER(struct tep_handle *pevent)
{
	tep_register_print_function(pevent,
				    process_scsi_trace_parse_cdb,
				    TEP_FUNC_ARG_STRING,
				    "scsi_trace_parse_cdb",
				    TEP_FUNC_ARG_PTR,
				    TEP_FUNC_ARG_PTR,
				    TEP_FUNC_ARG_INT,
				    TEP_FUNC_ARG_VOID);
	return 0;
}

void TEP_PLUGIN_UNLOADER(struct tep_handle *pevent)
{
	tep_unregister_print_function(pevent, process_scsi_trace_parse_cdb,
				      "scsi_trace_parse_cdb");
}
