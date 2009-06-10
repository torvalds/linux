/*
 *  include/linux/mg_disk.c
 *
 *  Support for the mGine m[g]flash IO mode.
 *  Based on legacy hd.c
 *
 * (c) 2008 mGine Co.,LTD
 * (c) 2008 unsik Kim <donari75@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#ifndef __MG_DISK_H__
#define __MG_DISK_H__

#include <linux/blkdev.h>
#include <linux/ata.h>

/* name for block device */
#define MG_DISK_NAME "mgd"
/* name for platform device */
#define MG_DEV_NAME "mg_disk"

#define MG_DISK_MAJ 0
#define MG_DISK_MAX_PART 16
#define MG_SECTOR_SIZE 512
#define MG_MAX_SECTS 256

/* Register offsets */
#define MG_BUFF_OFFSET			0x8000
#define MG_STORAGE_BUFFER_SIZE		0x200
#define MG_REG_OFFSET			0xC000
#define MG_REG_FEATURE			(MG_REG_OFFSET + 2)	/* write case */
#define MG_REG_ERROR			(MG_REG_OFFSET + 2)	/* read case */
#define MG_REG_SECT_CNT			(MG_REG_OFFSET + 4)
#define MG_REG_SECT_NUM			(MG_REG_OFFSET + 6)
#define MG_REG_CYL_LOW			(MG_REG_OFFSET + 8)
#define MG_REG_CYL_HIGH			(MG_REG_OFFSET + 0xA)
#define MG_REG_DRV_HEAD			(MG_REG_OFFSET + 0xC)
#define MG_REG_COMMAND			(MG_REG_OFFSET + 0xE)	/* write case */
#define MG_REG_STATUS			(MG_REG_OFFSET + 0xE)	/* read  case */
#define MG_REG_DRV_CTRL			(MG_REG_OFFSET + 0x10)
#define MG_REG_BURST_CTRL		(MG_REG_OFFSET + 0x12)

/* "Drive Select/Head Register" bit values */
#define MG_REG_HEAD_MUST_BE_ON		0xA0 /* These 2 bits are always on */
#define MG_REG_HEAD_DRIVE_MASTER	(0x00 | MG_REG_HEAD_MUST_BE_ON)
#define MG_REG_HEAD_DRIVE_SLAVE		(0x10 | MG_REG_HEAD_MUST_BE_ON)
#define MG_REG_HEAD_LBA_MODE		(0x40 | MG_REG_HEAD_MUST_BE_ON)


/* "Device Control Register" bit values */
#define MG_REG_CTRL_INTR_ENABLE			0x0
#define MG_REG_CTRL_INTR_DISABLE		(0x1<<1)
#define MG_REG_CTRL_RESET			(0x1<<2)
#define MG_REG_CTRL_INTR_POLA_ACTIVE_HIGH	0x0
#define MG_REG_CTRL_INTR_POLA_ACTIVE_LOW	(0x1<<4)
#define MG_REG_CTRL_DPD_POLA_ACTIVE_LOW		0x0
#define MG_REG_CTRL_DPD_POLA_ACTIVE_HIGH	(0x1<<5)
#define MG_REG_CTRL_DPD_DISABLE			0x0
#define MG_REG_CTRL_DPD_ENABLE			(0x1<<6)

/* Status register bit */
/* error bit in status register */
#define MG_REG_STATUS_BIT_ERROR			0x01
/* corrected error in status register */
#define MG_REG_STATUS_BIT_CORRECTED_ERROR	0x04
/* data request bit in status register */
#define MG_REG_STATUS_BIT_DATA_REQ		0x08
/* DSC - Drive Seek Complete */
#define MG_REG_STATUS_BIT_SEEK_DONE		0x10
/* DWF - Drive Write Fault */
#define MG_REG_STATUS_BIT_WRITE_FAULT		0x20
#define MG_REG_STATUS_BIT_READY			0x40
#define MG_REG_STATUS_BIT_BUSY			0x80

/* handy status */
#define MG_STAT_READY	(MG_REG_STATUS_BIT_READY | MG_REG_STATUS_BIT_SEEK_DONE)
#define MG_READY_OK(s)	(((s) & (MG_STAT_READY | \
				(MG_REG_STATUS_BIT_BUSY | \
				 MG_REG_STATUS_BIT_WRITE_FAULT | \
				 MG_REG_STATUS_BIT_ERROR))) == MG_STAT_READY)

