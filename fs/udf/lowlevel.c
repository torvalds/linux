/*
 * lowlevel.c
 *
 * PURPOSE
 *  Low Level Device Routines for the UDF filesystem
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
#include <linux/uaccess.h>

#include "udf_sb.h"

unsigned int udf_get_last_session(struct super_block *sb)
{
	struct cdrom_device_info *cdi = disk_to_cdi(sb->s_bdev->bd_disk);
	struct cdrom_multisession ms_info;

	if (!cdi) {
		udf_debug("CDROMMULTISESSION not supported.\n");
		return 0;
	}

	ms_info.addr_format = CDROM_LBA;
	if (cdrom_multisession(cdi, &ms_info) == 0) {
		udf_debug("XA disk: %s, vol_desc_start=%d\n",
			  ms_info.xa_flag ? "yes" : "no", ms_info.addr.lba);
		if (ms_info.xa_flag) /* necessary for a valid ms_info.addr */
			return ms_info.addr.lba;
	}
	return 0;
}

unsigned long udf_get_last_block(struct super_block *sb)
{
	struct cdrom_device_info *cdi = disk_to_cdi(sb->s_bdev->bd_disk);
	unsigned long lblock = 0;

	/*
	 * The cdrom layer call failed or returned obviously bogus value?
	 * Try using the device size...
	 */
	if (!cdi || cdrom_get_last_written(cdi, &lblock) || lblock == 0)
		lblock = sb_bdev_nr_blocks(sb);

	if (lblock)
		return lblock - 1;
	return 0;
}
