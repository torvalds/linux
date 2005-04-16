/*
 * Copyright (c) 2000-2001 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */
#ifndef __XFS_DIR2_SF_H__
#define	__XFS_DIR2_SF_H__

/*
 * Directory layout when stored internal to an inode.
 *
 * Small directories are packed as tightly as possible so as to
 * fit into the literal area of the inode.
 */

struct uio;
struct xfs_dabuf;
struct xfs_da_args;
struct xfs_dir2_block;
struct xfs_inode;
struct xfs_mount;
struct xfs_trans;

/*
 * Maximum size of a shortform directory.
 */
#define	XFS_DIR2_SF_MAX_SIZE	\
	(XFS_DINODE_MAX_SIZE - (uint)sizeof(xfs_dinode_core_t) - \
	 (uint)sizeof(xfs_agino_t))

/*
 * Inode number stored as 8 8-bit values.
 */
typedef	struct { __uint8_t i[8]; } xfs_dir2_ino8_t;

/*
 * Inode number stored as 4 8-bit values.
 * Works a lot of the time, when all the inode numbers in a directory
 * fit in 32 bits.
 */
typedef struct { __uint8_t i[4]; } xfs_dir2_ino4_t;

typedef union {
	xfs_dir2_ino8_t	i8;
	xfs_dir2_ino4_t	i4;
} xfs_dir2_inou_t;
#define	XFS_DIR2_MAX_SHORT_INUM	((xfs_ino_t)0xffffffffULL)

/*
 * Normalized offset (in a data block) of the entry, really xfs_dir2_data_off_t.
 * Only need 16 bits, this is the byte offset into the single block form.
 */
typedef struct { __uint8_t i[2]; } xfs_dir2_sf_off_t;

/*
 * The parent directory has a dedicated field, and the self-pointer must
 * be calculated on the fly.
 *
 * Entries are packed toward the top as tightly as possible.  The header
 * and the elements must be memcpy'd out into a work area to get correct
 * alignment for the inode number fields.
 */
typedef struct xfs_dir2_sf_hdr {
	__uint8_t		count;		/* count of entries */
	__uint8_t		i8count;	/* count of 8-byte inode #s */
	xfs_dir2_inou_t		parent;		/* parent dir inode number */
} xfs_dir2_sf_hdr_t;

typedef struct xfs_dir2_sf_entry {
	__uint8_t		namelen;	/* actual name length */
	xfs_dir2_sf_off_t	offset;		/* saved offset */
	__uint8_t		name[1];	/* name, variable size */
	xfs_dir2_inou_t		inumber;	/* inode number, var. offset */
} xfs_dir2_sf_entry_t;

typedef struct xfs_dir2_sf {
	xfs_dir2_sf_hdr_t	hdr;		/* shortform header */
	xfs_dir2_sf_entry_t	list[1];	/* shortform entries */
} xfs_dir2_sf_t;

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_DIR2_SF_HDR_SIZE)
int xfs_dir2_sf_hdr_size(int i8count);
#define	XFS_DIR2_SF_HDR_SIZE(i8count)	xfs_dir2_sf_hdr_size(i8count)
#else
#define	XFS_DIR2_SF_HDR_SIZE(i8count)	\
	((uint)sizeof(xfs_dir2_sf_hdr_t) - \
	 ((i8count) == 0) * \
	 ((uint)sizeof(xfs_dir2_ino8_t) - (uint)sizeof(xfs_dir2_ino4_t)))
#endif

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_DIR2_SF_INUMBERP)
xfs_dir2_inou_t *xfs_dir2_sf_inumberp(xfs_dir2_sf_entry_t *sfep);
#define	XFS_DIR2_SF_INUMBERP(sfep)	xfs_dir2_sf_inumberp(sfep)
#else
#define	XFS_DIR2_SF_INUMBERP(sfep)	\
	((xfs_dir2_inou_t *)&(sfep)->name[(sfep)->namelen])
#endif

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_DIR2_SF_GET_INUMBER)
xfs_intino_t xfs_dir2_sf_get_inumber(xfs_dir2_sf_t *sfp, xfs_dir2_inou_t *from);
#define	XFS_DIR2_SF_GET_INUMBER(sfp, from)	\
	xfs_dir2_sf_get_inumber(sfp, from)

#else
#define	XFS_DIR2_SF_GET_INUMBER(sfp, from)	\
	((sfp)->hdr.i8count == 0 ? \
		(xfs_intino_t)XFS_GET_DIR_INO4((from)->i4) : \
		(xfs_intino_t)XFS_GET_DIR_INO8((from)->i8))
#endif

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_DIR2_SF_PUT_INUMBER)
void xfs_dir2_sf_put_inumber(xfs_dir2_sf_t *sfp, xfs_ino_t *from,
				     xfs_dir2_inou_t *to);
#define	XFS_DIR2_SF_PUT_INUMBER(sfp,from,to)	\
	xfs_dir2_sf_put_inumber(sfp,from,to)
#else
#define	XFS_DIR2_SF_PUT_INUMBER(sfp,from,to)	\
	if ((sfp)->hdr.i8count == 0) { \
		XFS_PUT_DIR_INO4(*(from), (to)->i4); \
	} else { \
		XFS_PUT_DIR_INO8(*(from), (to)->i8); \
	}
