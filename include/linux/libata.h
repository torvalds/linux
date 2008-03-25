/*
 *  Copyright 2003-2005 Red Hat, Inc.  All rights reserved.
 *  Copyright 2003-2005 Jeff Garzik
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 *  libata documentation is available via 'make {ps|pdf}docs',
 *  as Documentation/DocBook/libata.*
 *
 */

#ifndef __LINUX_LIBATA_H__
#define __LINUX_LIBATA_H__

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>
#include <linux/io.h>
#include <linux/ata.h>
#include <linux/workqueue.h>
#include <scsi/scsi_host.h>
#include <linux/acpi.h>
#include <linux/cdrom.h>

/*
 * Define if arch has non-standard setup.  This is a _PCI_ standard
 * not a legacy or ISA standard.
 */
#ifdef CONFIG_ATA_NONSTANDARD
#include <asm/libata-portmap.h>
#else
#include <asm-generic/libata-portmap.h>
#endif

/*
 * compile-time options: to be removed as soon as all the drivers are
 * converted to the new debugging mechanism
 */
#undef ATA_DEBUG		/* debugging output */
#undef ATA_VERBOSE_DEBUG	/* yet more debugging output */
#undef ATA_IRQ_TRAP		/* define to ack screaming irqs */
#undef ATA_NDEBUG		/* define to disable quick runtime checks */


/* note: prints function name for you */
#ifdef ATA_DEBUG
#define DPRINTK(fmt, args...) printk(KERN_ERR "%s: " fmt, __FUNCTION__, ## args)
#ifdef ATA_VERBOSE_DEBUG
#define VPRINTK(fmt, args...) printk(KERN_ERR "%s: " fmt, __FUNCTION__, ## args)
#else
#define VPRINTK(fmt, args...)
#endif	/* ATA_VERBOSE_DEBUG */
#else
#define DPRINTK(fmt, args...)
#define VPRINTK(fmt, args...)
#endif	/* ATA_DEBUG */

#define BPRINTK(fmt, args...) if (ap->flags & ATA_FLAG_DEBUGMSG) printk(KERN_ERR "%s: " fmt, __FUNCTION__, ## args)

/* NEW: debug levels */
#define HAVE_LIBATA_MSG 1

enum {
	ATA_MSG_DRV	= 0x0001,
	ATA_MSG_INFO	= 0x0002,
	ATA_MSG_PROBE	= 0x0004,
	ATA_MSG_WARN	= 0x0008,
	ATA_MSG_MALLOC	= 0x0010,
	ATA_MSG_CTL	= 0x0020,
	ATA_MSG_INTR	= 0x0040,
	ATA_MSG_ERR	= 0x0080,
};

#define ata_msg_drv(p)    ((p)->msg_enable & ATA_MSG_DRV)
#define ata_msg_info(p)   ((p)->msg_enable & ATA_MSG_INFO)
#define ata_msg_probe(p)  ((p)->msg_enable & ATA_MSG_PROBE)
#define ata_msg_warn(p)   ((p)->msg_enable & ATA_MSG_WARN)
#define ata_msg_malloc(p) ((p)->msg_enable & ATA_MSG_MALLOC)
#define ata_msg_ctl(p)    ((p)->msg_enable & ATA_MSG_CTL)
#define ata_msg_intr(p)   ((p)->msg_enable & ATA_MSG_INTR)
#define ata_msg_err(p)    ((p)->msg_enable & ATA_MSG_ERR)

static inline u32 ata_msg_init(int dval, int default_msg_enable_bits)
{
	if (dval < 0 || dval >= (sizeof(u32) * 8))
		return default_msg_enable_bits; /* should be 0x1 - only driver info msgs */
	if (!dval)
		return 0;
	return (1 << dval) - 1;
}

/* defines only for the constants which don't work well as enums */
#define ATA_TAG_POISON		0xfafbfcfdU

enum {
	/* various global constants */
	LIBATA_MAX_PRD		= ATA_MAX_PRD / 2,
	LIBATA_DUMB_MAX_PRD	= ATA_MAX_PRD / 4,	/* Worst case */
	ATA_MAX_PORTS		= 8,
	ATA_DEF_QUEUE		= 1,
	/* tag ATA_MAX_QUEUE - 1 is reserved for internal commands */
	ATA_MAX_QUEUE		= 32,
	ATA_TAG_INTERNAL	= ATA_MAX_QUEUE - 1,
	ATA_MAX_BUS		= 2,
	ATA_DEF_BUSY_WAIT	= 10000,
	ATA_SHORT_PAUSE		= (HZ >> 6) + 1,

	ATAPI_MAX_DRAIN		= 16 << 10,

	ATA_SHT_EMULATED	= 1,
	ATA_SHT_CMD_PER_LUN	= 1,
	ATA_SHT_THIS_ID		= -1,
	ATA_SHT_USE_CLUSTERING	= 1,

	/* struct ata_device stuff */
	ATA_DFLAG_LBA		= (1 << 0), /* device supports LBA */
	ATA_DFLAG_LBA48		= (1 << 1), /* device supports LBA48 */
	ATA_DFLAG_CDB_INTR	= (1 << 2), /* device asserts INTRQ when ready for CDB */
	ATA_DFLAG_NCQ		= (1 << 3), /* device supports NCQ */
	ATA_DFLAG_FLUSH_EXT	= (1 << 4), /* do FLUSH_EXT instead of FLUSH */
	ATA_DFLAG_ACPI_PENDING	= (1 << 5), /* ACPI resume action pending */
	ATA_DFLAG_ACPI_FAILED	= (1 << 6), /* ACPI on devcfg has failed */
	ATA_DFLAG_AN		= (1 << 7), /* AN configured */
	ATA_DFLAG_HIPM		= (1 << 8), /* device supports HIPM */
	ATA_DFLAG_DIPM		= (1 << 9), /* device supports DIPM */
	ATA_DFLAG_DMADIR	= (1 << 10), /* device requires DMADIR */
	ATA_DFLAG_CFG_MASK	= (1 << 12) - 1,

	ATA_DFLAG_PIO		= (1 << 12), /* device limited to PIO mode */
	ATA_DFLAG_NCQ_OFF	= (1 << 13), /* device limited to non-NCQ mode */
	ATA_DFLAG_SPUNDOWN	= (1 << 14), /* XXX: for spindown_compat */
	ATA_DFLAG_SLEEPING	= (1 << 15), /* device is sleeping */
	ATA_DFLAG_DUBIOUS_XFER	= (1 << 16), /* data transfer not verified */
	ATA_DFLAG_INIT_MASK	= (1 << 24) - 1,

	ATA_DFLAG_DETACH	= (1 << 24),
	ATA_DFLAG_DETACHED	= (1 << 25),

	ATA_DEV_UNKNOWN		= 0,	/* unknown device */
	ATA_DEV_ATA		= 1,	/* ATA device */
	ATA_DEV_ATA_UNSUP	= 2,	/* ATA device (unsupported) */
	ATA_DEV_ATAPI		= 3,	/* ATAPI device */
	ATA_DEV_ATAPI_UNSUP	= 4,	/* ATAPI device (unsupported) */
	ATA_DEV_PMP		= 5,	/* SATA port multiplier */
	ATA_DEV_PMP_UNSUP	= 6,	/* SATA port multiplier (unsupported) */
	ATA_DEV_SEMB		= 7,	/* SEMB */
	ATA_DEV_SEMB_UNSUP	= 8,	/* SEMB (unsupported) */
	ATA_DEV_NONE		= 9,	/* no device */

