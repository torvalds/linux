/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2018 by Delphix. All rights reserved.
 * Copyright (c) 2014 Spectra Logic Corporation, All rights reserved.
 */

#ifndef	_SYS_DNODE_H
#define	_SYS_DNODE_H

#include <sys/zfs_context.h>
#include <sys/avl.h>
#include <sys/spa.h>
#include <sys/txg.h>
#include <sys/zio.h>
#include <sys/refcount.h>
#include <sys/dmu_zfetch.h>
#include <sys/zrlock.h>
#include <sys/multilist.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * dnode_hold() flags.
 */
#define	DNODE_MUST_BE_ALLOCATED	1
#define	DNODE_MUST_BE_FREE	2

/*
 * dnode_next_offset() flags.
 */
#define	DNODE_FIND_HOLE		1
#define	DNODE_FIND_BACKWARDS	2
#define	DNODE_FIND_HAVELOCK	4

/*
 * Fixed constants.
 */
#define	DNODE_SHIFT		9	/* 512 bytes */
#define	DN_MIN_INDBLKSHIFT	12	/* 4k */
/*
 * If we ever increase this value beyond 20, we need to revisit all logic that
 * does x << level * ebps to handle overflow.  With a 1M indirect block size,
 * 4 levels of indirect blocks would not be able to guarantee addressing an
 * entire object, so 5 levels will be used, but 5 * (20 - 7) = 65.
 */
#define	DN_MAX_INDBLKSHIFT	17	/* 128k */
#define	DNODE_BLOCK_SHIFT	14	/* 16k */
#define	DNODE_CORE_SIZE		64	/* 64 bytes for dnode sans blkptrs */
#define	DN_MAX_OBJECT_SHIFT	48	/* 256 trillion (zfs_fid_t limit) */
#define	DN_MAX_OFFSET_SHIFT	64	/* 2^64 bytes in a dnode */

/*
 * dnode id flags
 *
 * Note: a file will never ever have its
 * ids moved from bonus->spill
 * and only in a crypto environment would it be on spill
 */
#define	DN_ID_CHKED_BONUS	0x1
#define	DN_ID_CHKED_SPILL	0x2
#define	DN_ID_OLD_EXIST		0x4
#define	DN_ID_NEW_EXIST		0x8

/*
 * Derived constants.
 */
#define	DNODE_MIN_SIZE		(1 << DNODE_SHIFT)
#define	DNODE_MAX_SIZE		(1 << DNODE_BLOCK_SHIFT)
#define	DNODE_BLOCK_SIZE	(1 << DNODE_BLOCK_SHIFT)
#define	DNODE_MIN_SLOTS		(DNODE_MIN_SIZE >> DNODE_SHIFT)
#define	DNODE_MAX_SLOTS		(DNODE_MAX_SIZE >> DNODE_SHIFT)
#define	DN_BONUS_SIZE(dnsize)	((dnsize) - DNODE_CORE_SIZE - \
	(1 << SPA_BLKPTRSHIFT))
#define	DN_SLOTS_TO_BONUSLEN(slots)	DN_BONUS_SIZE((slots) << DNODE_SHIFT)
#define	DN_OLD_MAX_BONUSLEN	(DN_BONUS_SIZE(DNODE_MIN_SIZE))
#define	DN_MAX_NBLKPTR	((DNODE_MIN_SIZE - DNODE_CORE_SIZE) >> SPA_BLKPTRSHIFT)
#define	DN_MAX_OBJECT	(1ULL << DN_MAX_OBJECT_SHIFT)
#define	DN_ZERO_BONUSLEN	(DN_BONUS_SIZE(DNODE_MAX_SIZE) + 1)
#define	DN_KILL_SPILLBLK (1)

#define	DNODES_PER_BLOCK_SHIFT	(DNODE_BLOCK_SHIFT - DNODE_SHIFT)
#define	DNODES_PER_BLOCK	(1ULL << DNODES_PER_BLOCK_SHIFT)

/*
 * This is inaccurate if the indblkshift of the particular object is not the
 * max.  But it's only used by userland to calculate the zvol reservation.
 */
#define	DNODES_PER_LEVEL_SHIFT	(DN_MAX_INDBLKSHIFT - SPA_BLKPTRSHIFT)
#define	DNODES_PER_LEVEL	(1ULL << DNODES_PER_LEVEL_SHIFT)

/* The +2 here is a cheesy way to round up */
#define	DN_MAX_LEVELS	(2 + ((DN_MAX_OFFSET_SHIFT - SPA_MINBLOCKSHIFT) / \
	(DN_MIN_INDBLKSHIFT - SPA_BLKPTRSHIFT)))

#define	DN_BONUS(dnp)	((void*)((dnp)->dn_bonus + \
	(((dnp)->dn_nblkptr - 1) * sizeof (blkptr_t))))
