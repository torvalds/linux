/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Copyright 2003-2005 Red Hat, Inc.  All rights reserved.
 *  Copyright 2003-2005 Jeff Garzik
 *
 *  libata documentation is available via 'make {ps|pdf}docs',
 *  as Documentation/driver-api/libata.rst
 */

#ifndef __LINUX_LIBATA_H__
#define __LINUX_LIBATA_H__

#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>
#include <linux/io.h>
#include <linux/ata.h>
#include <linux/workqueue.h>
#include <scsi/scsi_host.h>
#include <linux/acpi.h>
#include <linux/cdrom.h>
#include <linux/sched.h>
#include <linux/async.h>

/*
 * Define if arch has non-standard setup.  This is a _PCI_ standard
 * not a legacy or ISA standard.
 */
#ifdef CONFIG_ATA_NONSTANDARD
#include <asm/libata-portmap.h>
#else
#define ATA_PRIMARY_IRQ(dev)	14
#define ATA_SECONDARY_IRQ(dev)	15
#endif

/*
 * compile-time options: to be removed as soon as all the drivers are
 * converted to the new debugging mechanism
 */
#undef ATA_IRQ_TRAP		/* define to ack screaming irqs */


#define ata_print_version_once(dev, version)			\
({								\
	static bool __print_once;				\
								\
	if (!__print_once) {					\
		__print_once = true;				\
		ata_print_version(dev, version);		\
	}							\
})

/* defines only for the constants which don't work well as enums */
#define ATA_TAG_POISON		0xfafbfcfdU

enum {
	/* various global constants */
	LIBATA_MAX_PRD		= ATA_MAX_PRD / 2,
	LIBATA_DUMB_MAX_PRD	= ATA_MAX_PRD / 4,	/* Worst case */
	ATA_DEF_QUEUE		= 1,
	ATA_MAX_QUEUE		= 32,
	ATA_TAG_INTERNAL	= ATA_MAX_QUEUE,
	ATA_SHORT_PAUSE		= 16,

	ATAPI_MAX_DRAIN		= 16 << 10,

	ATA_ALL_DEVICES		= (1 << ATA_MAX_DEVICES) - 1,

	ATA_SHT_EMULATED	= 1,
	ATA_SHT_THIS_ID		= -1,

	/* struct ata_taskfile flags */
	ATA_TFLAG_LBA48		= (1 << 0), /* enable 48-bit LBA and "HOB" */
	ATA_TFLAG_ISADDR	= (1 << 1), /* enable r/w to nsect/lba regs */
	ATA_TFLAG_DEVICE	= (1 << 2), /* enable r/w to device reg */
	ATA_TFLAG_WRITE		= (1 << 3), /* data dir: host->dev==1 (write) */
	ATA_TFLAG_LBA		= (1 << 4), /* enable LBA */
	ATA_TFLAG_FUA		= (1 << 5), /* enable FUA */
	ATA_TFLAG_POLLING	= (1 << 6), /* set nIEN to 1 and use polling */

	/* struct ata_device stuff */
	ATA_DFLAG_LBA		= (1 << 0), /* device supports LBA */
	ATA_DFLAG_LBA48		= (1 << 1), /* device supports LBA48 */
	ATA_DFLAG_CDB_INTR	= (1 << 2), /* device asserts INTRQ when ready for CDB */
	ATA_DFLAG_NCQ		= (1 << 3), /* device supports NCQ */
	ATA_DFLAG_FLUSH_EXT	= (1 << 4), /* do FLUSH_EXT instead of FLUSH */
	ATA_DFLAG_ACPI_PENDING	= (1 << 5), /* ACPI resume action pending */
	ATA_DFLAG_ACPI_FAILED	= (1 << 6), /* ACPI on devcfg has failed */
	ATA_DFLAG_AN		= (1 << 7), /* AN configured */
	ATA_DFLAG_TRUSTED	= (1 << 8), /* device supports trusted send/recv */
	ATA_DFLAG_DMADIR	= (1 << 10), /* device requires DMADIR */
	ATA_DFLAG_CFG_MASK	= (1 << 12) - 1,

	ATA_DFLAG_PIO		= (1 << 12), /* device limited to PIO mode */
	ATA_DFLAG_NCQ_OFF	= (1 << 13), /* device limited to non-NCQ mode */
	ATA_DFLAG_SLEEPING	= (1 << 15), /* device is sleeping */
	ATA_DFLAG_DUBIOUS_XFER	= (1 << 16), /* data transfer not verified */
	ATA_DFLAG_NO_UNLOAD	= (1 << 17), /* device doesn't support unload */
	ATA_DFLAG_UNLOCK_HPA	= (1 << 18), /* unlock HPA */
	ATA_DFLAG_NCQ_SEND_RECV = (1 << 19), /* device supports NCQ SEND and RECV */
	ATA_DFLAG_NCQ_PRIO	= (1 << 20), /* device supports NCQ priority */
	ATA_DFLAG_NCQ_PRIO_ENABLE = (1 << 21), /* Priority cmds sent to dev */
	ATA_DFLAG_INIT_MASK	= (1 << 24) - 1,

	ATA_DFLAG_DETACH	= (1 << 24),
	ATA_DFLAG_DETACHED	= (1 << 25),

	ATA_DFLAG_DA		= (1 << 26), /* device supports Device Attention */
	ATA_DFLAG_DEVSLP	= (1 << 27), /* device supports Device Sleep */
	ATA_DFLAG_ACPI_DISABLED = (1 << 28), /* ACPI for the device is disabled */
	ATA_DFLAG_D_SENSE	= (1 << 29), /* Descriptor sense requested */
	ATA_DFLAG_ZAC		= (1 << 30), /* ZAC device */

	ATA_DFLAG_FEATURES_MASK	= ATA_DFLAG_TRUSTED | ATA_DFLAG_DA | \
				  ATA_DFLAG_DEVSLP | ATA_DFLAG_NCQ_SEND_RECV | \
				  ATA_DFLAG_NCQ_PRIO,

	ATA_DEV_UNKNOWN		= 0,	/* unknown device */
	ATA_DEV_ATA		= 1,	/* ATA device */
	ATA_DEV_ATA_UNSUP	= 2,	/* ATA device (unsupported) */
	ATA_DEV_ATAPI		= 3,	/* ATAPI device */
	ATA_DEV_ATAPI_UNSUP	= 4,	/* ATAPI device (unsupported) */
	ATA_DEV_PMP		= 5,	/* SATA port multiplier */
	ATA_DEV_PMP_UNSUP	= 6,	/* SATA port multiplier (unsupported) */
	ATA_DEV_SEMB		= 7,	/* SEMB */
	ATA_DEV_SEMB_UNSUP	= 8,	/* SEMB (unsupported) */
	ATA_DEV_ZAC		= 9,	/* ZAC device */
	ATA_DEV_ZAC_UNSUP	= 10,	/* ZAC device (unsupported) */
	ATA_DEV_NONE		= 11,	/* no device */

	/* struct ata_link flags */
	/* NOTE: struct ata_force_param currently stores lflags in u16 */
	ATA_LFLAG_NO_HRST	= (1 << 1), /* avoid hardreset */
	ATA_LFLAG_NO_SRST	= (1 << 2), /* avoid softreset */
	ATA_LFLAG_ASSUME_ATA	= (1 << 3), /* assume ATA class */
	ATA_LFLAG_ASSUME_SEMB	= (1 << 4), /* assume SEMB class */
	ATA_LFLAG_ASSUME_CLASS	= ATA_LFLAG_ASSUME_ATA | ATA_LFLAG_ASSUME_SEMB,
	ATA_LFLAG_NO_RETRY	= (1 << 5), /* don't retry this link */
	ATA_LFLAG_DISABLED	= (1 << 6), /* link is disabled */
	ATA_LFLAG_SW_ACTIVITY	= (1 << 7), /* keep activity stats */
	ATA_LFLAG_NO_LPM	= (1 << 8), /* disable LPM on this link */
	ATA_LFLAG_RST_ONCE	= (1 << 9), /* limit recovery to one reset */
	ATA_LFLAG_CHANGED	= (1 << 10), /* LPM state changed on this link */
	ATA_LFLAG_NO_DEBOUNCE_DELAY = (1 << 11), /* no debounce delay on link resume */

	/* struct ata_port flags */
	ATA_FLAG_SLAVE_POSS	= (1 << 0), /* host supports slave dev */
					    /* (doesn't imply presence) */
	ATA_FLAG_SATA		= (1 << 1),
	ATA_FLAG_NO_LPM		= (1 << 2), /* host not happy with LPM */
	ATA_FLAG_NO_LOG_PAGE	= (1 << 5), /* do not issue log page read */
	ATA_FLAG_NO_ATAPI	= (1 << 6), /* No ATAPI support */
	ATA_FLAG_PIO_DMA	= (1 << 7), /* PIO cmds via DMA */
	ATA_FLAG_PIO_LBA48	= (1 << 8), /* Host DMA engine is LBA28 only */
	ATA_FLAG_PIO_POLLING	= (1 << 9), /* use polling PIO if LLD
					     * doesn't handle PIO interrupts */
	ATA_FLAG_NCQ		= (1 << 10), /* host supports NCQ */
	ATA_FLAG_NO_POWEROFF_SPINDOWN = (1 << 11), /* don't spindown before poweroff */
	ATA_FLAG_NO_HIBERNATE_SPINDOWN = (1 << 12), /* don't spindown before hibernation */
	ATA_FLAG_DEBUGMSG	= (1 << 13),
	ATA_FLAG_FPDMA_AA		= (1 << 14), /* driver supports Auto-Activate */
	ATA_FLAG_IGN_SIMPLEX	= (1 << 15), /* ignore SIMPLEX */
	ATA_FLAG_NO_IORDY	= (1 << 16), /* controller lacks iordy */
	ATA_FLAG_ACPI_SATA	= (1 << 17), /* need native SATA ACPI layout */
	ATA_FLAG_AN		= (1 << 18), /* controller supports AN */
	ATA_FLAG_PMP		= (1 << 19), /* controller supports PMP */
	ATA_FLAG_FPDMA_AUX	= (1 << 20), /* controller supports H2DFIS aux field */
	ATA_FLAG_EM		= (1 << 21), /* driver supports enclosure
					      * management */
	ATA_FLAG_SW_ACTIVITY	= (1 << 22), /* driver supports sw activity
					      * led */
	ATA_FLAG_NO_DIPM	= (1 << 23), /* host not happy with DIPM */
	ATA_FLAG_SAS_HOST	= (1 << 24), /* SAS host */

