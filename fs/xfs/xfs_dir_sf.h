/*
 * Copyright (c) 2000,2005 Silicon Graphics, Inc.
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
#ifndef __XFS_DIR_SF_H__
#define	__XFS_DIR_SF_H__

/*
 * Directory layout when stored internal to an inode.
 *
 * Small directories are packed as tightly as possible so as to
 * fit into the literal area of the inode.
 */

typedef struct { __uint8_t i[sizeof(xfs_ino_t)]; } xfs_dir_ino_t;

/*
 * The parent directory has a dedicated field, and the self-pointer must
 * be calculated on the fly.
 *
 * Entries are packed toward the top as tight as possible.  The header
 * and the elements much be memcpy'd out into a work area to get correct
 * alignment for the inode number fields.
 */
typedef struct xfs_dir_shortform {
	struct xfs_dir_sf_hdr {		/* constant-structure header block */
		xfs_dir_ino_t parent;	/* parent dir inode number */
		__uint8_t count;	/* count of active entries */
	} hdr;
	struct xfs_dir_sf_entry {
		xfs_dir_ino_t inumber;	/* referenced inode number */
		__uint8_t namelen;	/* actual length of name (no NULL) */
		__uint8_t name[1];	/* name */
	} list[1];			/* variable sized array */
} xfs_dir_shortform_t;
typedef struct xfs_dir_sf_hdr xfs_dir_sf_hdr_t;
typedef struct xfs_dir_sf_entry xfs_dir_sf_entry_t;

/*
 * We generate this then sort it, so that readdirs are returned in
 * hash-order.  Else seekdir won't work.
 */
typedef struct xfs_dir_sf_sort {
	__uint8_t	entno;		/* .=0, ..=1, else entry# + 2 */
	__uint8_t	seqno;		/* sequence # with same hash value */
	__uint8_t	namelen;	/* length of name value (no null) */
	xfs_dahash_t	hash;		/* this entry's hash value */
	xfs_intino_t	ino;		/* this entry's inode number */
	char		*name;		/* name value, pointer into buffer */
} xfs_dir_sf_sort_t;

#define	XFS_DIR_SF_GET_DIRINO(from,to)	xfs_dir_sf_get_dirino(from, to)
static inline void xfs_dir_sf_get_dirino(xfs_dir_ino_t *from, xfs_ino_t *to)
{
	*(to) = XFS_GET_DIR_INO8(*from);
}

#define	XFS_DIR_SF_PUT_DIRINO(from,to)	xfs_dir_sf_put_dirino(from, to)
static inline void xfs_dir_sf_put_dirino(xfs_ino_t *from, xfs_dir_ino_t *to)
{
	XFS_PUT_DIR_INO8(*(from), *(to));
}

#define XFS_DIR_SF_ENTSIZE_BYNAME(len)	xfs_dir_sf_entsize_byname(len)
static inline int xfs_dir_sf_entsize_byname(int len)
{
	return (uint)sizeof(xfs_dir_sf_entry_t)-1 + (len);
}

#define XFS_DIR_SF_ENTSIZE_BYENTRY(sfep)	xfs_dir_sf_entsize_byentry(sfep)
static inline int xfs_dir_sf_entsize_byentry(xfs_dir_sf_entry_t *sfep)
{
	return (uint)sizeof(xfs_dir_sf_entry_t)-1 + (sfep)->namelen;
}

#define XFS_DIR_SF_NEXTENTRY(sfep)		xfs_dir_sf_nextentry(sfep)
static inline xfs_dir_sf_entry_t *xfs_dir_sf_nextentry(xfs_dir_sf_entry_t *sfep)
{
	return (xfs_dir_sf_entry_t *) \
		((char *)(sfep) + XFS_DIR_SF_ENTSIZE_BYENTRY(sfep));
}

#define XFS_DIR_SF_ALLFIT(count,totallen)	\
	xfs_dir_sf_allfit(count,totallen)
static inline int xfs_dir_sf_allfit(int count, int totallen)
{
	return ((uint)sizeof(xfs_dir_sf_hdr_t) + \
	       ((uint)sizeof(xfs_dir_sf_entry_t)-1)*(count) + (totallen));
}

#if defined(XFS_DIR_TRACE)

/*
 * Kernel tracing support for directories.
 */
struct uio;
struct xfs_inode;
struct xfs_da_intnode;
struct xfs_dinode;
struct xfs_dir_leafblock;
struct xfs_dir_leaf_entry;

#define	XFS_DIR_TRACE_SIZE	4096	/* size of global trace buffer */
extern ktrace_t	*xfs_dir_trace_buf;

/*
 * Trace record types.
 */
#define	XFS_DIR_KTRACE_G_DU	1	/* dp, uio */
#define	XFS_DIR_KTRACE_G_DUB	2	/* dp, uio, bno */
#define	XFS_DIR_KTRACE_G_DUN	3	/* dp, uio, node */
#define	XFS_DIR_KTRACE_G_DUL	4	/* dp, uio, leaf */
#define	XFS_DIR_KTRACE_G_DUE	5	/* dp, uio, leaf entry */
#define	XFS_DIR_KTRACE_G_DUC	6	/* dp, uio, cookie */

void xfs_dir_trace_g_du(char *where, struct xfs_inode *dp, struct uio *uio);
void xfs_dir_trace_g_dub(char *where, struct xfs_inode *dp, struct uio *uio,
			      xfs_dablk_t bno);
void xfs_dir_trace_g_dun(char *where, struct xfs_inode *dp, struct uio *uio,
			      struct xfs_da_intnode *node);
void xfs_dir_trace_g_dul(char *where, struct xfs_inode *dp, struct uio *uio,
			      struct xfs_dir_leafblock *leaf);
void xfs_dir_trace_g_due(char *where, struct xfs_inode *dp, struct uio *uio,
			      struct xfs_dir_leaf_entry *entry);
void xfs_dir_trace_g_duc(char *where, struct xfs_inode *dp, struct uio *uio,
			      xfs_off_t cookie);
void xfs_dir_trace_enter(int type, char *where,
			     void *a0, void *a1, void *a2, void *a3,
			     void *a4, void *a5, void *a6, void *a7,
			     void *a8, void *a9, void *a10, void *a11);
#else
#define	xfs_dir_trace_g_du(w,d,u)
#define	xfs_dir_trace_g_dub(w,d,u,b)
#define	xfs_dir_trace_g_dun(w,d,u,n)
#define	xfs_dir_trace_g_dul(w,d,u,l)
#define	xfs_dir_trace_g_due(w,d,u,e)
#define	xfs_dir_trace_g_duc(w,d,u,c)
#endif /* DEBUG */

#endif	/* __XFS_DIR_SF_H__ */
