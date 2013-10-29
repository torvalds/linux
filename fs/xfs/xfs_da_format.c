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

/*
 * Shortform directory ops
 */
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


/*
 * Directory data block operations
 */
static int
__xfs_dir3_data_entsize(
	bool	ftype,
	int	n)
{
	int	size = offsetof(struct xfs_dir2_data_entry, name[0]);

	size += n;
	size += sizeof(xfs_dir2_data_off_t);
	if (ftype)
		size += sizeof(__uint8_t);
	return roundup(size, XFS_DIR2_DATA_ALIGN);
}

static int
xfs_dir2_data_entsize(
	int			n)
{
	return __xfs_dir3_data_entsize(false, n);
}
static int
xfs_dir3_data_entsize(
	int			n)
{
	return __xfs_dir3_data_entsize(true, n);
}

static __uint8_t
xfs_dir2_data_get_ftype(
	struct xfs_dir2_data_entry *dep)
{
	return XFS_DIR3_FT_UNKNOWN;
}

static void
xfs_dir2_data_put_ftype(
	struct xfs_dir2_data_entry *dep,
	__uint8_t		ftype)
{
	ASSERT(ftype < XFS_DIR3_FT_MAX);
}

static __uint8_t
xfs_dir3_data_get_ftype(
	struct xfs_dir2_data_entry *dep)
{
	__uint8_t	ftype = dep->name[dep->namelen];

	ASSERT(ftype < XFS_DIR3_FT_MAX);
	if (ftype >= XFS_DIR3_FT_MAX)
		return XFS_DIR3_FT_UNKNOWN;
	return ftype;
}

static void
xfs_dir3_data_put_ftype(
	struct xfs_dir2_data_entry *dep,
	__uint8_t		type)
{
	ASSERT(type < XFS_DIR3_FT_MAX);
	ASSERT(dep->namelen != 0);

	dep->name[dep->namelen] = type;
}

/*
 * Pointer to an entry's tag word.
 */
static __be16 *
xfs_dir2_data_entry_tag_p(
	struct xfs_dir2_data_entry *dep)
{
	return (__be16 *)((char *)dep +
		xfs_dir2_data_entsize(dep->namelen) - sizeof(__be16));
}

static __be16 *
xfs_dir3_data_entry_tag_p(
	struct xfs_dir2_data_entry *dep)
{
	return (__be16 *)((char *)dep +
		xfs_dir3_data_entsize(dep->namelen) - sizeof(__be16));
}

/*
 * Offsets of . and .. in data space (always block 0)
 */
static xfs_dir2_data_aoff_t
xfs_dir2_data_dot_offset(void)
{
	return sizeof(struct xfs_dir2_data_hdr);
}

static xfs_dir2_data_aoff_t
xfs_dir2_data_dotdot_offset(void)
{
	return xfs_dir2_data_dot_offset() + xfs_dir2_data_entsize(1);
}

static xfs_dir2_data_aoff_t
xfs_dir2_data_first_offset(void)
{
	return xfs_dir2_data_dotdot_offset() + xfs_dir2_data_entsize(2);
}

static xfs_dir2_data_aoff_t
xfs_dir3_data_dot_offset(void)
{
	return sizeof(struct xfs_dir3_data_hdr);
}

static xfs_dir2_data_aoff_t
xfs_dir3_data_dotdot_offset(void)
{
	return xfs_dir3_data_dot_offset() + xfs_dir3_data_entsize(1);
}

static xfs_dir2_data_aoff_t
xfs_dir3_data_first_offset(void)
{
	return xfs_dir3_data_dotdot_offset() + xfs_dir3_data_entsize(2);
}

/*
 * location of . and .. in data space (always block 0)
 */
static struct xfs_dir2_data_entry *
xfs_dir2_data_dot_entry_p(
	struct xfs_dir2_data_hdr *hdr)
{
	return (struct xfs_dir2_data_entry *)
		((char *)hdr + xfs_dir2_data_dot_offset());
}

static struct xfs_dir2_data_entry *
xfs_dir2_data_dotdot_entry_p(
	struct xfs_dir2_data_hdr *hdr)
{
	return (struct xfs_dir2_data_entry *)
		((char *)hdr + xfs_dir2_data_dotdot_offset());
}

static struct xfs_dir2_data_entry *
xfs_dir2_data_first_entry_p(
	struct xfs_dir2_data_hdr *hdr)
{
	return (struct xfs_dir2_data_entry *)
		((char *)hdr + xfs_dir2_data_first_offset());
}

static struct xfs_dir2_data_entry *
xfs_dir3_data_dot_entry_p(
	struct xfs_dir2_data_hdr *hdr)
{
	return (struct xfs_dir2_data_entry *)
		((char *)hdr + xfs_dir3_data_dot_offset());
}

static struct xfs_dir2_data_entry *
xfs_dir3_data_dotdot_entry_p(
	struct xfs_dir2_data_hdr *hdr)
{
	return (struct xfs_dir2_data_entry *)
		((char *)hdr + xfs_dir3_data_dotdot_offset());
}

static struct xfs_dir2_data_entry *
xfs_dir3_data_first_entry_p(
	struct xfs_dir2_data_hdr *hdr)
{
	return (struct xfs_dir2_data_entry *)
		((char *)hdr + xfs_dir3_data_first_offset());
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

	.data_entsize = xfs_dir2_data_entsize,
	.data_get_ftype = xfs_dir2_data_get_ftype,
	.data_put_ftype = xfs_dir2_data_put_ftype,
	.data_entry_tag_p = xfs_dir2_data_entry_tag_p,

	.data_dot_offset = xfs_dir2_data_dot_offset,
	.data_dotdot_offset = xfs_dir2_data_dotdot_offset,
	.data_first_offset = xfs_dir2_data_first_offset,
	.data_dot_entry_p = xfs_dir2_data_dot_entry_p,
	.data_dotdot_entry_p = xfs_dir2_data_dotdot_entry_p,
	.data_first_entry_p = xfs_dir2_data_first_entry_p,
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

	.data_entsize = xfs_dir3_data_entsize,
	.data_get_ftype = xfs_dir3_data_get_ftype,
	.data_put_ftype = xfs_dir3_data_put_ftype,
	.data_entry_tag_p = xfs_dir3_data_entry_tag_p,

	.data_dot_offset = xfs_dir2_data_dot_offset,
	.data_dotdot_offset = xfs_dir2_data_dotdot_offset,
	.data_first_offset = xfs_dir2_data_first_offset,
	.data_dot_entry_p = xfs_dir2_data_dot_entry_p,
	.data_dotdot_entry_p = xfs_dir2_data_dotdot_entry_p,
	.data_first_entry_p = xfs_dir2_data_first_entry_p,
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

	.data_entsize = xfs_dir3_data_entsize,
	.data_get_ftype = xfs_dir3_data_get_ftype,
	.data_put_ftype = xfs_dir3_data_put_ftype,
	.data_entry_tag_p = xfs_dir3_data_entry_tag_p,

	.data_dot_offset = xfs_dir3_data_dot_offset,
	.data_dotdot_offset = xfs_dir3_data_dotdot_offset,
	.data_first_offset = xfs_dir3_data_first_offset,
	.data_dot_entry_p = xfs_dir3_data_dot_entry_p,
	.data_dotdot_entry_p = xfs_dir3_data_dotdot_entry_p,
	.data_first_entry_p = xfs_dir3_data_first_entry_p,
};
