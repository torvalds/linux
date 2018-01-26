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
 * Copyright (c) 2013 by Saso Kiselkov. All rights reserved.
 * Copyright (c) 2013, Joyent, Inc. All rights reserved.
 * Copyright (c) 2016, Nexenta Systems, Inc. All rights reserved.
 * Copyright (c) 2015 by Chunwei Chen. All rights reserved.
 */

#include <sys/dmu.h>
#include <sys/dmu_impl.h>
#include <sys/dmu_tx.h>
#include <sys/dbuf.h>
#include <sys/dnode.h>
#include <sys/zfs_context.h>
#include <sys/dmu_objset.h>
#include <sys/dmu_traverse.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_pool.h>
#include <sys/dsl_synctask.h>
#include <sys/dsl_prop.h>
#include <sys/dmu_zfetch.h>
#include <sys/zfs_ioctl.h>
#include <sys/zap.h>
#include <sys/zio_checksum.h>
#include <sys/zio_compress.h>
#include <sys/sa.h>
#include <sys/zfeature.h>
#include <sys/abd.h>
#include <sys/trace_dmu.h>
#include <sys/zfs_rlock.h>
#ifdef _KERNEL
#include <sys/vmsystm.h>
#include <sys/zfs_znode.h>
#endif

/*
 * Enable/disable nopwrite feature.
 */
int zfs_nopwrite_enabled = 1;

/*
 * Tunable to control percentage of dirtied blocks from frees in one TXG.
 * After this threshold is crossed, additional dirty blocks from frees
 * wait until the next TXG.
 * A value of zero will disable this throttle.
 */
unsigned long zfs_per_txg_dirty_frees_percent = 30;

/*
 * Enable/disable forcing txg sync when dirty in dmu_offset_next.
 */
int zfs_dmu_offset_next_sync = 0;

const dmu_object_type_info_t dmu_ot[DMU_OT_NUMTYPES] = {
	{	DMU_BSWAP_UINT8,	TRUE,	"unallocated"		},
	{	DMU_BSWAP_ZAP,		TRUE,	"object directory"	},
	{	DMU_BSWAP_UINT64,	TRUE,	"object array"		},
	{	DMU_BSWAP_UINT8,	TRUE,	"packed nvlist"		},
	{	DMU_BSWAP_UINT64,	TRUE,	"packed nvlist size"	},
	{	DMU_BSWAP_UINT64,	TRUE,	"bpobj"			},
	{	DMU_BSWAP_UINT64,	TRUE,	"bpobj header"		},
	{	DMU_BSWAP_UINT64,	TRUE,	"SPA space map header"	},
	{	DMU_BSWAP_UINT64,	TRUE,	"SPA space map"		},
	{	DMU_BSWAP_UINT64,	TRUE,	"ZIL intent log"	},
	{	DMU_BSWAP_DNODE,	TRUE,	"DMU dnode"		},
	{	DMU_BSWAP_OBJSET,	TRUE,	"DMU objset"		},
	{	DMU_BSWAP_UINT64,	TRUE,	"DSL directory"		},
	{	DMU_BSWAP_ZAP,		TRUE,	"DSL directory child map"},
	{	DMU_BSWAP_ZAP,		TRUE,	"DSL dataset snap map"	},
	{	DMU_BSWAP_ZAP,		TRUE,	"DSL props"		},
	{	DMU_BSWAP_UINT64,	TRUE,	"DSL dataset"		},
	{	DMU_BSWAP_ZNODE,	TRUE,	"ZFS znode"		},
	{	DMU_BSWAP_OLDACL,	TRUE,	"ZFS V0 ACL"		},
	{	DMU_BSWAP_UINT8,	FALSE,	"ZFS plain file"	},
	{	DMU_BSWAP_ZAP,		TRUE,	"ZFS directory"		},
	{	DMU_BSWAP_ZAP,		TRUE,	"ZFS master node"	},
	{	DMU_BSWAP_ZAP,		TRUE,	"ZFS delete queue"	},
	{	DMU_BSWAP_UINT8,	FALSE,	"zvol object"		},
	{	DMU_BSWAP_ZAP,		TRUE,	"zvol prop"		},
	{	DMU_BSWAP_UINT8,	FALSE,	"other uint8[]"		},
	{	DMU_BSWAP_UINT64,	FALSE,	"other uint64[]"	},
	{	DMU_BSWAP_ZAP,		TRUE,	"other ZAP"		},
	{	DMU_BSWAP_ZAP,		TRUE,	"persistent error log"	},
	{	DMU_BSWAP_UINT8,	TRUE,	"SPA history"		},
	{	DMU_BSWAP_UINT64,	TRUE,	"SPA history offsets"	},
	{	DMU_BSWAP_ZAP,		TRUE,	"Pool properties"	},
	{	DMU_BSWAP_ZAP,		TRUE,	"DSL permissions"	},
	{	DMU_BSWAP_ACL,		TRUE,	"ZFS ACL"		},
	{	DMU_BSWAP_UINT8,	TRUE,	"ZFS SYSACL"		},
	{	DMU_BSWAP_UINT8,	TRUE,	"FUID table"		},
	{	DMU_BSWAP_UINT64,	TRUE,	"FUID table size"	},
	{	DMU_BSWAP_ZAP,		TRUE,	"DSL dataset next clones"},
	{	DMU_BSWAP_ZAP,		TRUE,	"scan work queue"	},
	{	DMU_BSWAP_ZAP,		TRUE,	"ZFS user/group used"	},
	{	DMU_BSWAP_ZAP,		TRUE,	"ZFS user/group quota"	},
	{	DMU_BSWAP_ZAP,		TRUE,	"snapshot refcount tags"},
	{	DMU_BSWAP_ZAP,		TRUE,	"DDT ZAP algorithm"	},
	{	DMU_BSWAP_ZAP,		TRUE,	"DDT statistics"	},
	{	DMU_BSWAP_UINT8,	TRUE,	"System attributes"	},
	{	DMU_BSWAP_ZAP,		TRUE,	"SA master node"	},
	{	DMU_BSWAP_ZAP,		TRUE,	"SA attr registration"	},
	{	DMU_BSWAP_ZAP,		TRUE,	"SA attr layouts"	},
	{	DMU_BSWAP_ZAP,		TRUE,	"scan translations"	},
	{	DMU_BSWAP_UINT8,	FALSE,	"deduplicated block"	},
	{	DMU_BSWAP_ZAP,		TRUE,	"DSL deadlist map"	},
	{	DMU_BSWAP_UINT64,	TRUE,	"DSL deadlist map hdr"	},
	{	DMU_BSWAP_ZAP,		TRUE,	"DSL dir clones"	},
	{	DMU_BSWAP_UINT64,	TRUE,	"bpobj subobj"		}
};

const dmu_object_byteswap_info_t dmu_ot_byteswap[DMU_BSWAP_NUMFUNCS] = {
	{	byteswap_uint8_array,	"uint8"		},
	{	byteswap_uint16_array,	"uint16"	},
	{	byteswap_uint32_array,	"uint32"	},
	{	byteswap_uint64_array,	"uint64"	},
	{	zap_byteswap,		"zap"		},
	{	dnode_buf_byteswap,	"dnode"		},
	{	dmu_objset_byteswap,	"objset"	},
	{	zfs_znode_byteswap,	"znode"		},
	{	zfs_oldacl_byteswap,	"oldacl"	},
	{	zfs_acl_byteswap,	"acl"		}
};

int
dmu_buf_hold_noread_by_dnode(dnode_t *dn, uint64_t offset,
    void *tag, dmu_buf_t **dbp)
{
	uint64_t blkid;
	dmu_buf_impl_t *db;

	blkid = dbuf_whichblock(dn, 0, offset);
	rw_enter(&dn->dn_struct_rwlock, RW_READER);
	db = dbuf_hold(dn, blkid, tag);
	rw_exit(&dn->dn_struct_rwlock);

	if (db == NULL) {
		*dbp = NULL;
		return (SET_ERROR(EIO));
	}

	*dbp = &db->db;
	return (0);
}
int
dmu_buf_hold_noread(objset_t *os, uint64_t object, uint64_t offset,
    void *tag, dmu_buf_t **dbp)
{
	dnode_t *dn;
	uint64_t blkid;
	dmu_buf_impl_t *db;
	int err;

	err = dnode_hold(os, object, FTAG, &dn);
	if (err)
		return (err);
	blkid = dbuf_whichblock(dn, 0, offset);
	rw_enter(&dn->dn_struct_rwlock, RW_READER);
	db = dbuf_hold(dn, blkid, tag);
	rw_exit(&dn->dn_struct_rwlock);
	dnode_rele(dn, FTAG);

	if (db == NULL) {
		*dbp = NULL;
		return (SET_ERROR(EIO));
	}

	*dbp = &db->db;
	return (err);
}

int
dmu_buf_hold_by_dnode(dnode_t *dn, uint64_t offset,
    void *tag, dmu_buf_t **dbp, int flags)
{
	int err;
	int db_flags = DB_RF_CANFAIL;

	if (flags & DMU_READ_NO_PREFETCH)
		db_flags |= DB_RF_NOPREFETCH;

	err = dmu_buf_hold_noread_by_dnode(dn, offset, tag, dbp);
	if (err == 0) {
		dmu_buf_impl_t *db = (dmu_buf_impl_t *)(*dbp);
		err = dbuf_read(db, NULL, db_flags);
		if (err != 0) {
			dbuf_rele(db, tag);
			*dbp = NULL;
		}
	}

	return (err);
}

int
dmu_buf_hold(objset_t *os, uint64_t object, uint64_t offset,
    void *tag, dmu_buf_t **dbp, int flags)
{
	int err;
	int db_flags = DB_RF_CANFAIL;

	if (flags & DMU_READ_NO_PREFETCH)
		db_flags |= DB_RF_NOPREFETCH;

	err = dmu_buf_hold_noread(os, object, offset, tag, dbp);
	if (err == 0) {
		dmu_buf_impl_t *db = (dmu_buf_impl_t *)(*dbp);
		err = dbuf_read(db, NULL, db_flags);
		if (err != 0) {
			dbuf_rele(db, tag);
			*dbp = NULL;
		}
	}

	return (err);
}

