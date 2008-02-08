/*
 * symlink.c
 *
 * PURPOSE
 *	Symlink handling routines for the OSTA-UDF(tm) filesystem.
 *
 * COPYRIGHT
 *	This file is distributed under the terms of the GNU General Public
 *	License (GPL). Copies of the GPL can be obtained from:
 *		ftp://prep.ai.mit.edu/pub/gnu/GPL
 *	Each contributing author retains all rights to their own work.
 *
 *  (C) 1998-2001 Ben Fennema
 *  (C) 1999 Stelias Computing Inc
 *
 * HISTORY
 *
 *  04/16/99 blf  Created.
 *
 */

#include "udfdecl.h"
#include <asm/uaccess.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/udf_fs.h>
#include <linux/time.h>
#include <linux/mm.h>
#include <linux/stat.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/smp_lock.h>
#include <linux/buffer_head.h>
#include "udf_i.h"

static void udf_pc_to_char(struct super_block *sb, char *from, int fromlen,
			   char *to)
{
	struct pathComponent *pc;
	int elen = 0;
	char *p = to;

	while (elen < fromlen) {
		pc = (struct pathComponent *)(from + elen);
		switch (pc->componentType) {
		case 1:
			if (pc->lengthComponentIdent == 0) {
				p = to;
				*p++ = '/';
			}
			break;
		case 3:
			memcpy(p, "../", 3);
			p += 3;
			break;
		case 4:
			memcpy(p, "./", 2);
			p += 2;
			/* that would be . - just ignore */
			break;
		case 5:
			p += udf_get_filename(sb, pc->componentIdent, p,
					      pc->lengthComponentIdent);
			*p++ = '/';
			break;
		}
		elen += sizeof(struct pathComponent) + pc->lengthComponentIdent;
	}
	if (p > to + 1)
		p[-1] = '\0';
	else
		p[0] = '\0';
}

static int udf_symlink_filler(struct file *file, struct page *page)
{
	struct inode *inode = page->mapping->host;
	struct buffer_head *bh = NULL;
	char *symlink;
	int err = -EIO;
	char *p = kmap(page);

	lock_kernel();
	if (UDF_I(inode)->i_alloc_type == ICBTAG_FLAG_AD_IN_ICB) {
		symlink = UDF_I(inode)->i_ext.i_data + UDF_I(inode)->i_lenEAttr;
	} else {
		bh = sb_bread(inode->i_sb, udf_block_map(inode, 0));

		if (!bh)
			goto out;

		symlink = bh->b_data;
	}

	udf_pc_to_char(inode->i_sb, symlink, inode->i_size, p);
	brelse(bh);

	unlock_kernel();
	SetPageUptodate(page);
	kunmap(page);
	unlock_page(page);
	return 0;

out:
	unlock_kernel();
	SetPageError(page);
	kunmap(page);
	unlock_page(page);
	return err;
}

/*
 * symlinks can't do much...
 */
const struct address_space_operations udf_symlink_aops = {
	.readpage		= udf_symlink_filler,
};