	/* bits 24:31 of ap->flags are reserved for LLD specific flags */


	/* struct ata_port pflags */
	ATA_PFLAG_EH_PENDING	= (1 << 0), /* EH pending */
	ATA_PFLAG_EH_IN_PROGRESS = (1 << 1), /* EH in progress */
	ATA_PFLAG_FROZEN	= (1 << 2), /* port is frozen */
	ATA_PFLAG_RECOVERED	= (1 << 3), /* recovery action performed */
	ATA_PFLAG_LOADING	= (1 << 4), /* boot/loading probe */
	ATA_PFLAG_SCSI_HOTPLUG	= (1 << 6), /* SCSI hotplug scheduled */
	ATA_PFLAG_INITIALIZING	= (1 << 7), /* being initialized, don't touch */
	ATA_PFLAG_RESETTING	= (1 << 8), /* reset in progress */
	ATA_PFLAG_UNLOADING	= (1 << 9), /* driver is being unloaded */
	ATA_PFLAG_UNLOADED	= (1 << 10), /* driver is unloaded */

	ATA_PFLAG_SUSPENDED	= (1 << 17), /* port is suspended (power) */
	ATA_PFLAG_PM_PENDING	= (1 << 18), /* PM operation pending */
	ATA_PFLAG_INIT_GTM_VALID = (1 << 19), /* initial gtm data valid */

	ATA_PFLAG_PIO32		= (1 << 20),  /* 32bit PIO */
	ATA_PFLAG_PIO32CHANGE	= (1 << 21),  /* 32bit PIO can be turned on/off */
	ATA_PFLAG_EXTERNAL	= (1 << 22),  /* eSATA/external port */

	/* struct ata_queued_cmd flags */
	ATA_QCFLAG_ACTIVE	= (1 << 0), /* cmd not yet ack'd to scsi lyer */
	ATA_QCFLAG_DMAMAP	= (1 << 1), /* SG table is DMA mapped */
	ATA_QCFLAG_IO		= (1 << 3), /* standard IO command */
	ATA_QCFLAG_RESULT_TF	= (1 << 4), /* result TF requested */
	ATA_QCFLAG_CLEAR_EXCL	= (1 << 5), /* clear excl_link on completion */
	ATA_QCFLAG_QUIET	= (1 << 6), /* don't report device error */
	ATA_QCFLAG_RETRY	= (1 << 7), /* retry after failure */

	ATA_QCFLAG_FAILED	= (1 << 16), /* cmd failed and is owned by EH */
	ATA_QCFLAG_SENSE_VALID	= (1 << 17), /* sense data valid */
	ATA_QCFLAG_EH_SCHEDULED = (1 << 18), /* EH scheduled (obsolete) */

	/* host set flags */
	ATA_HOST_SIMPLEX	= (1 << 0),	/* Host is simplex, one DMA channel per host only */
	ATA_HOST_STARTED	= (1 << 1),	/* Host started */
	ATA_HOST_PARALLEL_SCAN	= (1 << 2),	/* Ports on this host can be scanned in parallel */
	ATA_HOST_IGNORE_ATA	= (1 << 3),	/* Ignore ATA devices on this host. */

	/* bits 24:31 of host->flags are reserved for LLD specific flags */

	/* various lengths of time */
	ATA_TMOUT_BOOT		= 30000,	/* heuristic */
	ATA_TMOUT_BOOT_QUICK	=  7000,	/* heuristic */
	ATA_TMOUT_INTERNAL_QUICK = 5000,
	ATA_TMOUT_MAX_PARK	= 30000,

	/*
	 * GoVault needs 2s and iVDR disk HHD424020F7SV00 800ms.  2s
	 * is too much without parallel probing.  Use 2s if parallel
	 * probing is available, 800ms otherwise.
	 */
	ATA_TMOUT_FF_WAIT_LONG	=  2000,
	ATA_TMOUT_FF_WAIT	=   800,

	/* Spec mandates to wait for ">= 2ms" before checking status
	 * after reset.  We wait 150ms, because that was the magic
	 * delay used for ATAPI devices in Hale Landis's ATADRVR, for
	 * the period of time between when the ATA command register is
	 * written, and then status is checked.  Because waiting for
	 * "a while" before checking status is fine, post SRST, we
	 * perform this magic delay here as well.
	 *
	 * Old drivers/ide uses the 2mS rule and then waits for ready.
	 */
	ATA_WAIT_AFTER_RESET	=  150,

	/* If PMP is supported, we have to do follow-up SRST.  As some
	 * PMPs don't send D2H Reg FIS after hardreset, LLDs are
	 * advised to wait only for the following duration before
	 * doing SRST.
	 */
	ATA_TMOUT_PMP_SRST_WAIT	= 5000,

	/* When the LPM policy is set to ATA_LPM_MAX_POWER, there might
	 * be a spurious PHY event, so ignore the first PHY event that
	 * occurs within 10s after the policy change.
	 */
	ATA_TMOUT_SPURIOUS_PHY	= 10000,

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
	 * unsigned int bitmap
	 */
	ATA_NR_PIO_MODES	= 7,
	ATA_NR_MWDMA_MODES	= 5,
	ATA_NR_UDMA_MODES	= 8,

	ATA_SHIFT_PIO		= 0,
	ATA_SHIFT_MWDMA		= ATA_SHIFT_PIO + ATA_NR_PIO_MODES,
	ATA_SHIFT_UDMA		= ATA_SHIFT_MWDMA + ATA_NR_MWDMA_MODES,
	ATA_SHIFT_PRIO		= 6,

	ATA_PRIO_HIGH		= 2,
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
	ATA_EH_SOFTRESET	= (1 << 1), /* meaningful only in ->prereset */
	ATA_EH_HARDRESET	= (1 << 2), /* meaningful only in ->prereset */
	ATA_EH_RESET		= ATA_EH_SOFTRESET | ATA_EH_HARDRESET,
	ATA_EH_ENABLE_LINK	= (1 << 3),
	ATA_EH_PARK		= (1 << 5), /* unload heads and stop I/O */

	ATA_EH_PERDEV_MASK	= ATA_EH_REVALIDATE | ATA_EH_PARK,
	ATA_EH_ALL_ACTIONS	= ATA_EH_REVALIDATE | ATA_EH_RESET |
				  ATA_EH_ENABLE_LINK,

	/* ata_eh_info->flags */
	ATA_EHI_HOTPLUGGED	= (1 << 0),  /* could have been hotplugged */
	ATA_EHI_NO_AUTOPSY	= (1 << 2),  /* no autopsy */
	ATA_EHI_QUIET		= (1 << 3),  /* be quiet */
	ATA_EHI_NO_RECOVERY	= (1 << 4),  /* no recovery */

	ATA_EHI_DID_SOFTRESET	= (1 << 16), /* already soft-reset this port */
	ATA_EHI_DID_HARDRESET	= (1 << 17), /* already soft-reset this port */
	ATA_EHI_PRINTINFO	= (1 << 18), /* print configuration info */
	ATA_EHI_SETMODE		= (1 << 19), /* configure transfer mode */
	ATA_EHI_POST_SETMODE	= (1 << 20), /* revalidating after setmode */

	ATA_EHI_DID_RESET	= ATA_EHI_DID_SOFTRESET | ATA_EHI_DID_HARDRESET,

	/* mask of flags to transfer *to* the slave link */
	ATA_EHI_TO_SLAVE_MASK	= ATA_EHI_NO_AUTOPSY | ATA_EHI_QUIET,

	/* max tries if error condition is still set after ->error_handler */
	ATA_EH_MAX_TRIES	= 5,

	/* sometimes resuming a link requires several retries */
	ATA_LINK_RESUME_TRIES	= 5,

	/* how hard are we gonna try to probe/recover devices */
	ATA_PROBE_MAX_TRIES	= 3,
	ATA_EH_DEV_TRIES	= 3,
	ATA_EH_PMP_TRIES	= 5,
	ATA_EH_PMP_LINK_TRIES	= 3,

	SATA_PMP_RW_TIMEOUT	= 3000,		/* PMP read/write timeout */

	/* This should match the actual table size of
	 * ata_eh_cmd_timeout_table in libata-eh.c.
	 */
	ATA_EH_CMD_TIMEOUT_TABLE_SIZE = 7,

	/* Horkage types. May be set by libata or controller on drives
	   (some horkage may be drive/controller pair dependent */

