/*
 * lowlevel.c
 *
 * PURPOSE
 *  Low Level Device Routines for the UDF filesystem
 *
 * CONTACTS
 *	E-mail regarding any portion of the Linux UDF file system should be
 *	directed to the development team mailing list (run by majordomo):
 *		linux_udf@hpesjro.fc.hp.com
 *
 * COPYRIGHT
 *	This file is distributed under the terms of the GNU General Public
 *	License (GPL). Copies of the GPL can be obtained from:
 *		ftp://prep.ai.mit.edu/pub/gnu/GPL
 *	Each contributing author retains all rights to their own work.
 *
 *  (C) 1999-2001 Ben Fennema
 *
 * HISTORY
 *
 *  03/26/99 blf  Created.
 */

#include "udfdecl.h"

#include <linux/blkdev.h>
#include <linux/cdrom.h>
#include <asm/uaccess.h>

#include <linux/udf_fs.h>
#include "udf_sb.h"

unsigned int 
udf_get_last_session(struct super_block *sb)
{
	struct cdrom_multisession ms_info;
	unsigned int vol_desc_start;
	struct block_device *bdev = sb->s_bdev;
	int i;

	vol_desc_start=0;
	ms_info.addr_format=CDROM_LBA;
	i = ioctl_by_bdev(bdev, CDROMMULTISESSION, (unsigned long) &ms_info);

#define WE_OBEY_THE_WRITTEN_STANDARDS 1

	if (i == 0)
	{
		udf_debug("XA disk: %s, vol_desc_start=%d\n",
			(ms_info.xa_flag ? "yes" : "no"), ms_info.addr.lba);
#if WE_OBEY_THE_WRITTEN_STANDARDS
		if (ms_info.xa_flag) /* necessary for a valid ms_info.addr */
#endif
			vol_desc_start = ms_info.addr.lba;
	}
	else
	{
		udf_debug("CDROMMULTISESSION not supported: rc=%d\n", i);
	}
	return vol_desc_start;
}

unsigned long
udf_get_last_block(struct super_block *sb)
{
	struct block_device *bdev = sb->s_bdev;
	unsigned long lblock = 0;

	if (ioctl_by_bdev(bdev, CDROM_LAST_WRITTEN, (unsigned long) &lblock))
		lblock = bdev->bd_inode->i_size >> sb->s_blocksize_bits;

	if (lblock)
		return lblock - 1;
	else
		return 0;
}
