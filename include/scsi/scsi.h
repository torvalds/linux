/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This header file contains public constants and structures used by
 * the SCSI initiator code.
 */
#ifndef _SCSI_SCSI_H
#define _SCSI_SCSI_H

#include <linux/types.h>
#include <linux/scatterlist.h>
#include <linux/kernel.h>
#include <scsi/scsi_common.h>
#include <scsi/scsi_proto.h>
#include <scsi/scsi_status.h>

struct scsi_cmnd;

enum scsi_timeouts {
	SCSI_DEFAULT_EH_TIMEOUT		= 10 * HZ,
};

/*
 * DIX-capable adapters effectively support infinite chaining for the
 * protection information scatterlist
 */
#define SCSI_MAX_PROT_SG_SEGMENTS	0xFFFF

/*
 * Special value for scanning to specify scanning or rescanning of all
 * possible channels, (target) ids, or luns on a given shost.
 */
#define SCAN_WILD_CARD	~0

/*
 * standard mode-select header prepended to all mode-select commands
 */

struct ccs_modesel_head {
	__u8 _r1;			/* reserved */
	__u8 medium;		/* device-specific medium type */
	__u8 _r2;			/* reserved */
	__u8 block_desc_length;	/* block descriptor length */
	__u8 density;		/* device-specific density code */
	__u8 number_blocks_hi;	/* number of blocks in this block desc */
	__u8 number_blocks_med;
	__u8 number_blocks_lo;
	__u8 _r3;
	__u8 block_length_hi;	/* block length for blocks in this desc */
	__u8 block_length_med;
	__u8 block_length_lo;
};

/*
 * The Well Known LUNS (SAM-3) in our int representation of a LUN
 */
#define SCSI_W_LUN_BASE 0xc100
#define SCSI_W_LUN_REPORT_LUNS (SCSI_W_LUN_BASE + 1)
#define SCSI_W_LUN_ACCESS_CONTROL (SCSI_W_LUN_BASE + 2)
#define SCSI_W_LUN_TARGET_LOG_PAGE (SCSI_W_LUN_BASE + 3)

static inline int scsi_is_wlun(u64 lun)
{
	return (lun & 0xff00) == SCSI_W_LUN_BASE;
}

/**
 * scsi_status_is_check_condition - check the status return.
 *
 * @status: the status passed up from the driver (including host and
 *          driver components)
 *
 * This returns true if the status code is SAM_STAT_CHECK_CONDITION.
 */
static inline int scsi_status_is_check_condition(int status)
{
	if (status < 0)
		return false;
	status &= 0xfe;
	return status == SAM_STAT_CHECK_CONDITION;
}

/*
 *  Extended message codes.
 */
#define     EXTENDED_MODIFY_DATA_POINTER    0x00
#define     EXTENDED_SDTR                   0x01
#define     EXTENDED_EXTENDED_IDENTIFY      0x02    /* SCSI-I only */
#define     EXTENDED_WDTR                   0x03
#define     EXTENDED_PPR                    0x04
#define     EXTENDED_MODIFY_BIDI_DATA_PTR   0x05

/*
 * Internal return values.
 */
enum scsi_disposition {
	NEEDS_RETRY		= 0x2001,
	SUCCESS			= 0x2002,
	FAILED			= 0x2003,
	QUEUED			= 0x2004,
	SOFT_ERROR		= 0x2005,
	ADD_TO_MLQUEUE		= 0x2006,
	TIMEOUT_ERROR		= 0x2007,
	SCSI_RETURN_NOT_HANDLED	= 0x2008,
	FAST_IO_FAIL		= 0x2009,
};

/*
 * Midlevel queue return values.
 */
#define SCSI_MLQUEUE_HOST_BUSY   0x1055
#define SCSI_MLQUEUE_DEVICE_BUSY 0x1056
#define SCSI_MLQUEUE_EH_RETRY    0x1057
#define SCSI_MLQUEUE_TARGET_BUSY 0x1058

