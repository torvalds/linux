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
 * Copyright (c) 2011, 2017 by Delphix. All rights reserved.
 * Copyright 2011 Nexenta Systems, Inc. All rights reserved.
 * Copyright (c) 2012, Joyent, Inc. All rights reserved.
 * Copyright 2013 DEY Storage Systems, Inc.
 * Copyright 2014 HybridCluster. All rights reserved.
 * Copyright (c) 2014 Spectra Logic Corporation, All rights reserved.
 * Copyright 2013 Saso Kiselkov. All rights reserved.
 * Copyright (c) 2014 Integros [integros.com]
 */

/* Portions Copyright 2010 Robert Milkowski */

#ifndef	_SYS_DMU_H
#define	_SYS_DMU_H

/*
 * This file describes the interface that the DMU provides for its
 * consumers.
 *
 * The DMU also interacts with the SPA.  That interface is described in
 * dmu_spa.h.
 */

#include <sys/zfs_context.h>
#include <sys/cred.h>
#include <sys/fs/zfs.h>
#include <sys/zio_compress.h>
#include <sys/zio_priority.h>

#ifdef	__cplusplus
extern "C" {
#endif

struct uio;
struct xuio;
struct page;
struct vnode;
struct spa;
struct zilog;
struct zio;
struct blkptr;
struct zap_cursor;
struct dsl_dataset;
struct dsl_pool;
struct dnode;
struct drr_begin;
struct drr_end;
struct zbookmark_phys;
struct spa;
struct nvlist;
struct arc_buf;
struct zio_prop;
struct sa_handle;
struct file;

typedef struct objset objset_t;
typedef struct dmu_tx dmu_tx_t;
typedef struct dsl_dir dsl_dir_t;
typedef struct dnode dnode_t;

typedef enum dmu_object_byteswap {
	DMU_BSWAP_UINT8,
	DMU_BSWAP_UINT16,
	DMU_BSWAP_UINT32,
	DMU_BSWAP_UINT64,
	DMU_BSWAP_ZAP,
	DMU_BSWAP_DNODE,
	DMU_BSWAP_OBJSET,
	DMU_BSWAP_ZNODE,
	DMU_BSWAP_OLDACL,
	DMU_BSWAP_ACL,
	/*
	 * Allocating a new byteswap type number makes the on-disk format
	 * incompatible with any other format that uses the same number.
	 *
	 * Data can usually be structured to work with one of the
	 * DMU_BSWAP_UINT* or DMU_BSWAP_ZAP types.
	 */
	DMU_BSWAP_NUMFUNCS
} dmu_object_byteswap_t;

#define	DMU_OT_NEWTYPE 0x80
#define	DMU_OT_METADATA 0x40
#define	DMU_OT_BYTESWAP_MASK 0x3f

/*
 * Defines a uint8_t object type. Object types specify if the data
 * in the object is metadata (boolean) and how to byteswap the data
 * (dmu_object_byteswap_t). All of the types created by this method
 * are cached in the dbuf metadata cache.
 */
#define	DMU_OT(byteswap, metadata) \
	(DMU_OT_NEWTYPE | \
	((metadata) ? DMU_OT_METADATA : 0) | \
	((byteswap) & DMU_OT_BYTESWAP_MASK))

#define	DMU_OT_IS_VALID(ot) (((ot) & DMU_OT_NEWTYPE) ? \
	((ot) & DMU_OT_BYTESWAP_MASK) < DMU_BSWAP_NUMFUNCS : \
	(ot) < DMU_OT_NUMTYPES)

#define	DMU_OT_IS_METADATA(ot) (((ot) & DMU_OT_NEWTYPE) ? \
	((ot) & DMU_OT_METADATA) : \
	dmu_ot[(ot)].ot_metadata)

#define	DMU_OT_IS_METADATA_CACHED(ot) (((ot) & DMU_OT_NEWTYPE) ? \
	B_TRUE : dmu_ot[(ot)].ot_dbuf_metadata_cache)

/*
 * These object types use bp_fill != 1 for their L0 bp's. Therefore they can't
 * have their data embedded (i.e. use a BP_IS_EMBEDDED() bp), because bp_fill
 * is repurposed for embedded BPs.
 */
#define	DMU_OT_HAS_FILL(ot) \
	((ot) == DMU_OT_DNODE || (ot) == DMU_OT_OBJSET)

#define	DMU_OT_BYTESWAP(ot) (((ot) & DMU_OT_NEWTYPE) ? \
	((ot) & DMU_OT_BYTESWAP_MASK) : \
	dmu_ot[(ot)].ot_byteswap)