#define	DN_MAX_BONUS_LEN(dnp) \
	((dnp->dn_flags & DNODE_FLAG_SPILL_BLKPTR) ? \
	(uint8_t *)DN_SPILL_BLKPTR(dnp) - (uint8_t *)DN_BONUS(dnp) : \
	(uint8_t *)(dnp + (dnp->dn_extra_slots + 1)) - (uint8_t *)DN_BONUS(dnp))
	
#define	DN_USED_BYTES(dnp) (((dnp)->dn_flags & DNODE_FLAG_USED_BYTES) ? \
	(dnp)->dn_used : (dnp)->dn_used << SPA_MINBLOCKSHIFT)

#define	EPB(blkshift, typeshift)	(1 << (blkshift - typeshift))

struct dmu_buf_impl;
struct objset;
struct zio;

enum dnode_dirtycontext {
	DN_UNDIRTIED,
	DN_DIRTY_OPEN,
	DN_DIRTY_SYNC
};

/* Is dn_used in bytes?  if not, it's in multiples of SPA_MINBLOCKSIZE */
#define	DNODE_FLAG_USED_BYTES		(1<<0)
#define	DNODE_FLAG_USERUSED_ACCOUNTED	(1<<1)

/* Does dnode have a SA spill blkptr in bonus? */
#define	DNODE_FLAG_SPILL_BLKPTR	(1<<2)

typedef struct dnode_phys {
	uint8_t dn_type;		/* dmu_object_type_t */
	uint8_t dn_indblkshift;		/* ln2(indirect block size) */
	uint8_t dn_nlevels;		/* 1=dn_blkptr->data blocks */
	uint8_t dn_nblkptr;		/* length of dn_blkptr */
	uint8_t dn_bonustype;		/* type of data in bonus buffer */
	uint8_t	dn_checksum;		/* ZIO_CHECKSUM type */
	uint8_t	dn_compress;		/* ZIO_COMPRESS type */
	uint8_t dn_flags;		/* DNODE_FLAG_* */
	uint16_t dn_datablkszsec;	/* data block size in 512b sectors */
	uint16_t dn_bonuslen;		/* length of dn_bonus */
	uint8_t dn_extra_slots;		/* # of subsequent slots consumed */
	uint8_t dn_pad2[3];

	/* accounting is protected by dn_dirty_mtx */
	uint64_t dn_maxblkid;		/* largest allocated block ID */
	uint64_t dn_used;		/* bytes (or sectors) of disk space */

	/*
	 * Both dn_pad2 and dn_pad3 are protected by the block's MAC. This
	 * allows us to protect any fields that might be added here in the
	 * future. In either case, developers will want to check
	 * zio_crypt_init_uios_dnode() to ensure the new field is being
	 * protected properly.
	 */
	uint64_t dn_pad3[4];
	/*
	 * The tail region is 448 bytes for a 512 byte dnode, and
	 * correspondingly larger for larger dnode sizes. The spill
	 * block pointer, when present, is always at the end of the tail
	 * region. There are three ways this space may be used, using
	 * a 512 byte dnode for this diagram:
	 *
	 * 0       64      128     192     256     320     384     448 (offset)
	 * +---------------+---------------+---------------+-------+
	 * | dn_blkptr[0]  | dn_blkptr[1]  | dn_blkptr[2]  | /     |
	 * +---------------+---------------+---------------+-------+
	 * | dn_blkptr[0]  | dn_bonus[0..319]                      |
	 * +---------------+-----------------------+---------------+
	 * | dn_blkptr[0]  | dn_bonus[0..191]      | dn_spill      |
	 * +---------------+-----------------------+---------------+
	 */
	union {
		blkptr_t dn_blkptr[1+DN_OLD_MAX_BONUSLEN/sizeof (blkptr_t)];
		struct {
			blkptr_t __dn_ignore1;
			uint8_t dn_bonus[DN_OLD_MAX_BONUSLEN];
		};
		struct {
			blkptr_t __dn_ignore2;
			uint8_t __dn_ignore3[DN_OLD_MAX_BONUSLEN -
			    sizeof (blkptr_t)];
			blkptr_t dn_spill;
 		};
	};
} dnode_phys_t;

#define	DN_SPILL_BLKPTR(dnp)	(blkptr_t *)((char *)(dnp) + \
	(((dnp)->dn_extra_slots + 1) << DNODE_SHIFT) - (1 << SPA_BLKPTRSHIFT))

struct dnode {
	/*
	 * Protects the structure of the dnode, including the number of levels
	 * of indirection (dn_nlevels), dn_maxblkid, and dn_next_*
	 */
	krwlock_t dn_struct_rwlock;

	/* Our link on dn_objset->os_dnodes list; protected by os_lock.  */
	list_node_t dn_link;

