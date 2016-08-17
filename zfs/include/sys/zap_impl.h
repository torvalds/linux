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
 * Copyright (c) 2014 Spectra Logic Corporation, All rights reserved.
 * Copyright (c) 2013, 2016 by Delphix. All rights reserved.
 * Copyright 2017 Nexenta Systems, Inc.
 */

#ifndef	_SYS_ZAP_IMPL_H
#define	_SYS_ZAP_IMPL_H

#include <sys/zap.h>
#include <sys/zfs_context.h>
#include <sys/avl.h>

#ifdef	__cplusplus
extern "C" {
#endif

extern int fzap_default_block_shift;

#define	ZAP_MAGIC 0x2F52AB2ABULL

#define	FZAP_BLOCK_SHIFT(zap)	((zap)->zap_f.zap_block_shift)

#define	MZAP_ENT_LEN		64
#define	MZAP_NAME_LEN		(MZAP_ENT_LEN - 8 - 4 - 2)
#define	MZAP_MAX_BLKSZ		SPA_OLD_MAXBLOCKSIZE

#define	ZAP_NEED_CD		(-1U)

typedef struct mzap_ent_phys {
	uint64_t mze_value;
	uint32_t mze_cd;
	uint16_t mze_pad;	/* in case we want to chain them someday */
	char mze_name[MZAP_NAME_LEN];
} mzap_ent_phys_t;

typedef struct mzap_phys {
	uint64_t mz_block_type;	/* ZBT_MICRO */
	uint64_t mz_salt;
	uint64_t mz_normflags;
	uint64_t mz_pad[5];
	mzap_ent_phys_t mz_chunk[1];
	/* actually variable size depending on block size */
} mzap_phys_t;

typedef struct mzap_ent {
	avl_node_t mze_node;
	int mze_chunkid;
	uint64_t mze_hash;
	uint32_t mze_cd; /* copy from mze_phys->mze_cd */
} mzap_ent_t;

#define	MZE_PHYS(zap, mze) \
	(&zap_m_phys(zap)->mz_chunk[(mze)->mze_chunkid])

/*
 * The (fat) zap is stored in one object. It is an array of
 * 1<<FZAP_BLOCK_SHIFT byte blocks. The layout looks like one of:
 *
 * ptrtbl fits in first block:
 * 	[zap_phys_t zap_ptrtbl_shift < 6] [zap_leaf_t] ...
 *
 * ptrtbl too big for first block:
 * 	[zap_phys_t zap_ptrtbl_shift >= 6] [zap_leaf_t] [ptrtbl] ...
 *
 */

struct dmu_buf;
struct zap_leaf;

#define	ZBT_LEAF		((1ULL << 63) + 0)
#define	ZBT_HEADER		((1ULL << 63) + 1)
#define	ZBT_MICRO		((1ULL << 63) + 3)
/* any other values are ptrtbl blocks */

/*
 * the embedded pointer table takes up half a block:
 * block size / entry size (2^3) / 2
 */
#define	ZAP_EMBEDDED_PTRTBL_SHIFT(zap) (FZAP_BLOCK_SHIFT(zap) - 3 - 1)

/*
 * The embedded pointer table starts half-way through the block.  Since
 * the pointer table itself is half the block, it starts at (64-bit)
 * word number (1<<ZAP_EMBEDDED_PTRTBL_SHIFT(zap)).
 */
#define	ZAP_EMBEDDED_PTRTBL_ENT(zap, idx) \
	((uint64_t *)zap_f_phys(zap)) \
	[(idx) + (1<<ZAP_EMBEDDED_PTRTBL_SHIFT(zap))]

/*
 * TAKE NOTE:
 * If zap_phys_t is modified, zap_byteswap() must be modified.
 */
typedef struct zap_phys {
	uint64_t zap_block_type;	/* ZBT_HEADER */
	uint64_t zap_magic;		/* ZAP_MAGIC */

	struct zap_table_phys {
		uint64_t zt_blk;	/* starting block number */
		uint64_t zt_numblks;	/* number of blocks */
		uint64_t zt_shift;	/* bits to index it */
		uint64_t zt_nextblk;	/* next (larger) copy start block */
		uint64_t zt_blks_copied; /* number source blocks copied */
	} zap_ptrtbl;

	uint64_t zap_freeblk;		/* the next free block */
	uint64_t zap_num_leafs;		/* number of leafs */
	uint64_t zap_num_entries;	/* number of entries */
	uint64_t zap_salt;		/* salt to stir into hash function */
	uint64_t zap_normflags;		/* flags for u8_textprep_str() */
	uint64_t zap_flags;		/* zap_flags_t */
	/*
	 * This structure is followed by padding, and then the embedded
	 * pointer table.  The embedded pointer table takes up second
	 * half of the block.  It is accessed using the
	 * ZAP_EMBEDDED_PTRTBL_ENT() macro.
	 */
} zap_phys_t;

typedef struct zap_table_phys zap_table_phys_t;

typedef struct zap {
	dmu_buf_user_t zap_dbu;
	objset_t *zap_objset;
	uint64_t zap_object;
	struct dmu_buf *zap_dbuf;
	krwlock_t zap_rwlock;
	boolean_t zap_ismicro;
	int zap_normflags;
	uint64_t zap_salt;
	union {
		struct {
			/*
			 * zap_num_entries_mtx protects
			 * zap_num_entries
			 */
			kmutex_t zap_num_entries_mtx;
			int zap_block_shift;
		} zap_fat;
		struct {
			int16_t zap_num_entries;
			int16_t zap_num_chunks;
			int16_t zap_alloc_next;
			avl_tree_t zap_avl;
		} zap_micro;
	} zap_u;
} zap_t;

static inline zap_phys_t *
zap_f_phys(zap_t *zap)
{
	return (zap->zap_dbuf->db_data);
}

static inline mzap_phys_t *
zap_m_phys(zap_t *zap)
{
	return (zap->zap_dbuf->db_data);
}

typedef struct zap_name {
	zap_t *zn_zap;
	int zn_key_intlen;
	const void *zn_key_orig;
	int zn_key_orig_numints;
	const void *zn_key_norm;
	int zn_key_norm_numints;
	uint64_t zn_hash;
	matchtype_t zn_matchtype;
	int zn_normflags;
	char zn_normbuf[ZAP_MAXNAMELEN];
} zap_name_t;

#define	zap_f	zap_u.zap_fat
#define	zap_m	zap_u.zap_micro

boolean_t zap_match(zap_name_t *zn, const char *matchname);
int zap_lockdir(objset_t *os, uint64_t obj, dmu_tx_t *tx,
    krw_t lti, boolean_t fatreader, boolean_t adding, void *tag, zap_t **zapp);
void zap_unlockdir(zap_t *zap, void *tag);
void zap_evict_sync(void *dbu);
zap_name_t *zap_name_alloc(zap_t *zap, const char *key, matchtype_t mt);
void zap_name_free(zap_name_t *zn);
int zap_hashbits(zap_t *zap);
uint32_t zap_maxcd(zap_t *zap);
uint64_t zap_getflags(zap_t *zap);

#define	ZAP_HASH_IDX(hash, n) (((n) == 0) ? 0 : ((hash) >> (64 - (n))))

void fzap_byteswap(void *buf, size_t size);
int fzap_count(zap_t *zap, uint64_t *count);
int fzap_lookup(zap_name_t *zn,
    uint64_t integer_size, uint64_t num_integers, void *buf,
    char *realname, int rn_len, boolean_t *normalization_conflictp);
void fzap_prefetch(zap_name_t *zn);
int fzap_add(zap_name_t *zn, uint64_t integer_size, uint64_t num_integers,
    const void *val, void *tag, dmu_tx_t *tx);
int fzap_update(zap_name_t *zn,
    int integer_size, uint64_t num_integers, const void *val,
    void *tag, dmu_tx_t *tx);
int fzap_length(zap_name_t *zn,
    uint64_t *integer_size, uint64_t *num_integers);
int fzap_remove(zap_name_t *zn, dmu_tx_t *tx);
int fzap_cursor_retrieve(zap_t *zap, zap_cursor_t *zc, zap_attribute_t *za);
void fzap_get_stats(zap_t *zap, zap_stats_t *zs);
void zap_put_leaf(struct zap_leaf *l);

int fzap_add_cd(zap_name_t *zn,
    uint64_t integer_size, uint64_t num_integers,
    const void *val, uint32_t cd, void *tag, dmu_tx_t *tx);
void fzap_upgrade(zap_t *zap, dmu_tx_t *tx, zap_flags_t flags);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_ZAP_IMPL_H */