typedef enum dmu_object_type {
	DMU_OT_NONE,
	/* general: */
	DMU_OT_OBJECT_DIRECTORY,	/* ZAP */
	DMU_OT_OBJECT_ARRAY,		/* UINT64 */
	DMU_OT_PACKED_NVLIST,		/* UINT8 (XDR by nvlist_pack/unpack) */
	DMU_OT_PACKED_NVLIST_SIZE,	/* UINT64 */
	DMU_OT_BPOBJ,			/* UINT64 */
	DMU_OT_BPOBJ_HDR,		/* UINT64 */
	/* spa: */
	DMU_OT_SPACE_MAP_HEADER,	/* UINT64 */
	DMU_OT_SPACE_MAP,		/* UINT64 */
	/* zil: */
	DMU_OT_INTENT_LOG,		/* UINT64 */
	/* dmu: */
	DMU_OT_DNODE,			/* DNODE */
	DMU_OT_OBJSET,			/* OBJSET */
	/* dsl: */
	DMU_OT_DSL_DIR,			/* UINT64 */
	DMU_OT_DSL_DIR_CHILD_MAP,	/* ZAP */
	DMU_OT_DSL_DS_SNAP_MAP,		/* ZAP */
	DMU_OT_DSL_PROPS,		/* ZAP */
	DMU_OT_DSL_DATASET,		/* UINT64 */
	/* zpl: */
	DMU_OT_ZNODE,			/* ZNODE */
	DMU_OT_OLDACL,			/* Old ACL */
	DMU_OT_PLAIN_FILE_CONTENTS,	/* UINT8 */
	DMU_OT_DIRECTORY_CONTENTS,	/* ZAP */
	DMU_OT_MASTER_NODE,		/* ZAP */
	DMU_OT_UNLINKED_SET,		/* ZAP */
	/* zvol: */
	DMU_OT_ZVOL,			/* UINT8 */
	DMU_OT_ZVOL_PROP,		/* ZAP */
	/* other; for testing only! */
	DMU_OT_PLAIN_OTHER,		/* UINT8 */
	DMU_OT_UINT64_OTHER,		/* UINT64 */
	DMU_OT_ZAP_OTHER,		/* ZAP */
	/* new object types: */
	DMU_OT_ERROR_LOG,		/* ZAP */
	DMU_OT_SPA_HISTORY,		/* UINT8 */
	DMU_OT_SPA_HISTORY_OFFSETS,	/* spa_his_phys_t */
	DMU_OT_POOL_PROPS,		/* ZAP */
	DMU_OT_DSL_PERMS,		/* ZAP */
	DMU_OT_ACL,			/* ACL */
	DMU_OT_SYSACL,			/* SYSACL */
	DMU_OT_FUID,			/* FUID table (Packed NVLIST UINT8) */
	DMU_OT_FUID_SIZE,		/* FUID table size UINT64 */
	DMU_OT_NEXT_CLONES,		/* ZAP */
	DMU_OT_SCAN_QUEUE,		/* ZAP */
	DMU_OT_USERGROUP_USED,		/* ZAP */
	DMU_OT_USERGROUP_QUOTA,		/* ZAP */
	DMU_OT_USERREFS,		/* ZAP */
	DMU_OT_DDT_ZAP,			/* ZAP */
	DMU_OT_DDT_STATS,		/* ZAP */
	DMU_OT_SA,			/* System attr */
	DMU_OT_SA_MASTER_NODE,		/* ZAP */
	DMU_OT_SA_ATTR_REGISTRATION,	/* ZAP */
	DMU_OT_SA_ATTR_LAYOUTS,		/* ZAP */
	DMU_OT_SCAN_XLATE,		/* ZAP */
	DMU_OT_DEDUP,			/* fake dedup BP from ddt_bp_create() */
	DMU_OT_DEADLIST,		/* ZAP */
	DMU_OT_DEADLIST_HDR,		/* UINT64 */
	DMU_OT_DSL_CLONES,		/* ZAP */
	DMU_OT_BPOBJ_SUBOBJ,		/* UINT64 */
	/*
	 * Do not allocate new object types here. Doing so makes the on-disk
	 * format incompatible with any other format that uses the same object
	 * type number.
	 *
	 * When creating an object which does not have one of the above types
	 * use the DMU_OTN_* type with the correct byteswap and metadata
	 * values.
	 *
	 * The DMU_OTN_* types do not have entries in the dmu_ot table,
	 * use the DMU_OT_IS_METDATA() and DMU_OT_BYTESWAP() macros instead
	 * of indexing into dmu_ot directly (this works for both DMU_OT_* types
	 * and DMU_OTN_* types).
	 */
	DMU_OT_NUMTYPES,

	/*
	 * Names for valid types declared with DMU_OT().
	 */
	DMU_OTN_UINT8_DATA = DMU_OT(DMU_BSWAP_UINT8, B_FALSE),
	DMU_OTN_UINT8_METADATA = DMU_OT(DMU_BSWAP_UINT8, B_TRUE),
	DMU_OTN_UINT16_DATA = DMU_OT(DMU_BSWAP_UINT16, B_FALSE),
	DMU_OTN_UINT16_METADATA = DMU_OT(DMU_BSWAP_UINT16, B_TRUE),
	DMU_OTN_UINT32_DATA = DMU_OT(DMU_BSWAP_UINT32, B_FALSE),
	DMU_OTN_UINT32_METADATA = DMU_OT(DMU_BSWAP_UINT32, B_TRUE),
	DMU_OTN_UINT64_DATA = DMU_OT(DMU_BSWAP_UINT64, B_FALSE),
	DMU_OTN_UINT64_METADATA = DMU_OT(DMU_BSWAP_UINT64, B_TRUE),
	DMU_OTN_ZAP_DATA = DMU_OT(DMU_BSWAP_ZAP, B_FALSE),
	DMU_OTN_ZAP_METADATA = DMU_OT(DMU_BSWAP_ZAP, B_TRUE),
} dmu_object_type_t;

/*
 * These flags are intended to be used to specify the "txg_how"
 * parameter when calling the dmu_tx_assign() function. See the comment
 * above dmu_tx_assign() for more details on the meaning of these flags.
 */
#define	TXG_NOWAIT	(0ULL)
#define	TXG_WAIT	(1ULL<<0)
#define	TXG_NOTHROTTLE	(1ULL<<1)

void byteswap_uint64_array(void *buf, size_t size);
void byteswap_uint32_array(void *buf, size_t size);
void byteswap_uint16_array(void *buf, size_t size);
void byteswap_uint8_array(void *buf, size_t size);
void zap_byteswap(void *buf, size_t size);
void zfs_oldacl_byteswap(void *buf, size_t size);
void zfs_acl_byteswap(void *buf, size_t size);
void zfs_znode_byteswap(void *buf, size_t size);

#define	DS_FIND_SNAPSHOTS	(1<<0)
#define	DS_FIND_CHILDREN	(1<<1)
#define	DS_FIND_SERIALIZE	(1<<2)

/*
 * The maximum number of bytes that can be accessed as part of one
 * operation, including metadata.
 */