	/* immutable: */
	struct objset *dn_objset;
	uint64_t dn_object;
	struct dmu_buf_impl *dn_dbuf;
	struct dnode_handle *dn_handle;
	dnode_phys_t *dn_phys; /* pointer into dn->dn_dbuf->db.db_data */

	/*
	 * Copies of stuff in dn_phys.  They're valid in the open
	 * context (eg. even before the dnode is first synced).
	 * Where necessary, these are protected by dn_struct_rwlock.
	 */
	dmu_object_type_t dn_type;	/* object type */
	uint16_t dn_bonuslen;		/* bonus length */
	uint8_t dn_bonustype;		/* bonus type */
	uint8_t dn_nblkptr;		/* number of blkptrs (immutable) */
	uint8_t dn_checksum;		/* ZIO_CHECKSUM type */
	uint8_t dn_compress;		/* ZIO_COMPRESS type */
	uint8_t dn_nlevels;
	uint8_t dn_indblkshift;
	uint8_t dn_datablkshift;	/* zero if blksz not power of 2! */
	uint8_t dn_moved;		/* Has this dnode been moved? */
	uint16_t dn_datablkszsec;	/* in 512b sectors */
	uint32_t dn_datablksz;		/* in bytes */
	uint64_t dn_maxblkid;
	uint8_t dn_next_type[TXG_SIZE];
	uint8_t dn_num_slots;		/* metadnode slots consumed on disk */
	uint8_t dn_next_nblkptr[TXG_SIZE];
	uint8_t dn_next_nlevels[TXG_SIZE];
	uint8_t dn_next_indblkshift[TXG_SIZE];
	uint8_t dn_next_bonustype[TXG_SIZE];
	uint8_t dn_rm_spillblk[TXG_SIZE];	/* for removing spill blk */
	uint16_t dn_next_bonuslen[TXG_SIZE];
	uint32_t dn_next_blksz[TXG_SIZE];	/* next block size in bytes */

	/* protected by dn_dbufs_mtx; declared here to fill 32-bit hole */
	uint32_t dn_dbufs_count;	/* count of dn_dbufs */

	/* protected by os_lock: */
	multilist_node_t dn_dirty_link[TXG_SIZE]; /* next on dataset's dirty */

	/* protected by dn_mtx: */
	kmutex_t dn_mtx;
	list_t dn_dirty_records[TXG_SIZE];
	struct range_tree *dn_free_ranges[TXG_SIZE];
	uint64_t dn_allocated_txg;
	uint64_t dn_free_txg;
	uint64_t dn_assigned_txg;
	kcondvar_t dn_notxholds;
	enum dnode_dirtycontext dn_dirtyctx;
	uint8_t *dn_dirtyctx_firstset;		/* dbg: contents meaningless */

	/* protected by own devices */
	refcount_t dn_tx_holds;
	refcount_t dn_holds;

	kmutex_t dn_dbufs_mtx;
	/*
	 * Descendent dbufs, ordered by dbuf_compare. Note that dn_dbufs
	 * can contain multiple dbufs of the same (level, blkid) when a
	 * dbuf is marked DB_EVICTING without being removed from
	 * dn_dbufs. To maintain the avl invariant that there cannot be
	 * duplicate entries, we order the dbufs by an arbitrary value -
	 * their address in memory. This means that dn_dbufs cannot be used to
	 * directly look up a dbuf. Instead, callers must use avl_walk, have
	 * a reference to the dbuf, or look up a non-existant node with
	 * db_state = DB_SEARCH (see dbuf_free_range for an example).
	 */
	avl_tree_t dn_dbufs;

	/* protected by dn_struct_rwlock */
	struct dmu_buf_impl *dn_bonus;	/* bonus buffer dbuf */

	boolean_t dn_have_spill;	/* have spill or are spilling */

	/* parent IO for current sync write */
	zio_t *dn_zio;

	/* used in syncing context */
	uint64_t dn_oldused;	/* old phys used bytes */
	uint64_t dn_oldflags;	/* old phys dn_flags */
	uint64_t dn_olduid, dn_oldgid;
	uint64_t dn_newuid, dn_newgid;
	int dn_id_flags;

	/* holds prefetch structure */
	struct zfetch	dn_zfetch;
};

/*
 * Adds a level of indirection between the dbuf and the dnode to avoid
 * iterating descendent dbufs in dnode_move(). Handles are not allocated
 * individually, but as an array of child dnodes in dnode_hold_impl().
 */
typedef struct dnode_handle {
	/* Protects dnh_dnode from modification by dnode_move(). */
	zrlock_t dnh_zrlock;
	dnode_t *dnh_dnode;
} dnode_handle_t;

typedef struct dnode_children {
	dmu_buf_user_t dnc_dbu;		/* User evict data */
	size_t dnc_count;		/* number of children */
	dnode_handle_t dnc_children[];	/* sized dynamically */
} dnode_children_t;