	/* struct ata_link flags */
	ATA_LFLAG_HRST_TO_RESUME = (1 << 0), /* hardreset to resume link */
	ATA_LFLAG_SKIP_D2H_BSY	= (1 << 1), /* can't wait for the first D2H
					     * Register FIS clearing BSY */
	ATA_LFLAG_NO_SRST	= (1 << 2), /* avoid softreset */
	ATA_LFLAG_ASSUME_ATA	= (1 << 3), /* assume ATA class */
	ATA_LFLAG_ASSUME_SEMB	= (1 << 4), /* assume SEMB class */
	ATA_LFLAG_ASSUME_CLASS	= ATA_LFLAG_ASSUME_ATA | ATA_LFLAG_ASSUME_SEMB,
	ATA_LFLAG_NO_RETRY	= (1 << 5), /* don't retry this link */
	ATA_LFLAG_DISABLED	= (1 << 6), /* link is disabled */

	/* struct ata_port flags */
	ATA_FLAG_SLAVE_POSS	= (1 << 0), /* host supports slave dev */
					    /* (doesn't imply presence) */
	ATA_FLAG_SATA		= (1 << 1),
	ATA_FLAG_NO_LEGACY	= (1 << 2), /* no legacy mode check */
	ATA_FLAG_MMIO		= (1 << 3), /* use MMIO, not PIO */
	ATA_FLAG_SRST		= (1 << 4), /* (obsolete) use ATA SRST, not E.D.D. */
	ATA_FLAG_SATA_RESET	= (1 << 5), /* (obsolete) use COMRESET */
	ATA_FLAG_NO_ATAPI	= (1 << 6), /* No ATAPI support */
	ATA_FLAG_PIO_DMA	= (1 << 7), /* PIO cmds via DMA */
	ATA_FLAG_PIO_LBA48	= (1 << 8), /* Host DMA engine is LBA28 only */
	ATA_FLAG_PIO_POLLING	= (1 << 9), /* use polling PIO if LLD
					     * doesn't handle PIO interrupts */
	ATA_FLAG_NCQ		= (1 << 10), /* host supports NCQ */
	ATA_FLAG_DEBUGMSG	= (1 << 13),
	ATA_FLAG_IGN_SIMPLEX	= (1 << 15), /* ignore SIMPLEX */
	ATA_FLAG_NO_IORDY	= (1 << 16), /* controller lacks iordy */
	ATA_FLAG_ACPI_SATA	= (1 << 17), /* need native SATA ACPI layout */
	ATA_FLAG_AN		= (1 << 18), /* controller supports AN */
	ATA_FLAG_PMP		= (1 << 19), /* controller supports PMP */
	ATA_FLAG_IPM		= (1 << 20), /* driver can handle IPM */

	/* The following flag belongs to ap->pflags but is kept in
	 * ap->flags because it's referenced in many LLDs and will be
	 * removed in not-too-distant future.
	 */
	ATA_FLAG_DISABLED	= (1 << 23), /* port is disabled, ignore it */

	/* bits 24:31 of ap->flags are reserved for LLD specific flags */

	/* struct ata_port pflags */
	ATA_PFLAG_EH_PENDING	= (1 << 0), /* EH pending */
	ATA_PFLAG_EH_IN_PROGRESS = (1 << 1), /* EH in progress */
	ATA_PFLAG_FROZEN	= (1 << 2), /* port is frozen */
	ATA_PFLAG_RECOVERED	= (1 << 3), /* recovery action performed */
	ATA_PFLAG_LOADING	= (1 << 4), /* boot/loading probe */
	ATA_PFLAG_UNLOADING	= (1 << 5), /* module is unloading */
	ATA_PFLAG_SCSI_HOTPLUG	= (1 << 6), /* SCSI hotplug scheduled */
	ATA_PFLAG_INITIALIZING	= (1 << 7), /* being initialized, don't touch */
	ATA_PFLAG_RESETTING	= (1 << 8), /* reset in progress */

	ATA_PFLAG_SUSPENDED	= (1 << 17), /* port is suspended (power) */
	ATA_PFLAG_PM_PENDING	= (1 << 18), /* PM operation pending */
	ATA_PFLAG_INIT_GTM_VALID = (1 << 19), /* initial gtm data valid */

	/* struct ata_queued_cmd flags */
	ATA_QCFLAG_ACTIVE	= (1 << 0), /* cmd not yet ack'd to scsi lyer */
	ATA_QCFLAG_DMAMAP	= (1 << 1), /* SG table is DMA mapped */
	ATA_QCFLAG_IO		= (1 << 3), /* standard IO command */
	ATA_QCFLAG_RESULT_TF	= (1 << 4), /* result TF requested */
	ATA_QCFLAG_CLEAR_EXCL	= (1 << 5), /* clear excl_link on completion */
	ATA_QCFLAG_QUIET	= (1 << 6), /* don't report device error */

	ATA_QCFLAG_FAILED	= (1 << 16), /* cmd failed and is owned by EH */
	ATA_QCFLAG_SENSE_VALID	= (1 << 17), /* sense data valid */
	ATA_QCFLAG_EH_SCHEDULED = (1 << 18), /* EH scheduled (obsolete) */

	/* host set flags */
	ATA_HOST_SIMPLEX	= (1 << 0),	/* Host is simplex, one DMA channel per host only */
	ATA_HOST_STARTED	= (1 << 1),	/* Host started */

	/* bits 24:31 of host->flags are reserved for LLD specific flags */

	/* various lengths of time */
	ATA_TMOUT_BOOT		= 30 * HZ,	/* heuristic */
	ATA_TMOUT_BOOT_QUICK	= 7 * HZ,	/* heuristic */
	ATA_TMOUT_INTERNAL	= 30 * HZ,
	ATA_TMOUT_INTERNAL_QUICK = 5 * HZ,

	/* FIXME: GoVault needs 2s but we can't afford that without
	 * parallel probing.  800ms is enough for iVDR disk
	 * HHD424020F7SV00.  Increase to 2secs when parallel probing
	 * is in place.
	 */
	ATA_TMOUT_FF_WAIT	= 4 * HZ / 5,

	/* ATA bus states */
	BUS_UNKNOWN		= 0,
	BUS_DMA			= 1,
	BUS_IDLE		= 2,
	BUS_NOINTR		= 3,
	BUS_NODATA		= 4,
	BUS_TIMER		= 5,
	BUS_PIO			= 6,
	BUS_EDD			= 7,
	BUS_IDENTIFY		= 8,
	BUS_PACKET		= 9,

	/* SATA port states */
	PORT_UNKNOWN		= 0,
	PORT_ENABLED		= 1,
	PORT_DISABLED		= 2,

	/* encoding various smaller bitmaps into a single
	 * unsigned long bitmap
	 */
	ATA_NR_PIO_MODES	= 7,
	ATA_NR_MWDMA_MODES	= 5,
	ATA_NR_UDMA_MODES	= 8,

	ATA_SHIFT_PIO		= 0,
	ATA_SHIFT_MWDMA		= ATA_SHIFT_PIO + ATA_NR_PIO_MODES,
	ATA_SHIFT_UDMA		= ATA_SHIFT_MWDMA + ATA_NR_MWDMA_MODES,

	/* size of buffer to pad xfers ending on unaligned boundaries */
	ATA_DMA_PAD_SZ		= 4,

	/* ering size */
	ATA_ERING_SIZE		= 32,

	/* return values for ->qc_defer */
	ATA_DEFER_LINK		= 1,
	ATA_DEFER_PORT		= 2,

	/* desc_len for ata_eh_info and context */
	ATA_EH_DESC_LEN		= 80,

