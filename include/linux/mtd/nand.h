/*
 *  linux/include/linux/mtd/nand.h
 *
 *  Copyright (c) 2000 David Woodhouse <dwmw2@infradead.org>
 *                     Steven J. Hill <sjhill@realitydiluted.com>
 *		       Thomas Gleixner <tglx@linutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Info:
 *	Contains standard defines and IDs for NAND flash devices
 *
 * Changelog:
 *	See git changelog.
 */
#ifndef __LINUX_MTD_NAND_H
#define __LINUX_MTD_NAND_H

#include <linux/wait.h>
#include <linux/spinlock.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/flashchip.h>
#include <linux/mtd/bbm.h>

struct mtd_info;
/* Scan and identify a NAND device */
extern int nand_scan (struct mtd_info *mtd, int max_chips);
/* Separate phases of nand_scan(), allowing board driver to intervene
 * and override command or ECC setup according to flash type */
extern int nand_scan_ident(struct mtd_info *mtd, int max_chips);
extern int nand_scan_tail(struct mtd_info *mtd);

/* Free resources held by the NAND device */
extern void nand_release (struct mtd_info *mtd);

/* Internal helper for board drivers which need to override command function */
extern void nand_wait_ready(struct mtd_info *mtd);

/* The maximum number of NAND chips in an array */
#define NAND_MAX_CHIPS		8

/* This constant declares the max. oobsize / page, which
 * is supported now. If you add a chip with bigger oobsize/page
 * adjust this accordingly.
 */
#define NAND_MAX_OOBSIZE	128
#define NAND_MAX_PAGESIZE	4096

/*
 * Constants for hardware specific CLE/ALE/NCE function
 *
 * These are bits which can be or'ed to set/clear multiple
 * bits in one go.
 */
/* Select the chip by setting nCE to low */
#define NAND_NCE		0x01
/* Select the command latch by setting CLE to high */
#define NAND_CLE		0x02
/* Select the address latch by setting ALE to high */
#define NAND_ALE		0x04

#define NAND_CTRL_CLE		(NAND_NCE | NAND_CLE)
#define NAND_CTRL_ALE		(NAND_NCE | NAND_ALE)
#define NAND_CTRL_CHANGE	0x80

/*
 * Standard NAND flash commands
 */
#define NAND_CMD_READ0		0
#define NAND_CMD_READ1		1
#define NAND_CMD_RNDOUT		5
#define NAND_CMD_PAGEPROG	0x10
#define NAND_CMD_READOOB	0x50
#define NAND_CMD_ERASE1		0x60
#define NAND_CMD_STATUS		0x70
#define NAND_CMD_STATUS_MULTI	0x71
#define NAND_CMD_SEQIN		0x80
#define NAND_CMD_RNDIN		0x85
#define NAND_CMD_READID		0x90
#define NAND_CMD_ERASE2		0xd0
#define NAND_CMD_RESET		0xff

/* Extended commands for large page devices */
#define NAND_CMD_READSTART	0x30
#define NAND_CMD_RNDOUTSTART	0xE0
#define NAND_CMD_CACHEDPROG	0x15

/* Extended commands for AG-AND device */
/*
 * Note: the command for NAND_CMD_DEPLETE1 is really 0x00 but
 *       there is no way to distinguish that from NAND_CMD_READ0
 *       until the remaining sequence of commands has been completed
 *       so add a high order bit and mask it off in the command.
 */
#define NAND_CMD_DEPLETE1	0x100
#define NAND_CMD_DEPLETE2	0x38
#define NAND_CMD_STATUS_MULTI	0x71
#define NAND_CMD_STATUS_ERROR	0x72
/* multi-bank error status (banks 0-3) */
#define NAND_CMD_STATUS_ERROR0	0x73
#define NAND_CMD_STATUS_ERROR1	0x74
#define NAND_CMD_STATUS_ERROR2	0x75
#define NAND_CMD_STATUS_ERROR3	0x76
#define NAND_CMD_STATUS_RESET	0x7f
#define NAND_CMD_STATUS_CLEAR	0xff

