/*
 *  linux/include/linux/mtd/onenand.h
 *
 *  Copyright © 2005-2009 Samsung Electronics
 *  Kyungmin Park <kyungmin.park@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_MTD_ONENAND_H
#define __LINUX_MTD_ONENAND_H

#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/mtd/flashchip.h>
#include <linux/mtd/onenand_regs.h>
#include <linux/mtd/bbm.h>

#define MAX_DIES		2
#define MAX_BUFFERRAM		2

/* Scan and identify a OneNAND device */
extern int onenand_scan(struct mtd_info *mtd, int max_chips);
/* Free resources held by the OneNAND device */
extern void onenand_release(struct mtd_info *mtd);

/**
 * struct onenand_bufferram - OneNAND BufferRAM Data
 * @blockpage:		block & page address in BufferRAM
 */
struct onenand_bufferram {
	int	blockpage;
};

/**
 * struct onenand_chip - OneNAND Private Flash Chip Data
 * @base:		[BOARDSPECIFIC] address to access OneNAND
 * @dies:		[INTERN][FLEX-ONENAND] number of dies on chip
 * @boundary:		[INTERN][FLEX-ONENAND] Boundary of the dies
 * @diesize:		[INTERN][FLEX-ONENAND] Size of the dies
 * @chipsize:		[INTERN] the size of one chip for multichip arrays
 *			FIXME For Flex-OneNAND, chipsize holds maximum possible
 *			device size ie when all blocks are considered MLC
 * @device_id:		[INTERN] device ID
 * @density_mask:	chip density, used for DDP devices
 * @verstion_id:	[INTERN] version ID
 * @options:		[BOARDSPECIFIC] various chip options. They can
 *			partly be set to inform onenand_scan about
 * @erase_shift:	[INTERN] number of address bits in a block
 * @page_shift:		[INTERN] number of address bits in a page
 * @page_mask:		[INTERN] a page per block mask
 * @writesize:		[INTERN] a real page size
 * @bufferram_index:	[INTERN] BufferRAM index
 * @bufferram:		[INTERN] BufferRAM info
 * @readw:		[REPLACEABLE] hardware specific function for read short
 * @writew:		[REPLACEABLE] hardware specific function for write short
 * @command:		[REPLACEABLE] hardware specific function for writing
 *			commands to the chip
 * @wait:		[REPLACEABLE] hardware specific function for wait on ready
 * @bbt_wait:		[REPLACEABLE] hardware specific function for bbt wait on ready
 * @unlock_all:		[REPLACEABLE] hardware specific function for unlock all
 * @read_bufferram:	[REPLACEABLE] hardware specific function for BufferRAM Area
 * @write_bufferram:	[REPLACEABLE] hardware specific function for BufferRAM Area
 * @read_word:		[REPLACEABLE] hardware specific function for read
 *			register of OneNAND
 * @write_word:		[REPLACEABLE] hardware specific function for write
 *			register of OneNAND
 * @mmcontrol:		sync burst read function
 * @chip_probe:		[REPLACEABLE] hardware specific function for chip probe
 * @block_markbad:	function to mark a block as bad
 * @scan_bbt:		[REPLACEALBE] hardware specific function for scanning
 *			Bad block Table
 * @chip_lock:		[INTERN] spinlock used to protect access to this
 *			structure and the chip
 * @wq:			[INTERN] wait queue to sleep on if a OneNAND
 *			operation is in progress
 * @state:		[INTERN] the current state of the OneNAND device
 * @page_buf:		[INTERN] page main data buffer
 * @oob_buf:		[INTERN] page oob data buffer
 * @subpagesize:	[INTERN] holds the subpagesize
 * @ecclayout:		[REPLACEABLE] the default ecc placement scheme
 * @bbm:		[REPLACEABLE] pointer to Bad Block Management
 * @priv:		[OPTIONAL] pointer to private chip date
 */
struct onenand_chip {
	void __iomem		*base;
	unsigned		dies;
	unsigned		boundary[MAX_DIES];
	loff_t			diesize[MAX_DIES];
	unsigned int		chipsize;
	unsigned int		device_id;
	unsigned int		version_id;
	unsigned int		technology;
	unsigned int		density_mask;
	unsigned int		options;

	unsigned int		erase_shift;
	unsigned int		page_shift;
	unsigned int		page_mask;
	unsigned int		writesize;

	unsigned int		bufferram_index;
	struct onenand_bufferram	bufferram[MAX_BUFFERRAM];