#define	DMU_MAX_ACCESS (32 * 1024 * 1024) /* 32MB */
#define	DMU_MAX_DELETEBLKCNT (20480) /* ~5MB of indirect blocks */

#define	DMU_USERUSED_OBJECT	(-1ULL)
#define	DMU_GROUPUSED_OBJECT	(-2ULL)

/*
 * artificial blkids for bonus buffer and spill blocks
 */
#define	DMU_BONUS_BLKID		(-1ULL)
#define	DMU_SPILL_BLKID		(-2ULL)
/*
 * Public routines to create, destroy, open, and close objsets.
 */
int dmu_objset_hold(const char *name, void *tag, objset_t **osp);
int dmu_objset_own(const char *name, dmu_objset_type_t type,
    boolean_t readonly, void *tag, objset_t **osp);
void dmu_objset_rele(objset_t *os, void *tag);
void dmu_objset_disown(objset_t *os, void *tag);
int dmu_objset_open_ds(struct dsl_dataset *ds, objset_t **osp);

void dmu_objset_evict_dbufs(objset_t *os);
int dmu_objset_create(const char *name, dmu_objset_type_t type, uint64_t flags,
    void (*func)(objset_t *os, void *arg, cred_t *cr, dmu_tx_t *tx), void *arg);
int dmu_get_recursive_snaps_nvl(char *fsname, const char *snapname,
    struct nvlist *snaps);
int dmu_objset_clone(const char *name, const char *origin);
int dsl_destroy_snapshots_nvl(struct nvlist *snaps, boolean_t defer,
    struct nvlist *errlist);
int dmu_objset_snapshot_one(const char *fsname, const char *snapname);
int dmu_objset_snapshot_tmp(const char *, const char *, int);
int dmu_objset_find(char *name, int func(const char *, void *), void *arg,
    int flags);
void dmu_objset_byteswap(void *buf, size_t size);
int dsl_dataset_rename_snapshot(const char *fsname,
    const char *oldsnapname, const char *newsnapname, boolean_t recursive);
int dmu_objset_remap_indirects(const char *fsname);

typedef struct dmu_buf {
	uint64_t db_object;		/* object that this buffer is part of */
	uint64_t db_offset;		/* byte offset in this object */
	uint64_t db_size;		/* size of buffer in bytes */
	void *db_data;			/* data in buffer */
} dmu_buf_t;

/*
 * The names of zap entries in the DIRECTORY_OBJECT of the MOS.
 */
#define	DMU_POOL_DIRECTORY_OBJECT	1
#define	DMU_POOL_CONFIG			"config"
#define	DMU_POOL_FEATURES_FOR_WRITE	"features_for_write"
#define	DMU_POOL_FEATURES_FOR_READ	"features_for_read"
#define	DMU_POOL_FEATURE_DESCRIPTIONS	"feature_descriptions"
#define	DMU_POOL_FEATURE_ENABLED_TXG	"feature_enabled_txg"
#define	DMU_POOL_ROOT_DATASET		"root_dataset"
#define	DMU_POOL_SYNC_BPOBJ		"sync_bplist"
#define	DMU_POOL_ERRLOG_SCRUB		"errlog_scrub"
#define	DMU_POOL_ERRLOG_LAST		"errlog_last"
#define	DMU_POOL_SPARES			"spares"
#define	DMU_POOL_DEFLATE		"deflate"
#define	DMU_POOL_HISTORY		"history"
#define	DMU_POOL_PROPS			"pool_props"
#define	DMU_POOL_L2CACHE		"l2cache"
#define	DMU_POOL_TMP_USERREFS		"tmp_userrefs"
#define	DMU_POOL_DDT			"DDT-%s-%s-%s"
#define	DMU_POOL_DDT_STATS		"DDT-statistics"
#define	DMU_POOL_CREATION_VERSION	"creation_version"
#define	DMU_POOL_SCAN			"scan"
#define	DMU_POOL_FREE_BPOBJ		"free_bpobj"
#define	DMU_POOL_BPTREE_OBJ		"bptree_obj"
#define	DMU_POOL_EMPTY_BPOBJ		"empty_bpobj"
#define	DMU_POOL_CHECKSUM_SALT		"org.illumos:checksum_salt"
#define	DMU_POOL_VDEV_ZAP_MAP		"com.delphix:vdev_zap_map"
#define	DMU_POOL_REMOVING		"com.delphix:removing"
#define	DMU_POOL_OBSOLETE_BPOBJ		"com.delphix:obsolete_bpobj"
#define	DMU_POOL_CONDENSING_INDIRECT	"com.delphix:condensing_indirect"
#define	DMU_POOL_ZPOOL_CHECKPOINT	"com.delphix:zpool_checkpoint"

/*
 * Allocate an object from this objset.  The range of object numbers
 * available is (0, DN_MAX_OBJECT).  Object 0 is the meta-dnode.
 *
 * The transaction must be assigned to a txg.  The newly allocated
 * object will be "held" in the transaction (ie. you can modify the
 * newly allocated object in this transaction).
 *
 * dmu_object_alloc() chooses an object and returns it in *objectp.
 *
 * dmu_object_claim() allocates a specific object number.  If that
 * number is already allocated, it fails and returns EEXIST.
 *
 * Return 0 on success, or ENOSPC or EEXIST as specified above.
 */
uint64_t dmu_object_alloc(objset_t *os, dmu_object_type_t ot,
    int blocksize, dmu_object_type_t bonus_type, int bonus_len, dmu_tx_t *tx);
uint64_t dmu_object_alloc_ibs(objset_t *os, dmu_object_type_t ot, int blocksize,
    int indirect_blockshift,
    dmu_object_type_t bonustype, int bonuslen, dmu_tx_t *tx);
uint64_t dmu_object_alloc_dnsize(objset_t *os, dmu_object_type_t ot,
    int blocksize, dmu_object_type_t bonus_type, int bonus_len,
    int dnodesize, dmu_tx_t *tx);