int
dmu_bonus_max(void)
{
	return (DN_OLD_MAX_BONUSLEN);
}

int
dmu_set_bonus(dmu_buf_t *db_fake, int newsize, dmu_tx_t *tx)
{
	dmu_buf_impl_t *db = (dmu_buf_impl_t *)db_fake;
	dnode_t *dn;
	int error;

	DB_DNODE_ENTER(db);
	dn = DB_DNODE(db);

	if (dn->dn_bonus != db) {
		error = SET_ERROR(EINVAL);
	} else if (newsize < 0 || newsize > db_fake->db_size) {
		error = SET_ERROR(EINVAL);
	} else {
		dnode_setbonuslen(dn, newsize, tx);
		error = 0;
	}

	DB_DNODE_EXIT(db);
	return (error);
}

int
dmu_set_bonustype(dmu_buf_t *db_fake, dmu_object_type_t type, dmu_tx_t *tx)
{
	dmu_buf_impl_t *db = (dmu_buf_impl_t *)db_fake;
	dnode_t *dn;
	int error;

	DB_DNODE_ENTER(db);
	dn = DB_DNODE(db);

	if (!DMU_OT_IS_VALID(type)) {
		error = SET_ERROR(EINVAL);
	} else if (dn->dn_bonus != db) {
		error = SET_ERROR(EINVAL);
	} else {
		dnode_setbonus_type(dn, type, tx);
		error = 0;
	}

	DB_DNODE_EXIT(db);
	return (error);
}

dmu_object_type_t
dmu_get_bonustype(dmu_buf_t *db_fake)
{
	dmu_buf_impl_t *db = (dmu_buf_impl_t *)db_fake;
	dnode_t *dn;
	dmu_object_type_t type;

	DB_DNODE_ENTER(db);
	dn = DB_DNODE(db);
	type = dn->dn_bonustype;
	DB_DNODE_EXIT(db);

	return (type);
}

int
dmu_rm_spill(objset_t *os, uint64_t object, dmu_tx_t *tx)
{
	dnode_t *dn;
	int error;

	error = dnode_hold(os, object, FTAG, &dn);
	dbuf_rm_spill(dn, tx);
	rw_enter(&dn->dn_struct_rwlock, RW_WRITER);
	dnode_rm_spill(dn, tx);
	rw_exit(&dn->dn_struct_rwlock);
	dnode_rele(dn, FTAG);
	return (error);
}

/*
 * returns ENOENT, EIO, or 0.
 */
int
dmu_bonus_hold(objset_t *os, uint64_t object, void *tag, dmu_buf_t **dbp)
{
	dnode_t *dn;
	dmu_buf_impl_t *db;
	int error;

	error = dnode_hold(os, object, FTAG, &dn);
	if (error)
		return (error);

	rw_enter(&dn->dn_struct_rwlock, RW_READER);
	if (dn->dn_bonus == NULL) {
		rw_exit(&dn->dn_struct_rwlock);
		rw_enter(&dn->dn_struct_rwlock, RW_WRITER);
		if (dn->dn_bonus == NULL)
			dbuf_create_bonus(dn);
	}
	db = dn->dn_bonus;

	/* as long as the bonus buf is held, the dnode will be held */
	if (refcount_add(&db->db_holds, tag) == 1) {
		VERIFY(dnode_add_ref(dn, db));
		atomic_inc_32(&dn->dn_dbufs_count);
	}

	/*
	 * Wait to drop dn_struct_rwlock until after adding the bonus dbuf's
	 * hold and incrementing the dbuf count to ensure that dnode_move() sees
	 * a dnode hold for every dbuf.
	 */
	rw_exit(&dn->dn_struct_rwlock);

	dnode_rele(dn, FTAG);

	VERIFY(0 == dbuf_read(db, NULL, DB_RF_MUST_SUCCEED | DB_RF_NOPREFETCH));

	*dbp = &db->db;
	return (0);
}

/*
 * returns ENOENT, EIO, or 0.
 *
 * This interface will allocate a blank spill dbuf when a spill blk
 * doesn't already exist on the dnode.
 *
 * if you only want to find an already existing spill db, then
 * dmu_spill_hold_existing() should be used.
 */
int
dmu_spill_hold_by_dnode(dnode_t *dn, uint32_t flags, void *tag, dmu_buf_t **dbp)
{
	dmu_buf_impl_t *db = NULL;
	int err;

	if ((flags & DB_RF_HAVESTRUCT) == 0)
		rw_enter(&dn->dn_struct_rwlock, RW_READER);

	db = dbuf_hold(dn, DMU_SPILL_BLKID, tag);

	if ((flags & DB_RF_HAVESTRUCT) == 0)
		rw_exit(&dn->dn_struct_rwlock);

	if (db == NULL) {
		*dbp = NULL;
		return (SET_ERROR(EIO));
	}
	err = dbuf_read(db, NULL, flags);
	if (err == 0)
		*dbp = &db->db;
	else {
		dbuf_rele(db, tag);
		*dbp = NULL;
	}
	return (err);
}

int
dmu_spill_hold_existing(dmu_buf_t *bonus, void *tag, dmu_buf_t **dbp)
{
	dmu_buf_impl_t *db = (dmu_buf_impl_t *)bonus;
	dnode_t *dn;
	int err;

	DB_DNODE_ENTER(db);
	dn = DB_DNODE(db);

	if (spa_version(dn->dn_objset->os_spa) < SPA_VERSION_SA) {
		err = SET_ERROR(EINVAL);
	} else {
		rw_enter(&dn->dn_struct_rwlock, RW_READER);

		if (!dn->dn_have_spill) {
			err = SET_ERROR(ENOENT);
		} else {
			err = dmu_spill_hold_by_dnode(dn,
			    DB_RF_HAVESTRUCT | DB_RF_CANFAIL, tag, dbp);
		}

		rw_exit(&dn->dn_struct_rwlock);
	}

	DB_DNODE_EXIT(db);
	return (err);
}

int
dmu_spill_hold_by_bonus(dmu_buf_t *bonus, void *tag, dmu_buf_t **dbp)
{
	dmu_buf_impl_t *db = (dmu_buf_impl_t *)bonus;
	dnode_t *dn;
	int err;

	DB_DNODE_ENTER(db);
	dn = DB_DNODE(db);
	err = dmu_spill_hold_by_dnode(dn, DB_RF_CANFAIL, tag, dbp);
	DB_DNODE_EXIT(db);

	return (err);
}

/*
 * Note: longer-term, we should modify all of the dmu_buf_*() interfaces
 * to take a held dnode rather than <os, object> -- the lookup is wasteful,
 * and can induce severe lock contention when writing to several files
 * whose dnodes are in the same block.
 */
static int
dmu_buf_hold_array_by_dnode(dnode_t *dn, uint64_t offset, uint64_t length,
    boolean_t read, void *tag, int *numbufsp, dmu_buf_t ***dbpp, uint32_t flags)
{
	dmu_buf_t **dbp;
	uint64_t blkid, nblks, i;
	uint32_t dbuf_flags;
	int err;
	zio_t *zio;

	ASSERT(length <= DMU_MAX_ACCESS);

	/*
	 * Note: We directly notify the prefetch code of this read, so that
	 * we can tell it about the multi-block read.  dbuf_read() only knows
	 * about the one block it is accessing.
	 */
	dbuf_flags = DB_RF_CANFAIL | DB_RF_NEVERWAIT | DB_RF_HAVESTRUCT |
	    DB_RF_NOPREFETCH;

	rw_enter(&dn->dn_struct_rwlock, RW_READER);
	if (dn->dn_datablkshift) {
		int blkshift = dn->dn_datablkshift;
		nblks = (P2ROUNDUP(offset + length, 1ULL << blkshift) -
		    P2ALIGN(offset, 1ULL << blkshift)) >> blkshift;
	} else {
		if (offset + length > dn->dn_datablksz) {
			zfs_panic_recover("zfs: accessing past end of object "
			    "%llx/%llx (size=%u access=%llu+%llu)",
			    (longlong_t)dn->dn_objset->
			    os_dsl_dataset->ds_object,
			    (longlong_t)dn->dn_object, dn->dn_datablksz,
			    (longlong_t)offset, (longlong_t)length);
			rw_exit(&dn->dn_struct_rwlock);
			return (SET_ERROR(EIO));
		}
		nblks = 1;
	}
	dbp = kmem_zalloc(sizeof (dmu_buf_t *) * nblks, KM_SLEEP);

	zio = zio_root(dn->dn_objset->os_spa, NULL, NULL, ZIO_FLAG_CANFAIL);
	blkid = dbuf_whichblock(dn, 0, offset);
	for (i = 0; i < nblks; i++) {
		dmu_buf_impl_t *db = dbuf_hold(dn, blkid + i, tag);
		if (db == NULL) {
			rw_exit(&dn->dn_struct_rwlock);
			dmu_buf_rele_array(dbp, nblks, tag);
			zio_nowait(zio);
			return (SET_ERROR(EIO));
		}

		/* initiate async i/o */
		if (read)
			(void) dbuf_read(db, zio, dbuf_flags);
		dbp[i] = &db->db;
	}

	if ((flags & DMU_READ_NO_PREFETCH) == 0 &&
	    DNODE_META_IS_CACHEABLE(dn) && length <= zfetch_array_rd_sz) {
		dmu_zfetch(&dn->dn_zfetch, blkid, nblks,
		    read && DNODE_IS_CACHEABLE(dn));
	}
	rw_exit(&dn->dn_struct_rwlock);

	/* wait for async i/o */
	err = zio_wait(zio);
	if (err) {
		dmu_buf_rele_array(dbp, nblks, tag);
		return (err);
	}

	/* wait for other io to complete */
	if (read) {
		for (i = 0; i < nblks; i++) {
			dmu_buf_impl_t *db = (dmu_buf_impl_t *)dbp[i];
			mutex_enter(&db->db_mtx);
			while (db->db_state == DB_READ ||
			    db->db_state == DB_FILL)
				cv_wait(&db->db_changed, &db->db_mtx);
			if (db->db_state == DB_UNCACHED)
				err = SET_ERROR(EIO);
			mutex_exit(&db->db_mtx);
			if (err) {
				dmu_buf_rele_array(dbp, nblks, tag);
				return (err);
			}
		}
	}

	*numbufsp = nblks;
	*dbpp = dbp;
	return (0);
}

