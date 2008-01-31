/*
 *  include/asm-s390/cio.h
 *  include/asm-s390x/cio.h
 *
 * Common interface for I/O on S/390
 */
#ifndef _ASM_S390_CIO_H_
#define _ASM_S390_CIO_H_

#include <linux/spinlock.h>
#include <asm/types.h>

#ifdef __KERNEL__

#define LPM_ANYPATH 0xff
#define __MAX_CSSID 0

/**
 * struct scsw - subchannel status word
 * @key: subchannel key
 * @sctl: suspend control
 * @eswf: esw format
 * @cc: deferred condition code
 * @fmt: format
 * @pfch: prefetch
 * @isic: initial-status interruption control
 * @alcc: address-limit checking control
 * @ssi: suppress-suspended interruption
 * @zcc: zero condition code
 * @ectl: extended control
 * @pno: path not operational
 * @res: reserved
 * @fctl: function control
 * @actl: activity control
 * @stctl: status control
 * @cpa: channel program address
 * @dstat: device status
 * @cstat: subchannel status
 * @count: residual count
 */
struct scsw {
	__u32 key  : 4;
	__u32 sctl : 1;
	__u32 eswf : 1;
	__u32 cc   : 2;
	__u32 fmt  : 1;
	__u32 pfch : 1;
	__u32 isic : 1;
	__u32 alcc : 1;
	__u32 ssi  : 1;
	__u32 zcc  : 1;
	__u32 ectl : 1;
	__u32 pno  : 1;
	__u32 res  : 1;
	__u32 fctl : 3;
	__u32 actl : 7;
	__u32 stctl : 5;
	__u32 cpa;
	__u32 dstat : 8;
	__u32 cstat : 8;
	__u32 count : 16;
} __attribute__ ((packed));

#define SCSW_FCTL_CLEAR_FUNC	 0x1
#define SCSW_FCTL_HALT_FUNC	 0x2
#define SCSW_FCTL_START_FUNC	 0x4

#define SCSW_ACTL_SUSPENDED	 0x1
#define SCSW_ACTL_DEVACT	 0x2
#define SCSW_ACTL_SCHACT	 0x4
#define SCSW_ACTL_CLEAR_PEND	 0x8
#define SCSW_ACTL_HALT_PEND	 0x10
#define SCSW_ACTL_START_PEND	 0x20
#define SCSW_ACTL_RESUME_PEND	 0x40

#define SCSW_STCTL_STATUS_PEND	 0x1
#define SCSW_STCTL_SEC_STATUS	 0x2
#define SCSW_STCTL_PRIM_STATUS	 0x4
#define SCSW_STCTL_INTER_STATUS	 0x8
#define SCSW_STCTL_ALERT_STATUS	 0x10

#define DEV_STAT_ATTENTION	 0x80
#define DEV_STAT_STAT_MOD	 0x40
#define DEV_STAT_CU_END		 0x20
#define DEV_STAT_BUSY		 0x10
#define DEV_STAT_CHN_END	 0x08
#define DEV_STAT_DEV_END	 0x04
#define DEV_STAT_UNIT_CHECK	 0x02
#define DEV_STAT_UNIT_EXCEP	 0x01

#define SCHN_STAT_PCI		 0x80
#define SCHN_STAT_INCORR_LEN	 0x40
#define SCHN_STAT_PROG_CHECK	 0x20
#define SCHN_STAT_PROT_CHECK	 0x10
#define SCHN_STAT_CHN_DATA_CHK	 0x08
#define SCHN_STAT_CHN_CTRL_CHK	 0x04
#define SCHN_STAT_INTF_CTRL_CHK	 0x02
#define SCHN_STAT_CHAIN_CHECK	 0x01

/*
 * architectured values for first sense byte
 */
#define SNS0_CMD_REJECT		0x80
#define SNS_CMD_REJECT		SNS0_CMD_REJEC
#define SNS0_INTERVENTION_REQ	0x40
#define SNS0_BUS_OUT_CHECK	0x20
#define SNS0_EQUIPMENT_CHECK	0x10
#define SNS0_DATA_CHECK		0x08
#define SNS0_OVERRUN		0x04
#define SNS0_INCOMPL_DOMAIN	0x01

/*
 * architectured values for second sense byte
 */
#define SNS1_PERM_ERR		0x80
#define SNS1_INV_TRACK_FORMAT	0x40
#define SNS1_EOC		0x20
#define SNS1_MESSAGE_TO_OPER	0x10
#define SNS1_NO_REC_FOUND	0x08
#define SNS1_FILE_PROTECTED	0x04
#define SNS1_WRITE_INHIBITED	0x02
#define SNS1_INPRECISE_END	0x01