#define NAND_CMD_NONE		-1

/* Status bits */
#define NAND_STATUS_FAIL	0x01
#define NAND_STATUS_FAIL_N1	0x02
#define NAND_STATUS_TRUE_READY	0x20
#define NAND_STATUS_READY	0x40
#define NAND_STATUS_WP		0x80

/*
 * Constants for ECC_MODES
 */
typedef enum {
	NAND_ECC_NONE,
	NAND_ECC_SOFT,
	NAND_ECC_HW,
	NAND_ECC_HW_SYNDROME,
	NAND_ECC_HW_OOB_FIRST,
} nand_ecc_modes_t;

/*
 * Constants for Hardware ECC
 */
/* Reset Hardware ECC for read */
#define NAND_ECC_READ		0
/* Reset Hardware ECC for write */
#define NAND_ECC_WRITE		1
/* Enable Hardware ECC before syndrom is read back from flash */
#define NAND_ECC_READSYN	2

/* Bit mask for flags passed to do_nand_read_ecc */
#define NAND_GET_DEVICE		0x80


/* Option constants for bizarre disfunctionality and real
*  features
*/
/* Chip can not auto increment pages */
#define NAND_NO_AUTOINCR	0x00000001
/* Buswitdh is 16 bit */
#define NAND_BUSWIDTH_16	0x00000002
/* Device supports partial programming without padding */
#define NAND_NO_PADDING		0x00000004
/* Chip has cache program function */
#define NAND_CACHEPRG		0x00000008
/* Chip has copy back function */
#define NAND_COPYBACK		0x00000010
/* AND Chip which has 4 banks and a confusing page / block
 * assignment. See Renesas datasheet for further information */
#define NAND_IS_AND		0x00000020
/* Chip has a array of 4 pages which can be read without
 * additional ready /busy waits */
#define NAND_4PAGE_ARRAY	0x00000040
/* Chip requires that BBT is periodically rewritten to prevent
 * bits from adjacent blocks from 'leaking' in altering data.
 * This happens with the Renesas AG-AND chips, possibly others.  */
#define BBT_AUTO_REFRESH	0x00000080
/* Chip does not require ready check on read. True
 * for all large page devices, as they do not support
 * autoincrement.*/
#define NAND_NO_READRDY		0x00000100
/* Chip does not allow subpage writes */
#define NAND_NO_SUBPAGE_WRITE	0x00000200

/* Options valid for Samsung large page devices */
#define NAND_SAMSUNG_LP_OPTIONS \
	(NAND_NO_PADDING | NAND_CACHEPRG | NAND_COPYBACK)

/* Macros to identify the above */
#define NAND_CANAUTOINCR(chip) (!(chip->options & NAND_NO_AUTOINCR))
#define NAND_MUST_PAD(chip) (!(chip->options & NAND_NO_PADDING))
#define NAND_HAS_CACHEPROG(chip) ((chip->options & NAND_CACHEPRG))
#define NAND_HAS_COPYBACK(chip) ((chip->options & NAND_COPYBACK))
/* Large page NAND with SOFT_ECC should support subpage reads */
#define NAND_SUBPAGE_READ(chip) ((chip->ecc.mode == NAND_ECC_SOFT) \
					&& (chip->page_shift > 9))

/* Mask to zero out the chip options, which come from the id table */
#define NAND_CHIPOPTIONS_MSK	(0x0000ffff & ~NAND_NO_AUTOINCR)

/* Non chip related options */
/* Use a flash based bad block table. This option is passed to the
 * default bad block table function. */
#define NAND_USE_FLASH_BBT	0x00010000
/* This option skips the bbt scan during initialization. */
#define NAND_SKIP_BBTSCAN	0x00020000
/* This option is defined if the board driver allocates its own buffers
   (e.g. because it needs them DMA-coherent */