	/* reset / recovery action types */
	ATA_EH_REVALIDATE	= (1 << 0),
	ATA_EH_SOFTRESET	= (1 << 1),
	ATA_EH_HARDRESET	= (1 << 2),
	ATA_EH_ENABLE_LINK	= (1 << 3),

	ATA_EH_RESET_MASK	= ATA_EH_SOFTRESET | ATA_EH_HARDRESET,
	ATA_EH_PERDEV_MASK	= ATA_EH_REVALIDATE,

	/* ata_eh_info->flags */
	ATA_EHI_HOTPLUGGED	= (1 << 0),  /* could have been hotplugged */
	ATA_EHI_RESUME_LINK	= (1 << 1),  /* resume link (reset modifier) */
	ATA_EHI_NO_AUTOPSY	= (1 << 2),  /* no autopsy */
	ATA_EHI_QUIET		= (1 << 3),  /* be quiet */
	ATA_EHI_LPM		= (1 << 4),  /* link power management action */

	ATA_EHI_DID_SOFTRESET	= (1 << 16), /* already soft-reset this port */
	ATA_EHI_DID_HARDRESET	= (1 << 17), /* already soft-reset this port */
	ATA_EHI_PRINTINFO	= (1 << 18), /* print configuration info */
	ATA_EHI_SETMODE		= (1 << 19), /* configure transfer mode */
	ATA_EHI_POST_SETMODE	= (1 << 20), /* revaildating after setmode */

	ATA_EHI_DID_RESET	= ATA_EHI_DID_SOFTRESET | ATA_EHI_DID_HARDRESET,
	ATA_EHI_RESET_MODIFIER_MASK = ATA_EHI_RESUME_LINK,

	/* max tries if error condition is still set after ->error_handler */
	ATA_EH_MAX_TRIES	= 5,

	/* how hard are we gonna try to probe/recover devices */
	ATA_PROBE_MAX_TRIES	= 3,
	ATA_EH_DEV_TRIES	= 3,
	ATA_EH_PMP_TRIES	= 5,
	ATA_EH_PMP_LINK_TRIES	= 3,

	SATA_PMP_SCR_TIMEOUT	= 250,

	/* Horkage types. May be set by libata or controller on drives
	   (some horkage may be drive/controller pair dependant */

	ATA_HORKAGE_DIAGNOSTIC	= (1 << 0),	/* Failed boot diag */
	ATA_HORKAGE_NODMA	= (1 << 1),	/* DMA problems */
	ATA_HORKAGE_NONCQ	= (1 << 2),	/* Don't use NCQ */
	ATA_HORKAGE_MAX_SEC_128	= (1 << 3),	/* Limit max sects to 128 */
	ATA_HORKAGE_BROKEN_HPA	= (1 << 4),	/* Broken HPA */
	ATA_HORKAGE_SKIP_PM	= (1 << 5),	/* Skip PM operations */
	ATA_HORKAGE_HPA_SIZE	= (1 << 6),	/* native size off by one */
	ATA_HORKAGE_IPM		= (1 << 7),	/* Link PM problems */
	ATA_HORKAGE_IVB		= (1 << 8),	/* cbl det validity bit bugs */
	ATA_HORKAGE_STUCK_ERR	= (1 << 9),	/* stuck ERR on next PACKET */

	 /* DMA mask for user DMA control: User visible values; DO NOT
	    renumber */
	ATA_DMA_MASK_ATA	= (1 << 0),	/* DMA on ATA Disk */
	ATA_DMA_MASK_ATAPI	= (1 << 1),	/* DMA on ATAPI */
	ATA_DMA_MASK_CFA	= (1 << 2),	/* DMA on CF Card */

	/* ATAPI command types */
	ATAPI_READ		= 0,		/* READs */
	ATAPI_WRITE		= 1,		/* WRITEs */
	ATAPI_READ_CD		= 2,		/* READ CD [MSF] */
	ATAPI_MISC		= 3,		/* the rest */
};

enum ata_xfer_mask {
	ATA_MASK_PIO		= ((1LU << ATA_NR_PIO_MODES) - 1)
					<< ATA_SHIFT_PIO,
	ATA_MASK_MWDMA		= ((1LU << ATA_NR_MWDMA_MODES) - 1)
					<< ATA_SHIFT_MWDMA,
	ATA_MASK_UDMA		= ((1LU << ATA_NR_UDMA_MODES) - 1)
					<< ATA_SHIFT_UDMA,
};

enum hsm_task_states {
	HSM_ST_IDLE,		/* no command on going */
	HSM_ST_FIRST,		/* (waiting the device to)
				   write CDB or first data block */
	HSM_ST,			/* (waiting the device to) transfer data */
	HSM_ST_LAST,		/* (waiting the device to) complete command */
	HSM_ST_ERR,		/* error */
};

enum ata_completion_errors {
	AC_ERR_DEV		= (1 << 0), /* device reported error */
	AC_ERR_HSM		= (1 << 1), /* host state machine violation */
	AC_ERR_TIMEOUT		= (1 << 2), /* timeout */
	AC_ERR_MEDIA		= (1 << 3), /* media error */
	AC_ERR_ATA_BUS		= (1 << 4), /* ATA bus error */
	AC_ERR_HOST_BUS		= (1 << 5), /* host bus error */
	AC_ERR_SYSTEM		= (1 << 6), /* system error */
	AC_ERR_INVALID		= (1 << 7), /* invalid argument */
	AC_ERR_OTHER		= (1 << 8), /* unknown */
	AC_ERR_NODEV_HINT	= (1 << 9), /* polling device detection hint */
	AC_ERR_NCQ		= (1 << 10), /* marker for offending NCQ qc */
};

/* forward declarations */
struct scsi_device;
struct ata_port_operations;
struct ata_port;
struct ata_link;
struct ata_queued_cmd;

/* typedefs */
typedef void (*ata_qc_cb_t) (struct ata_queued_cmd *qc);
typedef int (*ata_prereset_fn_t)(struct ata_link *link, unsigned long deadline);
typedef int (*ata_reset_fn_t)(struct ata_link *link, unsigned int *classes,
			      unsigned long deadline);
typedef void (*ata_postreset_fn_t)(struct ata_link *link, unsigned int *classes);

/*
 * host pm policy: If you alter this, you also need to alter libata-scsi.c
 * (for the ascii descriptions)
 */
enum link_pm {
	NOT_AVAILABLE,
	MIN_POWER,
	MAX_PERFORMANCE,
	MEDIUM_POWER,
};
extern struct class_device_attribute class_device_attr_link_power_management_policy;

struct ata_ioports {
	void __iomem		*cmd_addr;
	void __iomem		*data_addr;
	void __iomem		*error_addr;
	void __iomem		*feature_addr;
	void __iomem		*nsect_addr;
	void __iomem		*lbal_addr;
	void __iomem		*lbam_addr;
	void __iomem		*lbah_addr;
	void __iomem		*device_addr;
	void __iomem		*status_addr;
	void __iomem		*command_addr;
	void __iomem		*altstatus_addr;
	void __iomem		*ctl_addr;
	void __iomem		*bmdma_addr;
	void __iomem		*scr_addr;
};

struct ata_host {
	spinlock_t		lock;
	struct device 		*dev;
	void __iomem * const	*iomap;
	unsigned int		n_ports;
	void			*private_data;
	const struct ata_port_operations *ops;
	unsigned long		flags;
#ifdef CONFIG_ATA_ACPI
	acpi_handle		acpi_handle;
#endif
	struct ata_port		*simplex_claimed;	/* channel owning the DMA */
	struct ata_port		*ports[0];
};

struct ata_queued_cmd {
	struct ata_port		*ap;
	struct ata_device	*dev;