	int (*command)(struct mtd_info *mtd, int cmd, loff_t address, size_t len);
	int (*wait)(struct mtd_info *mtd, int state);
	int (*bbt_wait)(struct mtd_info *mtd, int state);
	void (*unlock_all)(struct mtd_info *mtd);
	int (*read_bufferram)(struct mtd_info *mtd, int area,
			unsigned char *buffer, int offset, size_t count);
	int (*write_bufferram)(struct mtd_info *mtd, int area,
			const unsigned char *buffer, int offset, size_t count);
	unsigned short (*read_word)(void __iomem *addr);
	void (*write_word)(unsigned short value, void __iomem *addr);
	void (*mmcontrol)(struct mtd_info *mtd, int sync_read);
	int (*chip_probe)(struct mtd_info *mtd);
	int (*block_markbad)(struct mtd_info *mtd, loff_t ofs);
	int (*scan_bbt)(struct mtd_info *mtd);

	struct completion	complete;
	int			irq;

	spinlock_t		chip_lock;
	wait_queue_head_t	wq;
	flstate_t		state;
	unsigned char		*page_buf;
	unsigned char		*oob_buf;
#ifdef CONFIG_MTD_ONENAND_VERIFY_WRITE
	unsigned char		*verify_buf;
#endif

	int			subpagesize;
	struct nand_ecclayout	*ecclayout;

	void			*bbm;

	void			*priv;
};

/*
 * Helper macros
 */
#define ONENAND_PAGES_PER_BLOCK        (1<<6)

#define ONENAND_CURRENT_BUFFERRAM(this)		(this->bufferram_index)
#define ONENAND_NEXT_BUFFERRAM(this)		(this->bufferram_index ^ 1)
#define ONENAND_SET_NEXT_BUFFERRAM(this)	(this->bufferram_index ^= 1)
#define ONENAND_SET_PREV_BUFFERRAM(this)	(this->bufferram_index ^= 1)
#define ONENAND_SET_BUFFERRAM0(this)		(this->bufferram_index = 0)
#define ONENAND_SET_BUFFERRAM1(this)		(this->bufferram_index = 1)

#define FLEXONENAND(this)						\
	(this->device_id & DEVICE_IS_FLEXONENAND)
#define ONENAND_GET_SYS_CFG1(this)					\
	(this->read_word(this->base + ONENAND_REG_SYS_CFG1))
#define ONENAND_SET_SYS_CFG1(v, this)					\
	(this->write_word(v, this->base + ONENAND_REG_SYS_CFG1))

#define ONENAND_IS_DDP(this)						\
	(this->device_id & ONENAND_DEVICE_IS_DDP)

#define ONENAND_IS_MLC(this)						\
	(this->technology & ONENAND_TECHNOLOGY_IS_MLC)

#ifdef CONFIG_MTD_ONENAND_2X_PROGRAM
#define ONENAND_IS_2PLANE(this)						\
	(this->options & ONENAND_HAS_2PLANE)
#else
#define ONENAND_IS_2PLANE(this)			(0)
#endif

/* Check byte access in OneNAND */
#define ONENAND_CHECK_BYTE_ACCESS(addr)		(addr & 0x1)

/*
 * Options bits
 */
#define ONENAND_HAS_CONT_LOCK		(0x0001)
#define ONENAND_HAS_UNLOCK_ALL		(0x0002)
#define ONENAND_HAS_2PLANE		(0x0004)
#define ONENAND_HAS_4KB_PAGE		(0x0008)
#define ONENAND_SKIP_UNLOCK_CHECK	(0x0100)
#define ONENAND_PAGEBUF_ALLOC		(0x1000)
#define ONENAND_OOBBUF_ALLOC		(0x2000)

#define ONENAND_IS_4KB_PAGE(this)					\
	(this->options & ONENAND_HAS_4KB_PAGE)

/*
 * OneNAND Flash Manufacturer ID Codes
 */
#define ONENAND_MFR_SAMSUNG	0xec
#define ONENAND_MFR_NUMONYX	0x20

/**
 * struct onenand_manufacturers - NAND Flash Manufacturer ID Structure
 * @name:	Manufacturer name
 * @id:		manufacturer ID code of device.
*/
struct onenand_manufacturers {
        int id;
        char *name;
};

int onenand_bbt_read_oob(struct mtd_info *mtd, loff_t from,
			 struct mtd_oob_ops *ops);
unsigned onenand_block(struct onenand_chip *this, loff_t addr);
loff_t onenand_addr(struct onenand_chip *this, int block);
int flexonenand_region(struct mtd_info *mtd, loff_t addr);

struct mtd_partition;

struct onenand_platform_data {
	void		(*mmcontrol)(struct mtd_info *mtd, int sync_read);
	int		(*read_bufferram)(struct mtd_info *mtd, int area,
			unsigned char *buffer, int offset, size_t count);
	struct mtd_partition *parts;
	unsigned int	nr_parts;
};

#endif	/* __LINUX_MTD_ONENAND_H */
