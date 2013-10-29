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


/*
 * For filetype enabled shortform directories, the file type field is stored at
 * the end of the name.  Because it's only a single byte, endian conversion is
 * not necessary. For non-filetype enable directories, the type is always
 * unknown and we never store the value.
 */
static __uint8_t
xfs_dir2_sfe_get_ftype(
	struct xfs_dir2_sf_entry *sfep)
{
	return XFS_DIR3_FT_UNKNOWN;
}

static void
xfs_dir2_sfe_put_ftype(
	struct xfs_dir2_sf_entry *sfep,
	__uint8_t		ftype)
{
	ASSERT(ftype < XFS_DIR3_FT_MAX);
}

static __uint8_t
xfs_dir3_sfe_get_ftype(
	struct xfs_dir2_sf_entry *sfep)
{
	__uint8_t	ftype;

	ftype = sfep->name[sfep->namelen];
	if (ftype >= XFS_DIR3_FT_MAX)
		return XFS_DIR3_FT_UNKNOWN;
	return ftype;
}

static void
xfs_dir3_sfe_put_ftype(
	struct xfs_dir2_sf_entry *sfep,
	__uint8_t		ftype)
{
	ASSERT(ftype < XFS_DIR3_FT_MAX);

	sfep->name[sfep->namelen] = ftype;
}

/*
 * Inode numbers in short-form directories can come in two versions,
 * either 4 bytes or 8 bytes wide.  These helpers deal with the
 * two forms transparently by looking at the headers i8count field.
 *
 * For 64-bit inode number the most significant byte must be zero.
 */
static xfs_ino_t
xfs_dir2_sf_get_ino(
	struct xfs_dir2_sf_hdr	*hdr,
	xfs_dir2_inou_t		*from)
{
	if (hdr->i8count)
		return get_unaligned_be64(&from->i8.i) & 0x00ffffffffffffffULL;
	else
		return get_unaligned_be32(&from->i4.i);
}

static void
xfs_dir2_sf_put_ino(
	struct xfs_dir2_sf_hdr	*hdr,
	xfs_dir2_inou_t		*to,
	xfs_ino_t		ino)
{
	ASSERT((ino & 0xff00000000000000ULL) == 0);

	if (hdr->i8count)
		put_unaligned_be64(ino, &to->i8.i);
	else
		put_unaligned_be32(ino, &to->i4.i);
}

static xfs_ino_t
xfs_dir2_sf_get_parent_ino(
	struct xfs_dir2_sf_hdr	*hdr)
{
	return xfs_dir2_sf_get_ino(hdr, &hdr->parent);
}

static void
xfs_dir2_sf_put_parent_ino(
	struct xfs_dir2_sf_hdr	*hdr,
	xfs_ino_t		ino)
{
	xfs_dir2_sf_put_ino(hdr, &hdr->parent, ino);
}

/*
 * In short-form directory entries the inode numbers are stored at variable
 * offset behind the entry name. If the entry stores a filetype value, then it
 * sits between the name and the inode number. Hence the inode numbers may only
 * be accessed through the helpers below.
 */
static xfs_ino_t
xfs_dir2_sfe_get_ino(
	struct xfs_dir2_sf_hdr	*hdr,
	struct xfs_dir2_sf_entry *sfep)
{
	return xfs_dir2_sf_get_ino(hdr,
				(xfs_dir2_inou_t *)&sfep->name[sfep->namelen]);
}

static void
xfs_dir2_sfe_put_ino(
	struct xfs_dir2_sf_hdr	*hdr,
	struct xfs_dir2_sf_entry *sfep,
	xfs_ino_t		ino)
{
	xfs_dir2_sf_put_ino(hdr,
			    (xfs_dir2_inou_t *)&sfep->name[sfep->namelen], ino);
}

static xfs_ino_t
xfs_dir3_sfe_get_ino(
	struct xfs_dir2_sf_hdr	*hdr,
	struct xfs_dir2_sf_entry *sfep)
{
	return xfs_dir2_sf_get_ino(hdr,
			(xfs_dir2_inou_t *)&sfep->name[sfep->namelen + 1]);
}

static void
xfs_dir3_sfe_put_ino(
	struct xfs_dir2_sf_hdr	*hdr,
	struct xfs_dir2_sf_entry *sfep,
	xfs_ino_t		ino)
{
	xfs_dir2_sf_put_ino(hdr,
			(xfs_dir2_inou_t *)&sfep->name[sfep->namelen + 1], ino);
}

const struct xfs_dir_ops xfs_dir2_ops = {
	.sf_entsize = xfs_dir2_sf_entsize,
	.sf_nextentry = xfs_dir2_sf_nextentry,
	.sf_get_ftype = xfs_dir2_sfe_get_ftype,
	.sf_put_ftype = xfs_dir2_sfe_put_ftype,
	.sf_get_ino = xfs_dir2_sfe_get_ino,
	.sf_put_ino = xfs_dir2_sfe_put_ino,
	.sf_get_parent_ino = xfs_dir2_sf_get_parent_ino,
	.sf_put_parent_ino = xfs_dir2_sf_put_parent_ino,
};

const struct xfs_dir_ops xfs_dir2_ftype_ops = {
	.sf_entsize = xfs_dir3_sf_entsize,
	.sf_nextentry = xfs_dir3_sf_nextentry,
	.sf_get_ftype = xfs_dir3_sfe_get_ftype,
	.sf_put_ftype = xfs_dir3_sfe_put_ftype,
	.sf_get_ino = xfs_dir3_sfe_get_ino,
	.sf_put_ino = xfs_dir3_sfe_put_ino,
	.sf_get_parent_ino = xfs_dir2_sf_get_parent_ino,
	.sf_put_parent_ino = xfs_dir2_sf_put_parent_ino,
};

const struct xfs_dir_ops xfs_dir3_ops = {
	.sf_entsize = xfs_dir3_sf_entsize,
	.sf_nextentry = xfs_dir3_sf_nextentry,
	.sf_get_ftype = xfs_dir3_sfe_get_ftype,
	.sf_put_ftype = xfs_dir3_sfe_put_ftype,
	.sf_get_ino = xfs_dir3_sfe_get_ino,
	.sf_put_ino = xfs_dir3_sfe_put_ino,
	.sf_get_parent_ino = xfs_dir2_sf_get_parent_ino,
	.sf_put_parent_ino = xfs_dir2_sf_put_parent_ino,
};