int dmu_object_claim_dnsize(objset_t *os, uint64_t object, dmu_object_type_t ot,
    int blocksize, dmu_object_type_t bonus_type, int bonus_len,
    int dnodesize, dmu_tx_t *tx);
int dmu_object_reclaim_dnsize(objset_t *os, uint64_t object,
    dmu_object_type_t ot, int blocksize, dmu_object_type_t bonustype,
    int bonuslen, int dnodesize, dmu_tx_t *txp);
int dmu_object_claim(objset_t *os, uint64_t object, dmu_object_type_t ot,
    int blocksize, dmu_object_type_t bonus_type, int bonus_len, dmu_tx_t *tx);
int dmu_object_reclaim(objset_t *os, uint64_t object, dmu_object_type_t ot,
    int blocksize, dmu_object_type_t bonustype, int bonuslen, dmu_tx_t *txp);

/*
 * Free an object from this objset.
 *
 * The object's data will be freed as well (ie. you don't need to call
 * dmu_free(object, 0, -1, tx)).
 *
 * The object need not be held in the transaction.
 *
 * If there are any holds on this object's buffers (via dmu_buf_hold()),
 * or tx holds on the object (via dmu_tx_hold_object()), you can not
 * free it; it fails and returns EBUSY.
 *
 * If the object is not allocated, it fails and returns ENOENT.
 *
 * Return 0 on success, or EBUSY or ENOENT as specified above.
 */
int dmu_object_free(objset_t *os, uint64_t object, dmu_tx_t *tx);

/*
 * Find the next allocated or free object.
 *
 * The objectp parameter is in-out.  It will be updated to be the next
 * object which is allocated.  Ignore objects which have not been
 * modified since txg.
 *
 * XXX Can only be called on a objset with no dirty data.
 *
 * Returns 0 on success, or ENOENT if there are no more objects.
 */
int dmu_object_next(objset_t *os, uint64_t *objectp,
    boolean_t hole, uint64_t txg);

/*
 * Set the data blocksize for an object.
 *
 * The object cannot have any blocks allcated beyond the first.  If
 * the first block is allocated already, the new size must be greater
 * than the current block size.  If these conditions are not met,
 * ENOTSUP will be returned.
 *
 * Returns 0 on success, or EBUSY if there are any holds on the object
 * contents, or ENOTSUP as described above.
 */
int dmu_object_set_blocksize(objset_t *os, uint64_t object, uint64_t size,
    int ibs, dmu_tx_t *tx);

/*
 * Set the checksum property on a dnode.  The new checksum algorithm will
 * apply to all newly written blocks; existing blocks will not be affected.
 */
void dmu_object_set_checksum(objset_t *os, uint64_t object, uint8_t checksum,
    dmu_tx_t *tx);

/*
 * Set the compress property on a dnode.  The new compression algorithm will
 * apply to all newly written blocks; existing blocks will not be affected.
 */
void dmu_object_set_compress(objset_t *os, uint64_t object, uint8_t compress,
    dmu_tx_t *tx);

int dmu_object_remap_indirects(objset_t *os, uint64_t object, uint64_t txg);

void
dmu_write_embedded(objset_t *os, uint64_t object, uint64_t offset,
    void *data, uint8_t etype, uint8_t comp, int uncompressed_size,
    int compressed_size, int byteorder, dmu_tx_t *tx);

/*
 * Decide how to write a block: checksum, compression, number of copies, etc.
 */
#define	WP_NOFILL	0x1
#define	WP_DMU_SYNC	0x2
#define	WP_SPILL	0x4

void dmu_write_policy(objset_t *os, dnode_t *dn, int level, int wp,
    struct zio_prop *zp);
/*
 * The bonus data is accessed more or less like a regular buffer.
 * You must dmu_bonus_hold() to get the buffer, which will give you a
 * dmu_buf_t with db_offset==-1ULL, and db_size = the size of the bonus
 * data.  As with any normal buffer, you must call dmu_buf_will_dirty()
 * before modifying it, and the
 * object must be held in an assigned transaction before calling
 * dmu_buf_will_dirty.  You may use dmu_buf_set_user() on the bonus
 * buffer as well.  You must release your hold with dmu_buf_rele().
 *
 * Returns ENOENT, EIO, or 0.
 */
int dmu_bonus_hold(objset_t *os, uint64_t object, void *tag, dmu_buf_t **);
int dmu_bonus_max(void);
int dmu_set_bonus(dmu_buf_t *, int, dmu_tx_t *);
int dmu_set_bonustype(dmu_buf_t *, dmu_object_type_t, dmu_tx_t *);
dmu_object_type_t dmu_get_bonustype(dmu_buf_t *);
int dmu_rm_spill(objset_t *, uint64_t, dmu_tx_t *);

/*
 * Special spill buffer support used by "SA" framework
 */

int dmu_spill_hold_by_bonus(dmu_buf_t *bonus, void *tag, dmu_buf_t **dbp);
int dmu_spill_hold_by_dnode(dnode_t *dn, uint32_t flags,
    void *tag, dmu_buf_t **dbp);
int dmu_spill_hold_existing(dmu_buf_t *bonus, void *tag, dmu_buf_t **dbp);

/*
 * Obtain the DMU buffer from the specified object which contains the
 * specified offset.  dmu_buf_hold() puts a "hold" on the buffer, so
 * that it will remain in memory.  You must release the hold with
 * dmu_buf_rele().  You musn't access the dmu_buf_t after releasing your
 * hold.  You must have a hold on any dmu_buf_t* you pass to the DMU.
 *
 * You must call dmu_buf_read, dmu_buf_will_dirty, or dmu_buf_will_fill
 * on the returned buffer before reading or writing the buffer's
 * db_data.  The comments for those routines describe what particular
 * operations are valid after calling them.
 *
 * The object number must be a valid, allocated object number.
 */
