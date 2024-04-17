// SPDX-License-Identifier: GPL-2.0-only
/*
 * symlink.c
 *
 * PURPOSE
 *	Symlink handling routines for the OSTA-UDF(tm) filesystem.
 *
 * COPYRIGHT
 *  (C) 1998-2001 Ben Fennema
 *  (C) 1999 Stelias Computing Inc
 *
 * HISTORY
 *
 *  04/16/99 blf  Created.
 *
 */

#include "udfdecl.h"
#include <linux/uaccess.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/time.h>
#include <linux/mm.h>
#include <linux/stat.h>
#include <linux/pagemap.h>
#include "udf_i.h"

static int udf_pc_to_char(struct super_block *sb, unsigned char *from,
			  int fromlen, unsigned char *to, int tolen)
{
	struct pathComponent *pc;
	int elen = 0;
	int comp_len;
	unsigned char *p = to;

	/* Reserve one byte for terminating \0 */
	tolen--;
	while (elen < fromlen) {
		pc = (struct pathComponent *)(from + elen);
		elen += sizeof(struct pathComponent);
		switch (pc->componentType) {
		case 1:
			/*
			 * Symlink points to some place which should be agreed
 			 * upon between originator and receiver of the media. Ignore.
			 */
			if (pc->lengthComponentIdent > 0) {
				elen += pc->lengthComponentIdent;
				break;
			}
			fallthrough;
		case 2:
			if (tolen == 0)
				return -ENAMETOOLONG;
			p = to;
			*p++ = '/';
			tolen--;
			break;
		case 3:
			if (tolen < 3)
				return -ENAMETOOLONG;
			memcpy(p, "../", 3);
			p += 3;
			tolen -= 3;
			break;
		case 4:
			if (tolen < 2)
				return -ENAMETOOLONG;
			memcpy(p, "./", 2);
			p += 2;
			tolen -= 2;
			/* that would be . - just ignore */
			break;
		case 5:
			elen += pc->lengthComponentIdent;
			if (elen > fromlen)
				return -EIO;
			comp_len = udf_get_filename(sb, pc->componentIdent,
						    pc->lengthComponentIdent,
						    p, tolen);
			if (comp_len < 0)
				return comp_len;

			p += comp_len;
			tolen -= comp_len;
			if (tolen == 0)
				return -ENAMETOOLONG;
			*p++ = '/';
			tolen--;
			break;
		}
	}
	if (p > to + 1)
		p[-1] = '\0';
	else
		p[0] = '\0';
	return 0;
}

static int udf_symlink_filler(struct file *file, struct folio *folio)
{
	struct inode *inode = folio->mapping->host;
	struct buffer_head *bh = NULL;
	unsigned char *symlink;
	int err = 0;
	unsigned char *p = folio_address(folio);
	struct udf_inode_info *iinfo = UDF_I(inode);

	/* We don't support symlinks longer than one block */
	if (inode->i_size > inode->i_sb->s_blocksize) {
		err = -ENAMETOOLONG;
		goto out;
	}

	if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_IN_ICB) {
		symlink = iinfo->i_data + iinfo->i_lenEAttr;
	} else {
		bh = udf_bread(inode, 0, 0, &err);
		if (!bh) {
			if (!err)
				err = -EFSCORRUPTED;
			goto out;
		}
		symlink = bh->b_data;
	}

	err = udf_pc_to_char(inode->i_sb, symlink, inode->i_size, p, PAGE_SIZE);
	brelse(bh);
out:
	folio_end_read(folio, err == 0);
	return err;
}

static int udf_symlink_getattr(struct mnt_idmap *idmap,
			       const struct path *path, struct kstat *stat,
			       u32 request_mask, unsigned int flags)
{
	struct dentry *dentry = path->dentry;
	struct inode *inode = d_backing_inode(dentry);
	struct folio *folio;

	generic_fillattr(&nop_mnt_idmap, request_mask, inode, stat);
	folio = read_mapping_folio(inode->i_mapping, 0, NULL);
	if (IS_ERR(folio))
		return PTR_ERR(folio);
	/*
	 * UDF uses non-trivial encoding of symlinks so i_size does not match
	 * number of characters reported by readlink(2) which apparently some
	 * applications expect. Also POSIX says that "The value returned in the
	 * st_size field shall be the length of the contents of the symbolic
	 * link, and shall not count a trailing null if one is present." So
	 * let's report the length of string returned by readlink(2) for
	 * st_size.
	 */
	stat->size = strlen(folio_address(folio));
	folio_put(folio);

	return 0;
}

/*
 * symlinks can't do much...
 */
const struct address_space_operations udf_symlink_aops = {
	.read_folio		= udf_symlink_filler,
};

const struct inode_operations udf_symlink_inode_operations = {
	.get_link	= page_get_link,
	.getattr	= udf_symlink_getattr,
};
