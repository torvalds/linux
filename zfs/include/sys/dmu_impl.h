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
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Copyright (c) 2012, Joyent, Inc. All rights reserved.
 * Copyright (c) 2013 by Delphix. All rights reserved.
 */

#ifndef _SYS_DMU_IMPL_H
#define	_SYS_DMU_IMPL_H

#include <sys/txg_impl.h>
#include <sys/zio.h>
#include <sys/dnode.h>
#include <sys/zfs_context.h>
#include <sys/zfs_ioctl.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * This is the locking strategy for the DMU.  Numbers in parenthesis are
 * cases that use that lock order, referenced below:
 *
 * ARC is self-contained
 * bplist is self-contained
 * refcount is self-contained
 * txg is self-contained (hopefully!)
 * zst_lock
 * zf_rwlock
 *
 * XXX try to improve evicting path?
 *
 * dp_config_rwlock > os_obj_lock > dn_struct_rwlock >
 * 	dn_dbufs_mtx > hash_mutexes > db_mtx > dd_lock > leafs
 *
 * dp_config_rwlock
 *    must be held before: everything
 *    protects dd namespace changes
 *    protects property changes globally
 *    held from:
 *    	dsl_dir_open/r:
 *    	dsl_dir_create_sync/w:
 *    	dsl_dir_sync_destroy/w:
 *    	dsl_dir_rename_sync/w:
 *    	dsl_prop_changed_notify/r:
 *
 * os_obj_lock
 *   must be held before:
 *   	everything except dp_config_rwlock
 *   protects os_obj_next
 *   held from:
 *   	dmu_object_alloc: dn_dbufs_mtx, db_mtx, hash_mutexes, dn_struct_rwlock
 *
 * dn_struct_rwlock
 *   must be held before:
 *   	everything except dp_config_rwlock and os_obj_lock
 *   protects structure of dnode (eg. nlevels)
 *   	db_blkptr can change when syncing out change to nlevels
 *   	dn_maxblkid
 *   	dn_nlevels
 *   	dn_*blksz*
 *   	phys nlevels, maxblkid, physical blkptr_t's (?)
 *   held from:
 *   	callers of dbuf_read_impl, dbuf_hold[_impl], dbuf_prefetch
 *   	dmu_object_info_from_dnode: dn_dirty_mtx (dn_datablksz)
 *   	dmu_tx_count_free:
 *   	dbuf_read_impl: db_mtx, dmu_zfetch()
 *   	dmu_zfetch: zf_rwlock/r, zst_lock, dbuf_prefetch()
 *   	dbuf_new_size: db_mtx
 *   	dbuf_dirty: db_mtx
 *	dbuf_findbp: (callers, phys? - the real need)
 *	dbuf_create: dn_dbufs_mtx, hash_mutexes, db_mtx (phys?)
 *	dbuf_prefetch: dn_dirty_mtx, hash_mutexes, db_mtx, dn_dbufs_mtx
 *	dbuf_hold_impl: hash_mutexes, db_mtx, dn_dbufs_mtx, dbuf_findbp()
 *	dnode_sync/w (increase_indirection): db_mtx (phys)
 *	dnode_set_blksz/w: dn_dbufs_mtx (dn_*blksz*)
 *	dnode_new_blkid/w: (dn_maxblkid)
 *	dnode_free_range/w: dn_dirty_mtx (dn_maxblkid)
 *	dnode_next_offset: (phys)
 *
 * dn_dbufs_mtx
 *    must be held before:
 *    	db_mtx, hash_mutexes
 *    protects:
 *    	dn_dbufs
 *    	dn_evicted
 *    held from:
 *    	dmu_evict_user: db_mtx (dn_dbufs)
 *    	dbuf_free_range: db_mtx (dn_dbufs)
 *    	dbuf_remove_ref: db_mtx, callees:
 *    		dbuf_hash_remove: hash_mutexes, db_mtx
 *    	dbuf_create: hash_mutexes, db_mtx (dn_dbufs)
 *    	dnode_set_blksz: (dn_dbufs)
 *
 * hash_mutexes (global)
 *   must be held before:
 *   	db_mtx
 *   protects dbuf_hash_table (global) and db_hash_next
 *   held from:
 *   	dbuf_find: db_mtx
 *   	dbuf_hash_insert: db_mtx
 *   	dbuf_hash_remove: db_mtx
 *
 * db_mtx (meta-leaf)
 *   must be held before:
 *   	dn_mtx, dn_dirty_mtx, dd_lock (leaf mutexes)
 *   protects:
 *   	db_state
 * 	db_holds
 * 	db_buf
 * 	db_changed
 * 	db_data_pending
 * 	db_dirtied
 * 	db_link
 * 	db_dirty_node (??)
 * 	db_dirtycnt
 * 	db_d.*
 * 	db.*
 *   held from:
 * 	dbuf_dirty: dn_mtx, dn_dirty_mtx
 * 	dbuf_dirty->dsl_dir_willuse_space: dd_lock
 * 	dbuf_dirty->dbuf_new_block->dsl_dataset_block_freeable: dd_lock
 * 	dbuf_undirty: dn_dirty_mtx (db_d)
 * 	dbuf_write_done: dn_dirty_mtx (db_state)
 * 	dbuf_*
 * 	dmu_buf_update_user: none (db_d)
 * 	dmu_evict_user: none (db_d) (maybe can eliminate)
 *   	dbuf_find: none (db_holds)
 *   	dbuf_hash_insert: none (db_holds)
 *   	dmu_buf_read_array_impl: none (db_state, db_changed)
 *   	dmu_sync: none (db_dirty_node, db_d)
 *   	dnode_reallocate: none (db)
 *
 * dn_mtx (leaf)
 *   protects:
 *   	dn_dirty_dbufs
 *   	dn_ranges
 *   	phys accounting
 * 	dn_allocated_txg
 * 	dn_free_txg
 * 	dn_assigned_txg
 * 	dd_assigned_tx
 * 	dn_notxholds
 * 	dn_dirtyctx
 * 	dn_dirtyctx_firstset
 * 	(dn_phys copy fields?)
 * 	(dn_phys contents?)
 *   held from:
 *   	dnode_*
 *   	dbuf_dirty: none
 *   	dbuf_sync: none (phys accounting)
 *   	dbuf_undirty: none (dn_ranges, dn_dirty_dbufs)
 *   	dbuf_write_done: none (phys accounting)
 *   	dmu_object_info_from_dnode: none (accounting)
 *   	dmu_tx_commit: none
 *   	dmu_tx_hold_object_impl: none
 *   	dmu_tx_try_assign: dn_notxholds(cv)
 *   	dmu_tx_unassign: none
 *
 * dd_lock
 *    must be held before:
 *      ds_lock
 *      ancestors' dd_lock
 *    protects:
 *    	dd_prop_cbs
 *    	dd_sync_*
 *    	dd_used_bytes
 *    	dd_tempreserved
 *    	dd_space_towrite
 *    	dd_myname
 *    	dd_phys accounting?
 *    held from:
 *    	dsl_dir_*
 *    	dsl_prop_changed_notify: none (dd_prop_cbs)
 *    	dsl_prop_register: none (dd_prop_cbs)
 *    	dsl_prop_unregister: none (dd_prop_cbs)
 *    	dsl_dataset_block_freeable: none (dd_sync_*)
 *
 * os_lock (leaf)
 *   protects:
 *   	os_dirty_dnodes
 *   	os_free_dnodes
 *   	os_dnodes
 *   	os_downgraded_dbufs
 *   	dn_dirtyblksz
 *   	dn_dirty_link
 *   held from:
 *   	dnode_create: none (os_dnodes)
 *   	dnode_destroy: none (os_dnodes)
 *   	dnode_setdirty: none (dn_dirtyblksz, os_*_dnodes)
 *   	dnode_free: none (dn_dirtyblksz, os_*_dnodes)
 *
 * ds_lock
 *    protects:
 *    	ds_objset
 *    	ds_open_refcount
 *    	ds_snapname
 *    	ds_phys accounting
 *	ds_phys userrefs zapobj
 *	ds_reserved
 *    held from:
 *    	dsl_dataset_*
 *
 * dr_mtx (leaf)
 *    protects:
 *	dr_children
 *    held from:
 *	dbuf_dirty
 *	dbuf_undirty
 *	dbuf_sync_indirect
 *	dnode_new_blkid
 */