int dmu_buf_hold(objset_t *os, uint64_t object, uint64_t offset,
    void *tag, dmu_buf_t **, int flags);
int dmu_buf_hold_by_dnode(dnode_t *dn, uint64_t offset,
    void *tag, dmu_buf_t **dbp, int flags);

/*
 * Add a reference to a dmu buffer that has already been held via
 * dmu_buf_hold() in the current context.
 */
void dmu_buf_add_ref(dmu_buf_t *db, void* tag);

/*
 * Attempt to add a reference to a dmu buffer that is in an unknown state,
 * using a pointer that may have been invalidated by eviction processing.
 * The request will succeed if the passed in dbuf still represents the
 * same os/object/blkid, is ineligible for eviction, and has at least
 * one hold by a user other than the syncer.
 */
boolean_t dmu_buf_try_add_ref(dmu_buf_t *, objset_t *os, uint64_t object,
    uint64_t blkid, void *tag);

void dmu_buf_rele(dmu_buf_t *db, void *tag);
uint64_t dmu_buf_refcount(dmu_buf_t *db);

/*
 * dmu_buf_hold_array holds the DMU buffers which contain all bytes in a
 * range of an object.  A pointer to an array of dmu_buf_t*'s is
 * returned (in *dbpp).
 *
 * dmu_buf_rele_array releases the hold on an array of dmu_buf_t*'s, and
 * frees the array.  The hold on the array of buffers MUST be released
 * with dmu_buf_rele_array.  You can NOT release the hold on each buffer
 * individually with dmu_buf_rele.
 */
int dmu_buf_hold_array_by_bonus(dmu_buf_t *db, uint64_t offset,
    uint64_t length, boolean_t read, void *tag,
    int *numbufsp, dmu_buf_t ***dbpp);
int dmu_buf_hold_array_by_dnode(dnode_t *dn, uint64_t offset, uint64_t length,
    boolean_t read, void *tag, int *numbufsp, dmu_buf_t ***dbpp,
    uint32_t flags);
void dmu_buf_rele_array(dmu_buf_t **, int numbufs, void *tag);

typedef void dmu_buf_evict_func_t(void *user_ptr);

/*
 * A DMU buffer user object may be associated with a dbuf for the
 * duration of its lifetime.  This allows the user of a dbuf (client)
 * to attach private data to a dbuf (e.g. in-core only data such as a
 * dnode_children_t, zap_t, or zap_leaf_t) and be optionally notified
 * when that dbuf has been evicted.  Clients typically respond to the
 * eviction notification by freeing their private data, thus ensuring
 * the same lifetime for both dbuf and private data.
 *
 * The mapping from a dmu_buf_user_t to any client private data is the
 * client's responsibility.  All current consumers of the API with private
 * data embed a dmu_buf_user_t as the first member of the structure for
 * their private data.  This allows conversions between the two types
 * with a simple cast.  Since the DMU buf user API never needs access
 * to the private data, other strategies can be employed if necessary
 * or convenient for the client (e.g. using container_of() to do the
 * conversion for private data that cannot have the dmu_buf_user_t as
 * its first member).
 *
 * Eviction callbacks are executed without the dbuf mutex held or any
 * other type of mechanism to guarantee that the dbuf is still available.
 * For this reason, users must assume the dbuf has already been freed
 * and not reference the dbuf from the callback context.
 *
 * Users requesting "immediate eviction" are notified as soon as the dbuf
 * is only referenced by dirty records (dirties == holds).  Otherwise the
 * notification occurs after eviction processing for the dbuf begins.
 */
typedef struct dmu_buf_user {
	/*
	 * Asynchronous user eviction callback state.
	 */
	taskq_ent_t	dbu_tqent;

	/*
	 * This instance's eviction function pointers.
	 *
	 * dbu_evict_func_sync is called synchronously and then
	 * dbu_evict_func_async is executed asynchronously on a taskq.
	 */
	dmu_buf_evict_func_t *dbu_evict_func_sync;
	dmu_buf_evict_func_t *dbu_evict_func_async;
#ifdef ZFS_DEBUG
	/*
	 * Pointer to user's dbuf pointer.  NULL for clients that do
	 * not associate a dbuf with their user data.
	 *
	 * The dbuf pointer is cleared upon eviction so as to catch
	 * use-after-evict bugs in clients.
	 */
	dmu_buf_t **dbu_clear_on_evict_dbufp;
#endif
} dmu_buf_user_t;

/*
 * Initialize the given dmu_buf_user_t instance with the eviction function
 * evict_func, to be called when the user is evicted.
 *
 * NOTE: This function should only be called once on a given dmu_buf_user_t.
 *       To allow enforcement of this, dbu must already be zeroed on entry.
 */
/*ARGSUSED*/
inline void
dmu_buf_init_user(dmu_buf_user_t *dbu, dmu_buf_evict_func_t *evict_func_sync,
    dmu_buf_evict_func_t *evict_func_async, dmu_buf_t **clear_on_evict_dbufp)
{
	ASSERT(dbu->dbu_evict_func_sync == NULL);
	ASSERT(dbu->dbu_evict_func_async == NULL);

	/* must have at least one evict func */
	IMPLY(evict_func_sync == NULL, evict_func_async != NULL);
	dbu->dbu_evict_func_sync = evict_func_sync;
	dbu->dbu_evict_func_async = evict_func_async;
#ifdef ZFS_DEBUG
	dbu->dbu_clear_on_evict_dbufp = clear_on_evict_dbufp;
#endif
}

/*
 * Attach user data to a dbuf and mark it for normal (when the dbuf's
 * data is cleared or its reference count goes to zero) eviction processing.
 *
 * Returns NULL on success, or the existing user if another user currently
 * owns the buffer.
 */
void *dmu_buf_set_user(dmu_buf_t *db, dmu_buf_user_t *user);