static int
dmu_buf_hold_array(objset_t *os, uint64_t object, uint64_t offset,
    uint64_t length, int read, void *tag, int *numbufsp, dmu_buf_t ***dbpp)
{
	dnode_t *dn;
	int err;

	err = dnode_hold(os, object, FTAG, &dn);
	if (err)
		return (err);

	err = dmu_buf_hold_array_by_dnode(dn, offset, length, read, tag,
	    numbufsp, dbpp, DMU_READ_PREFETCH);

	dnode_rele(dn, FTAG);

	return (err);
}

int
dmu_buf_hold_array_by_bonus(dmu_buf_t *db_fake, uint64_t offset,
    uint64_t length, boolean_t read, void *tag, int *numbufsp,
    dmu_buf_t ***dbpp)
{
	dmu_buf_impl_t *db = (dmu_buf_impl_t *)db_fake;
	dnode_t *dn;
	int err;

	DB_DNODE_ENTER(db);
	dn = DB_DNODE(db);
	err = dmu_buf_hold_array_by_dnode(dn, offset, length, read, tag,
	    numbufsp, dbpp, DMU_READ_PREFETCH);
	DB_DNODE_EXIT(db);

	return (err);
}

void
dmu_buf_rele_array(dmu_buf_t **dbp_fake, int numbufs, void *tag)
{
	int i;
	dmu_buf_impl_t **dbp = (dmu_buf_impl_t **)dbp_fake;

	if (numbufs == 0)
		return;

	for (i = 0; i < numbufs; i++) {
		if (dbp[i])
			dbuf_rele(dbp[i], tag);
	}

	kmem_free(dbp, sizeof (dmu_buf_t *) * numbufs);
}

/*
 * Issue prefetch i/os for the given blocks.  If level is greater than 0, the
 * indirect blocks prefeteched will be those that point to the blocks containing
 * the data starting at offset, and continuing to offset + len.
 *
 * Note that if the indirect blocks above the blocks being prefetched are not in
 * cache, they will be asychronously read in.
 */
void
dmu_prefetch(objset_t *os, uint64_t object, int64_t level, uint64_t offset,
    uint64_t len, zio_priority_t pri)
{
	dnode_t *dn;
	uint64_t blkid;
	int nblks, err;

	if (len == 0) {  /* they're interested in the bonus buffer */
		dn = DMU_META_DNODE(os);

		if (object == 0 || object >= DN_MAX_OBJECT)
			return;

		rw_enter(&dn->dn_struct_rwlock, RW_READER);
		blkid = dbuf_whichblock(dn, level,
		    object * sizeof (dnode_phys_t));
		dbuf_prefetch(dn, level, blkid, pri, 0);
		rw_exit(&dn->dn_struct_rwlock);
		return;
	}

	/*
	 * XXX - Note, if the dnode for the requested object is not
	 * already cached, we will do a *synchronous* read in the
	 * dnode_hold() call.  The same is true for any indirects.
	 */
	err = dnode_hold(os, object, FTAG, &dn);
	if (err != 0)
		return;

	rw_enter(&dn->dn_struct_rwlock, RW_READER);
	/*
	 * offset + len - 1 is the last byte we want to prefetch for, and offset
	 * is the first.  Then dbuf_whichblk(dn, level, off + len - 1) is the
	 * last block we want to prefetch, and dbuf_whichblock(dn, level,
	 * offset)  is the first.  Then the number we need to prefetch is the
	 * last - first + 1.
	 */
	if (level > 0 || dn->dn_datablkshift != 0) {
		nblks = dbuf_whichblock(dn, level, offset + len - 1) -
		    dbuf_whichblock(dn, level, offset) + 1;
	} else {
		nblks = (offset < dn->dn_datablksz);
	}

	if (nblks != 0) {
		int i;

		blkid = dbuf_whichblock(dn, level, offset);
		for (i = 0; i < nblks; i++)
			dbuf_prefetch(dn, level, blkid + i, pri, 0);
	}

	rw_exit(&dn->dn_struct_rwlock);

	dnode_rele(dn, FTAG);
}

/*
 * Get the next "chunk" of file data to free.  We traverse the file from
 * the end so that the file gets shorter over time (if we crashes in the
 * middle, this will leave us in a better state).  We find allocated file
 * data by simply searching the allocated level 1 indirects.
 *
 * On input, *start should be the first offset that does not need to be
 * freed (e.g. "offset + length").  On return, *start will be the first
 * offset that should be freed.
 */
static int
get_next_chunk(dnode_t *dn, uint64_t *start, uint64_t minimum)
{
	uint64_t maxblks = DMU_MAX_ACCESS >> (dn->dn_indblkshift + 1);
	/* bytes of data covered by a level-1 indirect block */
	uint64_t iblkrange =
	    dn->dn_datablksz * EPB(dn->dn_indblkshift, SPA_BLKPTRSHIFT);
	uint64_t blks;

	ASSERT3U(minimum, <=, *start);

	if (*start - minimum <= iblkrange * maxblks) {
		*start = minimum;
		return (0);
	}
	ASSERT(ISP2(iblkrange));

	for (blks = 0; *start > minimum && blks < maxblks; blks++) {
		int err;

		/*
		 * dnode_next_offset(BACKWARDS) will find an allocated L1
		 * indirect block at or before the input offset.  We must
		 * decrement *start so that it is at the end of the region
		 * to search.
		 */
		(*start)--;
		err = dnode_next_offset(dn,
		    DNODE_FIND_BACKWARDS, start, 2, 1, 0);

		/* if there are no indirect blocks before start, we are done */
		if (err == ESRCH) {
			*start = minimum;
			break;
		} else if (err != 0) {
			return (err);
		}

		/* set start to the beginning of this L1 indirect */
		*start = P2ALIGN(*start, iblkrange);
	}
	if (*start < minimum)
		*start = minimum;
	return (0);
}

/*
 * If this objset is of type OST_ZFS return true if vfs's unmounted flag is set,
 * otherwise return false.
 * Used below in dmu_free_long_range_impl() to enable abort when unmounting
 */
/*ARGSUSED*/
static boolean_t
dmu_objset_zfs_unmounting(objset_t *os)
{
#ifdef _KERNEL
	if (dmu_objset_type(os) == DMU_OST_ZFS)
		return (zfs_get_vfs_flag_unmounted(os));
#endif
	return (B_FALSE);
}

static int
dmu_free_long_range_impl(objset_t *os, dnode_t *dn, uint64_t offset,
    uint64_t length)
{
	uint64_t object_size;
	int err;
	uint64_t dirty_frees_threshold;
	dsl_pool_t *dp = dmu_objset_pool(os);
	int t;

	if (dn == NULL)
		return (SET_ERROR(EINVAL));

	object_size = (dn->dn_maxblkid + 1) * dn->dn_datablksz;
	if (offset >= object_size)
		return (0);

	if (zfs_per_txg_dirty_frees_percent <= 100)
		dirty_frees_threshold =
		    zfs_per_txg_dirty_frees_percent * zfs_dirty_data_max / 100;
	else
		dirty_frees_threshold = zfs_dirty_data_max / 4;

	if (length == DMU_OBJECT_END || offset + length > object_size)
		length = object_size - offset;

	while (length != 0) {
		uint64_t chunk_end, chunk_begin, chunk_len;
		uint64_t long_free_dirty_all_txgs = 0;
		dmu_tx_t *tx;

		if (dmu_objset_zfs_unmounting(dn->dn_objset))
			return (SET_ERROR(EINTR));

		chunk_end = chunk_begin = offset + length;

		/* move chunk_begin backwards to the beginning of this chunk */
		err = get_next_chunk(dn, &chunk_begin, offset);
		if (err)
			return (err);
		ASSERT3U(chunk_begin, >=, offset);
		ASSERT3U(chunk_begin, <=, chunk_end);

		chunk_len = chunk_end - chunk_begin;

		mutex_enter(&dp->dp_lock);
		for (t = 0; t < TXG_SIZE; t++) {
			long_free_dirty_all_txgs +=
			    dp->dp_long_free_dirty_pertxg[t];
		}
		mutex_exit(&dp->dp_lock);

		/*
		 * To avoid filling up a TXG with just frees wait for
		 * the next TXG to open before freeing more chunks if
		 * we have reached the threshold of frees
		 */
		if (dirty_frees_threshold != 0 &&
		    long_free_dirty_all_txgs >= dirty_frees_threshold) {
			txg_wait_open(dp, 0);
			continue;
		}

		tx = dmu_tx_create(os);
		dmu_tx_hold_free(tx, dn->dn_object, chunk_begin, chunk_len);

		/*
		 * Mark this transaction as typically resulting in a net
		 * reduction in space used.
		 */
		dmu_tx_mark_netfree(tx);
		err = dmu_tx_assign(tx, TXG_WAIT);
		if (err) {
			dmu_tx_abort(tx);
			return (err);
		}

		mutex_enter(&dp->dp_lock);
		dp->dp_long_free_dirty_pertxg[dmu_tx_get_txg(tx) & TXG_MASK] +=
		    chunk_len;
		mutex_exit(&dp->dp_lock);
		DTRACE_PROBE3(free__long__range,
		    uint64_t, long_free_dirty_all_txgs, uint64_t, chunk_len,
		    uint64_t, dmu_tx_get_txg(tx));
		dnode_free_range(dn, chunk_begin, chunk_len, tx);
		dmu_tx_commit(tx);

		length -= chunk_len;
	}
	return (0);
}

int
dmu_free_long_range(objset_t *os, uint64_t object,
    uint64_t offset, uint64_t length)
{
	dnode_t *dn;
	int err;

	err = dnode_hold(os, object, FTAG, &dn);
	if (err != 0)
		return (err);
	err = dmu_free_long_range_impl(os, dn, offset, length);

	/*
	 * It is important to zero out the maxblkid when freeing the entire
	 * file, so that (a) subsequent calls to dmu_free_long_range_impl()
	 * will take the fast path, and (b) dnode_reallocate() can verify
	 * that the entire file has been freed.
	 */
	if (err == 0 && offset == 0 && length == DMU_OBJECT_END)
		dn->dn_maxblkid = 0;

	dnode_rele(dn, FTAG);
	return (err);
}

