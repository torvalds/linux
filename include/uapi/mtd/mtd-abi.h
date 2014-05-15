/*
 * Copyright © 1999-2010 David Woodhouse <dwmw2@infradead.org> et al.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifndef __MTD_ABI_H__
#define __MTD_ABI_H__

#include <linux/types.h>

struct erase_info_user {
	__u32 start;
	__u32 length;
};

struct erase_info_user64 {
	__u64 start;
	__u64 length;
};

struct mtd_oob_buf {
	__u32 start;
	__u32 length;
	unsigned char __user *ptr;
};

struct mtd_oob_buf64 {
	__u64 start;
	__u32 pad;
	__u32 length;
	__u64 usr_ptr;
};

/**
 * MTD operation modes
 *
 * @MTD_OPS_PLACE_OOB:	OOB data are placed at the given offset (default)
 * @MTD_OPS_AUTO_OOB:	OOB data are automatically placed at the free areas
 *			which are defined by the internal ecclayout
 * @MTD_OPS_RAW:	data are transferred as-is, with no error correction;
 *			this mode implies %MTD_OPS_PLACE_OOB
 *
 * These modes can be passed to ioctl(MEMWRITE) and are also used internally.
 * See notes on "MTD file modes" for discussion on %MTD_OPS_RAW vs.
 * %MTD_FILE_MODE_RAW.
 */
enum {
	MTD_OPS_PLACE_OOB = 0,
	MTD_OPS_AUTO_OOB = 1,
	MTD_OPS_RAW = 2,
};

/**
 * struct mtd_write_req - data structure for requesting a write operation
 *
 * @start:	start address
 * @len:	length of data buffer
 * @ooblen:	length of OOB buffer
 * @usr_data:	user-provided data buffer
 * @usr_oob:	user-provided OOB buffer
 * @mode:	MTD mode (see "MTD operation modes")
 * @padding:	reserved, must be set to 0
 *
 * This structure supports ioctl(MEMWRITE) operations, allowing data and/or OOB
 * writes in various modes. To write to OOB-only, set @usr_data == NULL, and to
 * write data-only, set @usr_oob == NULL. However, setting both @usr_data and
 * @usr_oob to NULL is not allowed.
 */
struct mtd_write_req {
	__u64 start;
	__u64 len;
	__u64 ooblen;
	__u64 usr_data;
	__u64 usr_oob;
	__u8 mode;
	__u8 padding[7];
};

#define MTD_ABSENT		0
#define MTD_RAM			1
#define MTD_ROM			2
#define MTD_NORFLASH		3
#define MTD_NANDFLASH		4	/* SLC NAND */
#define MTD_DATAFLASH		6
#define MTD_UBIVOLUME		7
#define MTD_MLCNANDFLASH	8	/* MLC NAND (including TLC) */

#define MTD_WRITEABLE		0x400	/* Device is writeable */
#define MTD_BIT_WRITEABLE	0x800	/* Single bits can be flipped */
#define MTD_NO_ERASE		0x1000	/* No erase necessary */
#define MTD_POWERUP_LOCK	0x2000	/* Always locked after reset */

/* Some common devices / combinations of capabilities */
#define MTD_CAP_ROM		0
#define MTD_CAP_RAM		(MTD_WRITEABLE | MTD_BIT_WRITEABLE | MTD_NO_ERASE)
#define MTD_CAP_NORFLASH	(MTD_WRITEABLE | MTD_BIT_WRITEABLE)
#define MTD_CAP_NANDFLASH	(MTD_WRITEABLE)
#define MTD_CAP_NVRAM		(MTD_WRITEABLE | MTD_BIT_WRITEABLE | MTD_NO_ERASE)

