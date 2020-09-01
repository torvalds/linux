// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef	__XFS_LOG_RECOVER_H__
#define __XFS_LOG_RECOVER_H__

/*
 * Each log item type (XFS_LI_*) gets its own xlog_recover_item_ops to
 * define how recovery should work for that type of log item.
 */
struct xlog_recover_item;

/* Sorting hat for log items as they're read in. */
enum xlog_recover_reorder {
	XLOG_REORDER_BUFFER_LIST,
	XLOG_REORDER_ITEM_LIST,
	XLOG_REORDER_INODE_BUFFER_LIST,
	XLOG_REORDER_CANCEL_LIST,
};

struct xlog_recover_item_ops {
	uint16_t	item_type;	/* XFS_LI_* type code. */

	/*
	 * Help sort recovered log items into the order required to replay them
	 * correctly.  Log item types that always use XLOG_REORDER_ITEM_LIST do
	 * not have to supply a function here.  See the comment preceding
	 * xlog_recover_reorder_trans for more details about what the return
	 * values mean.
	 */
	enum xlog_recover_reorder (*reorder)(struct xlog_recover_item *item);

	/* Start readahead for pass2, if provided. */
	void (*ra_pass2)(struct xlog *log, struct xlog_recover_item *item);

	/* Do whatever work we need to do for pass1, if provided. */
	int (*commit_pass1)(struct xlog *log, struct xlog_recover_item *item);

	/*
	 * This function should do whatever work is needed for pass2 of log
	 * recovery, if provided.
	 *
	 * If the recovered item is an intent item, this function should parse
	 * the recovered item to construct an in-core log intent item and
	 * insert it into the AIL.  The in-core log intent item should have 1
	 * refcount so that the item is freed either (a) when we commit the
	 * recovered log item for the intent-done item; (b) replay the work and
	 * log a new intent-done item; or (c) recovery fails and we have to
	 * abort.
	 *
	 * If the recovered item is an intent-done item, this function should
	 * parse the recovered item to find the id of the corresponding intent
	 * log item.  Next, it should find the in-core log intent item in the
	 * AIL and release it.
	 */
	int (*commit_pass2)(struct xlog *log, struct list_head *buffer_list,
			    struct xlog_recover_item *item, xfs_lsn_t lsn);
};

extern const struct xlog_recover_item_ops xlog_icreate_item_ops;
extern const struct xlog_recover_item_ops xlog_buf_item_ops;
extern const struct xlog_recover_item_ops xlog_inode_item_ops;
extern const struct xlog_recover_item_ops xlog_dquot_item_ops;
extern const struct xlog_recover_item_ops xlog_quotaoff_item_ops;
extern const struct xlog_recover_item_ops xlog_bui_item_ops;
extern const struct xlog_recover_item_ops xlog_bud_item_ops;
extern const struct xlog_recover_item_ops xlog_efi_item_ops;
extern const struct xlog_recover_item_ops xlog_efd_item_ops;
extern const struct xlog_recover_item_ops xlog_rui_item_ops;
extern const struct xlog_recover_item_ops xlog_rud_item_ops;
extern const struct xlog_recover_item_ops xlog_cui_item_ops;
extern const struct xlog_recover_item_ops xlog_cud_item_ops;

/*
 * Macros, structures, prototypes for internal log manager use.
 */

#define XLOG_RHASH_BITS  4
#define XLOG_RHASH_SIZE	16
#define XLOG_RHASH_SHIFT 2
#define XLOG_RHASH(tid)	\
	((((uint32_t)tid)>>XLOG_RHASH_SHIFT) & (XLOG_RHASH_SIZE-1))

#define XLOG_MAX_REGIONS_IN_ITEM   (XFS_MAX_BLOCKSIZE / XFS_BLF_CHUNK / 2 + 1)


/*
 * item headers are in ri_buf[0].  Additional buffers follow.
 */
struct xlog_recover_item {
	struct list_head	ri_list;
	int			ri_cnt;	/* count of regions found */
	int			ri_total;	/* total regions */
	struct xfs_log_iovec	*ri_buf;	/* ptr to regions buffer */
	const struct xlog_recover_item_ops *ri_ops;
};

struct xlog_recover {
	struct hlist_node	r_list;
	xlog_tid_t		r_log_tid;	/* log's transaction id */
	xfs_trans_header_t	r_theader;	/* trans header for partial */
	int			r_state;	/* not needed */
	xfs_lsn_t		r_lsn;		/* xact lsn */
	struct list_head	r_itemq;	/* q for items */
};

#define ITEM_TYPE(i)	(*(unsigned short *)(i)->ri_buf[0].i_addr)

/*
 * This is the number of entries in the l_buf_cancel_table used during
 * recovery.
 */
#define	XLOG_BC_TABLE_SIZE	64

#define	XLOG_RECOVER_CRCPASS	0
#define	XLOG_RECOVER_PASS1	1
#define	XLOG_RECOVER_PASS2	2

void xlog_buf_readahead(struct xlog *log, xfs_daddr_t blkno, uint len,
		const struct xfs_buf_ops *ops);
bool xlog_is_buffer_cancelled(struct xlog *log, xfs_daddr_t blkno, uint len);

void xlog_recover_release_intent(struct xlog *log, unsigned short intent_type,
		uint64_t intent_id);

#endif	/* __XFS_LOG_RECOVER_H__ */
