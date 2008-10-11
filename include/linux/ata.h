
/*
 *  Copyright 2003-2004 Red Hat, Inc.  All rights reserved.
 *  Copyright 2003-2004 Jeff Garzik
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
 *  Hardware documentation available from http://www.t13.org/
 *
 */

#ifndef __LINUX_ATA_H__
#define __LINUX_ATA_H__

#include <linux/types.h>

/* defines only for the constants which don't work well as enums */
#define ATA_DMA_BOUNDARY	0xffffUL
#define ATA_DMA_MASK		0xffffffffULL

enum {
	/* various global constants */
	ATA_MAX_DEVICES		= 2,	/* per bus/port */
	ATA_MAX_PRD		= 256,	/* we could make these 256/256 */
	ATA_SECT_SIZE		= 512,
	ATA_MAX_SECTORS_128	= 128,
	ATA_MAX_SECTORS		= 256,
	ATA_MAX_SECTORS_LBA48	= 65535,/* TODO: 65536? */
	ATA_MAX_SECTORS_TAPE	= 65535,

	ATA_ID_WORDS		= 256,
	ATA_ID_CONFIG		= 0,
	ATA_ID_CYLS		= 1,
	ATA_ID_HEADS		= 3,
	ATA_ID_SECTORS		= 6,
	ATA_ID_SERNO		= 10,
	ATA_ID_BUF_SIZE		= 21,
	ATA_ID_FW_REV		= 23,
	ATA_ID_PROD		= 27,
	ATA_ID_MAX_MULTSECT	= 47,
	ATA_ID_DWORD_IO		= 48,
	ATA_ID_CAPABILITY	= 49,
	ATA_ID_OLD_PIO_MODES	= 51,
	ATA_ID_OLD_DMA_MODES	= 52,
	ATA_ID_FIELD_VALID	= 53,
	ATA_ID_CUR_CYLS		= 54,
	ATA_ID_CUR_HEADS	= 55,
	ATA_ID_CUR_SECTORS	= 56,
	ATA_ID_MULTSECT		= 59,
	ATA_ID_LBA_CAPACITY	= 60,
	ATA_ID_SWDMA_MODES	= 62,
	ATA_ID_MWDMA_MODES	= 63,
	ATA_ID_PIO_MODES	= 64,
	ATA_ID_EIDE_DMA_MIN	= 65,
	ATA_ID_EIDE_DMA_TIME	= 66,
	ATA_ID_EIDE_PIO		= 67,
	ATA_ID_EIDE_PIO_IORDY	= 68,
	ATA_ID_QUEUE_DEPTH	= 75,
	ATA_ID_MAJOR_VER	= 80,
	ATA_ID_COMMAND_SET_1	= 82,
	ATA_ID_COMMAND_SET_2	= 83,
	ATA_ID_CFSSE		= 84,
	ATA_ID_CFS_ENABLE_1	= 85,
	ATA_ID_CFS_ENABLE_2	= 86,
	ATA_ID_CSF_DEFAULT	= 87,
	ATA_ID_UDMA_MODES	= 88,
	ATA_ID_HW_CONFIG	= 93,
	ATA_ID_SPG		= 98,
	ATA_ID_LBA_CAPACITY_2	= 100,
	ATA_ID_LAST_LUN		= 126,
	ATA_ID_DLF		= 128,
	ATA_ID_CSFO		= 129,
	ATA_ID_CFA_POWER	= 160,
	ATA_ID_ROT_SPEED	= 217,
	ATA_ID_PIO4		= (1 << 1),

	ATA_ID_SERNO_LEN	= 20,
	ATA_ID_FW_REV_LEN	= 8,
	ATA_ID_PROD_LEN		= 40,

	ATA_PCI_CTL_OFS		= 2,

	ATA_PIO0		= (1 << 0),
	ATA_PIO1		= ATA_PIO0 | (1 << 1),
	ATA_PIO2		= ATA_PIO1 | (1 << 2),
	ATA_PIO3		= ATA_PIO2 | (1 << 3),
	ATA_PIO4		= ATA_PIO3 | (1 << 4),
	ATA_PIO5		= ATA_PIO4 | (1 << 5),
	ATA_PIO6		= ATA_PIO5 | (1 << 6),

	ATA_SWDMA0		= (1 << 0),
	ATA_SWDMA1		= ATA_SWDMA0 | (1 << 1),
	ATA_SWDMA2		= ATA_SWDMA1 | (1 << 2),