int
dmu_free_long_object(objset_t *os, uint64_t object)
{
	dmu_tx_t *tx;
	int err;

	err = dmu_free_long_range(os, object, 0, DMU_OBJECT_END);
	if (err != 0)
		return (err);

	tx = dmu_tx_create(os);
	dmu_tx_hold_bonus(tx, object);
	dmu_tx_hold_free(tx, object, 0, DMU_OBJECT_END);
	dmu_tx_mark_netfree(tx);
	err = dmu_tx_assign(tx, TXG_WAIT);
	if (err == 0) {
		err = dmu_object_free(os, object, tx);
		dmu_tx_commit(tx);
	} else {
		dmu_tx_abort(tx);
	}

	return (err);
}

int
dmu_free_range(objset_t *os, uint64_t object, uint64_t offset,
    uint64_t size, dmu_tx_t *tx)
{
	dnode_t *dn;
	int err = dnode_hold(os, object, FTAG, &dn);
	if (err)
		return (err);
	ASSERT(offset < UINT64_MAX);
	ASSERT(size == -1ULL || size <= UINT64_MAX - offset);
	dnode_free_range(dn, offset, size, tx);
	dnode_rele(dn, FTAG);
	return (0);
}

static int
dmu_read_impl(dnode_t *dn, uint64_t offset, uint64_t size,
    void *buf, uint32_t flags)
{
	dmu_buf_t **dbp;
	int numbufs, err = 0;

	/*
	 * Deal with odd block sizes, where there can't be data past the first
	 * block.  If we ever do the tail block optimization, we will need to
	 * handle that here as well.
	 */
	if (dn->dn_maxblkid == 0) {
		uint64_t newsz = offset > dn->dn_datablksz ? 0 :
		    MIN(size, dn->dn_datablksz - offset);
		bzero((char *)buf + newsz, size - newsz);
		size = newsz;
	}

	while (size > 0) {
		uint64_t mylen = MIN(size, DMU_MAX_ACCESS / 2);
		int i;

		/*
		 * NB: we could do this block-at-a-time, but it's nice
		 * to be reading in parallel.
		 */
		err = dmu_buf_hold_array_by_dnode(dn, offset, mylen,
		    TRUE, FTAG, &numbufs, &dbp, flags);
		if (err)
			break;

		for (i = 0; i < numbufs; i++) {
			uint64_t tocpy;
			int64_t bufoff;
			dmu_buf_t *db = dbp[i];

			ASSERT(size > 0);

			bufoff = offset - db->db_offset;
			tocpy = MIN(db->db_size - bufoff, size);

			(void) memcpy(buf, (char *)db->db_data + bufoff, tocpy);

			offset += tocpy;
			size -= tocpy;
			buf = (char *)buf + tocpy;
		}
		dmu_buf_rele_array(dbp, numbufs, FTAG);
	}
	return (err);
}

int
dmu_read(objset_t *os, uint64_t object, uint64_t offset, uint64_t size,
    void *buf, uint32_t flags)
{
	dnode_t *dn;
	int err;

	err = dnode_hold(os, object, FTAG, &dn);
	if (err != 0)
		return (err);

	err = dmu_read_impl(dn, offset, size, buf, flags);
	dnode_rele(dn, FTAG);
	return (err);
}

int
dmu_read_by_dnode(dnode_t *dn, uint64_t offset, uint64_t size, void *buf,
    uint32_t flags)
{
	return (dmu_read_impl(dn, offset, size, buf, flags));
}

static void
dmu_write_impl(dmu_buf_t **dbp, int numbufs, uint64_t offset, uint64_t size,
    const void *buf, dmu_tx_t *tx)
{
	int i;

	for (i = 0; i < numbufs; i++) {
		uint64_t tocpy;
		int64_t bufoff;
		dmu_buf_t *db = dbp[i];

		ASSERT(size > 0);

		bufoff = offset - db->db_offset;
		tocpy = MIN(db->db_size - bufoff, size);

		ASSERT(i == 0 || i == numbufs-1 || tocpy == db->db_size);

		if (tocpy == db->db_size)
			dmu_buf_will_fill(db, tx);
		else
			dmu_buf_will_dirty(db, tx);

		(void) memcpy((char *)db->db_data + bufoff, buf, tocpy);

		if (tocpy == db->db_size)
			dmu_buf_fill_done(db, tx);

		offset += tocpy;
		size -= tocpy;
		buf = (char *)buf + tocpy;
	}
}

void
dmu_write(objset_t *os, uint64_t object, uint64_t offset, uint64_t size,
    const void *buf, dmu_tx_t *tx)
{
	dmu_buf_t **dbp;
	int numbufs;

	if (size == 0)
		return;

	VERIFY0(dmu_buf_hold_array(os, object, offset, size,
	    FALSE, FTAG, &numbufs, &dbp));
	dmu_write_impl(dbp, numbufs, offset, size, buf, tx);
	dmu_buf_rele_array(dbp, numbufs, FTAG);
}

void
dmu_write_by_dnode(dnode_t *dn, uint64_t offset, uint64_t size,
    const void *buf, dmu_tx_t *tx)
{
	dmu_buf_t **dbp;
	int numbufs;

	if (size == 0)
		return;

	VERIFY0(dmu_buf_hold_array_by_dnode(dn, offset, size,
	    FALSE, FTAG, &numbufs, &dbp, DMU_READ_PREFETCH));
	dmu_write_impl(dbp, numbufs, offset, size, buf, tx);
	dmu_buf_rele_array(dbp, numbufs, FTAG);
}

void
dmu_prealloc(objset_t *os, uint64_t object, uint64_t offset, uint64_t size,
    dmu_tx_t *tx)
{
	dmu_buf_t **dbp;
	int numbufs, i;

	if (size == 0)
		return;

	VERIFY(0 == dmu_buf_hold_array(os, object, offset, size,
	    FALSE, FTAG, &numbufs, &dbp));

	for (i = 0; i < numbufs; i++) {
		dmu_buf_t *db = dbp[i];

		dmu_buf_will_not_fill(db, tx);
	}
	dmu_buf_rele_array(dbp, numbufs, FTAG);
}

void
dmu_write_embedded(objset_t *os, uint64_t object, uint64_t offset,
    void *data, uint8_t etype, uint8_t comp, int uncompressed_size,
    int compressed_size, int byteorder, dmu_tx_t *tx)
{
	dmu_buf_t *db;

	ASSERT3U(etype, <, NUM_BP_EMBEDDED_TYPES);
	ASSERT3U(comp, <, ZIO_COMPRESS_FUNCTIONS);
	VERIFY0(dmu_buf_hold_noread(os, object, offset,
	    FTAG, &db));

	dmu_buf_write_embedded(db,
	    data, (bp_embedded_type_t)etype, (enum zio_compress)comp,
	    uncompressed_size, compressed_size, byteorder, tx);

	dmu_buf_rele(db, FTAG);
}

/*
 * DMU support for xuio
 */
kstat_t *xuio_ksp = NULL;

typedef struct xuio_stats {
	/* loaned yet not returned arc_buf */
	kstat_named_t xuiostat_onloan_rbuf;
	kstat_named_t xuiostat_onloan_wbuf;
	/* whether a copy is made when loaning out a read buffer */
	kstat_named_t xuiostat_rbuf_copied;
	kstat_named_t xuiostat_rbuf_nocopy;
	/* whether a copy is made when assigning a write buffer */
	kstat_named_t xuiostat_wbuf_copied;
	kstat_named_t xuiostat_wbuf_nocopy;
} xuio_stats_t;

static xuio_stats_t xuio_stats = {
	{ "onloan_read_buf",	KSTAT_DATA_UINT64 },
	{ "onloan_write_buf",	KSTAT_DATA_UINT64 },
	{ "read_buf_copied",	KSTAT_DATA_UINT64 },
	{ "read_buf_nocopy",	KSTAT_DATA_UINT64 },
	{ "write_buf_copied",	KSTAT_DATA_UINT64 },
	{ "write_buf_nocopy",	KSTAT_DATA_UINT64 }
};

#define	XUIOSTAT_INCR(stat, val)        \
	atomic_add_64(&xuio_stats.stat.value.ui64, (val))
#define	XUIOSTAT_BUMP(stat)	XUIOSTAT_INCR(stat, 1)

#ifdef HAVE_UIO_ZEROCOPY
int
dmu_xuio_init(xuio_t *xuio, int nblk)
{
	dmu_xuio_t *priv;
	uio_t *uio = &xuio->xu_uio;

	uio->uio_iovcnt = nblk;
	uio->uio_iov = kmem_zalloc(nblk * sizeof (iovec_t), KM_SLEEP);

	priv = kmem_zalloc(sizeof (dmu_xuio_t), KM_SLEEP);
	priv->cnt = nblk;
	priv->bufs = kmem_zalloc(nblk * sizeof (arc_buf_t *), KM_SLEEP);
	priv->iovp = (iovec_t *)uio->uio_iov;
	XUIO_XUZC_PRIV(xuio) = priv;

	if (XUIO_XUZC_RW(xuio) == UIO_READ)
		XUIOSTAT_INCR(xuiostat_onloan_rbuf, nblk);
	else
		XUIOSTAT_INCR(xuiostat_onloan_wbuf, nblk);

	return (0);
}

void
dmu_xuio_fini(xuio_t *xuio)
{
	dmu_xuio_t *priv = XUIO_XUZC_PRIV(xuio);
	int nblk = priv->cnt;

	kmem_free(priv->iovp, nblk * sizeof (iovec_t));
	kmem_free(priv->bufs, nblk * sizeof (arc_buf_t *));
	kmem_free(priv, sizeof (dmu_xuio_t));

	if (XUIO_XUZC_RW(xuio) == UIO_READ)
		XUIOSTAT_INCR(xuiostat_onloan_rbuf, -nblk);
	else
		XUIOSTAT_INCR(xuiostat_onloan_wbuf, -nblk);
}

/*
 * Initialize iov[priv->next] and priv->bufs[priv->next] with { off, n, abuf }
 * and increase priv->next by 1.
 */