	struct scsi_cmnd	*scsicmd;
	void			(*scsidone)(struct scsi_cmnd *);

	struct ata_taskfile	tf;
	u8			cdb[ATAPI_CDB_LEN];

	unsigned long		flags;		/* ATA_QCFLAG_xxx */
	unsigned int		tag;
	unsigned int		n_elem;

	int			dma_dir;

	unsigned int		sect_size;

	unsigned int		nbytes;
	unsigned int		extrabytes;
	unsigned int		curbytes;

	struct scatterlist	*cursg;
	unsigned int		cursg_ofs;

	struct scatterlist	sgent;

	struct scatterlist	*sg;

	unsigned int		err_mask;
	struct ata_taskfile	result_tf;
	ata_qc_cb_t		complete_fn;

	void			*private_data;
	void			*lldd_task;
};

struct ata_port_stats {
	unsigned long		unhandled_irq;
	unsigned long		idle_irq;
	unsigned long		rw_reqbuf;
};

struct ata_ering_entry {
	unsigned int		eflags;
	unsigned int		err_mask;
	u64			timestamp;
};

struct ata_ering {
	int			cursor;
	struct ata_ering_entry	ring[ATA_ERING_SIZE];
};

struct ata_device {
	struct ata_link		*link;
	unsigned int		devno;		/* 0 or 1 */
	unsigned long		flags;		/* ATA_DFLAG_xxx */
	unsigned int		horkage;	/* List of broken features */
	struct scsi_device	*sdev;		/* attached SCSI device */
#ifdef CONFIG_ATA_ACPI
	acpi_handle		acpi_handle;
	union acpi_object	*gtf_cache;
#endif
	/* n_sector is used as CLEAR_OFFSET, read comment above CLEAR_OFFSET */
	u64			n_sectors;	/* size of device, if ATA */
	unsigned int		class;		/* ATA_DEV_xxx */

	union {
		u16		id[ATA_ID_WORDS]; /* IDENTIFY xxx DEVICE data */
		u32		gscr[SATA_PMP_GSCR_DWORDS]; /* PMP GSCR block */
	};

	u8			pio_mode;
	u8			dma_mode;
	u8			xfer_mode;
	unsigned int		xfer_shift;	/* ATA_SHIFT_xxx */

	unsigned int		multi_count;	/* sectors count for
						   READ/WRITE MULTIPLE */
	unsigned int		max_sectors;	/* per-device max sectors */
	unsigned int		cdb_len;

	/* per-dev xfer mask */
	unsigned long		pio_mask;
	unsigned long		mwdma_mask;
	unsigned long		udma_mask;

	/* for CHS addressing */
	u16			cylinders;	/* Number of cylinders */
	u16			heads;		/* Number of heads */
	u16			sectors;	/* Number of sectors per track */

	/* error history */
	struct ata_ering	ering;
	int			spdn_cnt;
};

/* Offset into struct ata_device.  Fields above it are maintained
 * acress device init.  Fields below are zeroed.
 */
#define ATA_DEVICE_CLEAR_OFFSET		offsetof(struct ata_device, n_sectors)

struct ata_eh_info {
	struct ata_device	*dev;		/* offending device */
	u32			serror;		/* SError from LLDD */
	unsigned int		err_mask;	/* port-wide err_mask */
	unsigned int		action;		/* ATA_EH_* action mask */
	unsigned int		dev_action[ATA_MAX_DEVICES]; /* dev EH action */
	unsigned int		flags;		/* ATA_EHI_* flags */

	unsigned int		probe_mask;

	char			desc[ATA_EH_DESC_LEN];
	int			desc_len;
};

struct ata_eh_context {
	struct ata_eh_info	i;
	int			tries[ATA_MAX_DEVICES];
	unsigned int		classes[ATA_MAX_DEVICES];
	unsigned int		did_probe_mask;
	unsigned int		saved_ncq_enabled;
	u8			saved_xfer_mode[ATA_MAX_DEVICES];
};

struct ata_acpi_drive
{
	u32 pio;
	u32 dma;
} __packed;

struct ata_acpi_gtm {
	struct ata_acpi_drive drive[2];
	u32 flags;
} __packed;

struct ata_link {
	struct ata_port		*ap;
	int			pmp;		/* port multiplier port # */

	unsigned int		active_tag;	/* active tag on this link */
	u32			sactive;	/* active NCQ commands */

	unsigned int		flags;		/* ATA_LFLAG_xxx */

	unsigned int		hw_sata_spd_limit;
	unsigned int		sata_spd_limit;
	unsigned int		sata_spd;	/* current SATA PHY speed */

	/* record runtime error info, protected by host_set lock */
	struct ata_eh_info	eh_info;
	/* EH context */
	struct ata_eh_context	eh_context;

	struct ata_device	device[ATA_MAX_DEVICES];
};

struct ata_port {
	struct Scsi_Host	*scsi_host; /* our co-allocated scsi host */
	const struct ata_port_operations *ops;
	spinlock_t		*lock;
	unsigned long		flags;	/* ATA_FLAG_xxx */
	unsigned int		pflags; /* ATA_PFLAG_xxx */
	unsigned int		print_id; /* user visible unique port ID */
	unsigned int		port_no; /* 0 based port no. inside the host */

	struct ata_prd		*prd;	 /* our SG list */
	dma_addr_t		prd_dma; /* and its DMA mapping */

	struct ata_ioports	ioaddr;	/* ATA cmd/ctl/dma register blocks */

	u8			ctl;	/* cache of ATA control register */
	u8			last_ctl;	/* Cache last written value */
	unsigned int		pio_mask;
	unsigned int		mwdma_mask;
	unsigned int		udma_mask;
	unsigned int		cbl;	/* cable type; ATA_CBL_xxx */

	struct ata_queued_cmd	qcmd[ATA_MAX_QUEUE];
	unsigned long		qc_allocated;
	unsigned int		qc_active;
	int			nr_active_links; /* #links with active qcs */

	struct ata_link		link;	/* host default link */

	int			nr_pmp_links;	/* nr of available PMP links */
	struct ata_link		*pmp_link;	/* array of PMP links */
	struct ata_link		*excl_link;	/* for PMP qc exclusion */

	struct ata_port_stats	stats;
	struct ata_host		*host;
	struct device 		*dev;

	void			*port_task_data;
	struct delayed_work	port_task;
	struct delayed_work	hotplug_task;
	struct work_struct	scsi_rescan_task;

	unsigned int		hsm_task_state;

	u32			msg_enable;
	struct list_head	eh_done_q;
	wait_queue_head_t	eh_wait_q;
	int			eh_tries;

	pm_message_t		pm_mesg;
	int			*pm_result;
	enum link_pm		pm_policy;

	struct timer_list	fastdrain_timer;
	unsigned long		fastdrain_cnt;

	void			*private_data;

#ifdef CONFIG_ATA_ACPI
	acpi_handle		acpi_handle;
	struct ata_acpi_gtm	__acpi_init_gtm; /* use ata_acpi_init_gtm() */
#endif
	u8			sector_buf[ATA_SECT_SIZE]; /* owned by EH */
};

struct ata_port_operations {
	void (*dev_config) (struct ata_device *);

	void (*set_piomode) (struct ata_port *, struct ata_device *);
	void (*set_dmamode) (struct ata_port *, struct ata_device *);
	unsigned long (*mode_filter) (struct ata_device *, unsigned long);

	void (*tf_load) (struct ata_port *ap, const struct ata_taskfile *tf);
	void (*tf_read) (struct ata_port *ap, struct ata_taskfile *tf);

