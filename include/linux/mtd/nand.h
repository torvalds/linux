/*
 *  linux/include/linux/mtd/nand.h
 *
 *  Copyright (c) 2000 David Woodhouse <dwmw2@mvhi.com>
 *                     Steven J. Hill <sjhill@realitydiluted.com>
 *		       Thomas Gleixner <tglx@linutronix.de>
 *
 * $Id: nand.h,v 1.68 2004/11/12 10:40:37 gleixner Exp $
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Info:
 *   Contains standard defines and IDs for NAND flash devices
 *
 *  Changelog:
 *   01-31-2000 DMW     Created
 *   09-18-2000 SJH     Moved structure out of the Disk-On-Chip drivers
 *			so it can be used by other NAND flash device
 *			drivers. I also changed the copyright since none
 *			of the original contents of this file are specific
 *			to DoC devices. David can whack me with a baseball
 *			bat later if I did something naughty.
 *   10-11-2000 SJH     Added private NAND flash structure for driver
 *   10-24-2000 SJH     Added prototype for 'nand_scan' function
 *   10-29-2001 TG	changed nand_chip structure to support 
 *			hardwarespecific function for accessing control lines
 *   02-21-2002 TG	added support for different read/write adress and
 *			ready/busy line access function
 *   02-26-2002 TG	added chip_delay to nand_chip structure to optimize
 *			command delay times for different chips
 *   04-28-2002 TG	OOB config defines moved from nand.c to avoid duplicate
 *			defines in jffs2/wbuf.c
 *   08-07-2002 TG	forced bad block location to byte 5 of OOB, even if
 *			CONFIG_MTD_NAND_ECC_JFFS2 is not set
 *   08-10-2002 TG	extensions to nand_chip structure to support HW-ECC
 *
 *   08-29-2002 tglx 	nand_chip structure: data_poi for selecting 
 *			internal / fs-driver buffer
 *			support for 6byte/512byte hardware ECC
 *			read_ecc, write_ecc extended for different oob-layout
 *			oob layout selections: NAND_NONE_OOB, NAND_JFFS2_OOB,
 *			NAND_YAFFS_OOB
 *  11-25-2002 tglx	Added Manufacturer code FUJITSU, NATIONAL
 *			Split manufacturer and device ID structures 
 *
 *  02-08-2004 tglx 	added option field to nand structure for chip anomalities
 *  05-25-2004 tglx 	added bad block table support, ST-MICRO manufacturer id
 *			update of nand_chip structure description
 */
#ifndef __LINUX_MTD_NAND_H
#define __LINUX_MTD_NAND_H

#include <linux/config.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
#include <linux/mtd/mtd.h>

struct mtd_info;
/* Scan and identify a NAND device */
extern int nand_scan (struct mtd_info *mtd, int max_chips);
/* Free resources held by the NAND device */
extern void nand_release (struct mtd_info *mtd);

/* Read raw data from the device without ECC */
extern int nand_read_raw (struct mtd_info *mtd, uint8_t *buf, loff_t from, size_t len, size_t ooblen);


/* The maximum number of NAND chips in an array */
#define NAND_MAX_CHIPS		8

/* This constant declares the max. oobsize / page, which
 * is supported now. If you add a chip with bigger oobsize/page
 * adjust this accordingly.
 */
#define NAND_MAX_OOBSIZE	64

/*
 * Constants for hardware specific CLE/ALE/NCE function
*/
/* Select the chip by setting nCE to low */
#define NAND_CTL_SETNCE 	1
/* Deselect the chip by setting nCE to high */
#define NAND_CTL_CLRNCE		2
/* Select the command latch by setting CLE to high */
#define NAND_CTL_SETCLE		3
/* Deselect the command latch by setting CLE to low */
#define NAND_CTL_CLRCLE		4
/* Select the address latch by setting ALE to high */
#define NAND_CTL_SETALE		5
/* Deselect the address latch by setting ALE to low */
#define NAND_CTL_CLRALE		6
/* Set write protection by setting WP to high. Not used! */
#define NAND_CTL_SETWP		7
/* Clear write protection by setting WP to low. Not used! */
#define NAND_CTL_CLRWP		8