/*
 *  Use these to separate status msg and our bytes
 *
 *  These are set by:
 *
 *      status byte = set from target device
 *      msg_byte    (unused)
 *      host_byte   = set by low-level driver to indicate status.
 */
#define status_byte(result) (result & 0xff)
#define host_byte(result)   (((result) >> 16) & 0xff)

#define sense_class(sense)  (((sense) >> 4) & 0x7)
#define sense_error(sense)  ((sense) & 0xf)
#define sense_valid(sense)  ((sense) & 0x80)

/*
 * default timeouts
*/
#define FORMAT_UNIT_TIMEOUT		(2 * 60 * 60 * HZ)
#define START_STOP_TIMEOUT		(60 * HZ)
#define MOVE_MEDIUM_TIMEOUT		(5 * 60 * HZ)
#define READ_ELEMENT_STATUS_TIMEOUT	(5 * 60 * HZ)
#define READ_DEFECT_DATA_TIMEOUT	(60 * HZ )


#define IDENTIFY_BASE       0x80
#define IDENTIFY(can_disconnect, lun)   (IDENTIFY_BASE |\
		     ((can_disconnect) ?  0x40 : 0) |\
		     ((lun) & 0x07))

/*
 *  struct scsi_device::scsi_level values. For SCSI devices other than those
 *  prior to SCSI-2 (i.e. over 12 years old) this value is (resp[2] + 1)
 *  where "resp" is a byte array of the response to an INQUIRY. The scsi_level
 *  variable is visible to the user via sysfs.
 */

#define SCSI_UNKNOWN    0
#define SCSI_1          1
#define SCSI_1_CCS      2
#define SCSI_2          3
#define SCSI_3          4        /* SPC */
#define SCSI_SPC_2      5
#define SCSI_SPC_3      6

/*
 * INQ PERIPHERAL QUALIFIERS
 */
#define SCSI_INQ_PQ_CON         0x00
#define SCSI_INQ_PQ_NOT_CON     0x01
#define SCSI_INQ_PQ_NOT_CAP     0x03


/*
 * Here are some scsi specific ioctl commands which are sometimes useful.
 *
 * Note that include/linux/cdrom.h also defines IOCTL 0x5300 - 0x5395
 */

/* Used to obtain PUN and LUN info.  Conflicts with CDROMAUDIOBUFSIZ */
#define SCSI_IOCTL_GET_IDLUN		0x5382

/* 0x5383 and 0x5384 were used for SCSI_IOCTL_TAGGED_{ENABLE,DISABLE} */

/* Used to obtain the host number of a device. */
#define SCSI_IOCTL_PROBE_HOST		0x5385

/* Used to obtain the bus number for a device */
#define SCSI_IOCTL_GET_BUS_NUMBER	0x5386

/* Used to obtain the PCI location of a device */
#define SCSI_IOCTL_GET_PCI		0x5387

/** scsi_status_is_good - check the status return.
 *
 * @status: the status passed up from the driver (including host and
 *          driver components)
 *
 * This returns true for known good conditions that may be treated as
 * command completed normally
 */
static inline bool scsi_status_is_good(int status)
{
	if (status < 0)
		return false;

	if (host_byte(status) == DID_NO_CONNECT)
		return false;

	/*
	 * FIXME: bit0 is listed as reserved in SCSI-2, but is
	 * significant in SCSI-3.  For now, we follow the SCSI-2
	 * behaviour and ignore reserved bits.
	 */
	status &= 0xfe;
	return ((status == SAM_STAT_GOOD) ||
		(status == SAM_STAT_CONDITION_MET) ||
		/* Next two "intermediate" statuses are obsolete in SAM-4 */
		(status == SAM_STAT_INTERMEDIATE) ||
		(status == SAM_STAT_INTERMEDIATE_CONDITION_MET) ||
		/* FIXME: this is obsolete in SAM-3 */
		(status == SAM_STAT_COMMAND_TERMINATED));
}

#endif /* _SCSI_SCSI_H */