/*
 * Attach user data to a dbuf and mark it for immediate (its dirty and
 * reference counts are equal) eviction processing.
 *
 * Returns NULL on success, or the existing user if another user currently
 * owns the buffer.
 */
void *dmu_buf_set_user_ie(dmu_buf_t *db, dmu_buf_user_t *user);

/*
 * Replace the current user of a dbuf.
 *
 * If given the current user of a dbuf, replaces the dbuf's user with
 * "new_user" and returns the user data pointer that was replaced.
 * Otherwise returns the current, and unmodified, dbuf user pointer.
 */
void *dmu_buf_replace_user(dmu_buf_t *db,
    dmu_buf_user_t *old_user, dmu_buf_user_t *new_user);

/*
 * Remove the specified user data for a DMU buffer.
 *
 * Returns the user that was removed on success, or the current user if
 * another user currently owns the buffer.
 */
void *dmu_buf_remove_user(dmu_buf_t *db, dmu_buf_user_t *user);

/*
 * Returns the user data (dmu_buf_user_t *) associated with this dbuf.
 */
void *dmu_buf_get_user(dmu_buf_t *db);

objset_t *dmu_buf_get_objset(dmu_buf_t *db);
dnode_t *dmu_buf_dnode_enter(dmu_buf_t *db);
void dmu_buf_dnode_exit(dmu_buf_t *db);

/* Block until any in-progress dmu buf user evictions complete. */
void dmu_buf_user_evict_wait(void);

/*
 * Returns the blkptr associated with this dbuf, or NULL if not set.
 */
struct blkptr *dmu_buf_get_blkptr(dmu_buf_t *db);

/*
 * Indicate that you are going to modify the buffer's data (db_data).
 *
 * The transaction (tx) must be assigned to a txg (ie. you've called
 * dmu_tx_assign()).  The buffer's object must be held in the tx
 * (ie. you've called dmu_tx_hold_object(tx, db->db_object)).
 */
void dmu_buf_will_dirty(dmu_buf_t *db, dmu_tx_t *tx);

/*
 * You must create a transaction, then hold the objects which you will
 * (or might) modify as part of this transaction.  Then you must assign
 * the transaction to a transaction group.  Once the transaction has
 * been assigned, you can modify buffers which belong to held objects as
 * part of this transaction.  You can't modify buffers before the
 * transaction has been assigned; you can't modify buffers which don't
 * belong to objects which this transaction holds; you can't hold
 * objects once the transaction has been assigned.  You may hold an
 * object which you are going to free (with dmu_object_free()), but you
 * don't have to.
 *
 * You can abort the transaction before it has been assigned.
 *
 * Note that you may hold buffers (with dmu_buf_hold) at any time,
 * regardless of transaction state.
 */

#define	DMU_NEW_OBJECT	(-1ULL)
#define	DMU_OBJECT_END	(-1ULL)

dmu_tx_t *dmu_tx_create(objset_t *os);
void dmu_tx_hold_write(dmu_tx_t *tx, uint64_t object, uint64_t off, int len);
void dmu_tx_hold_write_by_dnode(dmu_tx_t *tx, dnode_t *dn, uint64_t off,
    int len);
void dmu_tx_hold_free(dmu_tx_t *tx, uint64_t object, uint64_t off,
    uint64_t len);
void dmu_tx_hold_free_by_dnode(dmu_tx_t *tx, dnode_t *dn, uint64_t off,
    uint64_t len);
void dmu_tx_hold_remap_l1indirect(dmu_tx_t *tx, uint64_t object);
void dmu_tx_hold_zap(dmu_tx_t *tx, uint64_t object, int add, const char *name);
void dmu_tx_hold_zap_by_dnode(dmu_tx_t *tx, dnode_t *dn, int add,
    const char *name);
void dmu_tx_hold_bonus(dmu_tx_t *tx, uint64_t object);
void dmu_tx_hold_bonus_by_dnode(dmu_tx_t *tx, dnode_t *dn);
void dmu_tx_hold_spill(dmu_tx_t *tx, uint64_t object);
void dmu_tx_hold_sa(dmu_tx_t *tx, struct sa_handle *hdl, boolean_t may_grow);
void dmu_tx_hold_sa_create(dmu_tx_t *tx, int total_size);
void dmu_tx_abort(dmu_tx_t *tx);
int dmu_tx_assign(dmu_tx_t *tx, uint64_t txg_how);
void dmu_tx_wait(dmu_tx_t *tx);
void dmu_tx_commit(dmu_tx_t *tx);
void dmu_tx_mark_netfree(dmu_tx_t *tx);

/*
 * To register a commit callback, dmu_tx_callback_register() must be called.
 *
 * dcb_data is a pointer to caller private data that is passed on as a
 * callback parameter. The caller is responsible for properly allocating and
 * freeing it.
 *
 * When registering a callback, the transaction must be already created, but
 * it cannot be committed or aborted. It can be assigned to a txg or not.
 *
 * The callback will be called after the transaction has been safely written
 * to stable storage and will also be called if the dmu_tx is aborted.
 * If there is any error which prevents the transaction from being committed to
 * disk, the callback will be called with a value of error != 0.
 */
typedef void dmu_tx_callback_func_t(void *dcb_data, int error);

void dmu_tx_callback_register(dmu_tx_t *tx, dmu_tx_callback_func_t *dcb_func,
    void *dcb_data);

/*
 * Free up the data blocks for a defined range of a file.  If size is
 * -1, the range from offset to end-of-file is freed.
 */
int dmu_free_range(objset_t *os, uint64_t object, uint64_t offset,
	uint64_t size, dmu_tx_t *tx);
int dmu_free_long_range(objset_t *os, uint64_t object, uint64_t offset,
	uint64_t size);
int dmu_free_long_object(objset_t *os, uint64_t object);