/* Obsolete ECC byte placement modes (used with obsolete MEMGETOOBSEL) */
#define MTD_NANDECC_OFF		0	// Switch off ECC (Not recommended)
#define MTD_NANDECC_PLACE	1	// Use the given placement in the structure (YAFFS1 legacy mode)
#define MTD_NANDECC_AUTOPLACE	2	// Use the default placement scheme
#define MTD_NANDECC_PLACEONLY	3	// Use the given placement in the structure (Do not store ecc result on read)
#define MTD_NANDECC_AUTOPL_USR 	4	// Use the given autoplacement scheme rather than using the default

/* OTP mode selection */
#define MTD_OTP_OFF		0
#define MTD_OTP_FACTORY		1
#define MTD_OTP_USER		2

struct mtd_info_user {
	__u8 type;
	__u32 flags;
	__u32 size;	/* Total size of the MTD */
	__u32 erasesize;
	__u32 writesize;
	__u32 oobsize;	/* Amount of OOB data per block (e.g. 16) */
	__u64 padding;	/* Old obsolete field; do not use */
};

struct region_info_user {
	__u32 offset;		/* At which this region starts,
				 * from the beginning of the MTD */
	__u32 erasesize;	/* For this region */
	__u32 numblocks;	/* Number of blocks in this region */
	__u32 regionindex;
};

struct otp_info {
	__u32 start;
	__u32 length;
	__u32 locked;
};

/*
 * Note, the following ioctl existed in the past and was removed:
 * #define MEMSETOOBSEL           _IOW('M', 9, struct nand_oobinfo)
 * Try to avoid adding a new ioctl with the same ioctl number.
 */

/* Get basic MTD characteristics info (better to use sysfs) */
#define MEMGETINFO		_IOR('M', 1, struct mtd_info_user)
/* Erase segment of MTD */
#define MEMERASE		_IOW('M', 2, struct erase_info_user)
/* Write out-of-band data from MTD */
#define MEMWRITEOOB		_IOWR('M', 3, struct mtd_oob_buf)
/* Read out-of-band data from MTD */
#define MEMREADOOB		_IOWR('M', 4, struct mtd_oob_buf)
/* Lock a chip (for MTD that supports it) */
#define MEMLOCK			_IOW('M', 5, struct erase_info_user)
/* Unlock a chip (for MTD that supports it) */
#define MEMUNLOCK		_IOW('M', 6, struct erase_info_user)
/* Get the number of different erase regions */
#define MEMGETREGIONCOUNT	_IOR('M', 7, int)
/* Get information about the erase region for a specific index */
#define MEMGETREGIONINFO	_IOWR('M', 8, struct region_info_user)
/* Get info about OOB modes (e.g., RAW, PLACE, AUTO) - legacy interface */
#define MEMGETOOBSEL		_IOR('M', 10, struct nand_oobinfo)
/* Check if an eraseblock is bad */
#define MEMGETBADBLOCK		_IOW('M', 11, __kernel_loff_t)
/* Mark an eraseblock as bad */
#define MEMSETBADBLOCK		_IOW('M', 12, __kernel_loff_t)
/* Set OTP (One-Time Programmable) mode (factory vs. user) */
#define OTPSELECT		_IOR('M', 13, int)
/* Get number of OTP (One-Time Programmable) regions */
#define OTPGETREGIONCOUNT	_IOW('M', 14, int)
/* Get all OTP (One-Time Programmable) info about MTD */
#define OTPGETREGIONINFO	_IOW('M', 15, struct otp_info)
/* Lock a given range of user data (must be in mode %MTD_FILE_MODE_OTP_USER) */
#define OTPLOCK			_IOR('M', 16, struct otp_info)
/* Get ECC layout (deprecated) */
#define ECCGETLAYOUT		_IOR('M', 17, struct nand_ecclayout_user)
/* Get statistics about corrected/uncorrected errors */
#define ECCGETSTATS		_IOR('M', 18, struct mtd_ecc_stats)
/* Set MTD mode on a per-file-descriptor basis (see "MTD file modes") */
#define MTDFILEMODE		_IO('M', 19)
/* Erase segment of MTD (supports 64-bit address) */
#define MEMERASE64		_IOW('M', 20, struct erase_info_user64)
/* Write data to OOB (64-bit version) */
#define MEMWRITEOOB64		_IOWR('M', 21, struct mtd_oob_buf64)
/* Read data from OOB (64-bit version) */
#define MEMREADOOB64		_IOWR('M', 22, struct mtd_oob_buf64)
/* Check if chip is locked (for MTD that supports it) */
#define MEMISLOCKED		_IOR('M', 23, struct erase_info_user)
/*
 * Most generic write interface; can write in-band and/or out-of-band in various
 * modes (see "struct mtd_write_req"). This ioctl is not supported for flashes
 * without OOB, e.g., NOR flash.
 */