	ATA_HORKAGE_DIAGNOSTIC	= (1 << 0),	/* Failed boot diag */
	ATA_HORKAGE_NODMA	= (1 << 1),	/* DMA problems */
	ATA_HORKAGE_NONCQ	= (1 << 2),	/* Don't use NCQ */
	ATA_HORKAGE_MAX_SEC_128	= (1 << 3),	/* Limit max sects to 128 */
	ATA_HORKAGE_BROKEN_HPA	= (1 << 4),	/* Broken HPA */
	ATA_HORKAGE_DISABLE	= (1 << 5),	/* Disable it */
	ATA_HORKAGE_HPA_SIZE	= (1 << 6),	/* native size off by one */
	ATA_HORKAGE_IVB		= (1 << 8),	/* cbl det validity bit bugs */
	ATA_HORKAGE_STUCK_ERR	= (1 << 9),	/* stuck ERR on next PACKET */
	ATA_HORKAGE_BRIDGE_OK	= (1 << 10),	/* no bridge limits */
	ATA_HORKAGE_ATAPI_MOD16_DMA = (1 << 11), /* use ATAPI DMA for commands
						    not multiple of 16 bytes */
	ATA_HORKAGE_FIRMWARE_WARN = (1 << 12),	/* firmware update warning */
	ATA_HORKAGE_1_5_GBPS	= (1 << 13),	/* force 1.5 Gbps */
	ATA_HORKAGE_NOSETXFER	= (1 << 14),	/* skip SETXFER, SATA only */
	ATA_HORKAGE_BROKEN_FPDMA_AA	= (1 << 15),	/* skip AA */
	ATA_HORKAGE_DUMP_ID	= (1 << 16),	/* dump IDENTIFY data */
	ATA_HORKAGE_MAX_SEC_LBA48 = (1 << 17),	/* Set max sects to 65535 */
	ATA_HORKAGE_ATAPI_DMADIR = (1 << 18),	/* device requires dmadir */
	ATA_HORKAGE_NO_NCQ_TRIM	= (1 << 19),	/* don't use queued TRIM */
	ATA_HORKAGE_NOLPM	= (1 << 20),	/* don't use LPM */
	ATA_HORKAGE_WD_BROKEN_LPM = (1 << 21),	/* some WDs have broken LPM */
	ATA_HORKAGE_ZERO_AFTER_TRIM = (1 << 22),/* guarantees zero after trim */
	ATA_HORKAGE_NO_DMA_LOG	= (1 << 23),	/* don't use DMA for log read */
	ATA_HORKAGE_NOTRIM	= (1 << 24),	/* don't use TRIM */
	ATA_HORKAGE_MAX_SEC_1024 = (1 << 25),	/* Limit max sects to 1024 */
	ATA_HORKAGE_MAX_TRIM_128M = (1 << 26),	/* Limit max trim size to 128M */
	ATA_HORKAGE_NO_NCQ_ON_ATI = (1 << 27),	/* Disable NCQ on ATI chipset */
	ATA_HORKAGE_NO_ID_DEV_LOG = (1 << 28),	/* Identify device log missing */
	ATA_HORKAGE_NO_LOG_DIR	= (1 << 29),	/* Do not read log directory */

	 /* DMA mask for user DMA control: User visible values; DO NOT
	    renumber */
	ATA_DMA_MASK_ATA	= (1 << 0),	/* DMA on ATA Disk */
	ATA_DMA_MASK_ATAPI	= (1 << 1),	/* DMA on ATAPI */
	ATA_DMA_MASK_CFA	= (1 << 2),	/* DMA on CF Card */

	/* ATAPI command types */
	ATAPI_READ		= 0,		/* READs */
	ATAPI_WRITE		= 1,		/* WRITEs */
	ATAPI_READ_CD		= 2,		/* READ CD [MSF] */
	ATAPI_PASS_THRU		= 3,		/* SAT pass-thru */
	ATAPI_MISC		= 4,		/* the rest */

	/* Timing constants */
	ATA_TIMING_SETUP	= (1 << 0),
	ATA_TIMING_ACT8B	= (1 << 1),
	ATA_TIMING_REC8B	= (1 << 2),
	ATA_TIMING_CYC8B	= (1 << 3),
	ATA_TIMING_8BIT		= ATA_TIMING_ACT8B | ATA_TIMING_REC8B |
				  ATA_TIMING_CYC8B,
	ATA_TIMING_ACTIVE	= (1 << 4),
	ATA_TIMING_RECOVER	= (1 << 5),
	ATA_TIMING_DMACK_HOLD	= (1 << 6),
	ATA_TIMING_CYCLE	= (1 << 7),
	ATA_TIMING_UDMA		= (1 << 8),
	ATA_TIMING_ALL		= ATA_TIMING_SETUP | ATA_TIMING_ACT8B |
				  ATA_TIMING_REC8B | ATA_TIMING_CYC8B |
				  ATA_TIMING_ACTIVE | ATA_TIMING_RECOVER |
				  ATA_TIMING_DMACK_HOLD | ATA_TIMING_CYCLE |
				  ATA_TIMING_UDMA,

	/* ACPI constants */
	ATA_ACPI_FILTER_SETXFER	= 1 << 0,
	ATA_ACPI_FILTER_LOCK	= 1 << 1,
	ATA_ACPI_FILTER_DIPM	= 1 << 2,
	ATA_ACPI_FILTER_FPDMA_OFFSET = 1 << 3,	/* FPDMA non-zero offset */
	ATA_ACPI_FILTER_FPDMA_AA = 1 << 4,	/* FPDMA auto activate */

	ATA_ACPI_FILTER_DEFAULT	= ATA_ACPI_FILTER_SETXFER |
				  ATA_ACPI_FILTER_LOCK |
				  ATA_ACPI_FILTER_DIPM,
};

enum ata_xfer_mask {
	ATA_MASK_PIO		= ((1U << ATA_NR_PIO_MODES) - 1) << ATA_SHIFT_PIO,
	ATA_MASK_MWDMA		= ((1U << ATA_NR_MWDMA_MODES) - 1) << ATA_SHIFT_MWDMA,
	ATA_MASK_UDMA		= ((1U << ATA_NR_UDMA_MODES) - 1) << ATA_SHIFT_UDMA,
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
	AC_ERR_OK		= 0,	    /* no error */
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

/*
 * Link power management policy: If you alter this, you also need to
 * alter libata-scsi.c (for the ascii descriptions)
 */
enum ata_lpm_policy {
	ATA_LPM_UNKNOWN,
	ATA_LPM_MAX_POWER,
	ATA_LPM_MED_POWER,
	ATA_LPM_MED_POWER_WITH_DIPM, /* Med power + DIPM as win IRST does */
	ATA_LPM_MIN_POWER_WITH_PARTIAL, /* Min Power + partial and slumber */
	ATA_LPM_MIN_POWER, /* Min power + no partial (slumber only) */
};

enum ata_lpm_hints {
	ATA_LPM_EMPTY		= (1 << 0), /* port empty/probing */
	ATA_LPM_HIPM		= (1 << 1), /* may use HIPM */
	ATA_LPM_WAKE_ONLY	= (1 << 2), /* only wake up link */
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

extern struct device_attribute dev_attr_unload_heads;
#ifdef CONFIG_SATA_HOST
extern struct device_attribute dev_attr_link_power_management_policy;
extern struct device_attribute dev_attr_ncq_prio_supported;
extern struct device_attribute dev_attr_ncq_prio_enable;
extern struct device_attribute dev_attr_em_message_type;
extern struct device_attribute dev_attr_em_message;
extern struct device_attribute dev_attr_sw_activity;
#endif

enum sw_activity {
	OFF,
	BLINK_ON,
	BLINK_OFF,
};

struct ata_taskfile {
	unsigned long		flags;		/* ATA_TFLAG_xxx */
	u8			protocol;	/* ATA_PROT_xxx */

	u8			ctl;		/* control reg */

	u8			hob_feature;	/* additional data */
	u8			hob_nsect;	/* to support LBA48 */
	u8			hob_lbal;
	u8			hob_lbam;
	u8			hob_lbah;

	union {
		u8		error;
		u8		feature;
	};
	u8			nsect;
	u8			lbal;
	u8			lbam;
	u8			lbah;

	u8			device;

	union {
		u8		status;
		u8		command;
	};

	u32			auxiliary;	/* auxiliary field */
						/* from SATA 3.1 and */
						/* ATA-8 ACS-3 */
};

#ifdef CONFIG_ATA_SFF
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
#ifdef CONFIG_ATA_BMDMA
	void __iomem		*bmdma_addr;
#endif /* CONFIG_ATA_BMDMA */
	void __iomem		*scr_addr;
};
#endif /* CONFIG_ATA_SFF */

struct ata_host {
	spinlock_t		lock;
	struct device 		*dev;
	void __iomem * const	*iomap;
	unsigned int		n_ports;
	unsigned int		n_tags;			/* nr of NCQ tags */
	void			*private_data;
	struct ata_port_operations *ops;
	unsigned long		flags;
	struct kref		kref;

	struct mutex		eh_mutex;
	struct task_struct	*eh_owner;

	struct ata_port		*simplex_claimed;	/* channel owning the DMA */
	struct ata_port		*ports[];
};

struct ata_queued_cmd {
	struct ata_port		*ap;
	struct ata_device	*dev;

	struct scsi_cmnd	*scsicmd;
	void			(*scsidone)(struct scsi_cmnd *);

	struct ata_taskfile	tf;
	u8			cdb[ATAPI_CDB_LEN];

	unsigned long		flags;		/* ATA_QCFLAG_xxx */
	unsigned int		tag;		/* libata core tag */
	unsigned int		hw_tag;		/* driver tag */
	unsigned int		n_elem;
	unsigned int		orig_n_elem;

	int			dma_dir;

	unsigned int		sect_size;

	unsigned int		nbytes;
	unsigned int		extrabytes;
	unsigned int		curbytes;

	struct scatterlist	sgent;

	struct scatterlist	*sg;

	struct scatterlist	*cursg;
	unsigned int		cursg_ofs;

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

struct ata_cpr {
	u8			num;
	u8			num_storage_elements;
	u64			start_lba;
	u64			num_lbas;
};

struct ata_cpr_log {
	u8			nr_cpr;
	struct ata_cpr		cpr[];
};

struct ata_device {
	struct ata_link		*link;
	unsigned int		devno;		/* 0 or 1 */
	unsigned int		horkage;	/* List of broken features */
	unsigned long		flags;		/* ATA_DFLAG_xxx */
	struct scsi_device	*sdev;		/* attached SCSI device */
	void			*private_data;
#ifdef CONFIG_ATA_ACPI
	union acpi_object	*gtf_cache;
	unsigned int		gtf_filter;
#endif
#ifdef CONFIG_SATA_ZPODD
	void			*zpodd;
#endif
	struct device		tdev;
	/* n_sector is CLEAR_BEGIN, read comment above CLEAR_BEGIN */
	u64			n_sectors;	/* size of device, if ATA */
	u64			n_native_sectors; /* native size, if ATA */
	unsigned int		class;		/* ATA_DEV_xxx */
	unsigned long		unpark_deadline;

	u8			pio_mode;
	u8			dma_mode;
	u8			xfer_mode;
	unsigned int		xfer_shift;	/* ATA_SHIFT_xxx */

	unsigned int		multi_count;	/* sectors count for
						   READ/WRITE MULTIPLE */
	unsigned int		max_sectors;	/* per-device max sectors */
	unsigned int		cdb_len;