#endif

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_DIR2_SF_GET_OFFSET)
xfs_dir2_data_aoff_t xfs_dir2_sf_get_offset(xfs_dir2_sf_entry_t *sfep);
#define	XFS_DIR2_SF_GET_OFFSET(sfep)	\
	xfs_dir2_sf_get_offset(sfep)
#else
#define	XFS_DIR2_SF_GET_OFFSET(sfep)	\
	INT_GET_UNALIGNED_16_BE(&(sfep)->offset.i)
#endif

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_DIR2_SF_PUT_OFFSET)
void xfs_dir2_sf_put_offset(xfs_dir2_sf_entry_t *sfep,
				    xfs_dir2_data_aoff_t off);
#define	XFS_DIR2_SF_PUT_OFFSET(sfep,off) \
	xfs_dir2_sf_put_offset(sfep,off)
#else
#define	XFS_DIR2_SF_PUT_OFFSET(sfep,off)	\
	INT_SET_UNALIGNED_16_BE(&(sfep)->offset.i,off)
#endif

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_DIR2_SF_ENTSIZE_BYNAME)
int xfs_dir2_sf_entsize_byname(xfs_dir2_sf_t *sfp, int len);
#define XFS_DIR2_SF_ENTSIZE_BYNAME(sfp,len)	\
	xfs_dir2_sf_entsize_byname(sfp,len)
#else
#define XFS_DIR2_SF_ENTSIZE_BYNAME(sfp,len)	/* space a name uses */ \
	((uint)sizeof(xfs_dir2_sf_entry_t) - 1 + (len) - \
	 ((sfp)->hdr.i8count == 0) * \
	 ((uint)sizeof(xfs_dir2_ino8_t) - (uint)sizeof(xfs_dir2_ino4_t)))
#endif

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_DIR2_SF_ENTSIZE_BYENTRY)
int xfs_dir2_sf_entsize_byentry(xfs_dir2_sf_t *sfp, xfs_dir2_sf_entry_t *sfep);
#define XFS_DIR2_SF_ENTSIZE_BYENTRY(sfp,sfep)	\
	xfs_dir2_sf_entsize_byentry(sfp,sfep)
#else
#define XFS_DIR2_SF_ENTSIZE_BYENTRY(sfp,sfep)	/* space an entry uses */ \
	((uint)sizeof(xfs_dir2_sf_entry_t) - 1 + (sfep)->namelen - \
	 ((sfp)->hdr.i8count == 0) * \
	 ((uint)sizeof(xfs_dir2_ino8_t) - (uint)sizeof(xfs_dir2_ino4_t)))
#endif

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_DIR2_SF_FIRSTENTRY)
xfs_dir2_sf_entry_t *xfs_dir2_sf_firstentry(xfs_dir2_sf_t *sfp);
#define XFS_DIR2_SF_FIRSTENTRY(sfp)	xfs_dir2_sf_firstentry(sfp)
#else
#define XFS_DIR2_SF_FIRSTENTRY(sfp)	/* first entry in struct */ \
	((xfs_dir2_sf_entry_t *) \
	 ((char *)(sfp) + XFS_DIR2_SF_HDR_SIZE(sfp->hdr.i8count)))
#endif

#if XFS_WANT_FUNCS || (XFS_WANT_SPACE && XFSSO_XFS_DIR2_SF_NEXTENTRY)
xfs_dir2_sf_entry_t *xfs_dir2_sf_nextentry(xfs_dir2_sf_t *sfp,
					   xfs_dir2_sf_entry_t *sfep);
#define XFS_DIR2_SF_NEXTENTRY(sfp,sfep)		xfs_dir2_sf_nextentry(sfp,sfep)
#else
#define XFS_DIR2_SF_NEXTENTRY(sfp,sfep)		/* next entry in struct */ \
	((xfs_dir2_sf_entry_t *) \
		((char *)(sfep) + XFS_DIR2_SF_ENTSIZE_BYENTRY(sfp,sfep)))
#endif

/*
 * Functions.
 */

extern int
	xfs_dir2_block_sfsize(struct xfs_inode *dp,
			      struct xfs_dir2_block *block,
			      xfs_dir2_sf_hdr_t *sfhp);

extern int
	xfs_dir2_block_to_sf(struct xfs_da_args *args, struct xfs_dabuf *bp,
			     int size, xfs_dir2_sf_hdr_t *sfhp);

extern int
	xfs_dir2_sf_addname(struct xfs_da_args *args);

extern int
	xfs_dir2_sf_create(struct xfs_da_args *args, xfs_ino_t pino);

extern int
	xfs_dir2_sf_getdents(struct xfs_inode *dp, struct uio *uio, int *eofp,
			     struct xfs_dirent *dbp, xfs_dir2_put_t put);

extern int
	xfs_dir2_sf_lookup(struct xfs_da_args *args);

extern int
	xfs_dir2_sf_removename(struct xfs_da_args *args);

extern int
	xfs_dir2_sf_replace(struct xfs_da_args *args);

#endif	/* __XFS_DIR2_SF_H__ */