#define NAND_OWN_BUFFERS	0x00040000
/* Chip may not exist, so silence any errors in scan */
#define NAND_SCAN_SILENT_NODEV	0x00080000

/* Options set by nand scan */
/* Nand scan has allocated controller struct */
#define NAND_CONTROLLER_ALLOC	0x80000000

/* Cell info constants */
#define NAND_CI_CHIPNR_MSK	0x03
#define NAND_CI_CELLTYPE_MSK	0x0C

/* Keep gcc happy */
struct nand_chip;

/**
 * struct nand_hw_control - Control structure for hardware controller (e.g ECC generator) shared among independent devices
 * @lock:               protection lock
 * @active:		the mtd device which holds the controller currently
 * @wq:			wait queue to sleep on if a NAND operation is in progress
 *                      used instead of the per chip wait queue when a hw controller is available
 */
struct nand_hw_control {
	spinlock_t	 lock;
	struct nand_chip *active;
	wait_queue_head_t wq;
};

/**
 * struct nand_ecc_ctrl - Control structure for ecc
 * @mode:	ecc mode
 * @steps:	number of ecc steps per page
 * @size:	data bytes per ecc step
 * @bytes:	ecc bytes per step
 * @total:	total number of ecc bytes per page
 * @prepad:	padding information for syndrome based ecc generators
 * @postpad:	padding information for syndrome based ecc generators
 * @layout:	ECC layout control struct pointer
 * @hwctl:	function to control hardware ecc generator. Must only
 *		be provided if an hardware ECC is available
 * @calculate:	function for ecc calculation or readback from ecc hardware
 * @correct:	function for ecc correction, matching to ecc generator (sw/hw)
 * @read_page_raw:	function to read a raw page without ECC
 * @write_page_raw:	function to write a raw page without ECC
 * @read_page:	function to read a page according to the ecc generator requirements
 * @read_subpage:	function to read parts of the page covered by ECC.
 * @write_page:	function to write a page according to the ecc generator requirements
 * @read_oob:	function to read chip OOB data
 * @write_oob:	function to write chip OOB data
 */
struct nand_ecc_ctrl {
	nand_ecc_modes_t	mode;
	int			steps;
	int			size;
	int			bytes;
	int			total;
	int			prepad;
	int			postpad;
	struct nand_ecclayout	*layout;
	void			(*hwctl)(struct mtd_info *mtd, int mode);
	int			(*calculate)(struct mtd_info *mtd,
					     const uint8_t *dat,
					     uint8_t *ecc_code);
	int			(*correct)(struct mtd_info *mtd, uint8_t *dat,
					   uint8_t *read_ecc,
					   uint8_t *calc_ecc);
	int			(*read_page_raw)(struct mtd_info *mtd,
						 struct nand_chip *chip,
						 uint8_t *buf, int page);
	void			(*write_page_raw)(struct mtd_info *mtd,
						  struct nand_chip *chip,
						  const uint8_t *buf);
	int			(*read_page)(struct mtd_info *mtd,
					     struct nand_chip *chip,
					     uint8_t *buf, int page);
	int			(*read_subpage)(struct mtd_info *mtd,
					     struct nand_chip *chip,
					     uint32_t offs, uint32_t len,
					     uint8_t *buf);
	void			(*write_page)(struct mtd_info *mtd,
					      struct nand_chip *chip,
					      const uint8_t *buf);
	int			(*read_oob)(struct mtd_info *mtd,
					    struct nand_chip *chip,
					    int page,
					    int sndcmd);
	int			(*write_oob)(struct mtd_info *mtd,
					     struct nand_chip *chip,
					     int page);
};

/**
 * struct nand_buffers - buffer structure for read/write
 * @ecccalc:	buffer for calculated ecc
 * @ecccode:	buffer for ecc read from flash
 * @databuf:	buffer for data - dynamically sized
 *
 * Do not change the order of buffers. databuf and oobrbuf must be in
 * consecutive order.
 */
