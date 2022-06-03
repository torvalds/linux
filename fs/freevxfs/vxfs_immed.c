// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2000-2001 Christoph Hellwig.
 */

/*
 * Veritas filesystem driver - support for 'immed' inodes.
 */
#include <linux/fs.h>
#include <linux/pagemap.h>

#include "vxfs.h"
#include "vxfs_extern.h"
#include "vxfs_inode.h"


static int	vxfs_immed_read_folio(struct file *, struct folio *);

/*
 * Address space operations for immed files and directories.
 */
const struct address_space_operations vxfs_immed_aops = {
	.read_folio =		vxfs_immed_read_folio,
};

/**
 * vxfs_immed_read_folio - read part of an immed inode into pagecache
 * @file:	file context (unused)
 * @folio:	folio to fill in.
 *
 * Description:
 *   vxfs_immed_read_folio reads a part of the immed area of the
 *   file that hosts @pp into the pagecache.
 *
 * Returns:
 *   Zero on success, else a negative error code.
 *
 * Locking status:
 *   @folio is locked and will be unlocked.
 */
static int
vxfs_immed_read_folio(struct file *fp, struct folio *folio)
{
	struct page *pp = &folio->page;
	struct vxfs_inode_info	*vip = VXFS_INO(pp->mapping->host);
	u_int64_t	offset = (u_int64_t)pp->index << PAGE_SHIFT;
	caddr_t		kaddr;

	kaddr = kmap(pp);
	memcpy(kaddr, vip->vii_immed.vi_immed + offset, PAGE_SIZE);
	kunmap(pp);
	
	flush_dcache_page(pp);
	SetPageUptodate(pp);
        unlock_page(pp);

	return 0;
}
