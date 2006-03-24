/*
 * Copyright (c) 2000,2002,2005 Silicon Graphics, Inc.
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
#ifndef __XFS_ATTR_SF_H__
#define	__XFS_ATTR_SF_H__

/*
 * Attribute storage when stored inside the inode.
 *
 * Small attribute lists are packed as tightly as possible so as
 * to fit into the literal area of the inode.
 */

struct xfs_inode;

/*
 * Entries are packed toward the top as tight as possible.
 */
typedef struct xfs_attr_shortform {
	struct xfs_attr_sf_hdr {	/* constant-structure header block */
		__be16	totsize;	/* total bytes in shortform list */
		__u8	count;	/* count of active entries */
	} hdr;
	struct xfs_attr_sf_entry {
		__uint8_t namelen;	/* actual length of name (no NULL) */
		__uint8_t valuelen;	/* actual length of value (no NULL) */
		__uint8_t flags;	/* flags bits (see xfs_attr_leaf.h) */
		__uint8_t nameval[1];	/* name & value bytes concatenated */
	} list[1];			/* variable sized array */
} xfs_attr_shortform_t;
typedef struct xfs_attr_sf_hdr xfs_attr_sf_hdr_t;
typedef struct xfs_attr_sf_entry xfs_attr_sf_entry_t;

/*
 * We generate this then sort it, attr_list() must return things in hash-order.
 */
typedef struct xfs_attr_sf_sort {
	__uint8_t	entno;		/* entry number in original list */
	__uint8_t	namelen;	/* length of name value (no null) */
	__uint8_t	valuelen;	/* length of value */
	__uint8_t	flags;		/* flags bits (see xfs_attr_leaf.h) */
	xfs_dahash_t	hash;		/* this entry's hash value */
	char		*name;		/* name value, pointer into buffer */
} xfs_attr_sf_sort_t;

#define XFS_ATTR_SF_ENTSIZE_BYNAME(nlen,vlen)	/* space name/value uses */ \
	(((int)sizeof(xfs_attr_sf_entry_t)-1 + (nlen)+(vlen)))
#define XFS_ATTR_SF_ENTSIZE_MAX			/* max space for name&value */ \
	((1 << (NBBY*(int)sizeof(__uint8_t))) - 1)
#define XFS_ATTR_SF_ENTSIZE(sfep)		/* space an entry uses */ \
	((int)sizeof(xfs_attr_sf_entry_t)-1 + (sfep)->namelen+(sfep)->valuelen)
#define XFS_ATTR_SF_NEXTENTRY(sfep)		/* next entry in struct */ \
	((xfs_attr_sf_entry_t *)((char *)(sfep) + XFS_ATTR_SF_ENTSIZE(sfep)))
#define XFS_ATTR_SF_TOTSIZE(dp)			/* total space in use */ \
	(be16_to_cpu(((xfs_attr_shortform_t *)	\
		((dp)->i_afp->if_u1.if_data))->hdr.totsize))

#if defined(XFS_ATTR_TRACE)
/*
 * Kernel tracing support for attribute lists
 */
struct xfs_attr_list_context;
struct xfs_da_intnode;
struct xfs_da_node_entry;
struct xfs_attr_leafblock;

#define	XFS_ATTR_TRACE_SIZE	4096	/* size of global trace buffer */
extern ktrace_t	*xfs_attr_trace_buf;

/*
 * Trace record types.
 */
#define	XFS_ATTR_KTRACE_L_C	1	/* context */
#define	XFS_ATTR_KTRACE_L_CN	2	/* context, node */
#define	XFS_ATTR_KTRACE_L_CB	3	/* context, btree */
#define	XFS_ATTR_KTRACE_L_CL	4	/* context, leaf */

void xfs_attr_trace_l_c(char *where, struct xfs_attr_list_context *context);
void xfs_attr_trace_l_cn(char *where, struct xfs_attr_list_context *context,
			      struct xfs_da_intnode *node);
void xfs_attr_trace_l_cb(char *where, struct xfs_attr_list_context *context,
			      struct xfs_da_node_entry *btree);
void xfs_attr_trace_l_cl(char *where, struct xfs_attr_list_context *context,
			      struct xfs_attr_leafblock *leaf);
void xfs_attr_trace_enter(int type, char *where,
			     __psunsigned_t a2, __psunsigned_t a3,
			     __psunsigned_t a4, __psunsigned_t a5,
			     __psunsigned_t a6, __psunsigned_t a7,
			     __psunsigned_t a8, __psunsigned_t a9,
			     __psunsigned_t a10, __psunsigned_t a11,
			     __psunsigned_t a12, __psunsigned_t a13,
			     __psunsigned_t a14, __psunsigned_t a15);
#else
#define	xfs_attr_trace_l_c(w,c)
#define	xfs_attr_trace_l_cn(w,c,n)
#define	xfs_attr_trace_l_cb(w,c,b)
#define	xfs_attr_trace_l_cl(w,c,l)
#endif /* XFS_ATTR_TRACE */

#endif	/* __XFS_ATTR_SF_H__ */