	void (*exec_command)(struct ata_port *ap, const struct ata_taskfile *tf);
	u8   (*check_status)(struct ata_port *ap);
	u8   (*check_altstatus)(struct ata_port *ap);
	void (*dev_select)(struct ata_port *ap, unsigned int device);

	void (*phy_reset) (struct ata_port *ap); /* obsolete */
	int  (*set_mode) (struct ata_link *link, struct ata_device **r_failed_dev);

	int (*cable_detect) (struct ata_port *ap);

	int  (*check_atapi_dma) (struct ata_queued_cmd *qc);

	void (*bmdma_setup) (struct ata_queued_cmd *qc);
	void (*bmdma_start) (struct ata_queued_cmd *qc);

	unsigned int (*data_xfer) (struct ata_device *dev, unsigned char *buf,
				   unsigned int buflen, int rw);

	int (*qc_defer) (struct ata_queued_cmd *qc);
	void (*qc_prep) (struct ata_queued_cmd *qc);
	unsigned int (*qc_issue) (struct ata_queued_cmd *qc);

	/* port multiplier */
	void (*pmp_attach) (struct ata_port *ap);
	void (*pmp_detach) (struct ata_port *ap);

	/* Error handlers.  ->error_handler overrides ->eng_timeout and
	 * indicates that new-style EH is in place.
	 */
	void (*eng_timeout) (struct ata_port *ap); /* obsolete */

	void (*freeze) (struct ata_port *ap);
	void (*thaw) (struct ata_port *ap);
	void (*error_handler) (struct ata_port *ap);
	void (*post_internal_cmd) (struct ata_queued_cmd *qc);

	irq_handler_t irq_handler;
	void (*irq_clear) (struct ata_port *);
	u8 (*irq_on) (struct ata_port *);

	int (*scr_read) (struct ata_port *ap, unsigned int sc_reg, u32 *val);
	int (*scr_write) (struct ata_port *ap, unsigned int sc_reg, u32 val);

	int (*port_suspend) (struct ata_port *ap, pm_message_t mesg);
	int (*port_resume) (struct ata_port *ap);
	int (*enable_pm) (struct ata_port *ap, enum link_pm policy);
	void (*disable_pm) (struct ata_port *ap);
	int (*port_start) (struct ata_port *ap);
	void (*port_stop) (struct ata_port *ap);

	void (*host_stop) (struct ata_host *host);

	void (*bmdma_stop) (struct ata_queued_cmd *qc);
	u8   (*bmdma_status) (struct ata_port *ap);
};

struct ata_port_info {
	struct scsi_host_template	*sht;
	unsigned long		flags;
	unsigned long		link_flags;
	unsigned long		pio_mask;
	unsigned long		mwdma_mask;
	unsigned long		udma_mask;
	const struct ata_port_operations *port_ops;
	irq_handler_t		irq_handler;
	void 			*private_data;
};

struct ata_timing {
	unsigned short mode;		/* ATA mode */
	unsigned short setup;		/* t1 */
	unsigned short act8b;		/* t2 for 8-bit I/O */
	unsigned short rec8b;		/* t2i for 8-bit I/O */
	unsigned short cyc8b;		/* t0 for 8-bit I/O */
	unsigned short active;		/* t2 or tD */
	unsigned short recover;		/* t2i or tK */
	unsigned short cycle;		/* t0 */
	unsigned short udma;		/* t2CYCTYP/2 */
};

#define FIT(v, vmin, vmax)	max_t(short, min_t(short, v, vmax), vmin)

extern const unsigned long sata_deb_timing_normal[];
extern const unsigned long sata_deb_timing_hotplug[];
extern const unsigned long sata_deb_timing_long[];

extern const struct ata_port_operations ata_dummy_port_ops;
extern const struct ata_port_info ata_dummy_port_info;

static inline const unsigned long *
sata_ehc_deb_timing(struct ata_eh_context *ehc)
{
	if (ehc->i.flags & ATA_EHI_HOTPLUGGED)
		return sata_deb_timing_hotplug;
	else
		return sata_deb_timing_normal;
}

static inline int ata_port_is_dummy(struct ata_port *ap)
{
	return ap->ops == &ata_dummy_port_ops;
}

extern void sata_print_link_status(struct ata_link *link);
extern void ata_port_probe(struct ata_port *);
extern void ata_bus_reset(struct ata_port *ap);
extern int sata_set_spd(struct ata_link *link);
extern int sata_link_debounce(struct ata_link *link,
			const unsigned long *params, unsigned long deadline);
extern int sata_link_resume(struct ata_link *link, const unsigned long *params,
			    unsigned long deadline);
extern int ata_std_prereset(struct ata_link *link, unsigned long deadline);
extern int ata_std_softreset(struct ata_link *link, unsigned int *classes,
			     unsigned long deadline);
extern int sata_link_hardreset(struct ata_link *link,
			const unsigned long *timing, unsigned long deadline);
extern int sata_std_hardreset(struct ata_link *link, unsigned int *class,
			      unsigned long deadline);
extern void ata_std_postreset(struct ata_link *link, unsigned int *classes);
extern void ata_port_disable(struct ata_port *);
extern void ata_std_ports(struct ata_ioports *ioaddr);

extern struct ata_host *ata_host_alloc(struct device *dev, int max_ports);
extern struct ata_host *ata_host_alloc_pinfo(struct device *dev,
			const struct ata_port_info * const * ppi, int n_ports);
extern int ata_host_start(struct ata_host *host);
extern int ata_host_register(struct ata_host *host,
			     struct scsi_host_template *sht);
extern int ata_host_activate(struct ata_host *host, int irq,
			     irq_handler_t irq_handler, unsigned long irq_flags,
			     struct scsi_host_template *sht);
extern void ata_host_detach(struct ata_host *host);
extern void ata_host_init(struct ata_host *, struct device *,
			  unsigned long, const struct ata_port_operations *);
extern int ata_scsi_detect(struct scsi_host_template *sht);
extern int ata_scsi_ioctl(struct scsi_device *dev, int cmd, void __user *arg);
extern int ata_scsi_queuecmd(struct scsi_cmnd *cmd, void (*done)(struct scsi_cmnd *));
extern void ata_sas_port_destroy(struct ata_port *);
extern struct ata_port *ata_sas_port_alloc(struct ata_host *,
					   struct ata_port_info *, struct Scsi_Host *);
extern int ata_sas_port_init(struct ata_port *);
extern int ata_sas_port_start(struct ata_port *ap);
extern void ata_sas_port_stop(struct ata_port *ap);
extern int ata_sas_slave_configure(struct scsi_device *, struct ata_port *);
extern int ata_sas_queuecmd(struct scsi_cmnd *cmd, void (*done)(struct scsi_cmnd *),
			    struct ata_port *ap);
extern unsigned int ata_host_intr(struct ata_port *ap, struct ata_queued_cmd *qc);
extern int sata_scr_valid(struct ata_link *link);
extern int sata_scr_read(struct ata_link *link, int reg, u32 *val);
extern int sata_scr_write(struct ata_link *link, int reg, u32 val);
extern int sata_scr_write_flush(struct ata_link *link, int reg, u32 val);
extern int ata_link_online(struct ata_link *link);
extern int ata_link_offline(struct ata_link *link);
#ifdef CONFIG_PM
extern int ata_host_suspend(struct ata_host *host, pm_message_t mesg);
extern void ata_host_resume(struct ata_host *host);
#endif
extern int ata_ratelimit(void);
extern int ata_busy_sleep(struct ata_port *ap,
			  unsigned long timeout_pat, unsigned long timeout);