/*
 * Standard NAND flash commands
 */
#define NAND_CMD_READ0		0
#define NAND_CMD_READ1		1
#define NAND_CMD_PAGEPROG	0x10
#define NAND_CMD_READOOB	0x50
#define NAND_CMD_ERASE1		0x60
#define NAND_CMD_STATUS		0x70
#define NAND_CMD_STATUS_MULTI	0x71
#define NAND_CMD_SEQIN		0x80
#define NAND_CMD_READID		0x90
#define NAND_CMD_ERASE2		0xd0
#define NAND_CMD_RESET		0xff

/* Extended commands for large page devices */
#define NAND_CMD_READSTART	0x30
#define NAND_CMD_CACHEDPROG	0x15

/* Status bits */
#define NAND_STATUS_FAIL	0x01
#define NAND_STATUS_FAIL_N1	0x02
#define NAND_STATUS_TRUE_READY	0x20
#define NAND_STATUS_READY	0x40
#define NAND_STATUS_WP		0x80

/* 
 * Constants for ECC_MODES
 */

/* No ECC. Usage is not recommended ! */
#define NAND_ECC_NONE		0
/* Software ECC 3 byte ECC per 256 Byte data */
#define NAND_ECC_SOFT		1
/* Hardware ECC 3 byte ECC per 256 Byte data */
#define NAND_ECC_HW3_256	2
/* Hardware ECC 3 byte ECC per 512 Byte data */
#define NAND_ECC_HW3_512	3
/* Hardware ECC 3 byte ECC per 512 Byte data */
#define NAND_ECC_HW6_512	4
/* Hardware ECC 8 byte ECC per 512 Byte data */
#define NAND_ECC_HW8_512	6
/* Hardware ECC 12 byte ECC per 2048 Byte data */
#define NAND_ECC_HW12_2048	7

/*
 * Constants for Hardware ECC
*/
/* Reset Hardware ECC for read */
#define NAND_ECC_READ		0
/* Reset Hardware ECC for write */
#define NAND_ECC_WRITE		1
/* Enable Hardware ECC before syndrom is read back from flash */
#define NAND_ECC_READSYN	2

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

/* Options valid for Samsung large page devices */
#define NAND_SAMSUNG_LP_OPTIONS \
	(NAND_NO_PADDING | NAND_CACHEPRG | NAND_COPYBACK)

/* Macros to identify the above */
#define NAND_CANAUTOINCR(chip) (!(chip->options & NAND_NO_AUTOINCR))
#define NAND_MUST_PAD(chip) (!(chip->options & NAND_NO_PADDING))
#define NAND_HAS_CACHEPROG(chip) ((chip->options & NAND_CACHEPRG))
#define NAND_HAS_COPYBACK(chip) ((chip->options & NAND_COPYBACK))

/* Mask to zero out the chip options, which come from the id table */
#define NAND_CHIPOPTIONS_MSK	(0x0000ffff & ~NAND_NO_AUTOINCR)

/* Non chip related options */
/* Use a flash based bad block table. This option is passed to the
 * default bad block table function. */
#define NAND_USE_FLASH_BBT	0x00010000
/* The hw ecc generator provides a syndrome instead a ecc value on read 
 * This can only work if we have the ecc bytes directly behind the 
 * data bytes. Applies for DOC and AG-AND Renesas HW Reed Solomon generators */
#define NAND_HWECC_SYNDROME	0x00020000


/* Options set by nand scan */
/* Nand scan has allocated oob_buf */
#define NAND_OOBBUF_ALLOC	0x40000000
/* Nand scan has allocated data_buf */
#define NAND_DATABUF_ALLOC	0x80000000


/*
 * nand_state_t - chip states
 * Enumeration for NAND flash chip state
 */
typedef enum {
	FL_READY,
	FL_READING,
	FL_WRITING,
	FL_ERASING,
	FL_SYNCING,
	FL_CACHEDPRG,
} nand_state_t;

