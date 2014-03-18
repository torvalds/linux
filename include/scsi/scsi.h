/*
 * This header file contains public constants and structures used by
 * the scsi code for linux.
 *
 * For documentation on the OPCODES, MESSAGES, and SENSE values,
 * please consult the SCSI standard.
 */
#ifndef _SCSI_SCSI_H
#define _SCSI_SCSI_H

#include <linux/types.h>
#include <linux/scatterlist.h>
#include <linux/kernel.h>

struct scsi_cmnd;

enum scsi_timeouts {
	SCSI_DEFAULT_EH_TIMEOUT		= 10 * HZ,
};

/*
 * The maximum number of SG segments that we will put inside a
 * scatterlist (unless chaining is used). Should ideally fit inside a
 * single page, to avoid a higher order allocation.  We could define this
 * to SG_MAX_SINGLE_ALLOC to pack correctly at the highest order.  The
 * minimum value is 32
 */
#define SCSI_MAX_SG_SEGMENTS	128

/*
 * Like SCSI_MAX_SG_SEGMENTS, but for archs that have sg chaining. This limit
 * is totally arbitrary, a setting of 2048 will get you at least 8mb ios.
 */
#ifdef ARCH_HAS_SG_CHAIN
#define SCSI_MAX_SG_CHAIN_SEGMENTS	2048
#else
#define SCSI_MAX_SG_CHAIN_SEGMENTS	SCSI_MAX_SG_SEGMENTS
#endif

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
#define RESERVE               0x16
#define RELEASE               0x17
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
#define WRITE_12              0xaa
#define READ_MEDIA_SERIAL_NUMBER 0xab
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
#define VERIFY_16	      0x8f
#define SYNCHRONIZE_CACHE_16  0x91
#define WRITE_SAME_16	      0x93
#define SERVICE_ACTION_IN     0x9e
/* values for service action in */
#define	SAI_READ_CAPACITY_16  0x10
#define SAI_GET_LBA_STATUS    0x12
#define SAI_REPORT_REFERRALS  0x13
/* values for VARIABLE_LENGTH_CMD service action codes
 * see spc4r17 Section D.3.5, table D.7 and D.8 */
#define VLC_SA_RECEIVE_CREDENTIAL 0x1800
/* values for maintenance in */
#define MI_REPORT_IDENTIFYING_INFORMATION 0x05
#define MI_REPORT_TARGET_PGS  0x0a
#define MI_REPORT_ALIASES     0x0b
#define MI_REPORT_SUPPORTED_OPERATION_CODES 0x0c
#define MI_REPORT_SUPPORTED_TASK_MANAGEMENT_FUNCTIONS 0x0d
#define MI_REPORT_PRIORITY   0x0e
#define MI_REPORT_TIMESTAMP  0x0f
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
/* values for variable length command */
#define XDREAD_32	      0x03
#define XDWRITE_32	      0x04
#define XPWRITE_32	      0x06
#define XDWRITEREAD_32	      0x07
#define READ_32		      0x09
#define VERIFY_32	      0x0a
#define WRITE_32	      0x0b
#define WRITE_SAME_32	      0x0d

/* Values for T10/04-262r7 */
#define	ATA_16		      0x85	/* 16-byte pass-thru */
#define	ATA_12		      0xa1	/* 12-byte pass-thru */

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

static inline unsigned
scsi_varlen_cdb_length(const void *hdr)
{
	return ((struct scsi_varlen_cdb_hdr *)hdr)->additional_cdb_length + 8;
}

extern const unsigned char scsi_command_size_tbl[8];
#define COMMAND_SIZE(opcode) scsi_command_size_tbl[((opcode) >> 5) & 7]

static inline unsigned
scsi_command_size(const unsigned char *cmnd)
{
	return (cmnd[0] == VARIABLE_LENGTH_CMD) ?
		scsi_varlen_cdb_length(cmnd) : COMMAND_SIZE(cmnd[0]);
}

#ifdef CONFIG_ACPI
struct acpi_bus_type;

extern int
scsi_register_acpi_bus_type(struct acpi_bus_type *bus);

extern void
scsi_unregister_acpi_bus_type(struct acpi_bus_type *bus);
#endif

/*
 *  SCSI Architecture Model (SAM) Status codes. Taken from SAM-3 draft
 *  T10/1561-D Revision 4 Draft dated 7th November 2002.
 */
#define SAM_STAT_GOOD            0x00
#define SAM_STAT_CHECK_CONDITION 0x02
#define SAM_STAT_CONDITION_MET   0x04
#define SAM_STAT_BUSY            0x08
#define SAM_STAT_INTERMEDIATE    0x10
#define SAM_STAT_INTERMEDIATE_CONDITION_MET 0x14
#define SAM_STAT_RESERVATION_CONFLICT 0x18
#define SAM_STAT_COMMAND_TERMINATED 0x22	/* obsolete in SAM-3 */
#define SAM_STAT_TASK_SET_FULL   0x28
#define SAM_STAT_ACA_ACTIVE      0x30
#define SAM_STAT_TASK_ABORTED    0x40

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
 *  Status codes. These are deprecated as they are shifted 1 bit right
 *  from those found in the SCSI standards. This causes confusion for
 *  applications that are ported to several OSes. Prefer SAM Status codes
 *  above.
 */

#define GOOD                 0x00
#define CHECK_CONDITION      0x01
#define CONDITION_GOOD       0x02
#define BUSY                 0x04
#define INTERMEDIATE_GOOD    0x08
#define INTERMEDIATE_C_GOOD  0x0a
#define RESERVATION_CONFLICT 0x0c
#define COMMAND_TERMINATED   0x11
#define QUEUE_FULL           0x14
#define ACA_ACTIVE           0x18
#define TASK_ABORTED         0x20

#define STATUS_MASK          0xfe

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
#define COPY_ABORTED        0x0a
#define ABORTED_COMMAND     0x0b
#define VOLUME_OVERFLOW     0x0d
#define MISCOMPARE          0x0e


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

/* Returns a human-readable name for the device */
extern const char * scsi_device_type(unsigned type);

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
 * ScsiLun: 8 byte LUN.
 */
struct scsi_lun {
	__u8 scsi_lun[8];
};

/*
 * The Well Known LUNS (SAM-3) in our int representation of a LUN
 */
#define SCSI_W_LUN_BASE 0xc100
#define SCSI_W_LUN_REPORT_LUNS (SCSI_W_LUN_BASE + 1)
#define SCSI_W_LUN_ACCESS_CONTROL (SCSI_W_LUN_BASE + 2)
#define SCSI_W_LUN_TARGET_LOG_PAGE (SCSI_W_LUN_BASE + 3)

static inline int scsi_is_wlun(unsigned int lun)
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