typedef struct free_range {
	avl_node_t fr_node;
	uint64_t fr_blkid;
	uint64_t fr_nblks;
} free_range_t;

void dnode_special_open(struct objset *dd, dnode_phys_t *dnp,
    uint64_t object, dnode_handle_t *dnh);
void dnode_special_close(dnode_handle_t *dnh);

void dnode_setbonuslen(dnode_t *dn, int newsize, dmu_tx_t *tx);
void dnode_setbonus_type(dnode_t *dn, dmu_object_type_t, dmu_tx_t *tx);
void dnode_rm_spill(dnode_t *dn, dmu_tx_t *tx);

int dnode_hold(struct objset *dd, uint64_t object,
    void *ref, dnode_t **dnp);
int dnode_hold_impl(struct objset *dd, uint64_t object, int flag, int dn_slots,
    void *ref, dnode_t **dnp);
boolean_t dnode_add_ref(dnode_t *dn, void *ref);
void dnode_rele(dnode_t *dn, void *ref);
void dnode_rele_and_unlock(dnode_t *dn, void *tag, boolean_t evicting);
void dnode_setdirty(dnode_t *dn, dmu_tx_t *tx);
void dnode_sync(dnode_t *dn, dmu_tx_t *tx);
void dnode_allocate(dnode_t *dn, dmu_object_type_t ot, int blocksize, int ibs,
    dmu_object_type_t bonustype, int bonuslen, int dn_slots, dmu_tx_t *tx);
void dnode_reallocate(dnode_t *dn, dmu_object_type_t ot, int blocksize,
    dmu_object_type_t bonustype, int bonuslen, int dn_slots, dmu_tx_t *tx);
void dnode_free(dnode_t *dn, dmu_tx_t *tx);
void dnode_byteswap(dnode_phys_t *dnp);
void dnode_buf_byteswap(void *buf, size_t size);
void dnode_verify(dnode_t *dn);
int dnode_set_blksz(dnode_t *dn, uint64_t size, int ibs, dmu_tx_t *tx);
void dnode_free_range(dnode_t *dn, uint64_t off, uint64_t len, dmu_tx_t *tx);
void dnode_diduse_space(dnode_t *dn, int64_t space);
void dnode_new_blkid(dnode_t *dn, uint64_t blkid, dmu_tx_t *tx, boolean_t);
uint64_t dnode_block_freed(dnode_t *dn, uint64_t blkid);
void dnode_init(void);
void dnode_fini(void);
int dnode_next_offset(dnode_t *dn, int flags, uint64_t *off,
    int minlvl, uint64_t blkfill, uint64_t txg);
void dnode_evict_dbufs(dnode_t *dn);
void dnode_evict_bonus(dnode_t *dn);
boolean_t dnode_needs_remap(const dnode_t *dn);

#define	DNODE_IS_CACHEABLE(_dn)						\
	((_dn)->dn_objset->os_primary_cache == ZFS_CACHE_ALL ||		\
	(DMU_OT_IS_METADATA((_dn)->dn_type) &&				\
	(_dn)->dn_objset->os_primary_cache == ZFS_CACHE_METADATA))

#define	DNODE_META_IS_CACHEABLE(_dn)					\
	((_dn)->dn_objset->os_primary_cache == ZFS_CACHE_ALL ||		\
	(_dn)->dn_objset->os_primary_cache == ZFS_CACHE_METADATA)

#ifdef ZFS_DEBUG

/*
 * There should be a ## between the string literal and fmt, to make it
 * clear that we're joining two strings together, but that piece of shit
 * gcc doesn't support that preprocessor token.
 */
#define	dprintf_dnode(dn, fmt, ...) do { \
	if (zfs_flags & ZFS_DEBUG_DPRINTF) { \
	char __db_buf[32]; \
	uint64_t __db_obj = (dn)->dn_object; \
	if (__db_obj == DMU_META_DNODE_OBJECT) \
		(void) strcpy(__db_buf, "mdn"); \
	else \
		(void) snprintf(__db_buf, sizeof (__db_buf), "%lld", \
		    (u_longlong_t)__db_obj);\
	dprintf_ds((dn)->dn_objset->os_dsl_dataset, "obj=%s " fmt, \
	    __db_buf, __VA_ARGS__); \
	} \
_NOTE(CONSTCOND) } while (0)

#define	DNODE_VERIFY(dn)		dnode_verify(dn)
#define	FREE_VERIFY(db, start, end, tx)	free_verify(db, start, end, tx)

#else

#define	dprintf_dnode(db, fmt, ...)
#define	DNODE_VERIFY(dn)
#define	FREE_VERIFY(db, start, end, tx)

#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_DNODE_H */