	ATA_SWDMA2_ONLY		= (1 << 2),

	ATA_MWDMA0		= (1 << 0),
	ATA_MWDMA1		= ATA_MWDMA0 | (1 << 1),
	ATA_MWDMA2		= ATA_MWDMA1 | (1 << 2),

	ATA_MWDMA12_ONLY	= (1 << 1) | (1 << 2),
	ATA_MWDMA2_ONLY		= (1 << 2),

	ATA_UDMA0		= (1 << 0),
	ATA_UDMA1		= ATA_UDMA0 | (1 << 1),
	ATA_UDMA2		= ATA_UDMA1 | (1 << 2),
	ATA_UDMA3		= ATA_UDMA2 | (1 << 3),
	ATA_UDMA4		= ATA_UDMA3 | (1 << 4),
	ATA_UDMA5		= ATA_UDMA4 | (1 << 5),
	ATA_UDMA6		= ATA_UDMA5 | (1 << 6),
	ATA_UDMA7		= ATA_UDMA6 | (1 << 7),
	/* ATA_UDMA7 is just for completeness... doesn't exist (yet?).  */

	ATA_UDMA_MASK_40C	= ATA_UDMA2,	/* udma0-2 */

	/* DMA-related */
	ATA_PRD_SZ		= 8,
	ATA_PRD_TBL_SZ		= (ATA_MAX_PRD * ATA_PRD_SZ),
	ATA_PRD_EOT		= (1 << 31),	/* end-of-table flag */

	ATA_DMA_TABLE_OFS	= 4,
	ATA_DMA_STATUS		= 2,
	ATA_DMA_CMD		= 0,
	ATA_DMA_WR		= (1 << 3),
	ATA_DMA_START		= (1 << 0),
	ATA_DMA_INTR		= (1 << 2),
	ATA_DMA_ERR		= (1 << 1),
	ATA_DMA_ACTIVE		= (1 << 0),

	/* bits in ATA command block registers */
	ATA_HOB			= (1 << 7),	/* LBA48 selector */
	ATA_NIEN		= (1 << 1),	/* disable-irq flag */
	ATA_LBA			= (1 << 6),	/* LBA28 selector */
	ATA_DEV1		= (1 << 4),	/* Select Device 1 (slave) */
	ATA_DEVICE_OBS		= (1 << 7) | (1 << 5), /* obs bits in dev reg */
	ATA_DEVCTL_OBS		= (1 << 3),	/* obsolete bit in devctl reg */
	ATA_BUSY		= (1 << 7),	/* BSY status bit */
	ATA_DRDY		= (1 << 6),	/* device ready */
	ATA_DF			= (1 << 5),	/* device fault */
	ATA_DSC			= (1 << 4),	/* drive seek complete */
	ATA_DRQ			= (1 << 3),	/* data request i/o */
	ATA_CORR		= (1 << 2),	/* corrected data error */
	ATA_IDX			= (1 << 1),	/* index */
	ATA_ERR			= (1 << 0),	/* have an error */
	ATA_SRST		= (1 << 2),	/* software reset */
	ATA_ICRC		= (1 << 7),	/* interface CRC error */
	ATA_BBK			= ATA_ICRC,	/* pre-EIDE: block marked bad */
	ATA_UNC			= (1 << 6),	/* uncorrectable media error */
	ATA_MC			= (1 << 5),	/* media changed */
	ATA_IDNF		= (1 << 4),	/* ID not found */
	ATA_MCR			= (1 << 3),	/* media change requested */
	ATA_ABORTED		= (1 << 2),	/* command aborted */
	ATA_TRK0NF		= (1 << 1),	/* track 0 not found */
	ATA_AMNF		= (1 << 0),	/* address mark not found */
	ATAPI_LFS		= 0xF0,		/* last failed sense */
	ATAPI_EOM		= ATA_TRK0NF,	/* end of media */
	ATAPI_ILI		= ATA_AMNF,	/* illegal length indication */
	ATAPI_IO		= (1 << 1),
	ATAPI_COD		= (1 << 0),

	/* ATA command block registers */
	ATA_REG_DATA		= 0x00,
	ATA_REG_ERR		= 0x01,
	ATA_REG_NSECT		= 0x02,
	ATA_REG_LBAL		= 0x03,
	ATA_REG_LBAM		= 0x04,
	ATA_REG_LBAH		= 0x05,
	ATA_REG_DEVICE		= 0x06,
	ATA_REG_STATUS		= 0x07,