/*
 * Convenience functions.
 *
 * Canfail routines will return 0 on success, or an errno if there is a
 * nonrecoverable I/O error.
 */
#define	DMU_READ_PREFETCH	0 /* prefetch */
#define	DMU_READ_NO_PREFETCH	1 /* don't prefetch */
int dmu_read(objset_t *os, uint64_t object, uint64_t offset, uint64_t size,
	void *buf, uint32_t flags);
int dmu_read_by_dnode(dnode_t *dn, uint64_t offset, uint64_t size, void *buf,
    uint32_t flags);
void dmu_write(objset_t *os, uint64_t object, uint64_t offset, uint64_t size,
	const void *buf, dmu_tx_t *tx);
void dmu_write_by_dnode(dnode_t *dn, uint64_t offset, uint64_t size,
    const void *buf, dmu_tx_t *tx);
void dmu_prealloc(objset_t *os, uint64_t object, uint64_t offset, uint64_t size,
	dmu_tx_t *tx);
int dmu_read_uio(objset_t *os, uint64_t object, struct uio *uio, uint64_t size);
int dmu_read_uio_dbuf(dmu_buf_t *zdb, struct uio *uio, uint64_t size);
int dmu_read_uio_dnode(dnode_t *dn, struct uio *uio, uint64_t size);
int dmu_write_uio(objset_t *os, uint64_t object, struct uio *uio, uint64_t size,
    dmu_tx_t *tx);
int dmu_write_uio_dbuf(dmu_buf_t *zdb, struct uio *uio, uint64_t size,
    dmu_tx_t *tx);
int dmu_write_uio_dnode(dnode_t *dn, struct uio *uio, uint64_t size,
    dmu_tx_t *tx);
#ifdef _KERNEL
#ifdef illumos
int dmu_write_pages(objset_t *os, uint64_t object, uint64_t offset,
    uint64_t size, struct page *pp, dmu_tx_t *tx);
#else
int dmu_write_pages(objset_t *os, uint64_t object, uint64_t offset,
    uint64_t size, struct vm_page **ppa, dmu_tx_t *tx);
int dmu_read_pages(objset_t *os, uint64_t object, vm_page_t *ma, int count,
    int *rbehind, int *rahead, int last_size);
#endif
#endif
struct arc_buf *dmu_request_arcbuf(dmu_buf_t *handle, int size);
void dmu_return_arcbuf(struct arc_buf *buf);
void dmu_assign_arcbuf_dnode(dnode_t *handle, uint64_t offset,
    struct arc_buf *buf, dmu_tx_t *tx);
void dmu_assign_arcbuf(dmu_buf_t *handle, uint64_t offset, struct arc_buf *buf,
    dmu_tx_t *tx);
int dmu_xuio_init(struct xuio *uio, int niov);
void dmu_xuio_fini(struct xuio *uio);
int dmu_xuio_add(struct xuio *uio, struct arc_buf *abuf, offset_t off,
    size_t n);
int dmu_xuio_cnt(struct xuio *uio);
struct arc_buf *dmu_xuio_arcbuf(struct xuio *uio, int i);
void dmu_xuio_clear(struct xuio *uio, int i);
void xuio_stat_wbuf_copied(void);
void xuio_stat_wbuf_nocopy(void);

extern boolean_t zfs_prefetch_disable;
extern int zfs_max_recordsize;

/*
 * Asynchronously try to read in the data.
 */
void dmu_prefetch(objset_t *os, uint64_t object, int64_t level, uint64_t offset,
    uint64_t len, enum zio_priority pri);

typedef struct dmu_object_info {
	/* All sizes are in bytes unless otherwise indicated. */
	uint32_t doi_data_block_size;
	uint32_t doi_metadata_block_size;
	dmu_object_type_t doi_type;
	dmu_object_type_t doi_bonus_type;
	uint64_t doi_bonus_size;
	uint8_t doi_indirection;		/* 2 = dnode->indirect->data */
	uint8_t doi_checksum;
	uint8_t doi_compress;
	uint8_t doi_nblkptr;
	uint8_t doi_pad[4];
	uint64_t doi_dnodesize;
	uint64_t doi_physical_blocks_512;	/* data + metadata, 512b blks */
	uint64_t doi_max_offset;
	uint64_t doi_fill_count;		/* number of non-empty blocks */
} dmu_object_info_t;

typedef void arc_byteswap_func_t(void *buf, size_t size);

typedef struct dmu_object_type_info {
	dmu_object_byteswap_t	ot_byteswap;
	boolean_t		ot_metadata;
	boolean_t		ot_dbuf_metadata_cache;
	char			*ot_name;
} dmu_object_type_info_t;

typedef struct dmu_object_byteswap_info {
	arc_byteswap_func_t	*ob_func;
	char			*ob_name;
} dmu_object_byteswap_info_t;

extern const dmu_object_type_info_t dmu_ot[DMU_OT_NUMTYPES];
extern const dmu_object_byteswap_info_t dmu_ot_byteswap[DMU_BSWAP_NUMFUNCS];

/*
 * Get information on a DMU object.
 *
 * Return 0 on success or ENOENT if object is not allocated.
 *
 * If doi is NULL, just indicates whether the object exists.
 */
int dmu_object_info(objset_t *os, uint64_t object, dmu_object_info_t *doi);
void __dmu_object_info_from_dnode(struct dnode *dn, dmu_object_info_t *doi);
/* Like dmu_object_info, but faster if you have a held dnode in hand. */
void dmu_object_info_from_dnode(dnode_t *dn, dmu_object_info_t *doi);
/* Like dmu_object_info, but faster if you have a held dbuf in hand. */
void dmu_object_info_from_db(dmu_buf_t *db, dmu_object_info_t *doi);
/*
 * Like dmu_object_info_from_db, but faster still when you only care about
 * the size.  This is specifically optimized for zfs_getattr().
 */