/* Keep gcc happy */
struct nand_chip;

/**
 * struct nand_hw_control - Control structure for hardware controller (e.g ECC generator) shared among independend devices
 * @lock:               protection lock  
 * @active:		the mtd device which holds the controller currently
 */
struct nand_hw_control {
	spinlock_t	 lock;
	struct nand_chip *active;
};

/**
 * struct nand_chip - NAND Private Flash Chip Data
 * @IO_ADDR_R:		[BOARDSPECIFIC] address to read the 8 I/O lines of the flash device 
 * @IO_ADDR_W:		[BOARDSPECIFIC] address to write the 8 I/O lines of the flash device 
 * @read_byte:		[REPLACEABLE] read one byte from the chip
 * @write_byte:		[REPLACEABLE] write one byte to the chip
 * @read_word:		[REPLACEABLE] read one word from the chip
 * @write_word:		[REPLACEABLE] write one word to the chip
 * @write_buf:		[REPLACEABLE] write data from the buffer to the chip
 * @read_buf:		[REPLACEABLE] read data from the chip into the buffer
 * @verify_buf:		[REPLACEABLE] verify buffer contents against the chip data
 * @select_chip:	[REPLACEABLE] select chip nr
 * @block_bad:		[REPLACEABLE] check, if the block is bad
 * @block_markbad:	[REPLACEABLE] mark the block bad
 * @hwcontrol:		[BOARDSPECIFIC] hardwarespecific function for accesing control-lines
 * @dev_ready:		[BOARDSPECIFIC] hardwarespecific function for accesing device ready/busy line
 *			If set to NULL no access to ready/busy is available and the ready/busy information
 *			is read from the chip status register
 * @cmdfunc:		[REPLACEABLE] hardwarespecific function for writing commands to the chip
 * @waitfunc:		[REPLACEABLE] hardwarespecific function for wait on ready
 * @calculate_ecc: 	[REPLACEABLE] function for ecc calculation or readback from ecc hardware
 * @correct_data:	[REPLACEABLE] function for ecc correction, matching to ecc generator (sw/hw)
 * @enable_hwecc:	[BOARDSPECIFIC] function to enable (reset) hardware ecc generator. Must only
 *			be provided if a hardware ECC is available
 * @erase_cmd:		[INTERN] erase command write function, selectable due to AND support
 * @scan_bbt:		[REPLACEABLE] function to scan bad block table
 * @eccmode:		[BOARDSPECIFIC] mode of ecc, see defines 
 * @eccsize: 		[INTERN] databytes used per ecc-calculation
 * @eccbytes: 		[INTERN] number of ecc bytes per ecc-calculation step
 * @eccsteps:		[INTERN] number of ecc calculation steps per page
 * @chip_delay:		[BOARDSPECIFIC] chip dependent delay for transfering data from array to read regs (tR)
 * @chip_lock:		[INTERN] spinlock used to protect access to this structure and the chip
 * @wq:			[INTERN] wait queue to sleep on if a NAND operation is in progress
 * @state: 		[INTERN] the current state of the NAND device
 * @page_shift:		[INTERN] number of address bits in a page (column address bits)
 * @phys_erase_shift:	[INTERN] number of address bits in a physical eraseblock
 * @bbt_erase_shift:	[INTERN] number of address bits in a bbt entry
 * @chip_shift:		[INTERN] number of address bits in one chip
 * @data_buf:		[INTERN] internal buffer for one page + oob 
 * @oob_buf:		[INTERN] oob buffer for one eraseblock
 * @oobdirty:		[INTERN] indicates that oob_buf must be reinitialized
 * @data_poi:		[INTERN] pointer to a data buffer
 * @options:		[BOARDSPECIFIC] various chip options. They can partly be set to inform nand_scan about
 *			special functionality. See the defines for further explanation
 * @badblockpos:	[INTERN] position of the bad block marker in the oob area
 * @numchips:		[INTERN] number of physical chips
 * @chipsize:		[INTERN] the size of one chip for multichip arrays
 * @pagemask:		[INTERN] page number mask = number of (pages / chip) - 1
 * @pagebuf:		[INTERN] holds the pagenumber which is currently in data_buf
 * @autooob:		[REPLACEABLE] the default (auto)placement scheme
 * @bbt:		[INTERN] bad block table pointer
 * @bbt_td:		[REPLACEABLE] bad block table descriptor for flash lookup
 * @bbt_md:		[REPLACEABLE] bad block table mirror descriptor
 * @badblock_pattern:	[REPLACEABLE] bad block scan pattern used for initial bad block scan 
 * @controller:		[OPTIONAL] a pointer to a hardware controller structure which is shared among multiple independend devices
 * @priv:		[OPTIONAL] pointer to private chip date
 */
 
