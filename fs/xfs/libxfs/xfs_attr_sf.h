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
typedef struct xfs_attr_sf_hdr xfs_attr_sf_hdr_t;
typedef struct xfs_attr_sf_entry xfs_attr_sf_entry_t;

/*
 * We generate this then sort it, attr_list() must return things in hash-order.
 */
typedef struct xfs_attr_sf_sort {
	uint8_t		entno;		/* entry number in original list */
	uint8_t		namelen;	/* length of name value (no null) */
	uint8_t		valuelen;	/* length of value */
	uint8_t		flags;		/* flags bits (see xfs_attr_leaf.h) */
	xfs_dahash_t	hash;		/* this entry's hash value */
	unsigned char	*name;		/* name value, pointer into buffer */
} xfs_attr_sf_sort_t;

#define XFS_ATTR_SF_ENTSIZE_BYNAME(nlen,vlen)	/* space name/value uses */ \
	(((int)sizeof(xfs_attr_sf_entry_t)-1 + (nlen)+(vlen)))
#define XFS_ATTR_SF_ENTSIZE_MAX			/* max space for name&value */ \
	((1 << (NBBY*(int)sizeof(uint8_t))) - 1)
#define XFS_ATTR_SF_ENTSIZE(sfep)		/* space an entry uses */ \
	((int)sizeof(xfs_attr_sf_entry_t)-1 + (sfep)->namelen+(sfep)->valuelen)
#define XFS_ATTR_SF_NEXTENTRY(sfep)		/* next entry in struct */ \
	((xfs_attr_sf_entry_t *)((char *)(sfep) + XFS_ATTR_SF_ENTSIZE(sfep)))
#define XFS_ATTR_SF_TOTSIZE(dp)			/* total space in use */ \
	(be16_to_cpu(((xfs_attr_shortform_t *)	\
		((dp)->i_afp->if_u1.if_data))->hdr.totsize))

#endif	/* __XFS_ATTR_SF_H__ */