int
dmu_xuio_add(xuio_t *xuio, arc_buf_t *abuf, offset_t off, size_t n)
{
	struct iovec *iov;
	uio_t *uio = &xuio->xu_uio;
	dmu_xuio_t *priv = XUIO_XUZC_PRIV(xuio);
	int i = priv->next++;

	ASSERT(i < priv->cnt);
	ASSERT(off + n <= arc_buf_lsize(abuf));
	iov = (iovec_t *)uio->uio_iov + i;
	iov->iov_base = (char *)abuf->b_data + off;
	iov->iov_len = n;
	priv->bufs[i] = abuf;
	return (0);
}

int
dmu_xuio_cnt(xuio_t *xuio)
{
	dmu_xuio_t *priv = XUIO_XUZC_PRIV(xuio);
	return (priv->cnt);
}

arc_buf_t *
dmu_xuio_arcbuf(xuio_t *xuio, int i)
{
	dmu_xuio_t *priv = XUIO_XUZC_PRIV(xuio);

	ASSERT(i < priv->cnt);
	return (priv->bufs[i]);
}

void
dmu_xuio_clear(xuio_t *xuio, int i)
{
	dmu_xuio_t *priv = XUIO_XUZC_PRIV(xuio);

	ASSERT(i < priv->cnt);
	priv->bufs[i] = NULL;
}
#endif /* HAVE_UIO_ZEROCOPY */

static void
xuio_stat_init(void)
{
	xuio_ksp = kstat_create("zfs", 0, "xuio_stats", "misc",
	    KSTAT_TYPE_NAMED, sizeof (xuio_stats) / sizeof (kstat_named_t),
	    KSTAT_FLAG_VIRTUAL);
	if (xuio_ksp != NULL) {
		xuio_ksp->ks_data = &xuio_stats;
		kstat_install(xuio_ksp);
	}
}

static void
xuio_stat_fini(void)
{
	if (xuio_ksp != NULL) {
		kstat_delete(xuio_ksp);
		xuio_ksp = NULL;
	}
}

void
xuio_stat_wbuf_copied(void)
{
	XUIOSTAT_BUMP(xuiostat_wbuf_copied);
}

void
xuio_stat_wbuf_nocopy(void)
{
	XUIOSTAT_BUMP(xuiostat_wbuf_nocopy);
}

#ifdef _KERNEL
int
dmu_read_uio_dnode(dnode_t *dn, uio_t *uio, uint64_t size)
{
	dmu_buf_t **dbp;
	int numbufs, i, err;
#ifdef HAVE_UIO_ZEROCOPY
	xuio_t *xuio = NULL;
#endif

	/*
	 * NB: we could do this block-at-a-time, but it's nice
	 * to be reading in parallel.
	 */
	err = dmu_buf_hold_array_by_dnode(dn, uio->uio_loffset, size,
	    TRUE, FTAG, &numbufs, &dbp, 0);
	if (err)
		return (err);

	for (i = 0; i < numbufs; i++) {
		uint64_t tocpy;
		int64_t bufoff;
		dmu_buf_t *db = dbp[i];

		ASSERT(size > 0);

		bufoff = uio->uio_loffset - db->db_offset;
		tocpy = MIN(db->db_size - bufoff, size);

#ifdef HAVE_UIO_ZEROCOPY
		if (xuio) {
			dmu_buf_impl_t *dbi = (dmu_buf_impl_t *)db;
			arc_buf_t *dbuf_abuf = dbi->db_buf;
			arc_buf_t *abuf = dbuf_loan_arcbuf(dbi);
			err = dmu_xuio_add(xuio, abuf, bufoff, tocpy);
			if (!err) {
				uio->uio_resid -= tocpy;
				uio->uio_loffset += tocpy;
			}

			if (abuf == dbuf_abuf)
				XUIOSTAT_BUMP(xuiostat_rbuf_nocopy);
			else
				XUIOSTAT_BUMP(xuiostat_rbuf_copied);
		} else
#endif
			err = uiomove((char *)db->db_data + bufoff, tocpy,
			    UIO_READ, uio);
		if (err)
			break;

		size -= tocpy;
	}
	dmu_buf_rele_array(dbp, numbufs, FTAG);

	return (err);
}

/*
 * Read 'size' bytes into the uio buffer.
 * From object zdb->db_object.
 * Starting at offset uio->uio_loffset.
 *
 * If the caller already has a dbuf in the target object
 * (e.g. its bonus buffer), this routine is faster than dmu_read_uio(),
 * because we don't have to find the dnode_t for the object.
 */
int
dmu_read_uio_dbuf(dmu_buf_t *zdb, uio_t *uio, uint64_t size)
{
	dmu_buf_impl_t *db = (dmu_buf_impl_t *)zdb;
	dnode_t *dn;
	int err;

	if (size == 0)
		return (0);

	DB_DNODE_ENTER(db);
	dn = DB_DNODE(db);
	err = dmu_read_uio_dnode(dn, uio, size);
	DB_DNODE_EXIT(db);

	return (err);
}

/*
 * Read 'size' bytes into the uio buffer.
 * From the specified object
 * Starting at offset uio->uio_loffset.
 */
int
dmu_read_uio(objset_t *os, uint64_t object, uio_t *uio, uint64_t size)
{
	dnode_t *dn;
	int err;

	if (size == 0)
		return (0);

	err = dnode_hold(os, object, FTAG, &dn);
	if (err)
		return (err);

	err = dmu_read_uio_dnode(dn, uio, size);

	dnode_rele(dn, FTAG);

	return (err);
}

int
dmu_write_uio_dnode(dnode_t *dn, uio_t *uio, uint64_t size, dmu_tx_t *tx)
{
	dmu_buf_t **dbp;
	int numbufs;
	int err = 0;
	int i;

	err = dmu_buf_hold_array_by_dnode(dn, uio->uio_loffset, size,
	    FALSE, FTAG, &numbufs, &dbp, DMU_READ_PREFETCH);
	if (err)
		return (err);

	for (i = 0; i < numbufs; i++) {
		uint64_t tocpy;
		int64_t bufoff;
		dmu_buf_t *db = dbp[i];

		ASSERT(size > 0);

		bufoff = uio->uio_loffset - db->db_offset;
		tocpy = MIN(db->db_size - bufoff, size);

		ASSERT(i == 0 || i == numbufs-1 || tocpy == db->db_size);

		if (tocpy == db->db_size)
			dmu_buf_will_fill(db, tx);
		else
			dmu_buf_will_dirty(db, tx);

		/*
		 * XXX uiomove could block forever (eg.nfs-backed
		 * pages).  There needs to be a uiolockdown() function
		 * to lock the pages in memory, so that uiomove won't
		 * block.
		 */
		err = uiomove((char *)db->db_data + bufoff, tocpy,
		    UIO_WRITE, uio);

		if (tocpy == db->db_size)
			dmu_buf_fill_done(db, tx);

		if (err)
			break;

		size -= tocpy;
	}

	dmu_buf_rele_array(dbp, numbufs, FTAG);
	return (err);
}

/*
 * Write 'size' bytes from the uio buffer.
 * To object zdb->db_object.
 * Starting at offset uio->uio_loffset.
 *
 * If the caller already has a dbuf in the target object
 * (e.g. its bonus buffer), this routine is faster than dmu_write_uio(),
 * because we don't have to find the dnode_t for the object.
 */
int
dmu_write_uio_dbuf(dmu_buf_t *zdb, uio_t *uio, uint64_t size,
    dmu_tx_t *tx)
{
	dmu_buf_impl_t *db = (dmu_buf_impl_t *)zdb;
	dnode_t *dn;
	int err;

	if (size == 0)
		return (0);

	DB_DNODE_ENTER(db);
	dn = DB_DNODE(db);
	err = dmu_write_uio_dnode(dn, uio, size, tx);
	DB_DNODE_EXIT(db);

	return (err);
}

/*
 * Write 'size' bytes from the uio buffer.
 * To the specified object.
 * Starting at offset uio->uio_loffset.
 */
int
dmu_write_uio(objset_t *os, uint64_t object, uio_t *uio, uint64_t size,
    dmu_tx_t *tx)
{
	dnode_t *dn;
	int err;

	if (size == 0)
		return (0);

	err = dnode_hold(os, object, FTAG, &dn);
	if (err)
		return (err);

	err = dmu_write_uio_dnode(dn, uio, size, tx);

	dnode_rele(dn, FTAG);

	return (err);
}
#endif /* _KERNEL */

/*
 * Allocate a loaned anonymous arc buffer.
 */
arc_buf_t *
dmu_request_arcbuf(dmu_buf_t *handle, int size)
{
	dmu_buf_impl_t *db = (dmu_buf_impl_t *)handle;

	return (arc_loan_buf(db->db_objset->os_spa, B_FALSE, size));
}

/*
 * Free a loaned arc buffer.
 */
void
dmu_return_arcbuf(arc_buf_t *buf)
{
	arc_return_buf(buf, FTAG);
	arc_buf_destroy(buf, FTAG);
}

/*
 * When possible directly assign passed loaned arc buffer to a dbuf.
 * If this is not possible copy the contents of passed arc buf via
 * dmu_write().
 */
void
dmu_assign_arcbuf(dmu_buf_t *handle, uint64_t offset, arc_buf_t *buf,
    dmu_tx_t *tx)
{
	dmu_buf_impl_t *dbuf = (dmu_buf_impl_t *)handle;
	dnode_t *dn;
	dmu_buf_impl_t *db;
	uint32_t blksz = (uint32_t)arc_buf_lsize(buf);
	uint64_t blkid;

	DB_DNODE_ENTER(dbuf);
	dn = DB_DNODE(dbuf);
	rw_enter(&dn->dn_struct_rwlock, RW_READER);
	blkid = dbuf_whichblock(dn, 0, offset);
	VERIFY((db = dbuf_hold(dn, blkid, FTAG)) != NULL);
	rw_exit(&dn->dn_struct_rwlock);
	DB_DNODE_EXIT(dbuf);

	/*
	 * We can only assign if the offset is aligned, the arc buf is the
	 * same size as the dbuf, and the dbuf is not metadata.
	 */
	if (offset == db->db.db_offset && blksz == db->db.db_size) {
		dbuf_assign_arcbuf(db, buf, tx);
		dbuf_rele(db, FTAG);
	} else {
		objset_t *os;
		uint64_t object;

		/* compressed bufs must always be assignable to their dbuf */
		ASSERT3U(arc_get_compression(buf), ==, ZIO_COMPRESS_OFF);
		ASSERT(!(buf->b_flags & ARC_BUF_FLAG_COMPRESSED));

		DB_DNODE_ENTER(dbuf);
		dn = DB_DNODE(dbuf);
		os = dn->dn_objset;
		object = dn->dn_object;
		DB_DNODE_EXIT(dbuf);

		dbuf_rele(db, FTAG);
		dmu_write(os, object, offset, blksz, buf->b_data, tx);
		dmu_return_arcbuf(buf);
		XUIOSTAT_BUMP(xuiostat_wbuf_copied);
	}
}