void dmu_object_size_from_db(dmu_buf_t *db, uint32_t *blksize,
    u_longlong_t *nblk512);

void dmu_object_dnsize_from_db(dmu_buf_t *db, int *dnsize);

typedef struct dmu_objset_stats {
	uint64_t dds_num_clones; /* number of clones of this */
	uint64_t dds_creation_txg;
	uint64_t dds_guid;
	dmu_objset_type_t dds_type;
	uint8_t dds_is_snapshot;
	uint8_t dds_inconsistent;
	char dds_origin[ZFS_MAX_DATASET_NAME_LEN];
} dmu_objset_stats_t;

/*
 * Get stats on a dataset.
 */
void dmu_objset_fast_stat(objset_t *os, dmu_objset_stats_t *stat);

/*
 * Add entries to the nvlist for all the objset's properties.  See
 * zfs_prop_table[] and zfs(1m) for details on the properties.
 */
void dmu_objset_stats(objset_t *os, struct nvlist *nv);

/*
 * Get the space usage statistics for statvfs().
 *
 * refdbytes is the amount of space "referenced" by this objset.
 * availbytes is the amount of space available to this objset, taking
 * into account quotas & reservations, assuming that no other objsets
 * use the space first.  These values correspond to the 'referenced' and
 * 'available' properties, described in the zfs(1m) manpage.
 *
 * usedobjs and availobjs are the number of objects currently allocated,
 * and available.
 */
void dmu_objset_space(objset_t *os, uint64_t *refdbytesp, uint64_t *availbytesp,
    uint64_t *usedobjsp, uint64_t *availobjsp);

/*
 * The fsid_guid is a 56-bit ID that can change to avoid collisions.
 * (Contrast with the ds_guid which is a 64-bit ID that will never
 * change, so there is a small probability that it will collide.)
 */
uint64_t dmu_objset_fsid_guid(objset_t *os);

/*
 * Get the [cm]time for an objset's snapshot dir
 */
timestruc_t dmu_objset_snap_cmtime(objset_t *os);

int dmu_objset_is_snapshot(objset_t *os);

extern struct spa *dmu_objset_spa(objset_t *os);
extern struct zilog *dmu_objset_zil(objset_t *os);
extern struct dsl_pool *dmu_objset_pool(objset_t *os);
extern struct dsl_dataset *dmu_objset_ds(objset_t *os);
extern void dmu_objset_name(objset_t *os, char *buf);
extern dmu_objset_type_t dmu_objset_type(objset_t *os);
extern uint64_t dmu_objset_id(objset_t *os);
extern uint64_t dmu_objset_dnodesize(objset_t *os);
extern zfs_sync_type_t dmu_objset_syncprop(objset_t *os);
extern zfs_logbias_op_t dmu_objset_logbias(objset_t *os);
extern int dmu_snapshot_list_next(objset_t *os, int namelen, char *name,
    uint64_t *id, uint64_t *offp, boolean_t *case_conflict);
extern int dmu_snapshot_realname(objset_t *os, char *name, char *real,
    int maxlen, boolean_t *conflict);
extern int dmu_dir_list_next(objset_t *os, int namelen, char *name,
    uint64_t *idp, uint64_t *offp);

typedef int objset_used_cb_t(dmu_object_type_t bonustype,
    void *bonus, uint64_t *userp, uint64_t *groupp);
extern void dmu_objset_register_type(dmu_objset_type_t ost,
    objset_used_cb_t *cb);
extern void dmu_objset_set_user(objset_t *os, void *user_ptr);
extern void *dmu_objset_get_user(objset_t *os);

/*
 * Return the txg number for the given assigned transaction.
 */
uint64_t dmu_tx_get_txg(dmu_tx_t *tx);

/*
 * Synchronous write.
 * If a parent zio is provided this function initiates a write on the
 * provided buffer as a child of the parent zio.
 * In the absence of a parent zio, the write is completed synchronously.
 * At write completion, blk is filled with the bp of the written block.
 * Note that while the data covered by this function will be on stable
 * storage when the write completes this new data does not become a
 * permanent part of the file until the associated transaction commits.
 */

/*
 * {zfs,zvol,ztest}_get_done() args
 */
typedef struct zgd {
	struct lwb	*zgd_lwb;
	struct blkptr	*zgd_bp;
	dmu_buf_t	*zgd_db;
	struct rl	*zgd_rl;
	void		*zgd_private;
} zgd_t;

typedef void dmu_sync_cb_t(zgd_t *arg, int error);
int dmu_sync(struct zio *zio, uint64_t txg, dmu_sync_cb_t *done, zgd_t *zgd);

/*
 * Find the next hole or data block in file starting at *off
 * Return found offset in *off. Return ESRCH for end of file.
 */
int dmu_offset_next(objset_t *os, uint64_t object, boolean_t hole,
    uint64_t *off);

/*
 * Check if a DMU object has any dirty blocks. If so, sync out
 * all pending transaction groups. Otherwise, this function
 * does not alter DMU state. This could be improved to only sync
 * out the necessary transaction groups for this particular
 * object.
 */
int dmu_object_wait_synced(objset_t *os, uint64_t object);

/*
 * Initial setup and final teardown.
 */
extern void dmu_init(void);
extern void dmu_fini(void);

typedef void (*dmu_traverse_cb_t)(objset_t *os, void *arg, struct blkptr *bp,
    uint64_t object, uint64_t offset, int len);
void dmu_traverse_objset(objset_t *os, uint64_t txg_start,
    dmu_traverse_cb_t cb, void *arg);
int dmu_diff(const char *tosnap_name, const char *fromsnap_name,
    struct file *fp, offset_t *offp);

/* CRC64 table */
#define	ZFS_CRC64_POLY	0xC96C5795D7870F42ULL	/* ECMA-182, reflected form */
extern uint64_t zfs_crc64_table[256];

extern int zfs_mdcomp_disable;

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_DMU_H */
