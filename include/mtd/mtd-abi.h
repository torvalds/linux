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

#define MTD_ABSENT		0
#define MTD_RAM			1
#define MTD_ROM			2
#define MTD_NORFLASH		3
#define MTD_NANDFLASH		4
#define MTD_DATAFLASH		6
#define MTD_UBIVOLUME		7

#define MTD_WRITEABLE		0x400	/* Device is writeable */
#define MTD_BIT_WRITEABLE	0x800	/* Single bits can be flipped */
#define MTD_NO_ERASE		0x1000	/* No erase necessary */
#define MTD_POWERUP_LOCK	0x2000	/* Always locked after reset */

// Some common devices / combinations of capabilities
#define MTD_CAP_ROM		0
#define MTD_CAP_RAM		(MTD_WRITEABLE | MTD_BIT_WRITEABLE | MTD_NO_ERASE)
#define MTD_CAP_NORFLASH	(MTD_WRITEABLE | MTD_BIT_WRITEABLE)
#define MTD_CAP_NANDFLASH	(MTD_WRITEABLE)

/* ECC byte placement */
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
	__u32 size;	 // Total size of the MTD
	__u32 erasesize;
	__u32 writesize;
	__u32 oobsize;   // Amount of OOB data per block (e.g. 16)
	/* The below two fields are obsolete and broken, do not use them
	 * (TODO: remove at some point) */
	__u32 ecctype;
	__u32 eccsize;
};

struct region_info_user {
	__u32 offset;		/* At which this region starts,
					 * from the beginning of the MTD */
	__u32 erasesize;		/* For this region */
	__u32 numblocks;		/* Number of blocks in this region */
	__u32 regionindex;
};

struct otp_info {
	__u32 start;
	__u32 length;
	__u32 locked;
};

#define MEMGETINFO		_IOR('M', 1, struct mtd_info_user)
#define MEMERASE		_IOW('M', 2, struct erase_info_user)
#define MEMWRITEOOB		_IOWR('M', 3, struct mtd_oob_buf)
#define MEMREADOOB		_IOWR('M', 4, struct mtd_oob_buf)
#define MEMLOCK			_IOW('M', 5, struct erase_info_user)
#define MEMUNLOCK		_IOW('M', 6, struct erase_info_user)
#define MEMGETREGIONCOUNT	_IOR('M', 7, int)
#define MEMGETREGIONINFO	_IOWR('M', 8, struct region_info_user)
#define MEMSETOOBSEL		_IOW('M', 9, struct nand_oobinfo)
#define MEMGETOOBSEL		_IOR('M', 10, struct nand_oobinfo)
#define MEMGETBADBLOCK		_IOW('M', 11, __kernel_loff_t)
#define MEMSETBADBLOCK		_IOW('M', 12, __kernel_loff_t)
#define OTPSELECT		_IOR('M', 13, int)
#define OTPGETREGIONCOUNT	_IOW('M', 14, int)
#define OTPGETREGIONINFO	_IOW('M', 15, struct otp_info)
#define OTPLOCK			_IOR('M', 16, struct otp_info)
#define ECCGETLAYOUT		_IOR('M', 17, struct nand_ecclayout)
#define ECCGETSTATS		_IOR('M', 18, struct mtd_ecc_stats)
#define MTDFILEMODE		_IO('M', 19)
#define MEMERASE64		_IOW('M', 20, struct erase_info_user64)
#define MEMWRITEOOB64		_IOWR('M', 21, struct mtd_oob_buf64)
#define MEMREADOOB64		_IOWR('M', 22, struct mtd_oob_buf64)
#define MEMISLOCKED		_IOR('M', 23, struct erase_info_user)

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
/*
 * ECC layout control structure. Exported to userspace for
 * diagnosis and to allow creation of raw images
 */
struct nand_ecclayout {
	__u32 eccbytes;
	__u32 eccpos[64];
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
 * Read/write file modes for access to MTD
 */
enum mtd_file_modes {
	MTD_MODE_NORMAL = MTD_OTP_OFF,
	MTD_MODE_OTP_FACTORY = MTD_OTP_FACTORY,
	MTD_MODE_OTP_USER = MTD_OTP_USER,
	MTD_MODE_RAW,
};

#endif /* __MTD_ABI_H__ */