typedef struct {
	dbuf_dirty_record_t	*dsa_dr;
	dmu_sync_cb_t		*dsa_done;
	zgd_t			*dsa_zgd;
	dmu_tx_t		*dsa_tx;
} dmu_sync_arg_t;

/* ARGSUSED */
static void
dmu_sync_ready(zio_t *zio, arc_buf_t *buf, void *varg)
{
	dmu_sync_arg_t *dsa = varg;
	dmu_buf_t *db = dsa->dsa_zgd->zgd_db;
	blkptr_t *bp = zio->io_bp;

	if (zio->io_error == 0) {
		if (BP_IS_HOLE(bp)) {
			/*
			 * A block of zeros may compress to a hole, but the
			 * block size still needs to be known for replay.
			 */
			BP_SET_LSIZE(bp, db->db_size);
		} else if (!BP_IS_EMBEDDED(bp)) {
			ASSERT(BP_GET_LEVEL(bp) == 0);
			bp->blk_fill = 1;
		}
	}
}

static void
dmu_sync_late_arrival_ready(zio_t *zio)
{
	dmu_sync_ready(zio, NULL, zio->io_private);
}

/* ARGSUSED */
static void
dmu_sync_done(zio_t *zio, arc_buf_t *buf, void *varg)
{
	dmu_sync_arg_t *dsa = varg;
	dbuf_dirty_record_t *dr = dsa->dsa_dr;
	dmu_buf_impl_t *db = dr->dr_dbuf;

	mutex_enter(&db->db_mtx);
	ASSERT(dr->dt.dl.dr_override_state == DR_IN_DMU_SYNC);
	if (zio->io_error == 0) {
		dr->dt.dl.dr_nopwrite = !!(zio->io_flags & ZIO_FLAG_NOPWRITE);
		if (dr->dt.dl.dr_nopwrite) {
			blkptr_t *bp = zio->io_bp;
			blkptr_t *bp_orig = &zio->io_bp_orig;
			uint8_t chksum = BP_GET_CHECKSUM(bp_orig);

			ASSERT(BP_EQUAL(bp, bp_orig));
			VERIFY(BP_EQUAL(bp, db->db_blkptr));
			ASSERT(zio->io_prop.zp_compress != ZIO_COMPRESS_OFF);
			VERIFY(zio_checksum_table[chksum].ci_flags &
			    ZCHECKSUM_FLAG_NOPWRITE);
		}
		dr->dt.dl.dr_overridden_by = *zio->io_bp;
		dr->dt.dl.dr_override_state = DR_OVERRIDDEN;
		dr->dt.dl.dr_copies = zio->io_prop.zp_copies;

		/*
		 * Old style holes are filled with all zeros, whereas
		 * new-style holes maintain their lsize, type, level,
		 * and birth time (see zio_write_compress). While we
		 * need to reset the BP_SET_LSIZE() call that happened
		 * in dmu_sync_ready for old style holes, we do *not*
		 * want to wipe out the information contained in new
		 * style holes. Thus, only zero out the block pointer if
		 * it's an old style hole.
		 */
		if (BP_IS_HOLE(&dr->dt.dl.dr_overridden_by) &&
		    dr->dt.dl.dr_overridden_by.blk_birth == 0)
			BP_ZERO(&dr->dt.dl.dr_overridden_by);
	} else {
		dr->dt.dl.dr_override_state = DR_NOT_OVERRIDDEN;
	}
	cv_broadcast(&db->db_changed);
	mutex_exit(&db->db_mtx);

	dsa->dsa_done(dsa->dsa_zgd, zio->io_error);

	kmem_free(dsa, sizeof (*dsa));
}

static void
dmu_sync_late_arrival_done(zio_t *zio)
{
	blkptr_t *bp = zio->io_bp;
	dmu_sync_arg_t *dsa = zio->io_private;
	ASSERTV(blkptr_t *bp_orig = &zio->io_bp_orig);

	if (zio->io_error == 0 && !BP_IS_HOLE(bp)) {
		ASSERT(!(zio->io_flags & ZIO_FLAG_NOPWRITE));
		ASSERT(BP_IS_HOLE(bp_orig) || !BP_EQUAL(bp, bp_orig));
		ASSERT(zio->io_bp->blk_birth == zio->io_txg);
		ASSERT(zio->io_txg > spa_syncing_txg(zio->io_spa));
		zio_free(zio->io_spa, zio->io_txg, zio->io_bp);
	}

	dmu_tx_commit(dsa->dsa_tx);

	dsa->dsa_done(dsa->dsa_zgd, zio->io_error);

	abd_put(zio->io_abd);
	kmem_free(dsa, sizeof (*dsa));
}

static int
dmu_sync_late_arrival(zio_t *pio, objset_t *os, dmu_sync_cb_t *done, zgd_t *zgd,
    zio_prop_t *zp, zbookmark_phys_t *zb)
{
	dmu_sync_arg_t *dsa;
	dmu_tx_t *tx;

	tx = dmu_tx_create(os);
	dmu_tx_hold_space(tx, zgd->zgd_db->db_size);
	if (dmu_tx_assign(tx, TXG_WAIT) != 0) {
		dmu_tx_abort(tx);
		/* Make zl_get_data do txg_waited_synced() */
		return (SET_ERROR(EIO));
	}

	dsa = kmem_alloc(sizeof (dmu_sync_arg_t), KM_SLEEP);
	dsa->dsa_dr = NULL;
	dsa->dsa_done = done;
	dsa->dsa_zgd = zgd;
	dsa->dsa_tx = tx;

	/*
	 * Since we are currently syncing this txg, it's nontrivial to
	 * determine what BP to nopwrite against, so we disable nopwrite.
	 *
	 * When syncing, the db_blkptr is initially the BP of the previous
	 * txg.  We can not nopwrite against it because it will be changed
	 * (this is similar to the non-late-arrival case where the dbuf is
	 * dirty in a future txg).
	 *
	 * Then dbuf_write_ready() sets bp_blkptr to the location we will write.
	 * We can not nopwrite against it because although the BP will not
	 * (typically) be changed, the data has not yet been persisted to this
	 * location.
	 *
	 * Finally, when dbuf_write_done() is called, it is theoretically
	 * possible to always nopwrite, because the data that was written in
	 * this txg is the same data that we are trying to write.  However we
	 * would need to check that this dbuf is not dirty in any future
	 * txg's (as we do in the normal dmu_sync() path). For simplicity, we
	 * don't nopwrite in this case.
	 */
	zp->zp_nopwrite = B_FALSE;

	zio_nowait(zio_write(pio, os->os_spa, dmu_tx_get_txg(tx), zgd->zgd_bp,
	    abd_get_from_buf(zgd->zgd_db->db_data, zgd->zgd_db->db_size),
	    zgd->zgd_db->db_size, zgd->zgd_db->db_size, zp,
	    dmu_sync_late_arrival_ready, NULL, NULL, dmu_sync_late_arrival_done,
	    dsa, ZIO_PRIORITY_SYNC_WRITE, ZIO_FLAG_CANFAIL, zb));

	return (0);
}

/*
 * Intent log support: sync the block associated with db to disk.
 * N.B. and XXX: the caller is responsible for making sure that the
 * data isn't changing while dmu_sync() is writing it.
 *
 * Return values:
 *
 *	EEXIST: this txg has already been synced, so there's nothing to do.
 *		The caller should not log the write.
 *
 *	ENOENT: the block was dbuf_free_range()'d, so there's nothing to do.
 *		The caller should not log the write.
 *
 *	EALREADY: this block is already in the process of being synced.
 *		The caller should track its progress (somehow).
 *
 *	EIO: could not do the I/O.
 *		The caller should do a txg_wait_synced().
 *
 *	0: the I/O has been initiated.
 *		The caller should log this blkptr in the done callback.
 *		It is possible that the I/O will fail, in which case
 *		the error will be reported to the done callback and
 *		propagated to pio from zio_done().
 */
