/*
 *  linux/cluster/ssi/cfs/symlink.c
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation; either version 2 of
 *	the License, or (at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE
 *	or NON INFRINGEMENT.  See the GNU General Public License for more
 *	details.
 *
 * 	You should have received a copy of the GNU General Public License
 * 	along with this program; if not, write to the Free Software
 * 	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *	Questions/Comments/Bugfixes to ssic-linux-devel@lists.sourceforge.net
 *
 *  Copyright (C) 1992  Rick Sladkey
 *
 *  Optimization changes Copyright (C) 1994 Florian La Roche
 *
 *  Jun 7 1999, cache symlink lookups in the page cache.  -DaveM
 *
 *  Portions Copyright (C) 2001 Compaq Computer Corporation
 *
 *  ocfs2 symlink handling code.
 *
 *  Copyright (C) 2004, 2005 Oracle.
 *
 */

#include <linux/fs.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/namei.h>

#include <cluster/masklog.h>

#include "ocfs2.h"

#include "alloc.h"
#include "file.h"
#include "inode.h"
#include "journal.h"
#include "symlink.h"
#include "xattr.h"

#include "buffer_head_io.h"


static int ocfs2_fast_symlink_read_folio(struct file *f, struct folio *folio)
{
	struct inode *inode = folio->mapping->host;
	struct buffer_head *bh = NULL;
	int status = ocfs2_read_inode_block(inode, &bh);
	struct ocfs2_dinode *fe;
	const char *link;
	size_t len;

	if (status < 0) {
		mlog_errno(status);
		goto out;
	}

	fe = (struct ocfs2_dinode *) bh->b_data;
	link = (char *) fe->id2.i_symlink;
	/* will be less than a page size */
	len = strnlen(link, ocfs2_fast_symlink_chars(inode->i_sb));
	memcpy_to_folio(folio, 0, link, len + 1);
out:
	folio_end_read(folio, status == 0);
	brelse(bh);
	return status;
}

const struct address_space_operations ocfs2_fast_symlink_aops = {
	.read_folio		= ocfs2_fast_symlink_read_folio,
};

const struct inode_operations ocfs2_symlink_inode_operations = {
	.get_link	= page_get_link,
	.getattr	= ocfs2_getattr,
	.setattr	= ocfs2_setattr,
	.listxattr	= ocfs2_listxattr,
	.fiemap		= ocfs2_fiemap,
};
