/*
 * Copyright (c) 2008-2010, Dave Chinner
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
#ifndef XFS_ICREATE_ITEM_H
#define XFS_ICREATE_ITEM_H	1

/*
 * on disk log item structure
 *
 * Log recovery assumes the first two entries are the type and size and they fit
 * in 32 bits. Also in host order (ugh) so they have to be 32 bit aligned so
 * decoding can be done correctly.
 */
struct xfs_icreate_log {
	__uint16_t	icl_type;	/* type of log format structure */
	__uint16_t	icl_size;	/* size of log format structure */
	__be32		icl_ag;		/* ag being allocated in */
	__be32		icl_agbno;	/* start block of inode range */
	__be32		icl_count;	/* number of inodes to initialise */
	__be32		icl_isize;	/* size of inodes */
	__be32		icl_length;	/* length of extent to initialise */
	__be32		icl_gen;	/* inode generation number to use */
};

/* in memory log item structure */
struct xfs_icreate_item {
	struct xfs_log_item	ic_item;
	struct xfs_icreate_log	ic_format;
};

extern kmem_zone_t *xfs_icreate_zone;	/* inode create item zone */

void xfs_icreate_log(struct xfs_trans *tp, xfs_agnumber_t agno,
			xfs_agblock_t agbno, unsigned int count,
			unsigned int inode_size, xfs_agblock_t length,
			unsigned int generation);

#endif	/* XFS_ICREATE_ITEM_H */