#define MEMWRITE		_IOWR('M', 24, struct mtd_write_req)

/*
 * Obsolete legacy interface. Keep it in order not to break userspace
 * interfaces
 */
struct nand_oobinfo {
	__u32 useecc;
	__u32 eccbytes;
	__u32 oobfree[8][2];
	__u32 eccpos[32];
};

struct nand_oobfree {
	__u32 offset;
	__u32 length;
};

#define MTD_MAX_OOBFREE_ENTRIES	8
#define MTD_MAX_ECCPOS_ENTRIES	64
/*
 * OBSOLETE: ECC layout control structure. Exported to user-space via ioctl
 * ECCGETLAYOUT for backwards compatbility and should not be mistaken as a
 * complete set of ECC information. The ioctl truncates the larger internal
 * structure to retain binary compatibility with the static declaration of the
 * ioctl. Note that the "MTD_MAX_..._ENTRIES" macros represent the max size of
 * the user struct, not the MAX size of the internal struct nand_ecclayout.
 */
struct nand_ecclayout_user {
	__u32 eccbytes;
	__u32 eccpos[MTD_MAX_ECCPOS_ENTRIES];
	__u32 oobavail;
	struct nand_oobfree oobfree[MTD_MAX_OOBFREE_ENTRIES];
};

/**
 * struct mtd_ecc_stats - error correction stats
 *
 * @corrected:	number of corrected bits
 * @failed:	number of uncorrectable errors
 * @badblocks:	number of bad blocks in this partition
 * @bbtblocks:	number of blocks reserved for bad block tables
 */
struct mtd_ecc_stats {
	__u32 corrected;
	__u32 failed;
	__u32 badblocks;
	__u32 bbtblocks;
};

/*
 * MTD file modes - for read/write access to MTD
 *
 * @MTD_FILE_MODE_NORMAL:	OTP disabled, ECC enabled
 * @MTD_FILE_MODE_OTP_FACTORY:	OTP enabled in factory mode
 * @MTD_FILE_MODE_OTP_USER:	OTP enabled in user mode
 * @MTD_FILE_MODE_RAW:		OTP disabled, ECC disabled
 *
 * These modes can be set via ioctl(MTDFILEMODE). The mode mode will be retained
 * separately for each open file descriptor.
 *
 * Note: %MTD_FILE_MODE_RAW provides the same functionality as %MTD_OPS_RAW -
 * raw access to the flash, without error correction or autoplacement schemes.
 * Wherever possible, the MTD_OPS_* mode will override the MTD_FILE_MODE_* mode
 * (e.g., when using ioctl(MEMWRITE)), but in some cases, the MTD_FILE_MODE is
 * used out of necessity (e.g., `write()', ioctl(MEMWRITEOOB64)).
 */
enum mtd_file_modes {
	MTD_FILE_MODE_NORMAL = MTD_OTP_OFF,
	MTD_FILE_MODE_OTP_FACTORY = MTD_OTP_FACTORY,
	MTD_FILE_MODE_OTP_USER = MTD_OTP_USER,
	MTD_FILE_MODE_RAW,
};

static inline int mtd_type_is_nand_user(const struct mtd_info_user *mtd)
{
	return mtd->type == MTD_NANDFLASH || mtd->type == MTD_MLCNANDFLASH;
}

#endif /* __MTD_ABI_H__ */