	ATA_REG_FEATURE		= ATA_REG_ERR, /* and their aliases */
	ATA_REG_CMD		= ATA_REG_STATUS,
	ATA_REG_BYTEL		= ATA_REG_LBAM,
	ATA_REG_BYTEH		= ATA_REG_LBAH,
	ATA_REG_DEVSEL		= ATA_REG_DEVICE,
	ATA_REG_IRQ		= ATA_REG_NSECT,

	/* ATA device commands */
	ATA_CMD_DEV_RESET	= 0x08, /* ATAPI device reset */
	ATA_CMD_CHK_POWER	= 0xE5, /* check power mode */
	ATA_CMD_STANDBY		= 0xE2, /* place in standby power mode */
	ATA_CMD_IDLE		= 0xE3, /* place in idle power mode */
	ATA_CMD_EDD		= 0x90,	/* execute device diagnostic */
	ATA_CMD_FLUSH		= 0xE7,
	ATA_CMD_FLUSH_EXT	= 0xEA,
	ATA_CMD_ID_ATA		= 0xEC,
	ATA_CMD_ID_ATAPI	= 0xA1,
	ATA_CMD_READ		= 0xC8,
	ATA_CMD_READ_EXT	= 0x25,
	ATA_CMD_WRITE		= 0xCA,
	ATA_CMD_WRITE_EXT	= 0x35,
	ATA_CMD_WRITE_FUA_EXT	= 0x3D,
	ATA_CMD_FPDMA_READ	= 0x60,
	ATA_CMD_FPDMA_WRITE	= 0x61,
	ATA_CMD_PIO_READ	= 0x20,
	ATA_CMD_PIO_READ_EXT	= 0x24,
	ATA_CMD_PIO_WRITE	= 0x30,
	ATA_CMD_PIO_WRITE_EXT	= 0x34,
	ATA_CMD_READ_MULTI	= 0xC4,
	ATA_CMD_READ_MULTI_EXT	= 0x29,
	ATA_CMD_WRITE_MULTI	= 0xC5,
	ATA_CMD_WRITE_MULTI_EXT	= 0x39,
	ATA_CMD_WRITE_MULTI_FUA_EXT = 0xCE,
	ATA_CMD_SET_FEATURES	= 0xEF,
	ATA_CMD_SET_MULTI	= 0xC6,
	ATA_CMD_PACKET		= 0xA0,
	ATA_CMD_VERIFY		= 0x40,
	ATA_CMD_VERIFY_EXT	= 0x42,
	ATA_CMD_STANDBYNOW1	= 0xE0,
	ATA_CMD_IDLEIMMEDIATE	= 0xE1,
	ATA_CMD_SLEEP		= 0xE6,
	ATA_CMD_INIT_DEV_PARAMS	= 0x91,
	ATA_CMD_READ_NATIVE_MAX	= 0xF8,
	ATA_CMD_READ_NATIVE_MAX_EXT = 0x27,
	ATA_CMD_SET_MAX		= 0xF9,
	ATA_CMD_SET_MAX_EXT	= 0x37,
	ATA_CMD_READ_LOG_EXT	= 0x2f,
	ATA_CMD_PMP_READ	= 0xE4,
	ATA_CMD_PMP_WRITE	= 0xE8,
	ATA_CMD_CONF_OVERLAY	= 0xB1,
	ATA_CMD_SEC_FREEZE_LOCK	= 0xF5,
	ATA_CMD_SMART		= 0xB0,
	ATA_CMD_MEDIA_LOCK	= 0xDE,
	ATA_CMD_MEDIA_UNLOCK	= 0xDF,
	/* marked obsolete in the ATA/ATAPI-7 spec */
	ATA_CMD_RESTORE		= 0x10,
	/* EXABYTE specific */
	ATA_EXABYTE_ENABLE_NEST	= 0xF0,

	/* READ_LOG_EXT pages */
	ATA_LOG_SATA_NCQ	= 0x10,

	/* READ/WRITE LONG (obsolete) */
	ATA_CMD_READ_LONG	= 0x22,
	ATA_CMD_READ_LONG_ONCE	= 0x23,
	ATA_CMD_WRITE_LONG	= 0x32,
	ATA_CMD_WRITE_LONG_ONCE	= 0x33,

