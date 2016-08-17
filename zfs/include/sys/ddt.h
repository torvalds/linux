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
 * Copyright (c) 2009, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2016 by Delphix. All rights reserved.
 */

#ifndef _SYS_DDT_H
#define	_SYS_DDT_H

#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/fs/zfs.h>
#include <sys/zio.h>
#include <sys/dmu.h>

#ifdef	__cplusplus
extern "C" {
#endif

struct abd;

/*
 * On-disk DDT formats, in the desired search order (newest version first).
 */
enum ddt_type {
	DDT_TYPE_ZAP = 0,
	DDT_TYPES
};

/*
 * DDT classes, in the desired search order (highest replication level first).
 */
enum ddt_class {
	DDT_CLASS_DITTO = 0,
	DDT_CLASS_DUPLICATE,
	DDT_CLASS_UNIQUE,
	DDT_CLASSES
};

#define	DDT_TYPE_CURRENT		0

#define	DDT_COMPRESS_BYTEORDER_MASK	0x80
#define	DDT_COMPRESS_FUNCTION_MASK	0x7f

/*
 * On-disk ddt entry:  key (name) and physical storage (value).
 */
typedef struct ddt_key {
	zio_cksum_t	ddk_cksum;	/* 256-bit block checksum */
	/*
	 * Encoded with logical & physical size, and compression, as follows:
	 *   +-------+-------+-------+-------+-------+-------+-------+-------+
	 *   |   0   |   0   |   0   | comp  |     PSIZE     |     LSIZE     |
	 *   +-------+-------+-------+-------+-------+-------+-------+-------+
	 */
	uint64_t	ddk_prop;
} ddt_key_t;

#define	DDK_GET_LSIZE(ddk)	\
	BF64_GET_SB((ddk)->ddk_prop, 0, 16, SPA_MINBLOCKSHIFT, 1)
#define	DDK_SET_LSIZE(ddk, x)	\
	BF64_SET_SB((ddk)->ddk_prop, 0, 16, SPA_MINBLOCKSHIFT, 1, x)

#define	DDK_GET_PSIZE(ddk)	\
	BF64_GET_SB((ddk)->ddk_prop, 16, 16, SPA_MINBLOCKSHIFT, 1)
#define	DDK_SET_PSIZE(ddk, x)	\
	BF64_SET_SB((ddk)->ddk_prop, 16, 16, SPA_MINBLOCKSHIFT, 1, x)

#define	DDK_GET_COMPRESS(ddk)		BF64_GET((ddk)->ddk_prop, 32, 8)
#define	DDK_SET_COMPRESS(ddk, x)	BF64_SET((ddk)->ddk_prop, 32, 8, x)

#define	DDT_KEY_WORDS	(sizeof (ddt_key_t) / sizeof (uint64_t))

typedef struct ddt_phys {
	dva_t		ddp_dva[SPA_DVAS_PER_BP];
	uint64_t	ddp_refcnt;
	uint64_t	ddp_phys_birth;
} ddt_phys_t;

enum ddt_phys_type {
	DDT_PHYS_DITTO = 0,
	DDT_PHYS_SINGLE = 1,
	DDT_PHYS_DOUBLE = 2,
	DDT_PHYS_TRIPLE = 3,
	DDT_PHYS_TYPES
};

/*
 * In-core ddt entry
 */
struct ddt_entry {
	ddt_key_t	dde_key;
	ddt_phys_t	dde_phys[DDT_PHYS_TYPES];
	zio_t		*dde_lead_zio[DDT_PHYS_TYPES];
	struct abd	*dde_repair_abd;
	enum ddt_type	dde_type;
	enum ddt_class	dde_class;
	uint8_t		dde_loading;
	uint8_t		dde_loaded;
	kcondvar_t	dde_cv;
	avl_node_t	dde_node;
};

/*
 * In-core ddt
 */
struct ddt {
	kmutex_t	ddt_lock;
	avl_tree_t	ddt_tree;
	avl_tree_t	ddt_repair_tree;
	enum zio_checksum ddt_checksum;
	spa_t		*ddt_spa;
	objset_t	*ddt_os;
	uint64_t	ddt_stat_object;
	uint64_t	ddt_object[DDT_TYPES][DDT_CLASSES];
	ddt_histogram_t	ddt_histogram[DDT_TYPES][DDT_CLASSES];
	ddt_histogram_t	ddt_histogram_cache[DDT_TYPES][DDT_CLASSES];
	ddt_object_t	ddt_object_stats[DDT_TYPES][DDT_CLASSES];
	avl_node_t	ddt_node;
};

/*
 * In-core and on-disk bookmark for DDT walks
 */
typedef struct ddt_bookmark {
	uint64_t	ddb_class;
	uint64_t	ddb_type;
	uint64_t	ddb_checksum;
	uint64_t	ddb_cursor;
} ddt_bookmark_t;

/*
 * Ops vector to access a specific DDT object type.
 */
typedef struct ddt_ops {
	char ddt_op_name[32];
	int (*ddt_op_create)(objset_t *os, uint64_t *object, dmu_tx_t *tx,
	    boolean_t prehash);
	int (*ddt_op_destroy)(objset_t *os, uint64_t object, dmu_tx_t *tx);
	int (*ddt_op_lookup)(objset_t *os, uint64_t object, ddt_entry_t *dde);
	void (*ddt_op_prefetch)(objset_t *os, uint64_t object,
	    ddt_entry_t *dde);
	int (*ddt_op_update)(objset_t *os, uint64_t object, ddt_entry_t *dde,
	    dmu_tx_t *tx);
	int (*ddt_op_remove)(objset_t *os, uint64_t object, ddt_entry_t *dde,
	    dmu_tx_t *tx);
	int (*ddt_op_walk)(objset_t *os, uint64_t object, ddt_entry_t *dde,
	    uint64_t *walk);
	int (*ddt_op_count)(objset_t *os, uint64_t object, uint64_t *count);
} ddt_ops_t;

#define	DDT_NAMELEN	80

extern void ddt_object_name(ddt_t *ddt, enum ddt_type type,
    enum ddt_class class, char *name);
extern int ddt_object_walk(ddt_t *ddt, enum ddt_type type,
    enum ddt_class class, uint64_t *walk, ddt_entry_t *dde);
extern int ddt_object_count(ddt_t *ddt, enum ddt_type type,
    enum ddt_class class, uint64_t *count);
extern int ddt_object_info(ddt_t *ddt, enum ddt_type type,
    enum ddt_class class, dmu_object_info_t *);
extern boolean_t ddt_object_exists(ddt_t *ddt, enum ddt_type type,
    enum ddt_class class);

extern void ddt_bp_fill(const ddt_phys_t *ddp, blkptr_t *bp,
    uint64_t txg);
extern void ddt_bp_create(enum zio_checksum checksum, const ddt_key_t *ddk,
    const ddt_phys_t *ddp, blkptr_t *bp);

extern void ddt_key_fill(ddt_key_t *ddk, const blkptr_t *bp);

extern void ddt_phys_fill(ddt_phys_t *ddp, const blkptr_t *bp);
extern void ddt_phys_clear(ddt_phys_t *ddp);
extern void ddt_phys_addref(ddt_phys_t *ddp);
extern void ddt_phys_decref(ddt_phys_t *ddp);
extern void ddt_phys_free(ddt_t *ddt, ddt_key_t *ddk, ddt_phys_t *ddp,
    uint64_t txg);
extern ddt_phys_t *ddt_phys_select(const ddt_entry_t *dde, const blkptr_t *bp);
extern uint64_t ddt_phys_total_refcnt(const ddt_entry_t *dde);

extern void ddt_stat_add(ddt_stat_t *dst, const ddt_stat_t *src, uint64_t neg);

extern void ddt_histogram_add(ddt_histogram_t *dst, const ddt_histogram_t *src);
extern void ddt_histogram_stat(ddt_stat_t *dds, const ddt_histogram_t *ddh);
extern boolean_t ddt_histogram_empty(const ddt_histogram_t *ddh);
extern void ddt_get_dedup_object_stats(spa_t *spa, ddt_object_t *ddo);
extern void ddt_get_dedup_histogram(spa_t *spa, ddt_histogram_t *ddh);
extern void ddt_get_dedup_stats(spa_t *spa, ddt_stat_t *dds_total);

extern uint64_t ddt_get_dedup_dspace(spa_t *spa);
extern uint64_t ddt_get_pool_dedup_ratio(spa_t *spa);

extern int ddt_ditto_copies_needed(ddt_t *ddt, ddt_entry_t *dde,
    ddt_phys_t *ddp_willref);
extern int ddt_ditto_copies_present(ddt_entry_t *dde);

extern size_t ddt_compress(void *src, uchar_t *dst, size_t s_len, size_t d_len);
extern void ddt_decompress(uchar_t *src, void *dst, size_t s_len, size_t d_len);

extern ddt_t *ddt_select(spa_t *spa, const blkptr_t *bp);
extern void ddt_enter(ddt_t *ddt);
extern void ddt_exit(ddt_t *ddt);
extern void ddt_init(void);
extern void ddt_fini(void);
extern ddt_entry_t *ddt_lookup(ddt_t *ddt, const blkptr_t *bp, boolean_t add);
extern void ddt_prefetch(spa_t *spa, const blkptr_t *bp);
extern void ddt_remove(ddt_t *ddt, ddt_entry_t *dde);

extern boolean_t ddt_class_contains(spa_t *spa, enum ddt_class max_class,
    const blkptr_t *bp);

extern ddt_entry_t *ddt_repair_start(ddt_t *ddt, const blkptr_t *bp);
extern void ddt_repair_done(ddt_t *ddt, ddt_entry_t *dde);

extern int ddt_entry_compare(const void *x1, const void *x2);

extern void ddt_create(spa_t *spa);
extern int ddt_load(spa_t *spa);
extern void ddt_unload(spa_t *spa);
extern void ddt_sync(spa_t *spa, uint64_t txg);
extern int ddt_walk(spa_t *spa, ddt_bookmark_t *ddb, ddt_entry_t *dde);
extern int ddt_object_update(ddt_t *ddt, enum ddt_type type,
    enum ddt_class class, ddt_entry_t *dde, dmu_tx_t *tx);

extern const ddt_ops_t ddt_zap_ops;

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_DDT_H */
