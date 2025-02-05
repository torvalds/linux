/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2020-2024 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#ifndef __XFS_EXCHMAPS_H__
#define __XFS_EXCHMAPS_H__

/* In-core deferred operation info about a file mapping exchange request. */
struct xfs_exchmaps_intent {
	/* List of other incore deferred work. */
	struct list_head	xmi_list;

	/* Inodes participating in the operation. */
	struct xfs_inode	*xmi_ip1;
	struct xfs_inode	*xmi_ip2;

	/* File offset range information. */
	xfs_fileoff_t		xmi_startoff1;
	xfs_fileoff_t		xmi_startoff2;
	xfs_filblks_t		xmi_blockcount;

	/* Set these file sizes after the operation, unless negative. */
	xfs_fsize_t		xmi_isize1;
	xfs_fsize_t		xmi_isize2;

	uint64_t		xmi_flags;	/* XFS_EXCHMAPS_* flags */
};

/* Try to convert inode2 from block to short format at the end, if possible. */
#define __XFS_EXCHMAPS_INO2_SHORTFORM	(1ULL << 63)

#define XFS_EXCHMAPS_INTERNAL_FLAGS	(__XFS_EXCHMAPS_INO2_SHORTFORM)

/* flags that can be passed to xfs_exchmaps_{estimate,mappings} */
#define XFS_EXCHMAPS_PARAMS		(XFS_EXCHMAPS_ATTR_FORK | \
					 XFS_EXCHMAPS_SET_SIZES | \
					 XFS_EXCHMAPS_INO1_WRITTEN)

static inline int
xfs_exchmaps_whichfork(const struct xfs_exchmaps_intent *xmi)
{
	if (xmi->xmi_flags & XFS_EXCHMAPS_ATTR_FORK)
		return XFS_ATTR_FORK;
	return XFS_DATA_FORK;
}

/* Parameters for a mapping exchange request. */
struct xfs_exchmaps_req {
	/* Inodes participating in the operation. */
	struct xfs_inode	*ip1;
	struct xfs_inode	*ip2;

	/* File offset range information. */
	xfs_fileoff_t		startoff1;
	xfs_fileoff_t		startoff2;
	xfs_filblks_t		blockcount;

	/* XFS_EXCHMAPS_* operation flags */
	uint64_t		flags;

	/*
	 * Fields below this line are filled out by xfs_exchmaps_estimate;
	 * callers should initialize this part of the struct to zero.
	 */

	/*
	 * Data device blocks to be moved out of ip1, and free space needed to
	 * handle the bmbt changes.
	 */
	xfs_filblks_t		ip1_bcount;

	/*
	 * Data device blocks to be moved out of ip2, and free space needed to
	 * handle the bmbt changes.
	 */
	xfs_filblks_t		ip2_bcount;

	/* rt blocks to be moved out of ip1. */
	xfs_filblks_t		ip1_rtbcount;

	/* rt blocks to be moved out of ip2. */
	xfs_filblks_t		ip2_rtbcount;

	/* Free space needed to handle the bmbt changes */
	unsigned long long	resblks;

	/* Number of exchanges needed to complete the operation */
	unsigned long long	nr_exchanges;
};

static inline int
xfs_exchmaps_reqfork(const struct xfs_exchmaps_req *req)
{
	if (req->flags & XFS_EXCHMAPS_ATTR_FORK)
		return XFS_ATTR_FORK;
	return XFS_DATA_FORK;
}

int xfs_exchmaps_estimate_overhead(struct xfs_exchmaps_req *req);
int xfs_exchmaps_estimate(struct xfs_exchmaps_req *req);

extern struct kmem_cache	*xfs_exchmaps_intent_cache;

int __init xfs_exchmaps_intent_init_cache(void);
void xfs_exchmaps_intent_destroy_cache(void);

struct xfs_exchmaps_intent *xfs_exchmaps_init_intent(
		const struct xfs_exchmaps_req *req);
void xfs_exchmaps_ensure_reflink(struct xfs_trans *tp,
		const struct xfs_exchmaps_intent *xmi);
void xfs_exchmaps_upgrade_extent_counts(struct xfs_trans *tp,
		const struct xfs_exchmaps_intent *xmi);

int xfs_exchmaps_finish_one(struct xfs_trans *tp,
		struct xfs_exchmaps_intent *xmi);

int xfs_exchmaps_check_forks(struct xfs_mount *mp,
		const struct xfs_exchmaps_req *req);

void xfs_exchange_mappings(struct xfs_trans *tp,
		const struct xfs_exchmaps_req *req);

#endif /* __XFS_EXCHMAPS_H__ */