	/* SETFEATURES stuff */
	SETFEATURES_XFER	= 0x03,
	XFER_UDMA_7		= 0x47,
	XFER_UDMA_6		= 0x46,
	XFER_UDMA_5		= 0x45,
	XFER_UDMA_4		= 0x44,
	XFER_UDMA_3		= 0x43,
	XFER_UDMA_2		= 0x42,
	XFER_UDMA_1		= 0x41,
	XFER_UDMA_0		= 0x40,
	XFER_MW_DMA_4		= 0x24,	/* CFA only */
	XFER_MW_DMA_3		= 0x23,	/* CFA only */
	XFER_MW_DMA_2		= 0x22,
	XFER_MW_DMA_1		= 0x21,
	XFER_MW_DMA_0		= 0x20,
	XFER_SW_DMA_2		= 0x12,
	XFER_SW_DMA_1		= 0x11,
	XFER_SW_DMA_0		= 0x10,
	XFER_PIO_6		= 0x0E,	/* CFA only */
	XFER_PIO_5		= 0x0D,	/* CFA only */
	XFER_PIO_4		= 0x0C,
	XFER_PIO_3		= 0x0B,
	XFER_PIO_2		= 0x0A,
	XFER_PIO_1		= 0x09,
	XFER_PIO_0		= 0x08,
	XFER_PIO_SLOW		= 0x00,

	SETFEATURES_WC_ON	= 0x02, /* Enable write cache */
	SETFEATURES_WC_OFF	= 0x82, /* Disable write cache */

	/* Enable/Disable Automatic Acoustic Management */
	SETFEATURES_AAM_ON	= 0x42,
	SETFEATURES_AAM_OFF	= 0xC2,

	SETFEATURES_SPINUP	= 0x07, /* Spin-up drive */

	SETFEATURES_SATA_ENABLE = 0x10, /* Enable use of SATA feature */
	SETFEATURES_SATA_DISABLE = 0x90, /* Disable use of SATA feature */

	/* SETFEATURE Sector counts for SATA features */
	SATA_AN			= 0x05,  /* Asynchronous Notification */
	SATA_DIPM		= 0x03,  /* Device Initiated Power Management */

	/* feature values for SET_MAX */
	ATA_SET_MAX_ADDR	= 0x00,
	ATA_SET_MAX_PASSWD	= 0x01,
	ATA_SET_MAX_LOCK	= 0x02,
	ATA_SET_MAX_UNLOCK	= 0x03,
	ATA_SET_MAX_FREEZE_LOCK	= 0x04,

	/* feature values for DEVICE CONFIGURATION OVERLAY */
	ATA_DCO_RESTORE		= 0xC0,
	ATA_DCO_FREEZE_LOCK	= 0xC1,
	ATA_DCO_IDENTIFY	= 0xC2,
	ATA_DCO_SET		= 0xC3,

	/* feature values for SMART */
	ATA_SMART_ENABLE	= 0xD8,
	ATA_SMART_READ_VALUES	= 0xD0,
	ATA_SMART_READ_THRESHOLDS = 0xD1,

	/* password used in LBA Mid / LBA High for executing SMART commands */
	ATA_SMART_LBAM_PASS	= 0x4F,
	ATA_SMART_LBAH_PASS	= 0xC2,

	/* ATAPI stuff */
	ATAPI_PKT_DMA		= (1 << 0),
	ATAPI_DMADIR		= (1 << 2),	/* ATAPI data dir:
						   0=to device, 1=to host */
	ATAPI_CDB_LEN		= 16,

	/* PMP stuff */
	SATA_PMP_MAX_PORTS	= 15,
	SATA_PMP_CTRL_PORT	= 15,

	SATA_PMP_GSCR_DWORDS	= 128,
	SATA_PMP_GSCR_PROD_ID	= 0,
	SATA_PMP_GSCR_REV	= 1,
	SATA_PMP_GSCR_PORT_INFO	= 2,
	SATA_PMP_GSCR_ERROR	= 32,
	SATA_PMP_GSCR_ERROR_EN	= 33,
	SATA_PMP_GSCR_FEAT	= 64,
	SATA_PMP_GSCR_FEAT_EN	= 96,