/*
 * architectured values for third sense byte
 */
#define SNS2_REQ_INH_WRITE	0x80
#define SNS2_CORRECTABLE	0x40
#define SNS2_FIRST_LOG_ERR	0x20
#define SNS2_ENV_DATA_PRESENT	0x10
#define SNS2_INPRECISE_END	0x04

/**
 * struct ccw1 - channel command word
 * @cmd_code: command code
 * @flags: flags, like IDA adressing, etc.
 * @count: byte count
 * @cda: data address
 *
 * The ccw is the basic structure to build channel programs that perform
 * operations with the device or the control unit. Only Format-1 channel
 * command words are supported.
 */
struct ccw1 {
	__u8  cmd_code;
	__u8  flags;
	__u16 count;
	__u32 cda;
} __attribute__ ((packed,aligned(8)));

#define CCW_FLAG_DC		0x80
#define CCW_FLAG_CC		0x40
#define CCW_FLAG_SLI		0x20
#define CCW_FLAG_SKIP		0x10
#define CCW_FLAG_PCI		0x08
#define CCW_FLAG_IDA		0x04
#define CCW_FLAG_SUSPEND	0x02

#define CCW_CMD_READ_IPL	0x02
#define CCW_CMD_NOOP		0x03
#define CCW_CMD_BASIC_SENSE	0x04
#define CCW_CMD_TIC		0x08
#define CCW_CMD_STLCK           0x14
#define CCW_CMD_SENSE_PGID	0x34
#define CCW_CMD_SUSPEND_RECONN	0x5B
#define CCW_CMD_RDC		0x64
#define CCW_CMD_RELEASE		0x94
#define CCW_CMD_SET_PGID	0xAF
#define CCW_CMD_SENSE_ID	0xE4
#define CCW_CMD_DCTL		0xF3

#define SENSE_MAX_COUNT		0x20

/**
 * struct erw - extended report word
 * @res0: reserved
 * @auth: authorization check
 * @pvrf: path-verification-required flag
 * @cpt: channel-path timeout
 * @fsavf: failing storage address validity flag
 * @cons: concurrent sense
 * @scavf: secondary ccw address validity flag
 * @fsaf: failing storage address format
 * @scnt: sense count, if @cons == %1
 * @res16: reserved
 */
struct erw {
	__u32 res0  : 3;
	__u32 auth  : 1;
	__u32 pvrf  : 1;
	__u32 cpt   : 1;
	__u32 fsavf : 1;
	__u32 cons  : 1;
	__u32 scavf : 1;
	__u32 fsaf  : 1;
	__u32 scnt  : 6;
	__u32 res16 : 16;
} __attribute__ ((packed));

/**
 * struct sublog - subchannel logout area
 * @res0: reserved
 * @esf: extended status flags
 * @lpum: last path used mask
 * @arep: ancillary report
 * @fvf: field-validity flags
 * @sacc: storage access code
 * @termc: termination code
 * @devsc: device-status check
 * @serr: secondary error
 * @ioerr: i/o-error alert
 * @seqc: sequence code
 */
struct sublog {
	__u32 res0  : 1;
	__u32 esf   : 7;
	__u32 lpum  : 8;
	__u32 arep  : 1;
	__u32 fvf   : 5;
	__u32 sacc  : 2;
	__u32 termc : 2;
	__u32 devsc : 1;
	__u32 serr  : 1;
	__u32 ioerr : 1;
	__u32 seqc  : 3;
} __attribute__ ((packed));

/**
 * struct esw0 - Format 0 Extended Status Word (ESW)
 * @sublog: subchannel logout
 * @erw: extended report word
 * @faddr: failing storage address
 * @saddr: secondary ccw address
 */
struct esw0 {
	struct sublog sublog;
	struct erw erw;
	__u32  faddr[2];
	__u32  saddr;
} __attribute__ ((packed));

/**
 * struct esw1 - Format 1 Extended Status Word (ESW)
 * @zero0: reserved zeros
 * @lpum: last path used mask
 * @zero16: reserved zeros
 * @erw: extended report word
 * @zeros: three fullwords of zeros
 */
struct esw1 {
	__u8  zero0;
	__u8  lpum;
	__u16 zero16;
	struct erw erw;
	__u32 zeros[3];
} __attribute__ ((packed));

