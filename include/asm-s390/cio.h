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

/*
 * subchannel status word
 */
struct scsw {
	__u32 key  : 4;		/* subchannel key */
	__u32 sctl : 1; 	/* suspend control */
	__u32 eswf : 1; 	/* ESW format */
	__u32 cc   : 2; 	/* deferred condition code */
	__u32 fmt  : 1; 	/* format */
	__u32 pfch : 1; 	/* prefetch */
	__u32 isic : 1; 	/* initial-status interruption control */
	__u32 alcc : 1; 	/* address-limit checking control */
	__u32 ssi  : 1; 	/* supress-suspended interruption */
	__u32 zcc  : 1; 	/* zero condition code */
	__u32 ectl : 1; 	/* extended control */
	__u32 pno  : 1;	    	/* path not operational */
	__u32 res  : 1;	    	/* reserved */
	__u32 fctl : 3;	    	/* function control */
	__u32 actl : 7;	    	/* activity control */
	__u32 stctl : 5;    	/* status control */
	__u32 cpa;	    	/* channel program address */
	__u32 dstat : 8;    	/* device status */
	__u32 cstat : 8;    	/* subchannel status */
	__u32 count : 16;   	/* residual count */
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

struct ccw1 {
	__u8  cmd_code;		/* command code */
	__u8  flags;   		/* flags, like IDA addressing, etc. */
	__u16 count;   		/* byte count */
	__u32 cda;     		/* data address */
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

struct erw {
	__u32 res0  : 3;  	/* reserved */
	__u32 auth  : 1;	/* Authorization check */
	__u32 pvrf  : 1;  	/* path-verification-required flag */
	__u32 cpt   : 1;  	/* channel-path timeout */
	__u32 fsavf : 1;  	/* Failing storage address validity flag */
	__u32 cons  : 1;  	/* concurrent-sense */
	__u32 scavf : 1;	/* Secondary ccw address validity flag */
	__u32 fsaf  : 1;	/* Failing storage address format */
	__u32 scnt  : 6;  	/* sense count if cons == 1 */
	__u32 res16 : 16; 	/* reserved */
} __attribute__ ((packed));

/*
 * subchannel logout area
 */
struct sublog {
	__u32 res0  : 1;  	/* reserved */
	__u32 esf   : 7;  	/* extended status flags */
	__u32 lpum  : 8;  	/* last path used mask */
	__u32 arep  : 1;  	/* ancillary report */
	__u32 fvf   : 5;  	/* field-validity flags */
	__u32 sacc  : 2;  	/* storage access code */
	__u32 termc : 2;  	/* termination code */
	__u32 devsc : 1;  	/* device-status check */
	__u32 serr  : 1;  	/* secondary error */
	__u32 ioerr : 1;  	/* i/o-error alert */
	__u32 seqc  : 3;  	/* sequence code */
} __attribute__ ((packed));

/*
 * Format 0 Extended Status Word (ESW)
 */
struct esw0 {
	struct sublog sublog;	/* subchannel logout */
	struct erw erw;	    	/* extended report word */
	__u32  faddr[2];    	/* failing storage address */
	__u32  saddr;  		/* secondary ccw address */
} __attribute__ ((packed));

/*
 * Format 1 Extended Status Word (ESW)
 */
struct esw1 {
	__u8  zero0;		/* reserved zeros */
	__u8  lpum;		/* last path used mask */
	__u16 zero16;		/* reserved zeros */
	struct erw erw;		/* extended report word */
	__u32 zeros[3]; 	/* 2 fullwords of zeros */
} __attribute__ ((packed));

/*
 * Format 2 Extended Status Word (ESW)
 */
struct esw2 {
	__u8  zero0;		/* reserved zeros */
	__u8  lpum;		/* last path used mask */
	__u16 dcti;		/* device-connect-time interval */
	struct erw erw;		/* extended report word */
	__u32 zeros[3]; 	/* 2 fullwords of zeros */
} __attribute__ ((packed));

/*
 * Format 3 Extended Status Word (ESW)
 */
struct esw3 {
	__u8  zero0;		/* reserved zeros */
	__u8  lpum;		/* last path used mask */
	__u16 res;		/* reserved */
	struct erw erw;		/* extended report word */
	__u32 zeros[3]; 	/* 2 fullwords of zeros */
} __attribute__ ((packed));

/*
 * interruption response block
 */
struct irb {
	struct scsw scsw;	/* subchannel status word */
	union {			/* extended status word, 4 formats */
		struct esw0 esw0;
		struct esw1 esw1;
		struct esw2 esw2;
		struct esw3 esw3;
	} esw;
	__u8   ecw[32];		/* extended control word */
} __attribute__ ((packed,aligned(4)));

/*
 * command information word  (CIW) layout
 */
struct ciw {
	__u32 et       :  2; 	/* entry type */
	__u32 reserved :  2; 	/* reserved */
	__u32 ct       :  4; 	/* command type */
	__u32 cmd      :  8; 	/* command */
	__u32 count    : 16; 	/* coun */
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

struct diag210 {
	__u16 vrdcdvno : 16;   /* device number (input) */
	__u16 vrdclen  : 16;   /* data block length (input) */
	__u32 vrdcvcla : 8;    /* virtual device class (output) */
	__u32 vrdcvtyp : 8;    /* virtual device type (output) */
	__u32 vrdcvsta : 8;    /* virtual device status (output) */
	__u32 vrdcvfla : 8;    /* virtual device flags (output) */
	__u32 vrdcrccl : 8;    /* real device class (output) */
	__u32 vrdccrty : 8;    /* real device type (output) */
	__u32 vrdccrmd : 8;    /* real device model (output) */
	__u32 vrdccrft : 8;    /* real device feature (output) */
} __attribute__ ((packed,aligned(4)));

struct ccw_dev_id {
	u8 ssid;
	u16 devno;
};

static inline int ccw_dev_id_is_equal(struct ccw_dev_id *dev_id1,
				      struct ccw_dev_id *dev_id2)
{
	if ((dev_id1->ssid == dev_id2->ssid) &&
	    (dev_id1->devno == dev_id2->devno))
		return 1;
	return 0;
}

extern int diag210(struct diag210 *addr);

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