	SATA_PMP_PSCR_STATUS	= 0,
	SATA_PMP_PSCR_ERROR	= 1,
	SATA_PMP_PSCR_CONTROL	= 2,

	SATA_PMP_FEAT_BIST	= (1 << 0),
	SATA_PMP_FEAT_PMREQ	= (1 << 1),
	SATA_PMP_FEAT_DYNSSC	= (1 << 2),
	SATA_PMP_FEAT_NOTIFY	= (1 << 3),

	/* cable types */
	ATA_CBL_NONE		= 0,
	ATA_CBL_PATA40		= 1,
	ATA_CBL_PATA80		= 2,
	ATA_CBL_PATA40_SHORT	= 3,	/* 40 wire cable to high UDMA spec */
	ATA_CBL_PATA_UNK	= 4,	/* don't know, maybe 80c? */
	ATA_CBL_PATA_IGN	= 5,	/* don't know, ignore cable handling */
	ATA_CBL_SATA		= 6,

	/* SATA Status and Control Registers */
	SCR_STATUS		= 0,
	SCR_ERROR		= 1,
	SCR_CONTROL		= 2,
	SCR_ACTIVE		= 3,
	SCR_NOTIFICATION	= 4,

	/* SError bits */
	SERR_DATA_RECOVERED	= (1 << 0), /* recovered data error */
	SERR_COMM_RECOVERED	= (1 << 1), /* recovered comm failure */
	SERR_DATA		= (1 << 8), /* unrecovered data error */
	SERR_PERSISTENT		= (1 << 9), /* persistent data/comm error */
	SERR_PROTOCOL		= (1 << 10), /* protocol violation */
	SERR_INTERNAL		= (1 << 11), /* host internal error */
	SERR_PHYRDY_CHG		= (1 << 16), /* PHY RDY changed */
	SERR_PHY_INT_ERR	= (1 << 17), /* PHY internal error */
	SERR_COMM_WAKE		= (1 << 18), /* Comm wake */
	SERR_10B_8B_ERR		= (1 << 19), /* 10b to 8b decode error */
	SERR_DISPARITY		= (1 << 20), /* Disparity */
	SERR_CRC		= (1 << 21), /* CRC error */
	SERR_HANDSHAKE		= (1 << 22), /* Handshake error */
	SERR_LINK_SEQ_ERR	= (1 << 23), /* Link sequence error */
	SERR_TRANS_ST_ERROR	= (1 << 24), /* Transport state trans. error */
	SERR_UNRECOG_FIS	= (1 << 25), /* Unrecognized FIS */
	SERR_DEV_XCHG		= (1 << 26), /* device exchanged */

	/* struct ata_taskfile flags */
	ATA_TFLAG_LBA48		= (1 << 0), /* enable 48-bit LBA and "HOB" */
	ATA_TFLAG_ISADDR	= (1 << 1), /* enable r/w to nsect/lba regs */
	ATA_TFLAG_DEVICE	= (1 << 2), /* enable r/w to device reg */
	ATA_TFLAG_WRITE		= (1 << 3), /* data dir: host->dev==1 (write) */
	ATA_TFLAG_LBA		= (1 << 4), /* enable LBA */
	ATA_TFLAG_FUA		= (1 << 5), /* enable FUA */
	ATA_TFLAG_POLLING	= (1 << 6), /* set nIEN to 1 and use polling */

	/* protocol flags */
	ATA_PROT_FLAG_PIO	= (1 << 0), /* is PIO */
	ATA_PROT_FLAG_DMA	= (1 << 1), /* is DMA */
	ATA_PROT_FLAG_DATA	= ATA_PROT_FLAG_PIO | ATA_PROT_FLAG_DMA,
	ATA_PROT_FLAG_NCQ	= (1 << 2), /* is NCQ */
	ATA_PROT_FLAG_ATAPI	= (1 << 3), /* is ATAPI */
};

enum ata_tf_protocols {
	/* ATA taskfile protocols */
	ATA_PROT_UNKNOWN,	/* unknown/invalid */
	ATA_PROT_NODATA,	/* no data */
	ATA_PROT_PIO,		/* PIO data xfer */
	ATA_PROT_DMA,		/* DMA */
	ATA_PROT_NCQ,		/* NCQ */
	ATAPI_PROT_NODATA,	/* packet command, no data */
	ATAPI_PROT_PIO,		/* packet command, PIO data xfer*/
	ATAPI_PROT_DMA,		/* packet command with special DMA sauce */
};

