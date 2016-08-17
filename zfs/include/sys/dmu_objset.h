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
 * Copyright (c) 2013 by Saso Kiselkov. All rights reserved.
 * Copyright (c) 2012, 2014 by Delphix. All rights reserved.
 * Copyright (c) 2014 Spectra Logic Corporation, All rights reserved.
 */

/* Portions Copyright 2010 Robert Milkowski */

#ifndef	_SYS_DMU_OBJSET_H
#define	_SYS_DMU_OBJSET_H

#include <sys/spa.h>
#include <sys/arc.h>
#include <sys/txg.h>
#include <sys/zfs_context.h>
#include <sys/dnode.h>
#include <sys/zio.h>
#include <sys/zil.h>
#include <sys/sa.h>

#ifdef	__cplusplus
extern "C" {
#endif

extern krwlock_t os_lock;

struct dsl_pool;
struct dsl_dataset;
struct dmu_tx;

#define	OBJSET_PHYS_SIZE 2048
#define	OBJSET_OLD_PHYS_SIZE 1024

#define	OBJSET_BUF_HAS_USERUSED(buf) \
	(arc_buf_size(buf) > OBJSET_OLD_PHYS_SIZE)

#define	OBJSET_FLAG_USERACCOUNTING_COMPLETE	(1ULL<<0)

typedef struct objset_phys {
	dnode_phys_t os_meta_dnode;
	zil_header_t os_zil_header;
	uint64_t os_type;
	uint64_t os_flags;
	char os_pad[OBJSET_PHYS_SIZE - sizeof (dnode_phys_t)*3 -
	    sizeof (zil_header_t) - sizeof (uint64_t)*2];
	dnode_phys_t os_userused_dnode;
	dnode_phys_t os_groupused_dnode;
} objset_phys_t;

struct objset {
	/* Immutable: */
	struct dsl_dataset *os_dsl_dataset;
	spa_t *os_spa;
	arc_buf_t *os_phys_buf;
	objset_phys_t *os_phys;
	/*
	 * The following "special" dnodes have no parent, are exempt
	 * from dnode_move(), and are not recorded in os_dnodes, but they
	 * root their descendents in this objset using handles anyway, so
	 * that all access to dnodes from dbufs consistently uses handles.
	 */
	dnode_handle_t os_meta_dnode;
	dnode_handle_t os_userused_dnode;
	dnode_handle_t os_groupused_dnode;
	zilog_t *os_zil;

	list_node_t os_evicting_node;

	/* can change, under dsl_dir's locks: */
	enum zio_checksum os_checksum;
	enum zio_compress os_compress;
	uint8_t os_copies;
	enum zio_checksum os_dedup_checksum;
	boolean_t os_dedup_verify;
	zfs_logbias_op_t os_logbias;
	zfs_cache_type_t os_primary_cache;
	zfs_cache_type_t os_secondary_cache;
	zfs_sync_type_t os_sync;
	zfs_redundant_metadata_type_t os_redundant_metadata;
	int os_recordsize;

	/* no lock needed: */
	struct dmu_tx *os_synctx; /* XXX sketchy */
	blkptr_t *os_rootbp;
	zil_header_t os_zil_header;
	list_t os_synced_dnodes;
	uint64_t os_flags;

	/* Protected by os_obj_lock */
	kmutex_t os_obj_lock;
	uint64_t os_obj_next;

	/* Protected by os_lock */
	kmutex_t os_lock;
	list_t os_dirty_dnodes[TXG_SIZE];
	list_t os_free_dnodes[TXG_SIZE];
	list_t os_dnodes;
	list_t os_downgraded_dbufs;

	/* stuff we store for the user */
	kmutex_t os_user_ptr_lock;
	void *os_user_ptr;
	sa_os_t *os_sa;
};

#define	DMU_META_OBJSET		0
#define	DMU_META_DNODE_OBJECT	0
#define	DMU_OBJECT_IS_SPECIAL(obj) ((int64_t)(obj) <= 0)
#define	DMU_META_DNODE(os)	((os)->os_meta_dnode.dnh_dnode)
#define	DMU_USERUSED_DNODE(os)	((os)->os_userused_dnode.dnh_dnode)
#define	DMU_GROUPUSED_DNODE(os)	((os)->os_groupused_dnode.dnh_dnode)

#define	DMU_OS_IS_L2CACHEABLE(os)				\
	((os)->os_secondary_cache == ZFS_CACHE_ALL ||		\
	(os)->os_secondary_cache == ZFS_CACHE_METADATA)

#define	DMU_OS_IS_L2COMPRESSIBLE(os)	(zfs_mdcomp_disable == B_FALSE)

/* called from zpl */
int dmu_objset_hold(const char *name, void *tag, objset_t **osp);
int dmu_objset_own(const char *name, dmu_objset_type_t type,
    boolean_t readonly, void *tag, objset_t **osp);
int dmu_objset_own_obj(struct dsl_pool *dp, uint64_t obj,
    dmu_objset_type_t type, boolean_t readonly, void *tag, objset_t **osp);
void dmu_objset_refresh_ownership(objset_t *os, void *tag);
void dmu_objset_rele(objset_t *os, void *tag);
void dmu_objset_disown(objset_t *os, void *tag);
int dmu_objset_from_ds(struct dsl_dataset *ds, objset_t **osp);

void dmu_objset_stats(objset_t *os, nvlist_t *nv);
void dmu_objset_fast_stat(objset_t *os, dmu_objset_stats_t *stat);
void dmu_objset_space(objset_t *os, uint64_t *refdbytesp, uint64_t *availbytesp,
    uint64_t *usedobjsp, uint64_t *availobjsp);
uint64_t dmu_objset_fsid_guid(objset_t *os);
int dmu_objset_find_dp(struct dsl_pool *dp, uint64_t ddobj,
    int func(struct dsl_pool *, struct dsl_dataset *, void *),
    void *arg, int flags);
void dmu_objset_evict_dbufs(objset_t *os);
timestruc_t dmu_objset_snap_cmtime(objset_t *os);

/* called from dsl */
void dmu_objset_sync(objset_t *os, zio_t *zio, dmu_tx_t *tx);
boolean_t dmu_objset_is_dirty(objset_t *os, uint64_t txg);
objset_t *dmu_objset_create_impl(spa_t *spa, struct dsl_dataset *ds,
    blkptr_t *bp, dmu_objset_type_t type, dmu_tx_t *tx);
int dmu_objset_open_impl(spa_t *spa, struct dsl_dataset *ds, blkptr_t *bp,
    objset_t **osp);
void dmu_objset_evict(objset_t *os);
void dmu_objset_do_userquota_updates(objset_t *os, dmu_tx_t *tx);
void dmu_objset_userquota_get_ids(dnode_t *dn, boolean_t before, dmu_tx_t *tx);
boolean_t dmu_objset_userused_enabled(objset_t *os);
int dmu_objset_userspace_upgrade(objset_t *os);
boolean_t dmu_objset_userspace_present(objset_t *os);
int dmu_fsname(const char *snapname, char *buf);

void dmu_objset_evict_done(objset_t *os);

void dmu_objset_init(void);
void dmu_objset_fini(void);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_DMU_OBJSET_H */