int
dmu_sync(zio_t *pio, uint64_t txg, dmu_sync_cb_t *done, zgd_t *zgd)
{
	dmu_buf_impl_t *db = (dmu_buf_impl_t *)zgd->zgd_db;
	objset_t *os = db->db_objset;
	dsl_dataset_t *ds = os->os_dsl_dataset;
	dbuf_dirty_record_t *dr;
	dmu_sync_arg_t *dsa;
	zbookmark_phys_t zb;
	zio_prop_t zp;
	dnode_t *dn;

	ASSERT(pio != NULL);
	ASSERT(txg != 0);

	/* dbuf is within the locked range */
	ASSERT3U(db->db.db_offset, >=, zgd->zgd_rl->r_off);
	ASSERT3U(db->db.db_offset + db->db.db_size, <=,
	    zgd->zgd_rl->r_off + zgd->zgd_rl->r_len);

	SET_BOOKMARK(&zb, ds->ds_object,
	    db->db.db_object, db->db_level, db->db_blkid);

	DB_DNODE_ENTER(db);
	dn = DB_DNODE(db);
	dmu_write_policy(os, dn, db->db_level, WP_DMU_SYNC, &zp);
	DB_DNODE_EXIT(db);

	/*
	 * If we're frozen (running ziltest), we always need to generate a bp.
	 */
	if (txg > spa_freeze_txg(os->os_spa))
		return (dmu_sync_late_arrival(pio, os, done, zgd, &zp, &zb));

	/*
	 * Grabbing db_mtx now provides a barrier between dbuf_sync_leaf()
	 * and us.  If we determine that this txg is not yet syncing,
	 * but it begins to sync a moment later, that's OK because the
	 * sync thread will block in dbuf_sync_leaf() until we drop db_mtx.
	 */
	mutex_enter(&db->db_mtx);

	if (txg <= spa_last_synced_txg(os->os_spa)) {
		/*
		 * This txg has already synced.  There's nothing to do.
		 */
		mutex_exit(&db->db_mtx);
		return (SET_ERROR(EEXIST));
	}

	if (txg <= spa_syncing_txg(os->os_spa)) {
		/*
		 * This txg is currently syncing, so we can't mess with
		 * the dirty record anymore; just write a new log block.
		 */
		mutex_exit(&db->db_mtx);
		return (dmu_sync_late_arrival(pio, os, done, zgd, &zp, &zb));
	}

	dr = db->db_last_dirty;
	while (dr && dr->dr_txg != txg)
		dr = dr->dr_next;

	if (dr == NULL) {
		/*
		 * There's no dr for this dbuf, so it must have been freed.
		 * There's no need to log writes to freed blocks, so we're done.
		 */
		mutex_exit(&db->db_mtx);
		return (SET_ERROR(ENOENT));
	}

	ASSERT(dr->dr_next == NULL || dr->dr_next->dr_txg < txg);

	if (db->db_blkptr != NULL) {
		/*
		 * We need to fill in zgd_bp with the current blkptr so that
		 * the nopwrite code can check if we're writing the same
		 * data that's already on disk.  We can only nopwrite if we
		 * are sure that after making the copy, db_blkptr will not
		 * change until our i/o completes.  We ensure this by
		 * holding the db_mtx, and only allowing nopwrite if the
		 * block is not already dirty (see below).  This is verified
		 * by dmu_sync_done(), which VERIFYs that the db_blkptr has
		 * not changed.
		 */
		*zgd->zgd_bp = *db->db_blkptr;
	}

	/*
	 * Assume the on-disk data is X, the current syncing data (in
	 * txg - 1) is Y, and the current in-memory data is Z (currently
	 * in dmu_sync).
	 *
	 * We usually want to perform a nopwrite if X and Z are the
	 * same.  However, if Y is different (i.e. the BP is going to
	 * change before this write takes effect), then a nopwrite will
	 * be incorrect - we would override with X, which could have
	 * been freed when Y was written.
	 *
	 * (Note that this is not a concern when we are nop-writing from
	 * syncing context, because X and Y must be identical, because
	 * all previous txgs have been synced.)
	 *
	 * Therefore, we disable nopwrite if the current BP could change
	 * before this TXG.  There are two ways it could change: by
	 * being dirty (dr_next is non-NULL), or by being freed
	 * (dnode_block_freed()).  This behavior is verified by
	 * zio_done(), which VERIFYs that the override BP is identical
	 * to the on-disk BP.
	 */
	DB_DNODE_ENTER(db);
	dn = DB_DNODE(db);
	if (dr->dr_next != NULL || dnode_block_freed(dn, db->db_blkid))
		zp.zp_nopwrite = B_FALSE;
	DB_DNODE_EXIT(db);

	ASSERT(dr->dr_txg == txg);
	if (dr->dt.dl.dr_override_state == DR_IN_DMU_SYNC ||
	    dr->dt.dl.dr_override_state == DR_OVERRIDDEN) {
		/*
		 * We have already issued a sync write for this buffer,
		 * or this buffer has already been synced.  It could not
		 * have been dirtied since, or we would have cleared the state.
		 */
		mutex_exit(&db->db_mtx);
		return (SET_ERROR(EALREADY));
	}

	ASSERT(dr->dt.dl.dr_override_state == DR_NOT_OVERRIDDEN);
	dr->dt.dl.dr_override_state = DR_IN_DMU_SYNC;
	mutex_exit(&db->db_mtx);

	dsa = kmem_alloc(sizeof (dmu_sync_arg_t), KM_SLEEP);
	dsa->dsa_dr = dr;
	dsa->dsa_done = done;
	dsa->dsa_zgd = zgd;
	dsa->dsa_tx = NULL;

	zio_nowait(arc_write(pio, os->os_spa, txg,
	    zgd->zgd_bp, dr->dt.dl.dr_data, DBUF_IS_L2CACHEABLE(db),
	    &zp, dmu_sync_ready, NULL, NULL, dmu_sync_done, dsa,
	    ZIO_PRIORITY_SYNC_WRITE, ZIO_FLAG_CANFAIL, &zb));

	return (0);
}

int
dmu_object_set_blocksize(objset_t *os, uint64_t object, uint64_t size, int ibs,
    dmu_tx_t *tx)
{
	dnode_t *dn;
	int err;

	err = dnode_hold(os, object, FTAG, &dn);
	if (err)
		return (err);
	err = dnode_set_blksz(dn, size, ibs, tx);
	dnode_rele(dn, FTAG);
	return (err);
}

void
dmu_object_set_checksum(objset_t *os, uint64_t object, uint8_t checksum,
    dmu_tx_t *tx)
{
	dnode_t *dn;

	/*
	 * Send streams include each object's checksum function.  This
	 * check ensures that the receiving system can understand the
	 * checksum function transmitted.
	 */
	ASSERT3U(checksum, <, ZIO_CHECKSUM_LEGACY_FUNCTIONS);

	VERIFY0(dnode_hold(os, object, FTAG, &dn));
	ASSERT3U(checksum, <, ZIO_CHECKSUM_FUNCTIONS);
	dn->dn_checksum = checksum;
	dnode_setdirty(dn, tx);
	dnode_rele(dn, FTAG);
}

void
dmu_object_set_compress(objset_t *os, uint64_t object, uint8_t compress,
    dmu_tx_t *tx)
{
	dnode_t *dn;

	/*
	 * Send streams include each object's compression function.  This
	 * check ensures that the receiving system can understand the
	 * compression function transmitted.
	 */
	ASSERT3U(compress, <, ZIO_COMPRESS_LEGACY_FUNCTIONS);

	VERIFY0(dnode_hold(os, object, FTAG, &dn));
	dn->dn_compress = compress;
	dnode_setdirty(dn, tx);
	dnode_rele(dn, FTAG);
}

int zfs_mdcomp_disable = 0;

/*
 * When the "redundant_metadata" property is set to "most", only indirect
 * blocks of this level and higher will have an additional ditto block.
 */
int zfs_redundant_metadata_most_ditto_level = 2;

void
dmu_write_policy(objset_t *os, dnode_t *dn, int level, int wp, zio_prop_t *zp)
{
	dmu_object_type_t type = dn ? dn->dn_type : DMU_OT_OBJSET;
	boolean_t ismd = (level > 0 || DMU_OT_IS_METADATA(type) ||
	    (wp & WP_SPILL));
	enum zio_checksum checksum = os->os_checksum;
	enum zio_compress compress = os->os_compress;
	enum zio_checksum dedup_checksum = os->os_dedup_checksum;
	boolean_t dedup = B_FALSE;
	boolean_t nopwrite = B_FALSE;
	boolean_t dedup_verify = os->os_dedup_verify;
	int copies = os->os_copies;

	/*
	 * We maintain different write policies for each of the following
	 * types of data:
	 *	 1. metadata
	 *	 2. preallocated blocks (i.e. level-0 blocks of a dump device)
	 *	 3. all other level 0 blocks
	 */
	if (ismd) {
		if (zfs_mdcomp_disable) {
			compress = ZIO_COMPRESS_EMPTY;
		} else {
			/*
			 * XXX -- we should design a compression algorithm
			 * that specializes in arrays of bps.
			 */
			compress = zio_compress_select(os->os_spa,
			    ZIO_COMPRESS_ON, ZIO_COMPRESS_ON);
		}

		/*
		 * Metadata always gets checksummed.  If the data
		 * checksum is multi-bit correctable, and it's not a
		 * ZBT-style checksum, then it's suitable for metadata
		 * as well.  Otherwise, the metadata checksum defaults
		 * to fletcher4.
		 */
		if (!(zio_checksum_table[checksum].ci_flags &
		    ZCHECKSUM_FLAG_METADATA) ||
		    (zio_checksum_table[checksum].ci_flags &
		    ZCHECKSUM_FLAG_EMBEDDED))
			checksum = ZIO_CHECKSUM_FLETCHER_4;

		if (os->os_redundant_metadata == ZFS_REDUNDANT_METADATA_ALL ||
		    (os->os_redundant_metadata ==
		    ZFS_REDUNDANT_METADATA_MOST &&
		    (level >= zfs_redundant_metadata_most_ditto_level ||
		    DMU_OT_IS_METADATA(type) || (wp & WP_SPILL))))
			copies++;
	} else if (wp & WP_NOFILL) {
		ASSERT(level == 0);

		/*
		 * If we're writing preallocated blocks, we aren't actually
		 * writing them so don't set any policy properties.  These
		 * blocks are currently only used by an external subsystem
		 * outside of zfs (i.e. dump) and not written by the zio
		 * pipeline.
		 */
		compress = ZIO_COMPRESS_OFF;
		checksum = ZIO_CHECKSUM_OFF;
	} else {
		compress = zio_compress_select(os->os_spa, dn->dn_compress,
		    compress);

		checksum = (dedup_checksum == ZIO_CHECKSUM_OFF) ?
		    zio_checksum_select(dn->dn_checksum, checksum) :
		    dedup_checksum;

		/*
		 * Determine dedup setting.  If we are in dmu_sync(),
		 * we won't actually dedup now because that's all
		 * done in syncing context; but we do want to use the
		 * dedup checkum.  If the checksum is not strong
		 * enough to ensure unique signatures, force
		 * dedup_verify.
		 */
		if (dedup_checksum != ZIO_CHECKSUM_OFF) {
			dedup = (wp & WP_DMU_SYNC) ? B_FALSE : B_TRUE;
			if (!(zio_checksum_table[checksum].ci_flags &
			    ZCHECKSUM_FLAG_DEDUP))
				dedup_verify = B_TRUE;
		}

		/*
		 * Enable nopwrite if we have secure enough checksum
		 * algorithm (see comment in zio_nop_write) and
		 * compression is enabled.  We don't enable nopwrite if
		 * dedup is enabled as the two features are mutually
		 * exclusive.
		 */
		nopwrite = (!dedup && (zio_checksum_table[checksum].ci_flags &
		    ZCHECKSUM_FLAG_NOPWRITE) &&
		    compress != ZIO_COMPRESS_OFF && zfs_nopwrite_enabled);
	}

	zp->zp_checksum = checksum;
	zp->zp_compress = compress;
	ASSERT3U(zp->zp_compress, !=, ZIO_COMPRESS_INHERIT);

	zp->zp_type = (wp & WP_SPILL) ? dn->dn_bonustype : type;
	zp->zp_level = level;
	zp->zp_copies = MIN(copies, spa_max_replication(os->os_spa));
	zp->zp_dedup = dedup;
	zp->zp_dedup_verify = dedup && dedup_verify;
	zp->zp_nopwrite = nopwrite;
}