extern void ata_wait_after_reset(struct ata_port *ap, unsigned long deadline);
extern int ata_wait_ready(struct ata_port *ap, unsigned long deadline);
extern u32 ata_wait_register(void __iomem *reg, u32 mask, u32 val,
			     unsigned long interval_msec,
			     unsigned long timeout_msec);
extern unsigned int ata_dev_try_classify(struct ata_device *dev, int present,
					 u8 *r_err);

/*
 * Default driver ops implementations
 */
extern void ata_tf_load(struct ata_port *ap, const struct ata_taskfile *tf);
extern void ata_tf_read(struct ata_port *ap, struct ata_taskfile *tf);
extern void ata_tf_to_fis(const struct ata_taskfile *tf,
			  u8 pmp, int is_cmd, u8 *fis);
extern void ata_tf_from_fis(const u8 *fis, struct ata_taskfile *tf);
extern unsigned long ata_pack_xfermask(unsigned long pio_mask,
			unsigned long mwdma_mask, unsigned long udma_mask);
extern void ata_unpack_xfermask(unsigned long xfer_mask,
			unsigned long *pio_mask, unsigned long *mwdma_mask,
			unsigned long *udma_mask);
extern u8 ata_xfer_mask2mode(unsigned long xfer_mask);
extern unsigned long ata_xfer_mode2mask(u8 xfer_mode);
extern int ata_xfer_mode2shift(unsigned long xfer_mode);
extern const char *ata_mode_string(unsigned long xfer_mask);
extern unsigned long ata_id_xfermask(const u16 *id);
extern void ata_noop_dev_select(struct ata_port *ap, unsigned int device);
extern void ata_std_dev_select(struct ata_port *ap, unsigned int device);
extern u8 ata_check_status(struct ata_port *ap);
extern u8 ata_altstatus(struct ata_port *ap);
extern void ata_exec_command(struct ata_port *ap, const struct ata_taskfile *tf);
extern int ata_port_start(struct ata_port *ap);
extern int ata_sff_port_start(struct ata_port *ap);
extern irqreturn_t ata_interrupt(int irq, void *dev_instance);
extern unsigned int ata_data_xfer(struct ata_device *dev,
			unsigned char *buf, unsigned int buflen, int rw);
extern unsigned int ata_data_xfer_noirq(struct ata_device *dev,
			unsigned char *buf, unsigned int buflen, int rw);
extern int ata_std_qc_defer(struct ata_queued_cmd *qc);
extern void ata_dumb_qc_prep(struct ata_queued_cmd *qc);
extern void ata_qc_prep(struct ata_queued_cmd *qc);
extern void ata_noop_qc_prep(struct ata_queued_cmd *qc);
extern unsigned int ata_qc_issue_prot(struct ata_queued_cmd *qc);
extern void ata_sg_init(struct ata_queued_cmd *qc, struct scatterlist *sg,
		 unsigned int n_elem);
extern unsigned int ata_dev_classify(const struct ata_taskfile *tf);
extern void ata_dev_disable(struct ata_device *adev);
extern void ata_id_string(const u16 *id, unsigned char *s,
			  unsigned int ofs, unsigned int len);
extern void ata_id_c_string(const u16 *id, unsigned char *s,
			    unsigned int ofs, unsigned int len);
extern void ata_bmdma_setup(struct ata_queued_cmd *qc);
extern void ata_bmdma_start(struct ata_queued_cmd *qc);
extern void ata_bmdma_stop(struct ata_queued_cmd *qc);
extern u8   ata_bmdma_status(struct ata_port *ap);
extern void ata_bmdma_irq_clear(struct ata_port *ap);
extern void ata_bmdma_freeze(struct ata_port *ap);
extern void ata_bmdma_thaw(struct ata_port *ap);
extern void ata_bmdma_drive_eh(struct ata_port *ap, ata_prereset_fn_t prereset,
			       ata_reset_fn_t softreset,
			       ata_reset_fn_t hardreset,
			       ata_postreset_fn_t postreset);
extern void ata_bmdma_error_handler(struct ata_port *ap);
extern void ata_bmdma_post_internal_cmd(struct ata_queued_cmd *qc);
extern int ata_hsm_move(struct ata_port *ap, struct ata_queued_cmd *qc,
			u8 status, int in_wq);
extern void ata_qc_complete(struct ata_queued_cmd *qc);
extern int ata_qc_complete_multiple(struct ata_port *ap, u32 qc_active,
				    void (*finish_qc)(struct ata_queued_cmd *));
extern void ata_scsi_simulate(struct ata_device *dev, struct scsi_cmnd *cmd,
			      void (*done)(struct scsi_cmnd *));
extern int ata_std_bios_param(struct scsi_device *sdev,
			      struct block_device *bdev,
			      sector_t capacity, int geom[]);
extern int ata_scsi_slave_config(struct scsi_device *sdev);
extern void ata_scsi_slave_destroy(struct scsi_device *sdev);
extern int ata_scsi_change_queue_depth(struct scsi_device *sdev,
				       int queue_depth);
extern struct ata_device *ata_dev_pair(struct ata_device *adev);
extern int ata_do_set_mode(struct ata_link *link, struct ata_device **r_failed_dev);
extern u8 ata_irq_on(struct ata_port *ap);

extern int ata_cable_40wire(struct ata_port *ap);
extern int ata_cable_80wire(struct ata_port *ap);
extern int ata_cable_sata(struct ata_port *ap);
extern int ata_cable_ignore(struct ata_port *ap);
extern int ata_cable_unknown(struct ata_port *ap);

/*
 * Timing helpers
 */

extern unsigned int ata_pio_need_iordy(const struct ata_device *);
extern const struct ata_timing *ata_timing_find_mode(u8 xfer_mode);
extern int ata_timing_compute(struct ata_device *, unsigned short,
			      struct ata_timing *, int, int);
extern void ata_timing_merge(const struct ata_timing *,
			     const struct ata_timing *, struct ata_timing *,
			     unsigned int);
extern u8 ata_timing_cycle2mode(unsigned int xfer_shift, int cycle);

enum {
	ATA_TIMING_SETUP	= (1 << 0),
	ATA_TIMING_ACT8B	= (1 << 1),
	ATA_TIMING_REC8B	= (1 << 2),
	ATA_TIMING_CYC8B	= (1 << 3),
	ATA_TIMING_8BIT		= ATA_TIMING_ACT8B | ATA_TIMING_REC8B |
				  ATA_TIMING_CYC8B,
	ATA_TIMING_ACTIVE	= (1 << 4),
	ATA_TIMING_RECOVER	= (1 << 5),
	ATA_TIMING_CYCLE	= (1 << 6),
	ATA_TIMING_UDMA		= (1 << 7),
	ATA_TIMING_ALL		= ATA_TIMING_SETUP | ATA_TIMING_ACT8B |
				  ATA_TIMING_REC8B | ATA_TIMING_CYC8B |
				  ATA_TIMING_ACTIVE | ATA_TIMING_RECOVER |
				  ATA_TIMING_CYCLE | ATA_TIMING_UDMA,
};

/* libata-acpi.c */
#ifdef CONFIG_ATA_ACPI
static inline const struct ata_acpi_gtm *ata_acpi_init_gtm(struct ata_port *ap)
{
	if (ap->pflags & ATA_PFLAG_INIT_GTM_VALID)
		return &ap->__acpi_init_gtm;
	return NULL;
}
int ata_acpi_stm(struct ata_port *ap, const struct ata_acpi_gtm *stm);
int ata_acpi_gtm(struct ata_port *ap, struct ata_acpi_gtm *stm);
unsigned long ata_acpi_gtm_xfermask(struct ata_device *dev,
				    const struct ata_acpi_gtm *gtm);
