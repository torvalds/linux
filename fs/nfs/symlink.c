// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/nfs/symlink.c
 *
 *  Copyright (C) 1992  Rick Sladkey
 *
 *  Optimization changes Copyright (C) 1994 Florian La Roche
 *
 *  Jun 7 1999, cache symlink lookups in the page cache.  -DaveM
 *
 *  nfs symlink handling code
 */

#include <linux/time.h>
#include <linux/errno.h>
#include <linux/sunrpc/clnt.h>
#include <linux/nfs.h>
#include <linux/nfs2.h>
#include <linux/nfs_fs.h>
#include <linux/pagemap.h>
#include <linux/stat.h>
#include <linux/mm.h>
#include <linux/string.h>

/* Symlink caching in the page cache is even more simplistic
 * and straight-forward than readdir caching.
 */

static int nfs_symlink_filler(struct file *file, struct folio *folio)
{
	struct inode *inode = folio->mapping->host;
	int error;

	error = NFS_PROTO(inode)->readlink(inode, &folio->page, 0, PAGE_SIZE);
	folio_end_read(folio, error == 0);
	return error;
}

static const char *nfs_get_link(struct dentry *dentry,
				struct inode *inode,
				struct delayed_call *done)
{
	struct folio *folio;
	void *err;

	if (!dentry) {
		err = ERR_PTR(nfs_revalidate_mapping_rcu(inode));
		if (err)
			return err;
		folio = filemap_get_folio(inode->i_mapping, 0);
		if (IS_ERR(folio))
			return ERR_PTR(-ECHILD);
		if (!folio_test_uptodate(folio)) {
			folio_put(folio);
			return ERR_PTR(-ECHILD);
		}
	} else {
		err = ERR_PTR(nfs_revalidate_mapping(inode, inode->i_mapping));
		if (err)
			return err;
		folio = read_cache_folio(&inode->i_data, 0, nfs_symlink_filler,
				NULL);
		if (IS_ERR(folio))
			return ERR_CAST(folio);
	}
	set_delayed_call(done, page_put_link, folio);
	return folio_address(folio);
}

/*
 * symlinks can't do much...
 */
const struct inode_operations nfs_symlink_inode_operations = {
	.get_link	= nfs_get_link,
	.getattr	= nfs_getattr,
	.setattr	= nfs_setattr,
};