enum ata_ioctls {
	ATA_IOC_GET_IO32	= 0x309,
	ATA_IOC_SET_IO32	= 0x324,
};

/* core structures */

struct ata_prd {
	__le32			addr;
	__le32			flags_len;
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

	u8			feature;
	u8			nsect;
	u8			lbal;
	u8			lbam;
	u8			lbah;

	u8			device;

	u8			command;	/* IO operation */
};

/*
 * protocol tests
 */
static inline unsigned int ata_prot_flags(u8 prot)
{
	switch (prot) {
	case ATA_PROT_NODATA:
		return 0;
	case ATA_PROT_PIO:
		return ATA_PROT_FLAG_PIO;
	case ATA_PROT_DMA:
		return ATA_PROT_FLAG_DMA;
	case ATA_PROT_NCQ:
		return ATA_PROT_FLAG_DMA | ATA_PROT_FLAG_NCQ;
	case ATAPI_PROT_NODATA:
		return ATA_PROT_FLAG_ATAPI;
	case ATAPI_PROT_PIO:
		return ATA_PROT_FLAG_ATAPI | ATA_PROT_FLAG_PIO;
	case ATAPI_PROT_DMA:
		return ATA_PROT_FLAG_ATAPI | ATA_PROT_FLAG_DMA;
	}
	return 0;
}

static inline int ata_is_atapi(u8 prot)
{
	return ata_prot_flags(prot) & ATA_PROT_FLAG_ATAPI;
}

static inline int ata_is_nodata(u8 prot)
{
	return !(ata_prot_flags(prot) & ATA_PROT_FLAG_DATA);
}

static inline int ata_is_pio(u8 prot)
{
	return ata_prot_flags(prot) & ATA_PROT_FLAG_PIO;
}

static inline int ata_is_dma(u8 prot)
{
	return ata_prot_flags(prot) & ATA_PROT_FLAG_DMA;
}

static inline int ata_is_ncq(u8 prot)
{
	return ata_prot_flags(prot) & ATA_PROT_FLAG_NCQ;
}

static inline int ata_is_data(u8 prot)
{
	return ata_prot_flags(prot) & ATA_PROT_FLAG_DATA;
}

/*
 * id tests
 */
#define ata_id_is_ata(id)	(((id)[ATA_ID_CONFIG] & (1 << 15)) == 0)
#define ata_id_has_lba(id)	((id)[ATA_ID_CAPABILITY] & (1 << 9))
#define ata_id_has_dma(id)	((id)[ATA_ID_CAPABILITY] & (1 << 8))
#define ata_id_has_ncq(id)	((id)[76] & (1 << 8))
#define ata_id_queue_depth(id)	(((id)[ATA_ID_QUEUE_DEPTH] & 0x1f) + 1)
#define ata_id_removeable(id)	((id)[ATA_ID_CONFIG] & (1 << 7))
#define ata_id_has_atapi_AN(id)	\
	( (((id)[76] != 0x0000) && ((id)[76] != 0xffff)) && \
	  ((id)[78] & (1 << 5)) )
#define ata_id_iordy_disable(id) ((id)[ATA_ID_CAPABILITY] & (1 << 10))
#define ata_id_has_iordy(id) ((id)[ATA_ID_CAPABILITY] & (1 << 11))
#define ata_id_u32(id,n)	\
	(((u32) (id)[(n) + 1] << 16) | ((u32) (id)[(n)]))
#define ata_id_u64(id,n)	\
	( ((u64) (id)[(n) + 3] << 48) |	\
	  ((u64) (id)[(n) + 2] << 32) |	\
	  ((u64) (id)[(n) + 1] << 16) |	\
	  ((u64) (id)[(n) + 0]) )

#define ata_id_cdb_intr(id)	(((id)[ATA_ID_CONFIG] & 0x60) == 0x20)

static inline bool ata_id_has_hipm(const u16 *id)
{
	u16 val = id[76];

	if (val == 0 || val == 0xffff)
		return false;

	return val & (1 << 9);
}

static inline bool ata_id_has_dipm(const u16 *id)
{
	u16 val = id[78];

	if (val == 0 || val == 0xffff)
		return false;

	return val & (1 << 3);
}


