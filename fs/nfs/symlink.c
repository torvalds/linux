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
#include <linux/erryes.h>
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

static int nfs_symlink_filler(void *data, struct page *page)
{
	struct iyesde *iyesde = data;
	int error;

	error = NFS_PROTO(iyesde)->readlink(iyesde, page, 0, PAGE_SIZE);
	if (error < 0)
		goto error;
	SetPageUptodate(page);
	unlock_page(page);
	return 0;

error:
	SetPageError(page);
	unlock_page(page);
	return -EIO;
}

static const char *nfs_get_link(struct dentry *dentry,
				struct iyesde *iyesde,
				struct delayed_call *done)
{
	struct page *page;
	void *err;

	if (!dentry) {
		err = ERR_PTR(nfs_revalidate_mapping_rcu(iyesde));
		if (err)
			return err;
		page = find_get_page(iyesde->i_mapping, 0);
		if (!page)
			return ERR_PTR(-ECHILD);
		if (!PageUptodate(page)) {
			put_page(page);
			return ERR_PTR(-ECHILD);
		}
	} else {
		err = ERR_PTR(nfs_revalidate_mapping(iyesde, iyesde->i_mapping));
		if (err)
			return err;
		page = read_cache_page(&iyesde->i_data, 0, nfs_symlink_filler,
				iyesde);
		if (IS_ERR(page))
			return ERR_CAST(page);
	}
	set_delayed_call(done, page_put_link, page);
	return page_address(page);
}

/*
 * symlinks can't do much...
 */
const struct iyesde_operations nfs_symlink_iyesde_operations = {
	.get_link	= nfs_get_link,
	.getattr	= nfs_getattr,
	.setattr	= nfs_setattr,
};