	/* per-dev xfer mask */
	unsigned int		pio_mask;
	unsigned int		mwdma_mask;
	unsigned int		udma_mask;

	/* for CHS addressing */
	u16			cylinders;	/* Number of cylinders */
	u16			heads;		/* Number of heads */
	u16			sectors;	/* Number of sectors per track */

	union {
		u16		id[ATA_ID_WORDS]; /* IDENTIFY xxx DEVICE data */
		u32		gscr[SATA_PMP_GSCR_DWORDS]; /* PMP GSCR block */
	} ____cacheline_aligned;

	/* DEVSLP Timing Variables from Identify Device Data Log */
	u8			devslp_timing[ATA_LOG_DEVSLP_SIZE];

	/* NCQ send and receive log subcommand support */
	u8			ncq_send_recv_cmds[ATA_LOG_NCQ_SEND_RECV_SIZE];
	u8			ncq_non_data_cmds[ATA_LOG_NCQ_NON_DATA_SIZE];

	/* ZAC zone configuration */
	u32			zac_zoned_cap;
	u32			zac_zones_optimal_open;
	u32			zac_zones_optimal_nonseq;
	u32			zac_zones_max_open;

	/* Concurrent positioning ranges */
	struct ata_cpr_log	*cpr_log;

	/* error history */
	int			spdn_cnt;
	/* ering is CLEAR_END, read comment above CLEAR_END */
	struct ata_ering	ering;
};

/* Fields between ATA_DEVICE_CLEAR_BEGIN and ATA_DEVICE_CLEAR_END are
 * cleared to zero on ata_dev_init().
 */
#define ATA_DEVICE_CLEAR_BEGIN		offsetof(struct ata_device, n_sectors)
#define ATA_DEVICE_CLEAR_END		offsetof(struct ata_device, ering)

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
	int			cmd_timeout_idx[ATA_MAX_DEVICES]
					       [ATA_EH_CMD_TIMEOUT_TABLE_SIZE];
	unsigned int		classes[ATA_MAX_DEVICES];
	unsigned int		did_probe_mask;
	unsigned int		unloaded_mask;
	unsigned int		saved_ncq_enabled;
	u8			saved_xfer_mode[ATA_MAX_DEVICES];
	/* timestamp for the last reset attempt or success */
	unsigned long		last_reset;
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

	struct device		tdev;
	unsigned int		active_tag;	/* active tag on this link */
	u32			sactive;	/* active NCQ commands */

	unsigned int		flags;		/* ATA_LFLAG_xxx */

	u32			saved_scontrol;	/* SControl on probe */
	unsigned int		hw_sata_spd_limit;
	unsigned int		sata_spd_limit;
	unsigned int		sata_spd;	/* current SATA PHY speed */
	enum ata_lpm_policy	lpm_policy;

	/* record runtime error info, protected by host_set lock */
	struct ata_eh_info	eh_info;
	/* EH context */
	struct ata_eh_context	eh_context;

	struct ata_device	device[ATA_MAX_DEVICES];

	unsigned long		last_lpm_change; /* when last LPM change happened */
};
#define ATA_LINK_CLEAR_BEGIN		offsetof(struct ata_link, active_tag)
#define ATA_LINK_CLEAR_END		offsetof(struct ata_link, device[0])

struct ata_port {
	struct Scsi_Host	*scsi_host; /* our co-allocated scsi host */
	struct ata_port_operations *ops;
	spinlock_t		*lock;
	/* Flags owned by the EH context. Only EH should touch these once the
	   port is active */
	unsigned long		flags;	/* ATA_FLAG_xxx */
	/* Flags that change dynamically, protected by ap->lock */
	unsigned int		pflags; /* ATA_PFLAG_xxx */
	unsigned int		print_id; /* user visible unique port ID */
	unsigned int            local_port_no; /* host local port num */
	unsigned int		port_no; /* 0 based port no. inside the host */

#ifdef CONFIG_ATA_SFF
	struct ata_ioports	ioaddr;	/* ATA cmd/ctl/dma register blocks */
	u8			ctl;	/* cache of ATA control register */
	u8			last_ctl;	/* Cache last written value */
	struct ata_link*	sff_pio_task_link; /* link currently used */
	struct delayed_work	sff_pio_task;
#ifdef CONFIG_ATA_BMDMA
	struct ata_bmdma_prd	*bmdma_prd;	/* BMDMA SG list */
	dma_addr_t		bmdma_prd_dma;	/* and its DMA mapping */
#endif /* CONFIG_ATA_BMDMA */
#endif /* CONFIG_ATA_SFF */

	unsigned int		pio_mask;
	unsigned int		mwdma_mask;
	unsigned int		udma_mask;
	unsigned int		cbl;	/* cable type; ATA_CBL_xxx */

	struct ata_queued_cmd	qcmd[ATA_MAX_QUEUE + 1];
	u64			qc_active;
	int			nr_active_links; /* #links with active qcs */

	struct ata_link		link;		/* host default link */
	struct ata_link		*slave_link;	/* see ata_slave_link_init() */

	int			nr_pmp_links;	/* nr of available PMP links */
	struct ata_link		*pmp_link;	/* array of PMP links */
	struct ata_link		*excl_link;	/* for PMP qc exclusion */

	struct ata_port_stats	stats;
	struct ata_host		*host;
	struct device 		*dev;
	struct device		tdev;

	struct mutex		scsi_scan_mutex;
	struct delayed_work	hotplug_task;
	struct work_struct	scsi_rescan_task;

	unsigned int		hsm_task_state;

	struct list_head	eh_done_q;
	wait_queue_head_t	eh_wait_q;
	int			eh_tries;
	struct completion	park_req_pending;

	pm_message_t		pm_mesg;
	enum ata_lpm_policy	target_lpm_policy;

	struct timer_list	fastdrain_timer;
	unsigned int		fastdrain_cnt;

	async_cookie_t		cookie;

	int			em_message_type;
	void			*private_data;

#ifdef CONFIG_ATA_ACPI
	struct ata_acpi_gtm	__acpi_init_gtm; /* use ata_acpi_init_gtm() */
#endif
	/* owned by EH */
	u8			sector_buf[ATA_SECT_SIZE] ____cacheline_aligned;
};

/* The following initializer overrides a method to NULL whether one of
 * its parent has the method defined or not.  This is equivalent to
 * ERR_PTR(-ENOENT).  Unfortunately, ERR_PTR doesn't render a constant
 * expression and thus can't be used as an initializer.
 */
#define ATA_OP_NULL		(void *)(unsigned long)(-ENOENT)

struct ata_port_operations {
	/*
	 * Command execution
	 */
	int (*qc_defer)(struct ata_queued_cmd *qc);
	int (*check_atapi_dma)(struct ata_queued_cmd *qc);
	enum ata_completion_errors (*qc_prep)(struct ata_queued_cmd *qc);
	unsigned int (*qc_issue)(struct ata_queued_cmd *qc);
	bool (*qc_fill_rtf)(struct ata_queued_cmd *qc);

	/*
	 * Configuration and exception handling
	 */
	int  (*cable_detect)(struct ata_port *ap);
	unsigned int (*mode_filter)(struct ata_device *dev, unsigned int xfer_mask);
	void (*set_piomode)(struct ata_port *ap, struct ata_device *dev);
	void (*set_dmamode)(struct ata_port *ap, struct ata_device *dev);
	int  (*set_mode)(struct ata_link *link, struct ata_device **r_failed_dev);
	unsigned int (*read_id)(struct ata_device *dev, struct ata_taskfile *tf,
				__le16 *id);

	void (*dev_config)(struct ata_device *dev);

	void (*freeze)(struct ata_port *ap);
	void (*thaw)(struct ata_port *ap);
	ata_prereset_fn_t	prereset;
	ata_reset_fn_t		softreset;
	ata_reset_fn_t		hardreset;
	ata_postreset_fn_t	postreset;
	ata_prereset_fn_t	pmp_prereset;
	ata_reset_fn_t		pmp_softreset;
	ata_reset_fn_t		pmp_hardreset;
	ata_postreset_fn_t	pmp_postreset;
	void (*error_handler)(struct ata_port *ap);
	void (*lost_interrupt)(struct ata_port *ap);
	void (*post_internal_cmd)(struct ata_queued_cmd *qc);
	void (*sched_eh)(struct ata_port *ap);
	void (*end_eh)(struct ata_port *ap);

	/*
	 * Optional features
	 */
	int  (*scr_read)(struct ata_link *link, unsigned int sc_reg, u32 *val);
	int  (*scr_write)(struct ata_link *link, unsigned int sc_reg, u32 val);
	void (*pmp_attach)(struct ata_port *ap);
	void (*pmp_detach)(struct ata_port *ap);
	int  (*set_lpm)(struct ata_link *link, enum ata_lpm_policy policy,
			unsigned hints);

	/*
	 * Start, stop, suspend and resume
	 */
	int  (*port_suspend)(struct ata_port *ap, pm_message_t mesg);
	int  (*port_resume)(struct ata_port *ap);
	int  (*port_start)(struct ata_port *ap);
	void (*port_stop)(struct ata_port *ap);
	void (*host_stop)(struct ata_host *host);

#ifdef CONFIG_ATA_SFF
	/*
	 * SFF / taskfile oriented ops
	 */
	void (*sff_dev_select)(struct ata_port *ap, unsigned int device);
	void (*sff_set_devctl)(struct ata_port *ap, u8 ctl);
	u8   (*sff_check_status)(struct ata_port *ap);
	u8   (*sff_check_altstatus)(struct ata_port *ap);
	void (*sff_tf_load)(struct ata_port *ap, const struct ata_taskfile *tf);
	void (*sff_tf_read)(struct ata_port *ap, struct ata_taskfile *tf);
	void (*sff_exec_command)(struct ata_port *ap,
				 const struct ata_taskfile *tf);
	unsigned int (*sff_data_xfer)(struct ata_queued_cmd *qc,
			unsigned char *buf, unsigned int buflen, int rw);
	void (*sff_irq_on)(struct ata_port *);
	bool (*sff_irq_check)(struct ata_port *);
	void (*sff_irq_clear)(struct ata_port *);
	void (*sff_drain_fifo)(struct ata_queued_cmd *qc);

#ifdef CONFIG_ATA_BMDMA
	void (*bmdma_setup)(struct ata_queued_cmd *qc);
	void (*bmdma_start)(struct ata_queued_cmd *qc);
	void (*bmdma_stop)(struct ata_queued_cmd *qc);
	u8   (*bmdma_status)(struct ata_port *ap);
#endif /* CONFIG_ATA_BMDMA */
#endif /* CONFIG_ATA_SFF */

