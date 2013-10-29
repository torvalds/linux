/*
 * Copyright (c) 2000,2002,2005 Silicon Graphics, Inc.
 * Copyright (c) 2013 Red Hat, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_mount.h"
#include "xfs_da_format.h"
#include "xfs_inode.h"
#include "xfs_dir2.h"


static int
xfs_dir2_sf_entsize(
	struct xfs_dir2_sf_hdr	*hdr,
	int			len)
{
	int count = sizeof(struct xfs_dir2_sf_entry);	/* namelen + offset */

	count += len;					/* name */
	count += hdr->i8count ? sizeof(xfs_dir2_ino8_t) :
				sizeof(xfs_dir2_ino4_t); /* ino # */
	return count;
}

static int
xfs_dir3_sf_entsize(
	struct xfs_dir2_sf_hdr	*hdr,
	int			len)
{
	return xfs_dir2_sf_entsize(hdr, len) + sizeof(__uint8_t);
}

static struct xfs_dir2_sf_entry *
xfs_dir2_sf_nextentry(
	struct xfs_dir2_sf_hdr	*hdr,
	struct xfs_dir2_sf_entry *sfep)
{
	return (struct xfs_dir2_sf_entry *)
		((char *)sfep + xfs_dir2_sf_entsize(hdr, sfep->namelen));
}

static struct xfs_dir2_sf_entry *
xfs_dir3_sf_nextentry(
	struct xfs_dir2_sf_hdr	*hdr,
	struct xfs_dir2_sf_entry *sfep)
{
	return (struct xfs_dir2_sf_entry *)
		((char *)sfep + xfs_dir3_sf_entsize(hdr, sfep->namelen));
}


const struct xfs_dir_ops xfs_dir2_ops = {
	.sf_entsize = xfs_dir2_sf_entsize,
	.sf_nextentry = xfs_dir2_sf_nextentry,
};

const struct xfs_dir_ops xfs_dir2_ftype_ops = {
	.sf_entsize = xfs_dir3_sf_entsize,
	.sf_nextentry = xfs_dir3_sf_nextentry,
};

const struct xfs_dir_ops xfs_dir3_ops = {
	.sf_entsize = xfs_dir3_sf_entsize,
	.sf_nextentry = xfs_dir3_sf_nextentry,
};