struct nand_buffers {
	uint8_t	ecccalc[NAND_MAX_OOBSIZE];
	uint8_t	ecccode[NAND_MAX_OOBSIZE];
	uint8_t databuf[NAND_MAX_PAGESIZE + NAND_MAX_OOBSIZE];
};

/**
 * struct nand_chip - NAND Private Flash Chip Data
 * @IO_ADDR_R:		[BOARDSPECIFIC] address to read the 8 I/O lines of the flash device
 * @IO_ADDR_W:		[BOARDSPECIFIC] address to write the 8 I/O lines of the flash device
 * @read_byte:		[REPLACEABLE] read one byte from the chip
 * @read_word:		[REPLACEABLE] read one word from the chip
 * @write_buf:		[REPLACEABLE] write data from the buffer to the chip
 * @read_buf:		[REPLACEABLE] read data from the chip into the buffer
 * @verify_buf:		[REPLACEABLE] verify buffer contents against the chip data
 * @select_chip:	[REPLACEABLE] select chip nr
 * @block_bad:		[REPLACEABLE] check, if the block is bad
 * @block_markbad:	[REPLACEABLE] mark the block bad
 * @cmd_ctrl:		[BOARDSPECIFIC] hardwarespecific funtion for controlling
 *			ALE/CLE/nCE. Also used to write command and address
 * @dev_ready:		[BOARDSPECIFIC] hardwarespecific function for accesing device ready/busy line
 *			If set to NULL no access to ready/busy is available and the ready/busy information
 *			is read from the chip status register
 * @cmdfunc:		[REPLACEABLE] hardwarespecific function for writing commands to the chip
 * @waitfunc:		[REPLACEABLE] hardwarespecific function for wait on ready
 * @ecc:		[BOARDSPECIFIC] ecc control ctructure
 * @buffers:		buffer structure for read/write
 * @hwcontrol:		platform-specific hardware control structure
 * @ops:		oob operation operands
 * @erase_cmd:		[INTERN] erase command write function, selectable due to AND support
 * @scan_bbt:		[REPLACEABLE] function to scan bad block table
 * @chip_delay:		[BOARDSPECIFIC] chip dependent delay for transfering data from array to read regs (tR)
 * @state:		[INTERN] the current state of the NAND device
 * @oob_poi:		poison value buffer
 * @page_shift:		[INTERN] number of address bits in a page (column address bits)
 * @phys_erase_shift:	[INTERN] number of address bits in a physical eraseblock
 * @bbt_erase_shift:	[INTERN] number of address bits in a bbt entry
 * @chip_shift:		[INTERN] number of address bits in one chip
 * @options:		[BOARDSPECIFIC] various chip options. They can partly be set to inform nand_scan about
 *			special functionality. See the defines for further explanation
 * @badblockpos:	[INTERN] position of the bad block marker in the oob area
 * @cellinfo:		[INTERN] MLC/multichip data from chip ident
 * @numchips:		[INTERN] number of physical chips
 * @chipsize:		[INTERN] the size of one chip for multichip arrays
 * @pagemask:		[INTERN] page number mask = number of (pages / chip) - 1
 * @pagebuf:		[INTERN] holds the pagenumber which is currently in data_buf
 * @subpagesize:	[INTERN] holds the subpagesize
 * @ecclayout:		[REPLACEABLE] the default ecc placement scheme
 * @bbt:		[INTERN] bad block table pointer
 * @bbt_td:		[REPLACEABLE] bad block table descriptor for flash lookup
 * @bbt_md:		[REPLACEABLE] bad block table mirror descriptor
 * @badblock_pattern:	[REPLACEABLE] bad block scan pattern used for initial bad block scan
 * @controller:		[REPLACEABLE] a pointer to a hardware controller structure
 *			which is shared among multiple independend devices
 * @priv:		[OPTIONAL] pointer to private chip date
 * @errstat:		[OPTIONAL] hardware specific function to perform additional error status checks
 *			(determine if errors are correctable)
 * @write_page:		[REPLACEABLE] High-level page write function
 */

