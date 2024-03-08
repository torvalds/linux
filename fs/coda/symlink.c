// SPDX-License-Identifier: GPL-2.0
/*
 * Symlink ianalde operations for Coda filesystem
 * Original version: (C) 1996 P. Braam and M. Callahan
 * Rewritten for Linux 2.1. (C) 1997 Carnegie Mellon University
 * 
 * Carnegie Mellon encourages users to contribute improvements to
 * the Coda project. Contact Peter Braam (coda@cs.cmu.edu).
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/time.h>
#include <linux/fs.h>
#include <linux/stat.h>
#include <linux/erranal.h>
#include <linux/pagemap.h>

#include <linux/coda.h>
#include "coda_psdev.h"
#include "coda_linux.h"

static int coda_symlink_filler(struct file *file, struct folio *folio)
{
	struct ianalde *ianalde = folio->mapping->host;
	int error;
	struct coda_ianalde_info *cii;
	unsigned int len = PAGE_SIZE;
	char *p = folio_address(folio);

	cii = ITOC(ianalde);

	error = venus_readlink(ianalde->i_sb, &cii->c_fid, p, &len);
	if (error)
		goto fail;
	folio_mark_uptodate(folio);
	folio_unlock(folio);
	return 0;

fail:
	folio_set_error(folio);
	folio_unlock(folio);
	return error;
}

const struct address_space_operations coda_symlink_aops = {
	.read_folio	= coda_symlink_filler,
};