static inline int ata_id_has_fua(const u16 *id)
{
	if ((id[ATA_ID_CFSSE] & 0xC000) != 0x4000)
		return 0;
	return id[ATA_ID_CFSSE] & (1 << 6);
}

static inline int ata_id_has_flush(const u16 *id)
{
	if ((id[ATA_ID_COMMAND_SET_2] & 0xC000) != 0x4000)
		return 0;
	return id[ATA_ID_COMMAND_SET_2] & (1 << 12);
}

static inline int ata_id_has_flush_ext(const u16 *id)
{
	if ((id[ATA_ID_COMMAND_SET_2] & 0xC000) != 0x4000)
		return 0;
	return id[ATA_ID_COMMAND_SET_2] & (1 << 13);
}

static inline int ata_id_has_lba48(const u16 *id)
{
	if ((id[ATA_ID_COMMAND_SET_2] & 0xC000) != 0x4000)
		return 0;
	if (!ata_id_u64(id, ATA_ID_LBA_CAPACITY_2))
		return 0;
	return id[ATA_ID_COMMAND_SET_2] & (1 << 10);
}

static inline int ata_id_hpa_enabled(const u16 *id)
{
	/* Yes children, word 83 valid bits cover word 82 data */
	if ((id[ATA_ID_COMMAND_SET_2] & 0xC000) != 0x4000)
		return 0;
	/* And 87 covers 85-87 */
	if ((id[ATA_ID_CSF_DEFAULT] & 0xC000) != 0x4000)
		return 0;
	/* Check command sets enabled as well as supported */
	if ((id[ATA_ID_CFS_ENABLE_1] & (1 << 10)) == 0)
		return 0;
	return id[ATA_ID_COMMAND_SET_1] & (1 << 10);
}

static inline int ata_id_has_wcache(const u16 *id)
{
	/* Yes children, word 83 valid bits cover word 82 data */
	if ((id[ATA_ID_COMMAND_SET_2] & 0xC000) != 0x4000)
		return 0;
	return id[ATA_ID_COMMAND_SET_1] & (1 << 5);
}

static inline int ata_id_has_pm(const u16 *id)
{
	if ((id[ATA_ID_COMMAND_SET_2] & 0xC000) != 0x4000)
		return 0;
	return id[ATA_ID_COMMAND_SET_1] & (1 << 3);
}

static inline int ata_id_rahead_enabled(const u16 *id)
{
	if ((id[ATA_ID_CSF_DEFAULT] & 0xC000) != 0x4000)
		return 0;
	return id[ATA_ID_CFS_ENABLE_1] & (1 << 6);
}

static inline int ata_id_wcache_enabled(const u16 *id)
{
	if ((id[ATA_ID_CSF_DEFAULT] & 0xC000) != 0x4000)
		return 0;
	return id[ATA_ID_CFS_ENABLE_1] & (1 << 5);
}

/**
 *	ata_id_major_version	-	get ATA level of drive
 *	@id: Identify data
 *
 *	Caveats:
 *		ATA-1 considers identify optional
 *		ATA-2 introduces mandatory identify
 *		ATA-3 introduces word 80 and accurate reporting
 *
 *	The practical impact of this is that ata_id_major_version cannot
 *	reliably report on drives below ATA3.
 */

static inline unsigned int ata_id_major_version(const u16 *id)
{
	unsigned int mver;

	if (id[ATA_ID_MAJOR_VER] == 0xFFFF)
		return 0;

	for (mver = 14; mver >= 1; mver--)
		if (id[ATA_ID_MAJOR_VER] & (1 << mver))
			break;
	return mver;
}

static inline int ata_id_is_sata(const u16 *id)
{
	return ata_id_major_version(id) >= 5 && id[ATA_ID_HW_CONFIG] == 0;
}

static inline int ata_id_has_tpm(const u16 *id)
{
	/* The TPM bits are only valid on ATA8 */
	if (ata_id_major_version(id) < 8)
		return 0;
	if ((id[48] & 0xC000) != 0x4000)
		return 0;
	return id[48] & (1 << 0);
}

static inline int ata_id_has_dword_io(const u16 *id)
{
	/* ATA 8 reuses this flag for "trusted" computing */
	if (ata_id_major_version(id) > 7)
		return 0;
	if (id[ATA_ID_DWORD_IO] & (1 << 0))
		return 1;
	return 0;
}