	ssize_t (*em_show)(struct ata_port *ap, char *buf);
	ssize_t (*em_store)(struct ata_port *ap, const char *message,
			    size_t size);
	ssize_t (*sw_activity_show)(struct ata_device *dev, char *buf);
	ssize_t (*sw_activity_store)(struct ata_device *dev,
				     enum sw_activity val);
	ssize_t (*transmit_led_message)(struct ata_port *ap, u32 state,
					ssize_t size);

	/*
	 * Obsolete
	 */
	void (*phy_reset)(struct ata_port *ap);
	void (*eng_timeout)(struct ata_port *ap);

	/*
	 * ->inherits must be the last field and all the preceding
	 * fields must be pointers.
	 */
	const struct ata_port_operations	*inherits;
};

struct ata_port_info {
	unsigned long		flags;
	unsigned long		link_flags;
	unsigned int		pio_mask;
	unsigned int		mwdma_mask;
	unsigned int		udma_mask;
	struct ata_port_operations *port_ops;
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
	unsigned short dmack_hold;	/* tj */
	unsigned short cycle;		/* t0 */
	unsigned short udma;		/* t2CYCTYP/2 */
};

/*
 * Core layer - drivers/ata/libata-core.c
 */
extern struct ata_port_operations ata_dummy_port_ops;
extern const struct ata_port_info ata_dummy_port_info;

static inline bool ata_is_atapi(u8 prot)
{
	return prot & ATA_PROT_FLAG_ATAPI;
}

static inline bool ata_is_pio(u8 prot)
{
	return prot & ATA_PROT_FLAG_PIO;
}

static inline bool ata_is_dma(u8 prot)
{
	return prot & ATA_PROT_FLAG_DMA;
}

static inline bool ata_is_ncq(u8 prot)
{
	return prot & ATA_PROT_FLAG_NCQ;
}

static inline bool ata_is_data(u8 prot)
{
	return prot & (ATA_PROT_FLAG_PIO | ATA_PROT_FLAG_DMA);
}

static inline int is_multi_taskfile(struct ata_taskfile *tf)
{
	return (tf->command == ATA_CMD_READ_MULTI) ||
	       (tf->command == ATA_CMD_WRITE_MULTI) ||
	       (tf->command == ATA_CMD_READ_MULTI_EXT) ||
	       (tf->command == ATA_CMD_WRITE_MULTI_EXT) ||
	       (tf->command == ATA_CMD_WRITE_MULTI_FUA_EXT);
}

static inline int ata_port_is_dummy(struct ata_port *ap)
{
	return ap->ops == &ata_dummy_port_ops;
}

extern int ata_std_prereset(struct ata_link *link, unsigned long deadline);
extern int ata_wait_after_reset(struct ata_link *link, unsigned long deadline,
				int (*check_ready)(struct ata_link *link));
extern int sata_std_hardreset(struct ata_link *link, unsigned int *class,
			      unsigned long deadline);
extern void ata_std_postreset(struct ata_link *link, unsigned int *classes);

extern struct ata_host *ata_host_alloc(struct device *dev, int max_ports);
extern struct ata_host *ata_host_alloc_pinfo(struct device *dev,
			const struct ata_port_info * const * ppi, int n_ports);
extern void ata_host_get(struct ata_host *host);
extern void ata_host_put(struct ata_host *host);
extern int ata_host_start(struct ata_host *host);
extern int ata_host_register(struct ata_host *host,
			     struct scsi_host_template *sht);
extern int ata_host_activate(struct ata_host *host, int irq,
			     irq_handler_t irq_handler, unsigned long irq_flags,
			     struct scsi_host_template *sht);
extern void ata_host_detach(struct ata_host *host);
extern void ata_host_init(struct ata_host *, struct device *, struct ata_port_operations *);
extern int ata_scsi_detect(struct scsi_host_template *sht);
extern int ata_scsi_ioctl(struct scsi_device *dev, unsigned int cmd,
			  void __user *arg);
#ifdef CONFIG_COMPAT
#define ATA_SCSI_COMPAT_IOCTL .compat_ioctl = ata_scsi_ioctl,
#else
#define ATA_SCSI_COMPAT_IOCTL /* empty */
#endif
extern int ata_scsi_queuecmd(struct Scsi_Host *h, struct scsi_cmnd *cmd);
#if IS_REACHABLE(CONFIG_ATA)
bool ata_scsi_dma_need_drain(struct request *rq);
#else
#define ata_scsi_dma_need_drain NULL
#endif
extern int ata_sas_scsi_ioctl(struct ata_port *ap, struct scsi_device *dev,
			    unsigned int cmd, void __user *arg);
extern bool ata_link_online(struct ata_link *link);
extern bool ata_link_offline(struct ata_link *link);
#ifdef CONFIG_PM
extern void ata_host_suspend(struct ata_host *host, pm_message_t mesg);
extern void ata_host_resume(struct ata_host *host);
extern void ata_sas_port_suspend(struct ata_port *ap);
extern void ata_sas_port_resume(struct ata_port *ap);
#else
static inline void ata_sas_port_suspend(struct ata_port *ap)
{
}
static inline void ata_sas_port_resume(struct ata_port *ap)
{
}
#endif
extern int ata_ratelimit(void);
extern void ata_msleep(struct ata_port *ap, unsigned int msecs);
extern u32 ata_wait_register(struct ata_port *ap, void __iomem *reg, u32 mask,
			u32 val, unsigned long interval, unsigned long timeout);
extern int atapi_cmd_type(u8 opcode);
extern unsigned int ata_pack_xfermask(unsigned int pio_mask,
				      unsigned int mwdma_mask,
				      unsigned int udma_mask);
extern void ata_unpack_xfermask(unsigned int xfer_mask,
				unsigned int *pio_mask,
				unsigned int *mwdma_mask,
				unsigned int *udma_mask);
extern u8 ata_xfer_mask2mode(unsigned int xfer_mask);
extern unsigned int ata_xfer_mode2mask(u8 xfer_mode);
extern int ata_xfer_mode2shift(u8 xfer_mode);
extern const char *ata_mode_string(unsigned int xfer_mask);
extern unsigned int ata_id_xfermask(const u16 *id);
extern int ata_std_qc_defer(struct ata_queued_cmd *qc);
extern enum ata_completion_errors ata_noop_qc_prep(struct ata_queued_cmd *qc);
extern void ata_sg_init(struct ata_queued_cmd *qc, struct scatterlist *sg,
		 unsigned int n_elem);
extern unsigned int ata_dev_classify(const struct ata_taskfile *tf);
extern unsigned int ata_port_classify(struct ata_port *ap,
				      const struct ata_taskfile *tf);
extern void ata_dev_disable(struct ata_device *adev);
extern void ata_id_string(const u16 *id, unsigned char *s,
			  unsigned int ofs, unsigned int len);
extern void ata_id_c_string(const u16 *id, unsigned char *s,
			    unsigned int ofs, unsigned int len);
extern unsigned int ata_do_dev_read_id(struct ata_device *dev,
				       struct ata_taskfile *tf, __le16 *id);
extern void ata_qc_complete(struct ata_queued_cmd *qc);
extern u64 ata_qc_get_active(struct ata_port *ap);
extern void ata_scsi_simulate(struct ata_device *dev, struct scsi_cmnd *cmd);
extern int ata_std_bios_param(struct scsi_device *sdev,
			      struct block_device *bdev,
			      sector_t capacity, int geom[]);
extern void ata_scsi_unlock_native_capacity(struct scsi_device *sdev);
extern int ata_scsi_slave_config(struct scsi_device *sdev);
extern void ata_scsi_slave_destroy(struct scsi_device *sdev);
extern int ata_scsi_change_queue_depth(struct scsi_device *sdev,
				       int queue_depth);
extern int __ata_change_queue_depth(struct ata_port *ap, struct scsi_device *sdev,
				    int queue_depth);
extern struct ata_device *ata_dev_pair(struct ata_device *adev);
extern int ata_do_set_mode(struct ata_link *link, struct ata_device **r_failed_dev);
extern void ata_scsi_port_error_handler(struct Scsi_Host *host, struct ata_port *ap);
extern void ata_scsi_cmd_error_handler(struct Scsi_Host *host, struct ata_port *ap, struct list_head *eh_q);

/*
 * SATA specific code - drivers/ata/libata-sata.c
 */
#ifdef CONFIG_SATA_HOST
extern const unsigned long sata_deb_timing_normal[];
extern const unsigned long sata_deb_timing_hotplug[];
extern const unsigned long sata_deb_timing_long[];

static inline const unsigned long *
sata_ehc_deb_timing(struct ata_eh_context *ehc)
{
	if (ehc->i.flags & ATA_EHI_HOTPLUGGED)
		return sata_deb_timing_hotplug;
	else
		return sata_deb_timing_normal;
}

extern int sata_scr_valid(struct ata_link *link);
extern int sata_scr_read(struct ata_link *link, int reg, u32 *val);
extern int sata_scr_write(struct ata_link *link, int reg, u32 val);
extern int sata_scr_write_flush(struct ata_link *link, int reg, u32 val);
extern int sata_set_spd(struct ata_link *link);
extern int sata_link_hardreset(struct ata_link *link,
			const unsigned long *timing, unsigned long deadline,
			bool *online, int (*check_ready)(struct ata_link *));
extern int sata_link_resume(struct ata_link *link, const unsigned long *params,
			    unsigned long deadline);
extern void ata_eh_analyze_ncq_error(struct ata_link *link);
#else
static inline const unsigned long *
sata_ehc_deb_timing(struct ata_eh_context *ehc)
{
	return NULL;
}
static inline int sata_scr_valid(struct ata_link *link) { return 0; }
static inline int sata_scr_read(struct ata_link *link, int reg, u32 *val)
{
	return -EOPNOTSUPP;
}
static inline int sata_scr_write(struct ata_link *link, int reg, u32 val)
{
	return -EOPNOTSUPP;
}
static inline int sata_scr_write_flush(struct ata_link *link, int reg, u32 val)
{
	return -EOPNOTSUPP;
}
static inline int sata_set_spd(struct ata_link *link) { return -EOPNOTSUPP; }
static inline int sata_link_hardreset(struct ata_link *link,
				      const unsigned long *timing,
				      unsigned long deadline,
				      bool *online,
				      int (*check_ready)(struct ata_link *))
{
	if (online)
		*online = false;
	return -EOPNOTSUPP;
}
static inline int sata_link_resume(struct ata_link *link,
				   const unsigned long *params,
				   unsigned long deadline)
{
	return -EOPNOTSUPP;
}
static inline void ata_eh_analyze_ncq_error(struct ata_link *link) { }
#endif
extern int sata_link_debounce(struct ata_link *link,
			const unsigned long *params, unsigned long deadline);
extern int sata_link_scr_lpm(struct ata_link *link, enum ata_lpm_policy policy,
			     bool spm_wakeup);
extern int ata_slave_link_init(struct ata_port *ap);
extern void ata_sas_port_destroy(struct ata_port *);
extern struct ata_port *ata_sas_port_alloc(struct ata_host *,
					   struct ata_port_info *, struct Scsi_Host *);
extern void ata_sas_async_probe(struct ata_port *ap);
extern int ata_sas_sync_probe(struct ata_port *ap);
extern int ata_sas_port_init(struct ata_port *);
extern int ata_sas_port_start(struct ata_port *ap);
extern int ata_sas_tport_add(struct device *parent, struct ata_port *ap);
extern void ata_sas_tport_delete(struct ata_port *ap);
extern void ata_sas_port_stop(struct ata_port *ap);
extern int ata_sas_slave_configure(struct scsi_device *, struct ata_port *);
extern int ata_sas_queuecmd(struct scsi_cmnd *cmd, struct ata_port *ap);
extern void ata_tf_to_fis(const struct ata_taskfile *tf,
			  u8 pmp, int is_cmd, u8 *fis);
extern void ata_tf_from_fis(const u8 *fis, struct ata_taskfile *tf);
extern int ata_qc_complete_multiple(struct ata_port *ap, u64 qc_active);
extern bool sata_lpm_ignore_phy_events(struct ata_link *link);
extern int sata_async_notification(struct ata_port *ap);

extern int ata_cable_40wire(struct ata_port *ap);
extern int ata_cable_80wire(struct ata_port *ap);
extern int ata_cable_sata(struct ata_port *ap);
extern int ata_cable_ignore(struct ata_port *ap);
extern int ata_cable_unknown(struct ata_port *ap);

/* Timing helpers */
extern unsigned int ata_pio_need_iordy(const struct ata_device *);
extern u8 ata_timing_cycle2mode(unsigned int xfer_shift, int cycle);

/* PCI */
#ifdef CONFIG_PCI
struct pci_dev;

struct pci_bits {
	unsigned int		reg;	/* PCI config register to read */
	unsigned int		width;	/* 1 (8 bit), 2 (16 bit), 4 (32 bit) */
	unsigned long		mask;
	unsigned long		val;
};

extern int pci_test_config_bits(struct pci_dev *pdev, const struct pci_bits *bits);
extern void ata_pci_shutdown_one(struct pci_dev *pdev);
extern void ata_pci_remove_one(struct pci_dev *pdev);

#ifdef CONFIG_PM
extern void ata_pci_device_do_suspend(struct pci_dev *pdev, pm_message_t mesg);
extern int __must_check ata_pci_device_do_resume(struct pci_dev *pdev);
extern int ata_pci_device_suspend(struct pci_dev *pdev, pm_message_t mesg);
extern int ata_pci_device_resume(struct pci_dev *pdev);
#endif /* CONFIG_PM */
#endif /* CONFIG_PCI */

struct platform_device;

extern int ata_platform_remove_one(struct platform_device *pdev);

/*
 * ACPI - drivers/ata/libata-acpi.c
 */
#ifdef CONFIG_ATA_ACPI
static inline const struct ata_acpi_gtm *ata_acpi_init_gtm(struct ata_port *ap)
{
	if (ap->pflags & ATA_PFLAG_INIT_GTM_VALID)
		return &ap->__acpi_init_gtm;
	return NULL;
}
int ata_acpi_stm(struct ata_port *ap, const struct ata_acpi_gtm *stm);
int ata_acpi_gtm(struct ata_port *ap, struct ata_acpi_gtm *stm);
unsigned int ata_acpi_gtm_xfermask(struct ata_device *dev,
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

/*
 * EH - drivers/ata/libata-eh.c
 */
extern void ata_port_schedule_eh(struct ata_port *ap);
extern void ata_port_wait_eh(struct ata_port *ap);
extern int ata_link_abort(struct ata_link *link);
extern int ata_port_abort(struct ata_port *ap);
extern int ata_port_freeze(struct ata_port *ap);

extern void ata_eh_freeze_port(struct ata_port *ap);
extern void ata_eh_thaw_port(struct ata_port *ap);

extern void ata_eh_qc_complete(struct ata_queued_cmd *qc);
extern void ata_eh_qc_retry(struct ata_queued_cmd *qc);

extern void ata_do_eh(struct ata_port *ap, ata_prereset_fn_t prereset,
		      ata_reset_fn_t softreset, ata_reset_fn_t hardreset,
		      ata_postreset_fn_t postreset);
extern void ata_std_error_handler(struct ata_port *ap);
extern void ata_std_sched_eh(struct ata_port *ap);
extern void ata_std_end_eh(struct ata_port *ap);
extern int ata_link_nr_enabled(struct ata_link *link);

/*
 * Base operations to inherit from and initializers for sht
 *
 * Operations
 *
 * base  : Common to all libata drivers.
 * sata  : SATA controllers w/ native interface.
 * pmp   : SATA controllers w/ PMP support.
 * sff   : SFF ATA controllers w/o BMDMA support.
 * bmdma : SFF ATA controllers w/ BMDMA support.
 *
 * sht initializers
 *
 * BASE  : Common to all libata drivers.  The user must set
 *	   sg_tablesize and dma_boundary.
 * PIO   : SFF ATA controllers w/ only PIO support.
 * BMDMA : SFF ATA controllers w/ BMDMA support.  sg_tablesize and
 *	   dma_boundary are set to BMDMA limits.
 * NCQ   : SATA controllers supporting NCQ.  The user must set
 *	   sg_tablesize, dma_boundary and can_queue.
 */
extern const struct ata_port_operations ata_base_port_ops;
extern const struct ata_port_operations sata_port_ops;
extern const struct attribute_group *ata_common_sdev_groups[];

/*
 * All sht initializers (BASE, PIO, BMDMA, NCQ) must be instantiated
 * by the edge drivers.  Because the 'module' field of sht must be the
 * edge driver's module reference, otherwise the driver can be unloaded
 * even if the scsi_device is being accessed.
 */
#define __ATA_BASE_SHT(drv_name)				\
	.module			= THIS_MODULE,			\
	.name			= drv_name,			\
	.ioctl			= ata_scsi_ioctl,		\
	ATA_SCSI_COMPAT_IOCTL					\
	.queuecommand		= ata_scsi_queuecmd,		\
	.dma_need_drain		= ata_scsi_dma_need_drain,	\
	.this_id		= ATA_SHT_THIS_ID,		\
	.emulated		= ATA_SHT_EMULATED,		\
	.proc_name		= drv_name,			\
	.slave_destroy		= ata_scsi_slave_destroy,	\
	.bios_param		= ata_std_bios_param,		\
	.unlock_native_capacity	= ata_scsi_unlock_native_capacity,\
	.max_sectors		= ATA_MAX_SECTORS_LBA48

#define ATA_SUBBASE_SHT(drv_name)				\
	__ATA_BASE_SHT(drv_name),				\
	.can_queue		= ATA_DEF_QUEUE,		\
	.tag_alloc_policy	= BLK_TAG_ALLOC_RR,		\
	.slave_configure	= ata_scsi_slave_config

#define ATA_SUBBASE_SHT_QD(drv_name, drv_qd)			\
	__ATA_BASE_SHT(drv_name),				\
	.can_queue		= drv_qd,			\
	.tag_alloc_policy	= BLK_TAG_ALLOC_RR,		\
	.slave_configure	= ata_scsi_slave_config

#define ATA_BASE_SHT(drv_name)					\
	ATA_SUBBASE_SHT(drv_name),				\
	.sdev_groups		= ata_common_sdev_groups

#ifdef CONFIG_SATA_HOST
extern const struct attribute_group *ata_ncq_sdev_groups[];

#define ATA_NCQ_SHT(drv_name)					\
	ATA_SUBBASE_SHT(drv_name),				\
	.sdev_groups		= ata_ncq_sdev_groups,		\
	.change_queue_depth	= ata_scsi_change_queue_depth

#define ATA_NCQ_SHT_QD(drv_name, drv_qd)			\
	ATA_SUBBASE_SHT_QD(drv_name, drv_qd),			\
	.sdev_groups		= ata_ncq_sdev_groups,		\
	.change_queue_depth	= ata_scsi_change_queue_depth
#endif

/*
 * PMP helpers
 */
#ifdef CONFIG_SATA_PMP
static inline bool sata_pmp_supported(struct ata_port *ap)
{
	return ap->flags & ATA_FLAG_PMP;
}

static inline bool sata_pmp_attached(struct ata_port *ap)
{
	return ap->nr_pmp_links != 0;
}

static inline bool ata_is_host_link(const struct ata_link *link)
{
	return link == &link->ap->link || link == link->ap->slave_link;
}
#else /* CONFIG_SATA_PMP */
static inline bool sata_pmp_supported(struct ata_port *ap)
{
	return false;
}

static inline bool sata_pmp_attached(struct ata_port *ap)
{
	return false;
}

static inline bool ata_is_host_link(const struct ata_link *link)
{
	return true;
}
#endif /* CONFIG_SATA_PMP */

static inline int sata_srst_pmp(struct ata_link *link)
{
	if (sata_pmp_supported(link->ap) && ata_is_host_link(link))
		return SATA_PMP_CTRL_PORT;
	return link->pmp;
}

#define ata_port_printk(level, ap, fmt, ...)			\
	pr_ ## level ("ata%u: " fmt, (ap)->print_id, ##__VA_ARGS__)

#define ata_port_err(ap, fmt, ...)				\
	ata_port_printk(err, ap, fmt, ##__VA_ARGS__)
#define ata_port_warn(ap, fmt, ...)				\
	ata_port_printk(warn, ap, fmt, ##__VA_ARGS__)
#define ata_port_notice(ap, fmt, ...)				\
	ata_port_printk(notice, ap, fmt, ##__VA_ARGS__)
#define ata_port_info(ap, fmt, ...)				\
	ata_port_printk(info, ap, fmt, ##__VA_ARGS__)
#define ata_port_dbg(ap, fmt, ...)				\
	ata_port_printk(debug, ap, fmt, ##__VA_ARGS__)

#define ata_link_printk(level, link, fmt, ...)			\
do {								\
	if (sata_pmp_attached((link)->ap) ||			\
	    (link)->ap->slave_link)				\
		pr_ ## level ("ata%u.%02u: " fmt,		\
			      (link)->ap->print_id,		\
			      (link)->pmp,			\
			      ##__VA_ARGS__);			\
        else							\
		pr_ ## level ("ata%u: " fmt,			\
			      (link)->ap->print_id,		\
			      ##__VA_ARGS__);			\
} while (0)

#define ata_link_err(link, fmt, ...)				\
	ata_link_printk(err, link, fmt, ##__VA_ARGS__)
#define ata_link_warn(link, fmt, ...)				\
	ata_link_printk(warn, link, fmt, ##__VA_ARGS__)
#define ata_link_notice(link, fmt, ...)				\
	ata_link_printk(notice, link, fmt, ##__VA_ARGS__)
#define ata_link_info(link, fmt, ...)				\
	ata_link_printk(info, link, fmt, ##__VA_ARGS__)
#define ata_link_dbg(link, fmt, ...)				\
	ata_link_printk(debug, link, fmt, ##__VA_ARGS__)

#define ata_dev_printk(level, dev, fmt, ...)			\
        pr_ ## level("ata%u.%02u: " fmt,			\
               (dev)->link->ap->print_id,			\
	       (dev)->link->pmp + (dev)->devno,			\
	       ##__VA_ARGS__)

#define ata_dev_err(dev, fmt, ...)				\
	ata_dev_printk(err, dev, fmt, ##__VA_ARGS__)
#define ata_dev_warn(dev, fmt, ...)				\
	ata_dev_printk(warn, dev, fmt, ##__VA_ARGS__)
#define ata_dev_notice(dev, fmt, ...)				\
	ata_dev_printk(notice, dev, fmt, ##__VA_ARGS__)
#define ata_dev_info(dev, fmt, ...)				\
	ata_dev_printk(info, dev, fmt, ##__VA_ARGS__)
#define ata_dev_dbg(dev, fmt, ...)				\
	ata_dev_printk(debug, dev, fmt, ##__VA_ARGS__)

void ata_print_version(const struct device *dev, const char *version);

/*
 * ata_eh_info helpers
 */
extern __printf(2, 3)
void __ata_ehi_push_desc(struct ata_eh_info *ehi, const char *fmt, ...);
extern __printf(2, 3)
void ata_ehi_push_desc(struct ata_eh_info *ehi, const char *fmt, ...);
extern void ata_ehi_clear_desc(struct ata_eh_info *ehi);

static inline void ata_ehi_hotplugged(struct ata_eh_info *ehi)
{
	ehi->probe_mask |= (1 << ATA_MAX_DEVICES) - 1;
	ehi->flags |= ATA_EHI_HOTPLUGGED;
	ehi->action |= ATA_EH_RESET | ATA_EH_ENABLE_LINK;
	ehi->err_mask |= AC_ERR_ATA_BUS;
}

/*
 * port description helpers
 */
extern __printf(2, 3)
void ata_port_desc(struct ata_port *ap, const char *fmt, ...);
#ifdef CONFIG_PCI
extern void ata_port_pbar_desc(struct ata_port *ap, int bar, ssize_t offset,
			       const char *name);
#endif

static inline bool ata_tag_internal(unsigned int tag)
{
	return tag == ATA_TAG_INTERNAL;
}

static inline bool ata_tag_valid(unsigned int tag)
{
	return tag < ATA_MAX_QUEUE || ata_tag_internal(tag);
}

#define __ata_qc_for_each(ap, qc, tag, max_tag, fn)		\
	for ((tag) = 0; (tag) < (max_tag) &&			\
	     ({ qc = fn((ap), (tag)); 1; }); (tag)++)		\

/*
 * Internal use only, iterate commands ignoring error handling and
 * status of 'qc'.
 */
#define ata_qc_for_each_raw(ap, qc, tag)					\
	__ata_qc_for_each(ap, qc, tag, ATA_MAX_QUEUE, __ata_qc_from_tag)

/*
 * Iterate all potential commands that can be queued
 */
#define ata_qc_for_each(ap, qc, tag)					\
	__ata_qc_for_each(ap, qc, tag, ATA_MAX_QUEUE, ata_qc_from_tag)

/*
 * Like ata_qc_for_each, but with the internal tag included
 */
#define ata_qc_for_each_with_internal(ap, qc, tag)			\
	__ata_qc_for_each(ap, qc, tag, ATA_MAX_QUEUE + 1, ata_qc_from_tag)

/*
 * device helpers
 */
static inline unsigned int ata_class_enabled(unsigned int class)
{
	return class == ATA_DEV_ATA || class == ATA_DEV_ATAPI ||
		class == ATA_DEV_PMP || class == ATA_DEV_SEMB ||
		class == ATA_DEV_ZAC;
}

static inline unsigned int ata_class_disabled(unsigned int class)
{
	return class == ATA_DEV_ATA_UNSUP || class == ATA_DEV_ATAPI_UNSUP ||
		class == ATA_DEV_PMP_UNSUP || class == ATA_DEV_SEMB_UNSUP ||
		class == ATA_DEV_ZAC_UNSUP;
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

/*
 * Iterators
 *
 * ATA_LITER_* constants are used to select link iteration mode and
 * ATA_DITER_* device iteration mode.
 *
 * For a custom iteration directly using ata_{link|dev}_next(), if
 * @link or @dev, respectively, is NULL, the first element is
 * returned.  @dev and @link can be any valid device or link and the
 * next element according to the iteration mode will be returned.
 * After the last element, NULL is returned.
 */
enum ata_link_iter_mode {
	ATA_LITER_EDGE,		/* if present, PMP links only; otherwise,
				 * host link.  no slave link */
	ATA_LITER_HOST_FIRST,	/* host link followed by PMP or slave links */
	ATA_LITER_PMP_FIRST,	/* PMP links followed by host link,
				 * slave link still comes after host link */
};

enum ata_dev_iter_mode {
	ATA_DITER_ENABLED,
	ATA_DITER_ENABLED_REVERSE,
	ATA_DITER_ALL,
	ATA_DITER_ALL_REVERSE,
};

extern struct ata_link *ata_link_next(struct ata_link *link,
				      struct ata_port *ap,
				      enum ata_link_iter_mode mode);

extern struct ata_device *ata_dev_next(struct ata_device *dev,
				       struct ata_link *link,
				       enum ata_dev_iter_mode mode);

/*
 * Shortcut notation for iterations
 *
 * ata_for_each_link() iterates over each link of @ap according to
 * @mode.  @link points to the current link in the loop.  @link is
 * NULL after loop termination.  ata_for_each_dev() works the same way
 * except that it iterates over each device of @link.
 *
 * Note that the mode prefixes ATA_{L|D}ITER_ shouldn't need to be
 * specified when using the following shorthand notations.  Only the
 * mode itself (EDGE, HOST_FIRST, ENABLED, etc...) should be
 * specified.  This not only increases brevity but also makes it
 * impossible to use ATA_LITER_* for device iteration or vice-versa.
 */
#define ata_for_each_link(link, ap, mode) \
	for ((link) = ata_link_next(NULL, (ap), ATA_LITER_##mode); (link); \
	     (link) = ata_link_next((link), (ap), ATA_LITER_##mode))

#define ata_for_each_dev(dev, link, mode) \
	for ((dev) = ata_dev_next(NULL, (link), ATA_DITER_##mode); (dev); \
	     (dev) = ata_dev_next((dev), (link), ATA_DITER_##mode))

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
	if (!IS_ENABLED(CONFIG_SATA_HOST))
		return 0;
	return (dev->flags & (ATA_DFLAG_PIO | ATA_DFLAG_NCQ_OFF |
			      ATA_DFLAG_NCQ)) == ATA_DFLAG_NCQ;
}

static inline bool ata_fpdma_dsm_supported(struct ata_device *dev)
{
	return (dev->flags & ATA_DFLAG_NCQ_SEND_RECV) &&
		(dev->ncq_send_recv_cmds[ATA_LOG_NCQ_SEND_RECV_DSM_OFFSET] &
		 ATA_LOG_NCQ_SEND_RECV_DSM_TRIM);
}

static inline bool ata_fpdma_read_log_supported(struct ata_device *dev)
{
	return (dev->flags & ATA_DFLAG_NCQ_SEND_RECV) &&
		(dev->ncq_send_recv_cmds[ATA_LOG_NCQ_SEND_RECV_RD_LOG_OFFSET] &
		 ATA_LOG_NCQ_SEND_RECV_RD_LOG_SUPPORTED);
}

static inline bool ata_fpdma_zac_mgmt_in_supported(struct ata_device *dev)
{
	return (dev->flags & ATA_DFLAG_NCQ_SEND_RECV) &&
		(dev->ncq_send_recv_cmds[ATA_LOG_NCQ_SEND_RECV_ZAC_MGMT_OFFSET] &
		ATA_LOG_NCQ_SEND_RECV_ZAC_MGMT_IN_SUPPORTED);
}

static inline bool ata_fpdma_zac_mgmt_out_supported(struct ata_device *dev)
{
	return (dev->ncq_non_data_cmds[ATA_LOG_NCQ_NON_DATA_ZAC_MGMT_OFFSET] &
		ATA_LOG_NCQ_NON_DATA_ZAC_MGMT_OUT);
}

static inline void ata_qc_set_polling(struct ata_queued_cmd *qc)
{
	qc->tf.ctl |= ATA_NIEN;
}

static inline struct ata_queued_cmd *__ata_qc_from_tag(struct ata_port *ap,
						       unsigned int tag)
{
	if (ata_tag_valid(tag))
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

#ifdef CONFIG_ATA_SFF
	tf->ctl = dev->link->ap->ctl;
#else
	tf->ctl = ATA_DEVCTL_OBS;
#endif
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

static inline int ata_check_ready(u8 status)
{
	if (!(status & ATA_BUSY))
		return 1;

	/* 0xff indicates either no device or device not ready */
	if (status == 0xff)
		return -ENODEV;

	return 0;
}

static inline unsigned long ata_deadline(unsigned long from_jiffies,
					 unsigned long timeout_msecs)
{
	return from_jiffies + msecs_to_jiffies(timeout_msecs);
}

/* Don't open code these in drivers as there are traps. Firstly the range may
   change in future hardware and specs, secondly 0xFF means 'no DMA' but is
   > UDMA_0. Dyma ddreigiau */

static inline int ata_using_mwdma(struct ata_device *adev)
{
	if (adev->dma_mode >= XFER_MW_DMA_0 && adev->dma_mode <= XFER_MW_DMA_4)
		return 1;
	return 0;
}

static inline int ata_using_udma(struct ata_device *adev)
{
	if (adev->dma_mode >= XFER_UDMA_0 && adev->dma_mode <= XFER_UDMA_7)
		return 1;
	return 0;
}

static inline int ata_dma_enabled(struct ata_device *adev)
{
	return (adev->dma_mode == 0xFF ? 0 : 1);
}

/**************************************************************************
 * PATA timings - drivers/ata/libata-pata-timings.c
 */
extern const struct ata_timing *ata_timing_find_mode(u8 xfer_mode);
extern int ata_timing_compute(struct ata_device *, unsigned short,
			      struct ata_timing *, int, int);
extern void ata_timing_merge(const struct ata_timing *,
			     const struct ata_timing *, struct ata_timing *,
			     unsigned int);

/**************************************************************************
 * PMP - drivers/ata/libata-pmp.c
 */
#ifdef CONFIG_SATA_PMP

extern const struct ata_port_operations sata_pmp_port_ops;

extern int sata_pmp_qc_defer_cmd_switch(struct ata_queued_cmd *qc);
extern void sata_pmp_error_handler(struct ata_port *ap);

#else /* CONFIG_SATA_PMP */

#define sata_pmp_port_ops		sata_port_ops
#define sata_pmp_qc_defer_cmd_switch	ata_std_qc_defer
#define sata_pmp_error_handler		ata_std_error_handler

#endif /* CONFIG_SATA_PMP */


/**************************************************************************
 * SFF - drivers/ata/libata-sff.c
 */
#ifdef CONFIG_ATA_SFF

extern const struct ata_port_operations ata_sff_port_ops;
extern const struct ata_port_operations ata_bmdma32_port_ops;

/* PIO only, sg_tablesize and dma_boundary limits can be removed */
#define ATA_PIO_SHT(drv_name)					\
	ATA_BASE_SHT(drv_name),					\
	.sg_tablesize		= LIBATA_MAX_PRD,		\
	.dma_boundary		= ATA_DMA_BOUNDARY

extern void ata_sff_dev_select(struct ata_port *ap, unsigned int device);
extern u8 ata_sff_check_status(struct ata_port *ap);
extern void ata_sff_pause(struct ata_port *ap);
extern void ata_sff_dma_pause(struct ata_port *ap);
extern int ata_sff_busy_sleep(struct ata_port *ap,
			      unsigned long timeout_pat, unsigned long timeout);
extern int ata_sff_wait_ready(struct ata_link *link, unsigned long deadline);
extern void ata_sff_tf_load(struct ata_port *ap, const struct ata_taskfile *tf);
extern void ata_sff_tf_read(struct ata_port *ap, struct ata_taskfile *tf);
extern void ata_sff_exec_command(struct ata_port *ap,
				 const struct ata_taskfile *tf);
extern unsigned int ata_sff_data_xfer(struct ata_queued_cmd *qc,
			unsigned char *buf, unsigned int buflen, int rw);
extern unsigned int ata_sff_data_xfer32(struct ata_queued_cmd *qc,
			unsigned char *buf, unsigned int buflen, int rw);
extern void ata_sff_irq_on(struct ata_port *ap);
extern void ata_sff_irq_clear(struct ata_port *ap);
extern int ata_sff_hsm_move(struct ata_port *ap, struct ata_queued_cmd *qc,
			    u8 status, int in_wq);
extern void ata_sff_queue_work(struct work_struct *work);
extern void ata_sff_queue_delayed_work(struct delayed_work *dwork,
		unsigned long delay);
extern void ata_sff_queue_pio_task(struct ata_link *link, unsigned long delay);
extern unsigned int ata_sff_qc_issue(struct ata_queued_cmd *qc);
extern bool ata_sff_qc_fill_rtf(struct ata_queued_cmd *qc);
extern unsigned int ata_sff_port_intr(struct ata_port *ap,
				      struct ata_queued_cmd *qc);
extern irqreturn_t ata_sff_interrupt(int irq, void *dev_instance);
extern void ata_sff_lost_interrupt(struct ata_port *ap);
extern void ata_sff_freeze(struct ata_port *ap);
extern void ata_sff_thaw(struct ata_port *ap);
extern int ata_sff_prereset(struct ata_link *link, unsigned long deadline);
extern unsigned int ata_sff_dev_classify(struct ata_device *dev, int present,
					  u8 *r_err);
extern int ata_sff_wait_after_reset(struct ata_link *link, unsigned int devmask,
				    unsigned long deadline);
extern int ata_sff_softreset(struct ata_link *link, unsigned int *classes,
			     unsigned long deadline);
extern int sata_sff_hardreset(struct ata_link *link, unsigned int *class,
			       unsigned long deadline);
extern void ata_sff_postreset(struct ata_link *link, unsigned int *classes);
extern void ata_sff_drain_fifo(struct ata_queued_cmd *qc);
extern void ata_sff_error_handler(struct ata_port *ap);
extern void ata_sff_std_ports(struct ata_ioports *ioaddr);
#ifdef CONFIG_PCI
extern int ata_pci_sff_init_host(struct ata_host *host);
extern int ata_pci_sff_prepare_host(struct pci_dev *pdev,
				    const struct ata_port_info * const * ppi,
				    struct ata_host **r_host);
extern int ata_pci_sff_activate_host(struct ata_host *host,
				     irq_handler_t irq_handler,
				     struct scsi_host_template *sht);
extern int ata_pci_sff_init_one(struct pci_dev *pdev,
		const struct ata_port_info * const * ppi,
		struct scsi_host_template *sht, void *host_priv, int hflags);
#endif /* CONFIG_PCI */

#ifdef CONFIG_ATA_BMDMA

extern const struct ata_port_operations ata_bmdma_port_ops;

#define ATA_BMDMA_SHT(drv_name)					\
	ATA_BASE_SHT(drv_name),					\
	.sg_tablesize		= LIBATA_MAX_PRD,		\
	.dma_boundary		= ATA_DMA_BOUNDARY

extern enum ata_completion_errors ata_bmdma_qc_prep(struct ata_queued_cmd *qc);
extern unsigned int ata_bmdma_qc_issue(struct ata_queued_cmd *qc);
extern enum ata_completion_errors ata_bmdma_dumb_qc_prep(struct ata_queued_cmd *qc);
extern unsigned int ata_bmdma_port_intr(struct ata_port *ap,
				      struct ata_queued_cmd *qc);
extern irqreturn_t ata_bmdma_interrupt(int irq, void *dev_instance);
extern void ata_bmdma_error_handler(struct ata_port *ap);
extern void ata_bmdma_post_internal_cmd(struct ata_queued_cmd *qc);
extern void ata_bmdma_irq_clear(struct ata_port *ap);
extern void ata_bmdma_setup(struct ata_queued_cmd *qc);
extern void ata_bmdma_start(struct ata_queued_cmd *qc);
extern void ata_bmdma_stop(struct ata_queued_cmd *qc);
extern u8 ata_bmdma_status(struct ata_port *ap);
extern int ata_bmdma_port_start(struct ata_port *ap);
extern int ata_bmdma_port_start32(struct ata_port *ap);

#ifdef CONFIG_PCI
extern int ata_pci_bmdma_clear_simplex(struct pci_dev *pdev);
extern void ata_pci_bmdma_init(struct ata_host *host);
extern int ata_pci_bmdma_prepare_host(struct pci_dev *pdev,
				      const struct ata_port_info * const * ppi,
				      struct ata_host **r_host);
extern int ata_pci_bmdma_init_one(struct pci_dev *pdev,
				  const struct ata_port_info * const * ppi,
				  struct scsi_host_template *sht,
				  void *host_priv, int hflags);
#endif /* CONFIG_PCI */
#endif /* CONFIG_ATA_BMDMA */

/**
 *	ata_sff_busy_wait - Wait for a port status register
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
static inline u8 ata_sff_busy_wait(struct ata_port *ap, unsigned int bits,
				   unsigned int max)
{
	u8 status;

	do {
		udelay(10);
		status = ap->ops->sff_check_status(ap);
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
	u8 status = ata_sff_busy_wait(ap, ATA_BUSY | ATA_DRQ, 1000);

	if (status != 0xff && (status & (ATA_BUSY | ATA_DRQ)))
		ata_port_dbg(ap, "abnormal Status 0x%X\n", status);

	return status;
}
#endif /* CONFIG_ATA_SFF */

#endif /* __LINUX_LIBATA_H__ */