struct nand_chip {
	void  __iomem	*IO_ADDR_R;
	void  __iomem	*IO_ADDR_W;

	uint8_t		(*read_byte)(struct mtd_info *mtd);
	u16		(*read_word)(struct mtd_info *mtd);
	void		(*write_buf)(struct mtd_info *mtd, const uint8_t *buf, int len);
	void		(*read_buf)(struct mtd_info *mtd, uint8_t *buf, int len);
	int		(*verify_buf)(struct mtd_info *mtd, const uint8_t *buf, int len);
	void		(*select_chip)(struct mtd_info *mtd, int chip);
	int		(*block_bad)(struct mtd_info *mtd, loff_t ofs, int getchip);
	int		(*block_markbad)(struct mtd_info *mtd, loff_t ofs);
	void		(*cmd_ctrl)(struct mtd_info *mtd, int dat,
				    unsigned int ctrl);
	int		(*dev_ready)(struct mtd_info *mtd);
	void		(*cmdfunc)(struct mtd_info *mtd, unsigned command, int column, int page_addr);
	int		(*waitfunc)(struct mtd_info *mtd, struct nand_chip *this);
	void		(*erase_cmd)(struct mtd_info *mtd, int page);
	int		(*scan_bbt)(struct mtd_info *mtd);
	int		(*errstat)(struct mtd_info *mtd, struct nand_chip *this, int state, int status, int page);
	int		(*write_page)(struct mtd_info *mtd, struct nand_chip *chip,
				      const uint8_t *buf, int page, int cached, int raw);

	int		chip_delay;
	unsigned int	options;

	int		page_shift;
	int		phys_erase_shift;
	int		bbt_erase_shift;
	int		chip_shift;
	int		numchips;
	uint64_t	chipsize;
	int		pagemask;
	int		pagebuf;
	int		subpagesize;
	uint8_t		cellinfo;
	int		badblockpos;

	flstate_t	state;

	uint8_t		*oob_poi;
	struct nand_hw_control  *controller;
	struct nand_ecclayout	*ecclayout;

	struct nand_ecc_ctrl ecc;
	struct nand_buffers *buffers;
	struct nand_hw_control hwcontrol;

	struct mtd_oob_ops ops;

	uint8_t		*bbt;
	struct nand_bbt_descr	*bbt_td;
	struct nand_bbt_descr	*bbt_md;

	struct nand_bbt_descr	*badblock_pattern;

	void		*priv;
};

/*
 * NAND Flash Manufacturer ID Codes
 */
#define NAND_MFR_TOSHIBA	0x98
#define NAND_MFR_SAMSUNG	0xec
#define NAND_MFR_FUJITSU	0x04
#define NAND_MFR_NATIONAL	0x8f
#define NAND_MFR_RENESAS	0x07
#define NAND_MFR_STMICRO	0x20
#define NAND_MFR_HYNIX		0xad
#define NAND_MFR_MICRON		0x2c
#define NAND_MFR_AMD		0x01

/**
 * struct nand_flash_dev - NAND Flash Device ID Structure
 * @name:	Identify the device type
 * @id:		device ID code
 * @pagesize:	Pagesize in bytes. Either 256 or 512 or 0
 *		If the pagesize is 0, then the real pagesize
 *		and the eraseize are determined from the
 *		extended id bytes in the chip
 * @erasesize:	Size of an erase block in the flash device.
 * @chipsize:	Total chipsize in Mega Bytes
 * @options:	Bitfield to store chip relevant options
 */
struct nand_flash_dev {
	char *name;
	int id;
	unsigned long pagesize;
	unsigned long chipsize;
	unsigned long erasesize;
	unsigned long options;
};