/**
 * struct esw2 - Format 2 Extended Status Word (ESW)
 * @zero0: reserved zeros
 * @lpum: last path used mask
 * @dcti: device-connect-time interval
 * @erw: extended report word
 * @zeros: three fullwords of zeros
 */
struct esw2 {
	__u8  zero0;
	__u8  lpum;
	__u16 dcti;
	struct erw erw;
	__u32 zeros[3];
} __attribute__ ((packed));

/**
 * struct esw3 - Format 3 Extended Status Word (ESW)
 * @zero0: reserved zeros
 * @lpum: last path used mask
 * @res: reserved
 * @erw: extended report word
 * @zeros: three fullwords of zeros
 */
struct esw3 {
	__u8  zero0;
	__u8  lpum;
	__u16 res;
	struct erw erw;
	__u32 zeros[3];
} __attribute__ ((packed));

/**
 * struct irb - interruption response block
 * @scsw: subchannel status word
 * @esw: extened status word, 4 formats
 * @ecw: extended control word
 *
 * The irb that is handed to the device driver when an interrupt occurs. For
 * solicited interrupts, the common I/O layer already performs checks whether
 * a field is valid; a field not being valid is always passed as %0.
 * If a unit check occured, @ecw may contain sense data; this is retrieved
 * by the common I/O layer itself if the device doesn't support concurrent
 * sense (so that the device driver never needs to perform basic sene itself).
 * For unsolicited interrupts, the irb is passed as-is (expect for sense data,
 * if applicable).
 */
struct irb {
	struct scsw scsw;
	union {
		struct esw0 esw0;
		struct esw1 esw1;
		struct esw2 esw2;
		struct esw3 esw3;
	} esw;
	__u8   ecw[32];
} __attribute__ ((packed,aligned(4)));

/**
 * struct ciw - command information word  (CIW) layout
 * @et: entry type
 * @reserved: reserved bits
 * @ct: command type
 * @cmd: command code
 * @count: command count
 */
struct ciw {
	__u32 et       :  2;
	__u32 reserved :  2;
	__u32 ct       :  4;
	__u32 cmd      :  8;
	__u32 count    : 16;
} __attribute__ ((packed));

#define CIW_TYPE_RCD	0x0    	/* read configuration data */
#define CIW_TYPE_SII	0x1    	/* set interface identifier */
#define CIW_TYPE_RNI	0x2    	/* read node identifier */

/*
 * Flags used as input parameters for do_IO()
 */
#define DOIO_ALLOW_SUSPEND	 0x0001 /* allow for channel prog. suspend */
#define DOIO_DENY_PREFETCH	 0x0002 /* don't allow for CCW prefetch */
#define DOIO_SUPPRESS_INTER	 0x0004 /* suppress intermediate inter. */
					/* ... for suspended CCWs */
/* Device or subchannel gone. */
#define CIO_GONE       0x0001
/* No path to device. */
#define CIO_NO_PATH    0x0002
/* Device has appeared. */
#define CIO_OPER       0x0004
/* Sick revalidation of device. */
#define CIO_REVALIDATE 0x0008

/**
 * struct ccw_dev_id - unique identifier for ccw devices
 * @ssid: subchannel set id
 * @devno: device number
 *
 * This structure is not directly based on any hardware structure. The
 * hardware identifies a device by its device number and its subchannel,
 * which is in turn identified by its id. In order to get a unique identifier
 * for ccw devices across subchannel sets, @struct ccw_dev_id has been
 * introduced.
 */
struct ccw_dev_id {
	u8 ssid;
	u16 devno;
};

/**
 * ccw_device_id_is_equal() - compare two ccw_dev_ids
 * @dev_id1: a ccw_dev_id
 * @dev_id2: another ccw_dev_id
 * Returns:
 *  %1 if the two structures are equal field-by-field,
 *  %0 if not.
 * Context:
 *  any
 */
static inline int ccw_dev_id_is_equal(struct ccw_dev_id *dev_id1,
				      struct ccw_dev_id *dev_id2)
{
	if ((dev_id1->ssid == dev_id2->ssid) &&
	    (dev_id1->devno == dev_id2->devno))
		return 1;
	return 0;
}

extern void wait_cons_dev(void);

extern void css_schedule_reprobe(void);

extern void reipl_ccw_dev(struct ccw_dev_id *id);

struct cio_iplinfo {
	u16 devno;
	int is_qdio;
};

extern int cio_get_iplinfo(struct cio_iplinfo *iplinfo);

#endif

#endif