/* Error register */
#define MG_REG_ERR_AMNF		0x01
#define MG_REG_ERR_ABRT		0x04
#define MG_REG_ERR_IDNF		0x10
#define MG_REG_ERR_UNC		0x40
#define MG_REG_ERR_BBK		0x80

/* error code for others */
#define MG_ERR_NONE		0
#define MG_ERR_TIMEOUT		0x100
#define MG_ERR_INIT_STAT	0x101
#define MG_ERR_TRANSLATION	0x102
#define MG_ERR_CTRL_RST		0x103
#define MG_ERR_INV_STAT		0x104
#define MG_ERR_RSTOUT		0x105

#define MG_MAX_ERRORS	6	/* Max read/write errors */

/* command */
#define MG_CMD_RD 0x20
#define MG_CMD_WR 0x30
#define MG_CMD_SLEEP 0x99
#define MG_CMD_WAKEUP 0xC3
#define MG_CMD_ID 0xEC
#define MG_CMD_WR_CONF 0x3C
#define MG_CMD_RD_CONF 0x40

/* operation mode */
#define MG_OP_CASCADE (1 << 0)
#define MG_OP_CASCADE_SYNC_RD (1 << 1)
#define MG_OP_CASCADE_SYNC_WR (1 << 2)
#define MG_OP_INTERLEAVE (1 << 3)

/* synchronous */
#define MG_BURST_LAT_4 (3 << 4)
#define MG_BURST_LAT_5 (4 << 4)
#define MG_BURST_LAT_6 (5 << 4)
#define MG_BURST_LAT_7 (6 << 4)
#define MG_BURST_LAT_8 (7 << 4)
#define MG_BURST_LEN_4 (1 << 1)
#define MG_BURST_LEN_8 (2 << 1)
#define MG_BURST_LEN_16 (3 << 1)
#define MG_BURST_LEN_32 (4 << 1)
#define MG_BURST_LEN_CONT (0 << 1)

/* timeout value (unit: ms) */
#define MG_TMAX_CONF_TO_CMD	1
#define MG_TMAX_WAIT_RD_DRQ	10
#define MG_TMAX_WAIT_WR_DRQ	500
#define MG_TMAX_RST_TO_BUSY	10
#define MG_TMAX_HDRST_TO_RDY	500
#define MG_TMAX_SWRST_TO_RDY	500
#define MG_TMAX_RSTOUT		3000

/* device attribution */
/* use mflash as boot device */
#define MG_BOOT_DEV		(1 << 0)
/* use mflash as storage device */
#define MG_STORAGE_DEV		(1 << 1)
/* same as MG_STORAGE_DEV, but bootloader already done reset sequence */
#define MG_STORAGE_DEV_SKIP_RST	(1 << 2)

#define MG_DEV_MASK (MG_BOOT_DEV | MG_STORAGE_DEV | MG_STORAGE_DEV_SKIP_RST)

/* names of GPIO resource */
#define MG_RST_PIN	"mg_rst"
/* except MG_BOOT_DEV, reset-out pin should be assigned */
#define MG_RSTOUT_PIN	"mg_rstout"

/* private driver data */
struct mg_drv_data {
	/* disk resource */
	u32 use_polling;

	/* device attribution */
	u32 dev_attr;

	/* internally used */
	struct mg_host *host;
};

/* main structure for mflash driver */
struct mg_host {
	struct device *dev;

	struct request_queue *breq;
	spinlock_t lock;
	struct gendisk *gd;

	struct timer_list timer;
	void (*mg_do_intr) (struct mg_host *);

	u16 id[ATA_ID_WORDS];

	u16 cyls;
	u16 heads;
	u16 sectors;
	u32 n_sectors;
	u32 nres_sectors;

	void __iomem *dev_base;
	unsigned int irq;
	unsigned int rst;
	unsigned int rstout;

	u32 major;
	u32 error;
};

/*
 * Debugging macro and defines
 */
#undef DO_MG_DEBUG
#ifdef DO_MG_DEBUG
#  define MG_DBG(fmt, args...) \
	printk(KERN_DEBUG "%s:%d "fmt, __func__, __LINE__, ##args)
#else /* CONFIG_MG_DEBUG */
#  define MG_DBG(fmt, args...) do { } while (0)
#endif /* CONFIG_MG_DEBUG */

#endif