static inline int ata_id_has_unload(const u16 *id)
{
	if (ata_id_major_version(id) >= 7 &&
	    (id[ATA_ID_CFSSE] & 0xC000) == 0x4000 &&
	    id[ATA_ID_CFSSE] & (1 << 13))
		return 1;
	return 0;
}

static inline int ata_id_current_chs_valid(const u16 *id)
{
	/* For ATA-1 devices, if the INITIALIZE DEVICE PARAMETERS command
	   has not been issued to the device then the values of
	   id[ATA_ID_CUR_CYLS] to id[ATA_ID_CUR_SECTORS] are vendor specific. */
	return (id[ATA_ID_FIELD_VALID] & 1) && /* Current translation valid */
		id[ATA_ID_CUR_CYLS] &&  /* cylinders in current translation */
		id[ATA_ID_CUR_HEADS] &&  /* heads in current translation */
		id[ATA_ID_CUR_HEADS] <= 16 &&
		id[ATA_ID_CUR_SECTORS];    /* sectors in current translation */
}

static inline int ata_id_is_cfa(const u16 *id)
{
	if (id[ATA_ID_CONFIG] == 0x848A)	/* Standard CF */
		return 1;
	/* Could be CF hiding as standard ATA */
	if (ata_id_major_version(id) >= 3 &&
	    id[ATA_ID_COMMAND_SET_1] != 0xFFFF &&
	   (id[ATA_ID_COMMAND_SET_1] & (1 << 2)))
		return 1;
	return 0;
}

static inline int ata_id_is_ssd(const u16 *id)
{
	return id[ATA_ID_ROT_SPEED] == 0x01;
}

static inline int ata_drive_40wire(const u16 *dev_id)
{
	if (ata_id_is_sata(dev_id))
		return 0;	/* SATA */
	if ((dev_id[ATA_ID_HW_CONFIG] & 0xE000) == 0x6000)
		return 0;	/* 80 wire */
	return 1;
}

static inline int ata_drive_40wire_relaxed(const u16 *dev_id)
{
	if ((dev_id[ATA_ID_HW_CONFIG] & 0x2000) == 0x2000)
		return 0;	/* 80 wire */
	return 1;
}

static inline int atapi_cdb_len(const u16 *dev_id)
{
	u16 tmp = dev_id[ATA_ID_CONFIG] & 0x3;
	switch (tmp) {
	case 0:		return 12;
	case 1:		return 16;
	default:	return -1;
	}
}

static inline int atapi_command_packet_set(const u16 *dev_id)
{
	return (dev_id[ATA_ID_CONFIG] >> 8) & 0x1f;
}

static inline int atapi_id_dmadir(const u16 *dev_id)
{
	return ata_id_major_version(dev_id) >= 7 && (dev_id[62] & 0x8000);
}

static inline int is_multi_taskfile(struct ata_taskfile *tf)
{
	return (tf->command == ATA_CMD_READ_MULTI) ||
	       (tf->command == ATA_CMD_WRITE_MULTI) ||
	       (tf->command == ATA_CMD_READ_MULTI_EXT) ||
	       (tf->command == ATA_CMD_WRITE_MULTI_EXT) ||
	       (tf->command == ATA_CMD_WRITE_MULTI_FUA_EXT);
}

static inline int ata_ok(u8 status)
{
	return ((status & (ATA_BUSY | ATA_DRDY | ATA_DF | ATA_DRQ | ATA_ERR))
			== ATA_DRDY);
}

static inline int lba_28_ok(u64 block, u32 n_block)
{
	/* check the ending block number */
	return ((block + n_block) < ((u64)1 << 28)) && (n_block <= 256);
}

static inline int lba_48_ok(u64 block, u32 n_block)
{
	/* check the ending block number */
	return ((block + n_block - 1) < ((u64)1 << 48)) && (n_block <= 65536);
}

#define sata_pmp_gscr_vendor(gscr)	((gscr)[SATA_PMP_GSCR_PROD_ID] & 0xffff)
#define sata_pmp_gscr_devid(gscr)	((gscr)[SATA_PMP_GSCR_PROD_ID] >> 16)
#define sata_pmp_gscr_rev(gscr)		(((gscr)[SATA_PMP_GSCR_REV] >> 8) & 0xff)
#define sata_pmp_gscr_ports(gscr)	((gscr)[SATA_PMP_GSCR_PORT_INFO] & 0xf)

#endif /* __LINUX_ATA_H__ */