/*
 * This function is only called from zfs_holey_common() for zpl_llseek()
 * in order to determine the location of holes.  In order to accurately
 * report holes all dirty data must be synced to disk.  This causes extremely
 * poor performance when seeking for holes in a dirty file.  As a compromise,
 * only provide hole data when the dnode is clean.  When a dnode is dirty
 * report the dnode as having no holes which is always a safe thing to do.
 */
int
dmu_offset_next(objset_t *os, uint64_t object, boolean_t hole, uint64_t *off)
{
	dnode_t *dn;
	int i, err;
	boolean_t clean = B_TRUE;

	err = dnode_hold(os, object, FTAG, &dn);
	if (err)
		return (err);

	/*
	 * Check if dnode is dirty
	 */
	for (i = 0; i < TXG_SIZE; i++) {
		if (list_link_active(&dn->dn_dirty_link[i])) {
			clean = B_FALSE;
			break;
		}
	}

	/*
	 * If compatibility option is on, sync any current changes before
	 * we go trundling through the block pointers.
	 */
	if (!clean && zfs_dmu_offset_next_sync) {
		clean = B_TRUE;
		dnode_rele(dn, FTAG);
		txg_wait_synced(dmu_objset_pool(os), 0);
		err = dnode_hold(os, object, FTAG, &dn);
		if (err)
			return (err);
	}

	if (clean)
		err = dnode_next_offset(dn,
		    (hole ? DNODE_FIND_HOLE : 0), off, 1, 1, 0);
	else
		err = SET_ERROR(EBUSY);

	dnode_rele(dn, FTAG);

	return (err);
}

void
__dmu_object_info_from_dnode(dnode_t *dn, dmu_object_info_t *doi)
{
	dnode_phys_t *dnp = dn->dn_phys;
	int i;

	doi->doi_data_block_size = dn->dn_datablksz;
	doi->doi_metadata_block_size = dn->dn_indblkshift ?
	    1ULL << dn->dn_indblkshift : 0;
	doi->doi_type = dn->dn_type;
	doi->doi_bonus_type = dn->dn_bonustype;
	doi->doi_bonus_size = dn->dn_bonuslen;
	doi->doi_dnodesize = dn->dn_num_slots << DNODE_SHIFT;
	doi->doi_indirection = dn->dn_nlevels;
	doi->doi_checksum = dn->dn_checksum;
	doi->doi_compress = dn->dn_compress;
	doi->doi_nblkptr = dn->dn_nblkptr;
	doi->doi_physical_blocks_512 = (DN_USED_BYTES(dnp) + 256) >> 9;
	doi->doi_max_offset = (dn->dn_maxblkid + 1) * dn->dn_datablksz;
	doi->doi_fill_count = 0;
	for (i = 0; i < dnp->dn_nblkptr; i++)
		doi->doi_fill_count += BP_GET_FILL(&dnp->dn_blkptr[i]);
}

void
dmu_object_info_from_dnode(dnode_t *dn, dmu_object_info_t *doi)
{
	rw_enter(&dn->dn_struct_rwlock, RW_READER);
	mutex_enter(&dn->dn_mtx);

	__dmu_object_info_from_dnode(dn, doi);

	mutex_exit(&dn->dn_mtx);
	rw_exit(&dn->dn_struct_rwlock);
}

/*
 * Get information on a DMU object.
 * If doi is NULL, just indicates whether the object exists.
 */
int
dmu_object_info(objset_t *os, uint64_t object, dmu_object_info_t *doi)
{
	dnode_t *dn;
	int err = dnode_hold(os, object, FTAG, &dn);

	if (err)
		return (err);

	if (doi != NULL)
		dmu_object_info_from_dnode(dn, doi);

	dnode_rele(dn, FTAG);
	return (0);
}

/*
 * As above, but faster; can be used when you have a held dbuf in hand.
 */
void
dmu_object_info_from_db(dmu_buf_t *db_fake, dmu_object_info_t *doi)
{
	dmu_buf_impl_t *db = (dmu_buf_impl_t *)db_fake;

	DB_DNODE_ENTER(db);
	dmu_object_info_from_dnode(DB_DNODE(db), doi);
	DB_DNODE_EXIT(db);
}

/*
 * Faster still when you only care about the size.
 * This is specifically optimized for zfs_getattr().
 */
void
dmu_object_size_from_db(dmu_buf_t *db_fake, uint32_t *blksize,
    u_longlong_t *nblk512)
{
	dmu_buf_impl_t *db = (dmu_buf_impl_t *)db_fake;
	dnode_t *dn;

	DB_DNODE_ENTER(db);
	dn = DB_DNODE(db);

	*blksize = dn->dn_datablksz;
	/* add in number of slots used for the dnode itself */
	*nblk512 = ((DN_USED_BYTES(dn->dn_phys) + SPA_MINBLOCKSIZE/2) >>
	    SPA_MINBLOCKSHIFT) + dn->dn_num_slots;
	DB_DNODE_EXIT(db);
}

void
dmu_object_dnsize_from_db(dmu_buf_t *db_fake, int *dnsize)
{
	dmu_buf_impl_t *db = (dmu_buf_impl_t *)db_fake;
	dnode_t *dn;

	DB_DNODE_ENTER(db);
	dn = DB_DNODE(db);
	*dnsize = dn->dn_num_slots << DNODE_SHIFT;
	DB_DNODE_EXIT(db);
}

void
byteswap_uint64_array(void *vbuf, size_t size)
{
	uint64_t *buf = vbuf;
	size_t count = size >> 3;
	int i;

	ASSERT((size & 7) == 0);

	for (i = 0; i < count; i++)
		buf[i] = BSWAP_64(buf[i]);
}

void
byteswap_uint32_array(void *vbuf, size_t size)
{
	uint32_t *buf = vbuf;
	size_t count = size >> 2;
	int i;

	ASSERT((size & 3) == 0);

	for (i = 0; i < count; i++)
		buf[i] = BSWAP_32(buf[i]);
}

void
byteswap_uint16_array(void *vbuf, size_t size)
{
	uint16_t *buf = vbuf;
	size_t count = size >> 1;
	int i;

	ASSERT((size & 1) == 0);

	for (i = 0; i < count; i++)
		buf[i] = BSWAP_16(buf[i]);
}

/* ARGSUSED */
void
byteswap_uint8_array(void *vbuf, size_t size)
{
}

void
dmu_init(void)
{
	abd_init();
	zfs_dbgmsg_init();
	sa_cache_init();
	xuio_stat_init();
	dmu_objset_init();
	dnode_init();
	zfetch_init();
	dmu_tx_init();
	l2arc_init();
	arc_init();
	dbuf_init();
}

void
dmu_fini(void)
{
	arc_fini(); /* arc depends on l2arc, so arc must go first */
	l2arc_fini();
	dmu_tx_fini();
	zfetch_fini();
	dbuf_fini();
	dnode_fini();
	dmu_objset_fini();
	xuio_stat_fini();
	sa_cache_fini();
	zfs_dbgmsg_fini();
	abd_fini();
}

#if defined(_KERNEL) && defined(HAVE_SPL)
EXPORT_SYMBOL(dmu_bonus_hold);
EXPORT_SYMBOL(dmu_buf_hold_array_by_bonus);
EXPORT_SYMBOL(dmu_buf_rele_array);
EXPORT_SYMBOL(dmu_prefetch);
EXPORT_SYMBOL(dmu_free_range);
EXPORT_SYMBOL(dmu_free_long_range);
EXPORT_SYMBOL(dmu_free_long_object);
EXPORT_SYMBOL(dmu_read);
EXPORT_SYMBOL(dmu_read_by_dnode);
EXPORT_SYMBOL(dmu_write);
EXPORT_SYMBOL(dmu_write_by_dnode);
EXPORT_SYMBOL(dmu_prealloc);
EXPORT_SYMBOL(dmu_object_info);
EXPORT_SYMBOL(dmu_object_info_from_dnode);
EXPORT_SYMBOL(dmu_object_info_from_db);
EXPORT_SYMBOL(dmu_object_size_from_db);
EXPORT_SYMBOL(dmu_object_dnsize_from_db);
EXPORT_SYMBOL(dmu_object_set_blocksize);
EXPORT_SYMBOL(dmu_object_set_checksum);
EXPORT_SYMBOL(dmu_object_set_compress);
EXPORT_SYMBOL(dmu_write_policy);
EXPORT_SYMBOL(dmu_sync);
EXPORT_SYMBOL(dmu_request_arcbuf);
EXPORT_SYMBOL(dmu_return_arcbuf);
EXPORT_SYMBOL(dmu_assign_arcbuf);
EXPORT_SYMBOL(dmu_buf_hold);
EXPORT_SYMBOL(dmu_ot);

/* BEGIN CSTYLED */
module_param(zfs_mdcomp_disable, int, 0644);
MODULE_PARM_DESC(zfs_mdcomp_disable, "Disable meta data compression");

module_param(zfs_nopwrite_enabled, int, 0644);
MODULE_PARM_DESC(zfs_nopwrite_enabled, "Enable NOP writes");

module_param(zfs_per_txg_dirty_frees_percent, ulong, 0644);
MODULE_PARM_DESC(zfs_per_txg_dirty_frees_percent,
	"percentage of dirtied blocks from frees in one TXG");

module_param(zfs_dmu_offset_next_sync, int, 0644);
MODULE_PARM_DESC(zfs_dmu_offset_next_sync,
	"Enable forcing txg sync to find holes");

/* END CSTYLED */

#endif
