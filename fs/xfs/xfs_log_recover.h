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
#ifndef	__XFS_LOG_RECOVER_H__
#define __XFS_LOG_RECOVER_H__

/*
 * Macros, structures, prototypes for internal log manager use.
 */

#define XLOG_RHASH_BITS  4
#define XLOG_RHASH_SIZE	16
#define XLOG_RHASH_SHIFT 2
#define XLOG_RHASH(tid)	\
	((((__uint32_t)tid)>>XLOG_RHASH_SHIFT) & (XLOG_RHASH_SIZE-1))

#define XLOG_MAX_REGIONS_IN_ITEM   (XFS_MAX_BLOCKSIZE / XFS_BLI_CHUNK / 2 + 1)


/*
 * item headers are in ri_buf[0].  Additional buffers follow.
 */
typedef struct xlog_recover_item {
	struct xlog_recover_item *ri_next;
	struct xlog_recover_item *ri_prev;
	int			 ri_type;
	int			 ri_cnt;	/* count of regions found */
	int			 ri_total;	/* total regions */
	xfs_log_iovec_t		 *ri_buf;	/* ptr to regions buffer */
} xlog_recover_item_t;

struct xlog_tid;
typedef struct xlog_recover {
	struct xlog_recover *r_next;
	xlog_tid_t	    r_log_tid;		/* log's transaction id */
	xfs_trans_header_t  r_theader;		/* trans header for partial */
	int		    r_state;		/* not needed */
	xfs_lsn_t	    r_lsn;		/* xact lsn */
	xlog_recover_item_t *r_itemq;		/* q for items */
} xlog_recover_t;

#define ITEM_TYPE(i)	(*(ushort *)(i)->ri_buf[0].i_addr)

/*
 * This is the number of entries in the l_buf_cancel_table used during
 * recovery.
 */
#define	XLOG_BC_TABLE_SIZE	64

#define	XLOG_RECOVER_PASS1	1
#define	XLOG_RECOVER_PASS2	2

#endif	/* __XFS_LOG_RECOVER_H__ */