int ata_acpi_cbl_80wire(struct ata_port *ap, const struct ata_acpi_gtm *gtm);
#else
static inline const struct ata_acpi_gtm *ata_acpi_init_gtm(struct ata_port *ap)
{
	return NULL;
}

static inline int ata_acpi_stm(const struct ata_port *ap,
			       struct ata_acpi_gtm *stm)
{
	return -ENOSYS;
}

static inline int ata_acpi_gtm(const struct ata_port *ap,
			       struct ata_acpi_gtm *stm)
{
	return -ENOSYS;
}

static inline unsigned int ata_acpi_gtm_xfermask(struct ata_device *dev,
					const struct ata_acpi_gtm *gtm)
{
	return 0;
}

static inline int ata_acpi_cbl_80wire(struct ata_port *ap,
				      const struct ata_acpi_gtm *gtm)
{
	return 0;
}
#endif

#ifdef CONFIG_PCI
struct pci_dev;

extern int ata_pci_init_one(struct pci_dev *pdev,
			     const struct ata_port_info * const * ppi);
extern void ata_pci_remove_one(struct pci_dev *pdev);
#ifdef CONFIG_PM
extern void ata_pci_device_do_suspend(struct pci_dev *pdev, pm_message_t mesg);
extern int __must_check ata_pci_device_do_resume(struct pci_dev *pdev);
extern int ata_pci_device_suspend(struct pci_dev *pdev, pm_message_t mesg);
extern int ata_pci_device_resume(struct pci_dev *pdev);
#endif
extern int ata_pci_clear_simplex(struct pci_dev *pdev);

struct pci_bits {
	unsigned int		reg;	/* PCI config register to read */
	unsigned int		width;	/* 1 (8 bit), 2 (16 bit), 4 (32 bit) */
	unsigned long		mask;
	unsigned long		val;
};

extern int ata_pci_init_sff_host(struct ata_host *host);
extern int ata_pci_init_bmdma(struct ata_host *host);
extern int ata_pci_prepare_sff_host(struct pci_dev *pdev,
				    const struct ata_port_info * const * ppi,
				    struct ata_host **r_host);
extern int ata_pci_activate_sff_host(struct ata_host *host,
				     irq_handler_t irq_handler,
				     struct scsi_host_template *sht);
extern int pci_test_config_bits(struct pci_dev *pdev, const struct pci_bits *bits);
extern unsigned long ata_pci_default_filter(struct ata_device *dev,
					    unsigned long xfer_mask);
#endif /* CONFIG_PCI */

/*
 * PMP
 */
extern int sata_pmp_qc_defer_cmd_switch(struct ata_queued_cmd *qc);
extern int sata_pmp_std_prereset(struct ata_link *link, unsigned long deadline);
extern int sata_pmp_std_hardreset(struct ata_link *link, unsigned int *class,
				  unsigned long deadline);
extern void sata_pmp_std_postreset(struct ata_link *link, unsigned int *class);
extern void sata_pmp_do_eh(struct ata_port *ap,
		ata_prereset_fn_t prereset, ata_reset_fn_t softreset,
		ata_reset_fn_t hardreset, ata_postreset_fn_t postreset,
		ata_prereset_fn_t pmp_prereset, ata_reset_fn_t pmp_softreset,
		ata_reset_fn_t pmp_hardreset, ata_postreset_fn_t pmp_postreset);

/*
 * EH
 */
extern void ata_port_schedule_eh(struct ata_port *ap);
extern int ata_link_abort(struct ata_link *link);
extern int ata_port_abort(struct ata_port *ap);
extern int ata_port_freeze(struct ata_port *ap);
extern int sata_async_notification(struct ata_port *ap);

extern void ata_eh_freeze_port(struct ata_port *ap);
extern void ata_eh_thaw_port(struct ata_port *ap);

extern void ata_eh_qc_complete(struct ata_queued_cmd *qc);
extern void ata_eh_qc_retry(struct ata_queued_cmd *qc);

extern void ata_do_eh(struct ata_port *ap, ata_prereset_fn_t prereset,
		      ata_reset_fn_t softreset, ata_reset_fn_t hardreset,
		      ata_postreset_fn_t postreset);

/*
 * printk helpers
 */