/**
 * struct nand_manufacturers - NAND Flash Manufacturer ID Structure
 * @name:	Manufacturer name
 * @id:		manufacturer ID code of device.
*/
struct nand_manufacturers {
	int id;
	char * name;
};

extern struct nand_flash_dev nand_flash_ids[];
extern struct nand_manufacturers nand_manuf_ids[];

extern int nand_scan_bbt(struct mtd_info *mtd, struct nand_bbt_descr *bd);
extern int nand_update_bbt(struct mtd_info *mtd, loff_t offs);
extern int nand_default_bbt(struct mtd_info *mtd);
extern int nand_isbad_bbt(struct mtd_info *mtd, loff_t offs, int allowbbt);
extern int nand_erase_nand(struct mtd_info *mtd, struct erase_info *instr,
			   int allowbbt);
extern int nand_do_read(struct mtd_info *mtd, loff_t from, size_t len,
			size_t * retlen, uint8_t * buf);

/**
 * struct platform_nand_chip - chip level device structure
 * @nr_chips:		max. number of chips to scan for
 * @chip_offset:	chip number offset
 * @nr_partitions:	number of partitions pointed to by partitions (or zero)
 * @partitions:		mtd partition list
 * @chip_delay:		R/B delay value in us
 * @options:		Option flags, e.g. 16bit buswidth
 * @ecclayout:		ecc layout info structure
 * @part_probe_types:	NULL-terminated array of probe types
 * @set_parts:		platform specific function to set partitions
 * @priv:		hardware controller specific settings
 */
struct platform_nand_chip {
	int			nr_chips;
	int			chip_offset;
	int			nr_partitions;
	struct mtd_partition	*partitions;
	struct nand_ecclayout	*ecclayout;
	int			chip_delay;
	unsigned int		options;
	const char		**part_probe_types;
	void			(*set_parts)(uint64_t size,
					struct platform_nand_chip *chip);
	void			*priv;
};

/* Keep gcc happy */
struct platform_device;

/**
 * struct platform_nand_ctrl - controller level device structure
 * @probe:		platform specific function to probe/setup hardware
 * @remove:		platform specific function to remove/teardown hardware
 * @hwcontrol:		platform specific hardware control structure
 * @dev_ready:		platform specific function to read ready/busy pin
 * @select_chip:	platform specific chip select function
 * @cmd_ctrl:		platform specific function for controlling
 *			ALE/CLE/nCE. Also used to write command and address
 * @write_buf:		platform specific function for write buffer
 * @read_buf:		platform specific function for read buffer
 * @priv:		private data to transport driver specific settings
 *
 * All fields are optional and depend on the hardware driver requirements
 */
struct platform_nand_ctrl {
	int		(*probe)(struct platform_device *pdev);
	void		(*remove)(struct platform_device *pdev);
	void		(*hwcontrol)(struct mtd_info *mtd, int cmd);
	int		(*dev_ready)(struct mtd_info *mtd);
	void		(*select_chip)(struct mtd_info *mtd, int chip);
	void		(*cmd_ctrl)(struct mtd_info *mtd, int dat,
				    unsigned int ctrl);
	void		(*write_buf)(struct mtd_info *mtd,
				    const uint8_t *buf, int len);
	void		(*read_buf)(struct mtd_info *mtd,
				    uint8_t *buf, int len);
	void		*priv;
};

/**
 * struct platform_nand_data - container structure for platform-specific data
 * @chip:		chip level chip structure
 * @ctrl:		controller level device structure
 */
struct platform_nand_data {
	struct platform_nand_chip	chip;
	struct platform_nand_ctrl	ctrl;
};

/* Some helpers to access the data structures */
static inline
struct platform_nand_chip *get_platform_nandchip(struct mtd_info *mtd)
{
	struct nand_chip *chip = mtd->priv;

	return chip->priv;
}

#endif /* __LINUX_MTD_NAND_H */