struct objset;
struct dmu_pool;

typedef struct dmu_xuio {
	int next;
	int cnt;
	struct arc_buf **bufs;
	iovec_t *iovp;
} dmu_xuio_t;

/*
 * The list of data whose inclusion in a send stream can be pending from
 * one call to backup_cb to another.  Multiple calls to dump_free() and
 * dump_freeobjects() can be aggregated into a single DRR_FREE or
 * DRR_FREEOBJECTS replay record.
 */
typedef enum {
	PENDING_NONE,
	PENDING_FREE,
	PENDING_FREEOBJECTS
} dmu_pendop_t;

typedef struct dmu_sendarg {
	list_node_t dsa_link;
	dmu_replay_record_t *dsa_drr;
	vnode_t *dsa_vp;
	int dsa_outfd;
	proc_t *dsa_proc;
	offset_t *dsa_off;
	objset_t *dsa_os;
	zio_cksum_t dsa_zc;
	uint64_t dsa_toguid;
	int dsa_err;
	dmu_pendop_t dsa_pending_op;
	boolean_t dsa_incremental;
	uint64_t dsa_featureflags;
	uint64_t dsa_last_data_object;
	uint64_t dsa_last_data_offset;
} dmu_sendarg_t;

void dmu_object_zapify(objset_t *, uint64_t, dmu_object_type_t, dmu_tx_t *);
void dmu_object_free_zapified(objset_t *, uint64_t, dmu_tx_t *);
int dmu_buf_hold_noread(objset_t *, uint64_t, uint64_t,
    void *, dmu_buf_t **);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_DMU_IMPL_H */
