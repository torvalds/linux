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

/** scsi_status_is_good - check the status return.
 *
 * @status: the status passed up from the driver (including host and
 *          driver components)
 *
 * This returns true for known good conditions that may be treated as
 * command completed normally
 */
static inline int scsi_status_is_good(int status)
{
	/*
	 * FIXME: bit0 is listed as reserved in SCSI-2, but is
	 * significant in SCSI-3.  For now, we follow the SCSI-2
	 * behaviour and ignore reserved bits.
	 */
	status &= 0xfe;
	return ((status == SAM_STAT_GOOD) ||
		(status == SAM_STAT_INTERMEDIATE) ||
		(status == SAM_STAT_INTERMEDIATE_CONDITION_MET) ||
		/* FIXME: this is obsolete in SAM-3 */
		(status == SAM_STAT_COMMAND_TERMINATED));
}


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


/*
 *  MESSAGE CODES
 */

#define COMMAND_COMPLETE    0x00
#define EXTENDED_MESSAGE    0x01
#define     EXTENDED_MODIFY_DATA_POINTER    0x00
#define     EXTENDED_SDTR                   0x01
#define     EXTENDED_EXTENDED_IDENTIFY      0x02    /* SCSI-I only */
#define     EXTENDED_WDTR                   0x03
#define     EXTENDED_PPR                    0x04
#define     EXTENDED_MODIFY_BIDI_DATA_PTR   0x05
#define SAVE_POINTERS       0x02
#define RESTORE_POINTERS    0x03
#define DISCONNECT          0x04
#define INITIATOR_ERROR     0x05
#define ABORT_TASK_SET      0x06
#define MESSAGE_REJECT      0x07
#define NOP                 0x08
#define MSG_PARITY_ERROR    0x09
#define LINKED_CMD_COMPLETE 0x0a
#define LINKED_FLG_CMD_COMPLETE 0x0b
#define TARGET_RESET        0x0c
#define ABORT_TASK          0x0d
#define CLEAR_TASK_SET      0x0e
#define INITIATE_RECOVERY   0x0f            /* SCSI-II only */
#define RELEASE_RECOVERY    0x10            /* SCSI-II only */
#define CLEAR_ACA           0x16
#define LOGICAL_UNIT_RESET  0x17
#define SIMPLE_QUEUE_TAG    0x20
#define HEAD_OF_QUEUE_TAG   0x21
#define ORDERED_QUEUE_TAG   0x22
#define IGNORE_WIDE_RESIDUE 0x23
#define ACA                 0x24
#define QAS_REQUEST         0x55

/* Old SCSI2 names, don't use in new code */
#define BUS_DEVICE_RESET    TARGET_RESET
#define ABORT               ABORT_TASK_SET

/*
 * Host byte codes
 */

#define DID_OK          0x00	/* NO error                                */
#define DID_NO_CONNECT  0x01	/* Couldn't connect before timeout period  */
#define DID_BUS_BUSY    0x02	/* BUS stayed busy through time out period */
#define DID_TIME_OUT    0x03	/* TIMED OUT for other reason              */
#define DID_BAD_TARGET  0x04	/* BAD target.                             */
#define DID_ABORT       0x05	/* Told to abort for some other reason     */
#define DID_PARITY      0x06	/* Parity error                            */
#define DID_ERROR       0x07	/* Internal error                          */
#define DID_RESET       0x08	/* Reset by somebody.                      */
#define DID_BAD_INTR    0x09	/* Got an interrupt we weren't expecting.  */
#define DID_PASSTHROUGH 0x0a	/* Force command past mid-layer            */
#define DID_SOFT_ERROR  0x0b	/* The low level driver just wish a retry  */
#define DID_IMM_RETRY   0x0c	/* Retry without decrementing retry count  */
#define DID_REQUEUE	0x0d	/* Requeue command (no immediate retry) also
				 * without decrementing the retry count	   */
#define DID_TRANSPORT_DISRUPTED 0x0e /* Transport error disrupted execution
				      * and the driver blocked the port to
				      * recover the link. Transport class will
				      * retry or fail IO */
#define DID_TRANSPORT_FAILFAST	0x0f /* Transport class fastfailed the io */
#define DID_TARGET_FAILURE 0x10 /* Permanent target failure, do not retry on
				 * other paths */
#define DID_NEXUS_FAILURE 0x11  /* Permanent nexus failure, retry on other
				 * paths might yield different results */
#define DID_ALLOC_FAILURE 0x12  /* Space allocation on the device failed */
#define DID_MEDIUM_ERROR  0x13  /* Medium error */
#define DRIVER_OK       0x00	/* Driver status                           */

/*
 *  These indicate the error that occurred, and what is available.
 */

#define DRIVER_BUSY         0x01
#define DRIVER_SOFT         0x02
#define DRIVER_MEDIA        0x03
#define DRIVER_ERROR        0x04

#define DRIVER_INVALID      0x05
#define DRIVER_TIMEOUT      0x06
#define DRIVER_HARD         0x07
#define DRIVER_SENSE	    0x08

/*
 * Internal return values.
 */

#define NEEDS_RETRY     0x2001
#define SUCCESS         0x2002
#define FAILED          0x2003
#define QUEUED          0x2004
#define SOFT_ERROR      0x2005
#define ADD_TO_MLQUEUE  0x2006
#define TIMEOUT_ERROR   0x2007
#define SCSI_RETURN_NOT_HANDLED   0x2008
#define FAST_IO_FAIL	0x2009

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
 *      msg_byte    = return status from host adapter itself.
 *      host_byte   = set by low-level driver to indicate status.
 *      driver_byte = set by mid-level.
 */
#define status_byte(result) (((result) >> 1) & 0x7f)
#define msg_byte(result)    (((result) >> 8) & 0xff)
#define host_byte(result)   (((result) >> 16) & 0xff)
#define driver_byte(result) (((result) >> 24) & 0xff)

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

/* Pull a u32 out of a SCSI message (using BE SCSI conventions) */
static inline __u32 scsi_to_u32(__u8 *ptr)
{
	return (ptr[0]<<24) + (ptr[1]<<16) + (ptr[2]<<8) + ptr[3];
}

#endif /* _SCSI_SCSI_H */