#define ata_port_printk(ap, lv, fmt, args...) \
	printk("%sata%u: "fmt, lv, (ap)->print_id , ##args)

#define ata_link_printk(link, lv, fmt, args...) do { \
	if ((link)->ap->nr_pmp_links) \
		printk("%sata%u.%02u: "fmt, lv, (link)->ap->print_id,	\
		       (link)->pmp , ##args); \
	else \
		printk("%sata%u: "fmt, lv, (link)->ap->print_id , ##args); \
	} while(0)

#define ata_dev_printk(dev, lv, fmt, args...) \
	printk("%sata%u.%02u: "fmt, lv, (dev)->link->ap->print_id,	\
	       (dev)->link->pmp + (dev)->devno , ##args)

/*
 * ata_eh_info helpers
 */
extern void __ata_ehi_push_desc(struct ata_eh_info *ehi, const char *fmt, ...)
	__attribute__ ((format (printf, 2, 3)));
extern void ata_ehi_push_desc(struct ata_eh_info *ehi, const char *fmt, ...)
	__attribute__ ((format (printf, 2, 3)));
extern void ata_ehi_clear_desc(struct ata_eh_info *ehi);

static inline void ata_ehi_schedule_probe(struct ata_eh_info *ehi)
{
	ehi->flags |= ATA_EHI_RESUME_LINK;
	ehi->action |= ATA_EH_SOFTRESET;
	ehi->probe_mask |= (1 << ATA_MAX_DEVICES) - 1;
}

static inline void ata_ehi_hotplugged(struct ata_eh_info *ehi)
{
	ata_ehi_schedule_probe(ehi);
	ehi->flags |= ATA_EHI_HOTPLUGGED;
	ehi->action |= ATA_EH_ENABLE_LINK;
	ehi->err_mask |= AC_ERR_ATA_BUS;
}

/*
 * port description helpers
 */
extern void ata_port_desc(struct ata_port *ap, const char *fmt, ...)
	__attribute__ ((format (printf, 2, 3)));
#ifdef CONFIG_PCI
extern void ata_port_pbar_desc(struct ata_port *ap, int bar, ssize_t offset,
			       const char *name);
#endif

static inline unsigned int ata_tag_valid(unsigned int tag)
{
	return (tag < ATA_MAX_QUEUE) ? 1 : 0;
}

static inline unsigned int ata_tag_internal(unsigned int tag)
{
	return tag == ATA_MAX_QUEUE - 1;
}

/*
 * device helpers
 */
static inline unsigned int ata_class_enabled(unsigned int class)
{
	return class == ATA_DEV_ATA || class == ATA_DEV_ATAPI ||
		class == ATA_DEV_PMP || class == ATA_DEV_SEMB;
}

static inline unsigned int ata_class_disabled(unsigned int class)
{
	return class == ATA_DEV_ATA_UNSUP || class == ATA_DEV_ATAPI_UNSUP ||
		class == ATA_DEV_PMP_UNSUP || class == ATA_DEV_SEMB_UNSUP;
}

static inline unsigned int ata_class_absent(unsigned int class)
{
	return !ata_class_enabled(class) && !ata_class_disabled(class);
}

static inline unsigned int ata_dev_enabled(const struct ata_device *dev)
{
	return ata_class_enabled(dev->class);
}

static inline unsigned int ata_dev_disabled(const struct ata_device *dev)
{
	return ata_class_disabled(dev->class);
}

static inline unsigned int ata_dev_absent(const struct ata_device *dev)
{
	return ata_class_absent(dev->class);
}

/*
 * link helpers
 */
static inline int ata_is_host_link(const struct ata_link *link)
{
	return link == &link->ap->link;
}

static inline int ata_link_max_devices(const struct ata_link *link)
{
	if (ata_is_host_link(link) && link->ap->flags & ATA_FLAG_SLAVE_POSS)
		return 2;
	return 1;
}

static inline int ata_link_active(struct ata_link *link)
{
	return ata_tag_valid(link->active_tag) || link->sactive;
}

static inline struct ata_link *ata_port_first_link(struct ata_port *ap)
{
	if (ap->nr_pmp_links)
		return ap->pmp_link;
	return &ap->link;
}

static inline struct ata_link *ata_port_next_link(struct ata_link *link)
{
	struct ata_port *ap = link->ap;

	if (link == &ap->link) {
		if (!ap->nr_pmp_links)
			return NULL;
		return ap->pmp_link;
	}

	if (++link < ap->nr_pmp_links + ap->pmp_link)
		return link;
	return NULL;
}

#define __ata_port_for_each_link(lk, ap) \
	for ((lk) = &(ap)->link; (lk); (lk) = ata_port_next_link(lk))

#define ata_port_for_each_link(link, ap) \
	for ((link) = ata_port_first_link(ap); (link); \
	     (link) = ata_port_next_link(link))

#define ata_link_for_each_dev(dev, link) \
	for ((dev) = (link)->device; \
	     (dev) < (link)->device + ata_link_max_devices(link) || ((dev) = NULL); \
	     (dev)++)

#define ata_link_for_each_dev_reverse(dev, link) \
	for ((dev) = (link)->device + ata_link_max_devices(link) - 1; \
	     (dev) >= (link)->device || ((dev) = NULL); (dev)--)

static inline u8 ata_chk_status(struct ata_port *ap)
{
	return ap->ops->check_status(ap);
}

/**
 *	ata_ncq_enabled - Test whether NCQ is enabled
 *	@dev: ATA device to test for
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 *
 *	RETURNS:
 *	1 if NCQ is enabled for @dev, 0 otherwise.
 */
static inline int ata_ncq_enabled(struct ata_device *dev)
{
	return (dev->flags & (ATA_DFLAG_PIO | ATA_DFLAG_NCQ_OFF |
			      ATA_DFLAG_NCQ)) == ATA_DFLAG_NCQ;
}

/**
 *	ata_pause - Flush writes and pause 400 nanoseconds.
 *	@ap: Port to wait for.
 *
 *	LOCKING:
 *	Inherited from caller.
 */

static inline void ata_pause(struct ata_port *ap)
{
	ata_altstatus(ap);
	ndelay(400);
}


/**
 *	ata_busy_wait - Wait for a port status register
 *	@ap: Port to wait for.
 *	@bits: bits that must be clear
 *	@max: number of 10uS waits to perform
 *
 *	Waits up to max*10 microseconds for the selected bits in the port's
 *	status register to be cleared.
 *	Returns final value of status register.
 *
 *	LOCKING:
 *	Inherited from caller.
 */

static inline u8 ata_busy_wait(struct ata_port *ap, unsigned int bits,
			       unsigned int max)
{
	u8 status;

	do {
		udelay(10);
		status = ata_chk_status(ap);
		max--;
	} while (status != 0xff && (status & bits) && (max > 0));

	return status;
}


/**
 *	ata_wait_idle - Wait for a port to be idle.
 *	@ap: Port to wait for.
 *
 *	Waits up to 10ms for port's BUSY and DRQ signals to clear.
 *	Returns final value of status register.
 *
 *	LOCKING:
 *	Inherited from caller.
 */

static inline u8 ata_wait_idle(struct ata_port *ap)
{
	u8 status = ata_busy_wait(ap, ATA_BUSY | ATA_DRQ, 1000);

#ifdef ATA_DEBUG
	if (status != 0xff && (status & (ATA_BUSY | ATA_DRQ)))
		ata_port_printk(ap, KERN_DEBUG, "abnormal Status 0x%X\n",
				status);
#endif

	return status;
}

static inline void ata_qc_set_polling(struct ata_queued_cmd *qc)
{
	qc->tf.ctl |= ATA_NIEN;
}

static inline struct ata_queued_cmd *__ata_qc_from_tag(struct ata_port *ap,
						       unsigned int tag)
{
	if (likely(ata_tag_valid(tag)))
		return &ap->qcmd[tag];
	return NULL;
}

static inline struct ata_queued_cmd *ata_qc_from_tag(struct ata_port *ap,
						     unsigned int tag)
{
	struct ata_queued_cmd *qc = __ata_qc_from_tag(ap, tag);

	if (unlikely(!qc) || !ap->ops->error_handler)
		return qc;

	if ((qc->flags & (ATA_QCFLAG_ACTIVE |
			  ATA_QCFLAG_FAILED)) == ATA_QCFLAG_ACTIVE)
		return qc;

	return NULL;
}

static inline unsigned int ata_qc_raw_nbytes(struct ata_queued_cmd *qc)
{
	return qc->nbytes - min(qc->extrabytes, qc->nbytes);
}

static inline void ata_tf_init(struct ata_device *dev, struct ata_taskfile *tf)
{
	memset(tf, 0, sizeof(*tf));

	tf->ctl = dev->link->ap->ctl;
	if (dev->devno == 0)
		tf->device = ATA_DEVICE_OBS;
	else
		tf->device = ATA_DEVICE_OBS | ATA_DEV1;
}

static inline void ata_qc_reinit(struct ata_queued_cmd *qc)
{
	qc->dma_dir = DMA_NONE;
	qc->sg = NULL;
	qc->flags = 0;
	qc->cursg = NULL;
	qc->cursg_ofs = 0;
	qc->nbytes = qc->extrabytes = qc->curbytes = 0;
	qc->n_elem = 0;
	qc->err_mask = 0;
	qc->sect_size = ATA_SECT_SIZE;

	ata_tf_init(qc->dev, &qc->tf);

	/* init result_tf such that it indicates normal completion */
	qc->result_tf.command = ATA_DRDY;
	qc->result_tf.feature = 0;
}

static inline int ata_try_flush_cache(const struct ata_device *dev)
{
	return ata_id_wcache_enabled(dev->id) ||
	       ata_id_has_flush(dev->id) ||
	       ata_id_has_flush_ext(dev->id);
}

static inline int atapi_cmd_type(u8 opcode)
{
	switch (opcode) {
	case GPCMD_READ_10:
	case GPCMD_READ_12:
		return ATAPI_READ;

	case GPCMD_WRITE_10:
	case GPCMD_WRITE_12:
	case GPCMD_WRITE_AND_VERIFY_10:
		return ATAPI_WRITE;

	case GPCMD_READ_CD:
	case GPCMD_READ_CD_MSF:
		return ATAPI_READ_CD;

	default:
		return ATAPI_MISC;
	}
}

static inline unsigned int ac_err_mask(u8 status)
{
	if (status & (ATA_BUSY | ATA_DRQ))
		return AC_ERR_HSM;
	if (status & (ATA_ERR | ATA_DF))
		return AC_ERR_DEV;
	return 0;
}

static inline unsigned int __ac_err_mask(u8 status)
{
	unsigned int mask = ac_err_mask(status);
	if (mask == 0)
		return AC_ERR_OTHER;
	return mask;
}

static inline struct ata_port *ata_shost_to_port(struct Scsi_Host *host)
{
	return *(struct ata_port **)&host->hostdata[0];
}

#endif /* __LINUX_LIBATA_H__ */
