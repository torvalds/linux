// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2006 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#ifndef XFS_SYNC_H
#define XFS_SYNC_H 1

struct xfs_mount;
struct xfs_perag;

struct xfs_eofblocks {
	__u32		eof_flags;
	kuid_t		eof_uid;
	kgid_t		eof_gid;
	prid_t		eof_prid;
	__u64		eof_min_file_size;
};

#define SYNC_WAIT		0x0001	/* wait for i/o to complete */
#define SYNC_TRYLOCK		0x0002  /* only try to lock iyesdes */

/*
 * tags for iyesde radix tree
 */
#define XFS_ICI_NO_TAG		(-1)	/* special flag for an untagged lookup
					   in xfs_iyesde_ag_iterator */
#define XFS_ICI_RECLAIM_TAG	0	/* iyesde is to be reclaimed */
#define XFS_ICI_EOFBLOCKS_TAG	1	/* iyesde has blocks beyond EOF */
#define XFS_ICI_COWBLOCKS_TAG	2	/* iyesde can have cow blocks to gc */

/*
 * Flags for xfs_iget()
 */
#define XFS_IGET_CREATE		0x1
#define XFS_IGET_UNTRUSTED	0x2
#define XFS_IGET_DONTCACHE	0x4
#define XFS_IGET_INCORE		0x8	/* don't read from disk or reinit */

/*
 * flags for AG iyesde iterator
 */
#define XFS_AGITER_INEW_WAIT	0x1	/* wait on new iyesdes */

int xfs_iget(struct xfs_mount *mp, struct xfs_trans *tp, xfs_iyes_t iyes,
	     uint flags, uint lock_flags, xfs_iyesde_t **ipp);

/* recovery needs direct iyesde allocation capability */
struct xfs_iyesde * xfs_iyesde_alloc(struct xfs_mount *mp, xfs_iyes_t iyes);
void xfs_iyesde_free(struct xfs_iyesde *ip);

void xfs_reclaim_worker(struct work_struct *work);

int xfs_reclaim_iyesdes(struct xfs_mount *mp, int mode);
int xfs_reclaim_iyesdes_count(struct xfs_mount *mp);
long xfs_reclaim_iyesdes_nr(struct xfs_mount *mp, int nr_to_scan);

void xfs_iyesde_set_reclaim_tag(struct xfs_iyesde *ip);

void xfs_iyesde_set_eofblocks_tag(struct xfs_iyesde *ip);
void xfs_iyesde_clear_eofblocks_tag(struct xfs_iyesde *ip);
int xfs_icache_free_eofblocks(struct xfs_mount *, struct xfs_eofblocks *);
int xfs_iyesde_free_quota_eofblocks(struct xfs_iyesde *ip);
void xfs_eofblocks_worker(struct work_struct *);
void xfs_queue_eofblocks(struct xfs_mount *);

void xfs_iyesde_set_cowblocks_tag(struct xfs_iyesde *ip);
void xfs_iyesde_clear_cowblocks_tag(struct xfs_iyesde *ip);
int xfs_icache_free_cowblocks(struct xfs_mount *, struct xfs_eofblocks *);
int xfs_iyesde_free_quota_cowblocks(struct xfs_iyesde *ip);
void xfs_cowblocks_worker(struct work_struct *);
void xfs_queue_cowblocks(struct xfs_mount *);

int xfs_iyesde_ag_iterator(struct xfs_mount *mp,
	int (*execute)(struct xfs_iyesde *ip, int flags, void *args),
	int flags, void *args);
int xfs_iyesde_ag_iterator_flags(struct xfs_mount *mp,
	int (*execute)(struct xfs_iyesde *ip, int flags, void *args),
	int flags, void *args, int iter_flags);
int xfs_iyesde_ag_iterator_tag(struct xfs_mount *mp,
	int (*execute)(struct xfs_iyesde *ip, int flags, void *args),
	int flags, void *args, int tag);

static inline int
xfs_fs_eofblocks_from_user(
	struct xfs_fs_eofblocks		*src,
	struct xfs_eofblocks		*dst)
{
	if (src->eof_version != XFS_EOFBLOCKS_VERSION)
		return -EINVAL;

	if (src->eof_flags & ~XFS_EOF_FLAGS_VALID)
		return -EINVAL;

	if (memchr_inv(&src->pad32, 0, sizeof(src->pad32)) ||
	    memchr_inv(src->pad64, 0, sizeof(src->pad64)))
		return -EINVAL;

	dst->eof_flags = src->eof_flags;
	dst->eof_prid = src->eof_prid;
	dst->eof_min_file_size = src->eof_min_file_size;

	dst->eof_uid = INVALID_UID;
	if (src->eof_flags & XFS_EOF_FLAGS_UID) {
		dst->eof_uid = make_kuid(current_user_ns(), src->eof_uid);
		if (!uid_valid(dst->eof_uid))
			return -EINVAL;
	}

	dst->eof_gid = INVALID_GID;
	if (src->eof_flags & XFS_EOF_FLAGS_GID) {
		dst->eof_gid = make_kgid(current_user_ns(), src->eof_gid);
		if (!gid_valid(dst->eof_gid))
			return -EINVAL;
	}
	return 0;
}

int xfs_icache_iyesde_is_allocated(struct xfs_mount *mp, struct xfs_trans *tp,
				  xfs_iyes_t iyes, bool *inuse);

void xfs_stop_block_reaping(struct xfs_mount *mp);
void xfs_start_block_reaping(struct xfs_mount *mp);

#endif