struct nand_chip {
	void  __iomem	*IO_ADDR_R;
	void  __iomem 	*IO_ADDR_W;
	
	u_char		(*read_byte)(struct mtd_info *mtd);
	void		(*write_byte)(struct mtd_info *mtd, u_char byte);
	u16		(*read_word)(struct mtd_info *mtd);
	void		(*write_word)(struct mtd_info *mtd, u16 word);
	
	void		(*write_buf)(struct mtd_info *mtd, const u_char *buf, int len);
	void		(*read_buf)(struct mtd_info *mtd, u_char *buf, int len);
	int		(*verify_buf)(struct mtd_info *mtd, const u_char *buf, int len);
	void		(*select_chip)(struct mtd_info *mtd, int chip);
	int		(*block_bad)(struct mtd_info *mtd, loff_t ofs, int getchip);
	int		(*block_markbad)(struct mtd_info *mtd, loff_t ofs);
	void 		(*hwcontrol)(struct mtd_info *mtd, int cmd);
	int  		(*dev_ready)(struct mtd_info *mtd);
	void 		(*cmdfunc)(struct mtd_info *mtd, unsigned command, int column, int page_addr);
	int 		(*waitfunc)(struct mtd_info *mtd, struct nand_chip *this, int state);
	int		(*calculate_ecc)(struct mtd_info *mtd, const u_char *dat, u_char *ecc_code);
	int 		(*correct_data)(struct mtd_info *mtd, u_char *dat, u_char *read_ecc, u_char *calc_ecc);
	void		(*enable_hwecc)(struct mtd_info *mtd, int mode);
	void		(*erase_cmd)(struct mtd_info *mtd, int page);
	int		(*scan_bbt)(struct mtd_info *mtd);
	int		eccmode;
	int		eccsize;
	int		eccbytes;
	int		eccsteps;
	int 		chip_delay;
	spinlock_t	chip_lock;
	wait_queue_head_t wq;
	nand_state_t 	state;
	int 		page_shift;
	int		phys_erase_shift;
	int		bbt_erase_shift;
	int		chip_shift;
	u_char 		*data_buf;
	u_char		*oob_buf;
	int		oobdirty;
	u_char		*data_poi;
	unsigned int	options;
	int		badblockpos;
	int		numchips;
	unsigned long	chipsize;
	int		pagemask;
	int		pagebuf;
	struct nand_oobinfo	*autooob;
	uint8_t		*bbt;
	struct nand_bbt_descr	*bbt_td;
	struct nand_bbt_descr	*bbt_md;
	struct nand_bbt_descr	*badblock_pattern;
	struct nand_hw_control  *controller;
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

/**
 * struct nand_flash_dev - NAND Flash Device ID Structure
 *
 * @name:  	Identify the device type
 * @id:   	device ID code
 * @pagesize:  	Pagesize in bytes. Either 256 or 512 or 0
 *		If the pagesize is 0, then the real pagesize 
 *		and the eraseize are determined from the
 *		extended id bytes in the chip
 * @erasesize: 	Size of an erase block in the flash device.
 * @chipsize:  	Total chipsize in Mega Bytes
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
 * @id: 	manufacturer ID code of device.
*/
struct nand_manufacturers {
	int id;
	char * name;
};

extern struct nand_flash_dev nand_flash_ids[];
extern struct nand_manufacturers nand_manuf_ids[];

/** 
 * struct nand_bbt_descr - bad block table descriptor
 * @options:	options for this descriptor
 * @pages:	the page(s) where we find the bbt, used with option BBT_ABSPAGE
 *		when bbt is searched, then we store the found bbts pages here.
 *		Its an array and supports up to 8 chips now
 * @offs:	offset of the pattern in the oob area of the page
 * @veroffs:	offset of the bbt version counter in the oob are of the page
 * @version:	version read from the bbt page during scan
 * @len:	length of the pattern, if 0 no pattern check is performed
 * @maxblocks:	maximum number of blocks to search for a bbt. This number of
 *		blocks is reserved at the end of the device where the tables are 
 *		written.
 * @reserved_block_code: if non-0, this pattern denotes a reserved (rather than
 *              bad) block in the stored bbt
 * @pattern:	pattern to identify bad block table or factory marked good / 
 *		bad blocks, can be NULL, if len = 0
 *
 * Descriptor for the bad block table marker and the descriptor for the 
 * pattern which identifies good and bad blocks. The assumption is made
 * that the pattern and the version count are always located in the oob area
 * of the first block.
 */
struct nand_bbt_descr {
	int	options;
	int	pages[NAND_MAX_CHIPS];
	int	offs;
	int	veroffs;
	uint8_t	version[NAND_MAX_CHIPS];
	int	len;
	int 	maxblocks;
	int	reserved_block_code;
	uint8_t	*pattern;
};

/* Options for the bad block table descriptors */

/* The number of bits used per block in the bbt on the device */
#define NAND_BBT_NRBITS_MSK	0x0000000F
#define NAND_BBT_1BIT		0x00000001
#define NAND_BBT_2BIT		0x00000002
#define NAND_BBT_4BIT		0x00000004
#define NAND_BBT_8BIT		0x00000008
/* The bad block table is in the last good block of the device */
#define	NAND_BBT_LASTBLOCK	0x00000010
/* The bbt is at the given page, else we must scan for the bbt */
#define NAND_BBT_ABSPAGE	0x00000020
/* The bbt is at the given page, else we must scan for the bbt */
#define NAND_BBT_SEARCH		0x00000040
/* bbt is stored per chip on multichip devices */
#define NAND_BBT_PERCHIP	0x00000080
/* bbt has a version counter at offset veroffs */
#define NAND_BBT_VERSION	0x00000100
/* Create a bbt if none axists */
#define NAND_BBT_CREATE		0x00000200
/* Search good / bad pattern through all pages of a block */
#define NAND_BBT_SCANALLPAGES	0x00000400
/* Scan block empty during good / bad block scan */
#define NAND_BBT_SCANEMPTY	0x00000800
/* Write bbt if neccecary */
#define NAND_BBT_WRITE		0x00001000
/* Read and write back block contents when writing bbt */
#define NAND_BBT_SAVECONTENT	0x00002000
/* Search good / bad pattern on the first and the second page */
#define NAND_BBT_SCAN2NDPAGE	0x00004000

/* The maximum number of blocks to scan for a bbt */
#define NAND_BBT_SCAN_MAXBLOCKS	4

extern int nand_scan_bbt (struct mtd_info *mtd, struct nand_bbt_descr *bd);
extern int nand_update_bbt (struct mtd_info *mtd, loff_t offs);
extern int nand_default_bbt (struct mtd_info *mtd);
extern int nand_isbad_bbt (struct mtd_info *mtd, loff_t offs, int allowbbt);
extern int nand_erase_nand (struct mtd_info *mtd, struct erase_info *instr, int allowbbt);

/*
* Constants for oob configuration
*/
#define NAND_SMALL_BADBLOCK_POS		5
#define NAND_LARGE_BADBLOCK_POS		0

#endif /* __LINUX_MTD_NAND_H */
