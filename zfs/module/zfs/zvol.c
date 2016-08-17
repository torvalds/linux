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
 * Copyright (C) 2008-2010 Lawrence Livermore National Security, LLC.
 * Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 * Rewritten for Linux by Brian Behlendorf <behlendorf1@llnl.gov>.
 * LLNL-CODE-403049.
 *
 * ZFS volume emulation driver.
 *
 * Makes a DMU object look like a volume of arbitrary size, up to 2^64 bytes.
 * Volumes are accessed through the symbolic links named:
 *
 * /dev/<pool_name>/<dataset_name>
 *
 * Volumes are persistent through reboot and module load.  No user command
 * needs to be run before opening and using a device.
 *
 * Copyright 2014 Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 2016 Actifio, Inc. All rights reserved.
 * Copyright (c) 2012, 2017 by Delphix. All rights reserved.
 */

/*
 * Note on locking of zvol state structures.
 *
 * These structures are used to maintain internal state used to emulate block
 * devices on top of zvols. In particular, management of device minor number
 * operations - create, remove, rename, and set_snapdev - involves access to
 * these structures. The zvol_state_lock is primarily used to protect the
 * zvol_state_list. The zv->zv_state_lock is used to protect the contents
 * of the zvol_state_t structures, as well as to make sure that when the
 * time comes to remove the structure from the list, it is not in use, and
 * therefore, it can be taken off zvol_state_list and freed.
 *
 * The zv_suspend_lock was introduced to allow for suspending I/O to a zvol,
 * e.g. for the duration of receive and rollback operations. This lock can be
 * held for significant periods of time. Given that it is undesirable to hold
 * mutexes for long periods of time, the following lock ordering applies:
 * - take zvol_state_lock if necessary, to protect zvol_state_list
 * - take zv_suspend_lock if necessary, by the code path in question
 * - take zv_state_lock to protect zvol_state_t
 *
 * The minor operations are issued to spa->spa_zvol_taskq queues, that are
 * single-threaded (to preserve order of minor operations), and are executed
 * through the zvol_task_cb that dispatches the specific operations. Therefore,
 * these operations are serialized per pool. Consequently, we can be certain
 * that for a given zvol, there is only one operation at a time in progress.
 * That is why one can be sure that first, zvol_state_t for a given zvol is
 * allocated and placed on zvol_state_list, and then other minor operations
 * for this zvol are going to proceed in the order of issue.
 *
 * It is also worth keeping in mind that once add_disk() is called, the zvol is
 * announced to the world, and zvol_open()/zvol_release() can be called at any
 * time. Incidentally, add_disk() itself calls zvol_open()->zvol_first_open()
 * and zvol_release()->zvol_last_close() directly as well.
 */

#include <sys/dbuf.h>
#include <sys/dmu_traverse.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_prop.h>
#include <sys/dsl_dir.h>
#include <sys/zap.h>
#include <sys/zfeature.h>
#include <sys/zil_impl.h>
#include <sys/dmu_tx.h>
#include <sys/zio.h>
#include <sys/zfs_rlock.h>
#include <sys/zfs_znode.h>
#include <sys/spa_impl.h>
#include <sys/zvol.h>
#include <linux/blkdev_compat.h>

unsigned int zvol_inhibit_dev = 0;
unsigned int zvol_major = ZVOL_MAJOR;
unsigned int zvol_threads = 32;
unsigned int zvol_request_sync = 0;
unsigned int zvol_prefetch_bytes = (128 * 1024);
unsigned long zvol_max_discard_blocks = 16384;
unsigned int zvol_volmode = ZFS_VOLMODE_GEOM;

static taskq_t *zvol_taskq;
static krwlock_t zvol_state_lock;
static list_t zvol_state_list;

#define	ZVOL_HT_SIZE	1024
static struct hlist_head *zvol_htable;
#define	ZVOL_HT_HEAD(hash)	(&zvol_htable[(hash) & (ZVOL_HT_SIZE-1)])

static struct ida zvol_ida;

/*
 * The in-core state of each volume.
 */
struct zvol_state {
	char			zv_name[MAXNAMELEN];	/* name */
	uint64_t		zv_volsize;		/* advertised space */
	uint64_t		zv_volblocksize;	/* volume block size */
	objset_t		*zv_objset;	/* objset handle */
	uint32_t		zv_flags;	/* ZVOL_* flags */
	uint32_t		zv_open_count;	/* open counts */
	uint32_t		zv_changed;	/* disk changed */
	zilog_t			*zv_zilog;	/* ZIL handle */
	zfs_rlock_t		zv_range_lock;	/* range lock */
	dnode_t			*zv_dn;		/* dnode hold */
	dev_t			zv_dev;		/* device id */
	struct gendisk		*zv_disk;	/* generic disk */
	struct request_queue	*zv_queue;	/* request queue */
	list_node_t		zv_next;	/* next zvol_state_t linkage */
	uint64_t		zv_hash;	/* name hash */
	struct hlist_node	zv_hlink;	/* hash link */
	kmutex_t		zv_state_lock;	/* protects zvol_state_t */
	atomic_t		zv_suspend_ref;	/* refcount for suspend */
	krwlock_t		zv_suspend_lock;	/* suspend lock */
};

typedef enum {
	ZVOL_ASYNC_CREATE_MINORS,
	ZVOL_ASYNC_REMOVE_MINORS,
	ZVOL_ASYNC_RENAME_MINORS,
	ZVOL_ASYNC_SET_SNAPDEV,
	ZVOL_ASYNC_SET_VOLMODE,
	ZVOL_ASYNC_MAX
} zvol_async_op_t;

typedef struct {
	zvol_async_op_t op;
	char pool[MAXNAMELEN];
	char name1[MAXNAMELEN];
	char name2[MAXNAMELEN];
	zprop_source_t source;
	uint64_t value;
} zvol_task_t;

#define	ZVOL_RDONLY	0x1

static uint64_t
zvol_name_hash(const char *name)
{
	int i;
	uint64_t crc = -1ULL;
	uint8_t *p = (uint8_t *)name;
	ASSERT(zfs_crc64_table[128] == ZFS_CRC64_POLY);
	for (i = 0; i < MAXNAMELEN - 1 && *p; i++, p++) {
		crc = (crc >> 8) ^ zfs_crc64_table[(crc ^ (*p)) & 0xFF];
	}
	return (crc);
}

/*
 * Find a zvol_state_t given the full major+minor dev_t. If found,
 * return with zv_state_lock taken, otherwise, return (NULL) without
 * taking zv_state_lock.
 */
static zvol_state_t *
zvol_find_by_dev(dev_t dev)
{
	zvol_state_t *zv;

	rw_enter(&zvol_state_lock, RW_READER);
	for (zv = list_head(&zvol_state_list); zv != NULL;
	    zv = list_next(&zvol_state_list, zv)) {
		mutex_enter(&zv->zv_state_lock);
		if (zv->zv_dev == dev) {
			rw_exit(&zvol_state_lock);
			return (zv);
		}
		mutex_exit(&zv->zv_state_lock);
	}
	rw_exit(&zvol_state_lock);

	return (NULL);
}

/*
 * Find a zvol_state_t given the name and hash generated by zvol_name_hash.
 * If found, return with zv_suspend_lock and zv_state_lock taken, otherwise,
 * return (NULL) without the taking locks. The zv_suspend_lock is always taken
 * before zv_state_lock. The mode argument indicates the mode (including none)
 * for zv_suspend_lock to be taken.
 */
static zvol_state_t *
zvol_find_by_name_hash(const char *name, uint64_t hash, int mode)
{
	zvol_state_t *zv;
	struct hlist_node *p = NULL;

	rw_enter(&zvol_state_lock, RW_READER);
	hlist_for_each(p, ZVOL_HT_HEAD(hash)) {
		zv = hlist_entry(p, zvol_state_t, zv_hlink);
		mutex_enter(&zv->zv_state_lock);
		if (zv->zv_hash == hash &&
		    strncmp(zv->zv_name, name, MAXNAMELEN) == 0) {
			/*
			 * this is the right zvol, take the locks in the
			 * right order
			 */
			if (mode != RW_NONE &&
			    !rw_tryenter(&zv->zv_suspend_lock, mode)) {
				mutex_exit(&zv->zv_state_lock);
				rw_enter(&zv->zv_suspend_lock, mode);
				mutex_enter(&zv->zv_state_lock);
				/*
				 * zvol cannot be renamed as we continue
				 * to hold zvol_state_lock
				 */
				ASSERT(zv->zv_hash == hash &&
				    strncmp(zv->zv_name, name, MAXNAMELEN)
				    == 0);
			}
			rw_exit(&zvol_state_lock);
			return (zv);
		}
		mutex_exit(&zv->zv_state_lock);
	}
	rw_exit(&zvol_state_lock);

	return (NULL);
}

/*
 * Find a zvol_state_t given the name.
 * If found, return with zv_suspend_lock and zv_state_lock taken, otherwise,
 * return (NULL) without the taking locks. The zv_suspend_lock is always taken
 * before zv_state_lock. The mode argument indicates the mode (including none)
 * for zv_suspend_lock to be taken.
 */
static zvol_state_t *
zvol_find_by_name(const char *name, int mode)
{
	return (zvol_find_by_name_hash(name, zvol_name_hash(name), mode));
}


/*
 * Given a path, return TRUE if path is a ZVOL.
 */
boolean_t
zvol_is_zvol(const char *device)
{
	struct block_device *bdev;
	unsigned int major;

	bdev = vdev_lookup_bdev(device);
	if (IS_ERR(bdev))
		return (B_FALSE);

	major = MAJOR(bdev->bd_dev);
	bdput(bdev);

	if (major == zvol_major)
		return (B_TRUE);

	return (B_FALSE);
}

/*
 * ZFS_IOC_CREATE callback handles dmu zvol and zap object creation.
 */
void
zvol_create_cb(objset_t *os, void *arg, cred_t *cr, dmu_tx_t *tx)
{
	zfs_creat_t *zct = arg;
	nvlist_t *nvprops = zct->zct_props;
	int error;
	uint64_t volblocksize, volsize;

	VERIFY(nvlist_lookup_uint64(nvprops,
	    zfs_prop_to_name(ZFS_PROP_VOLSIZE), &volsize) == 0);
	if (nvlist_lookup_uint64(nvprops,
	    zfs_prop_to_name(ZFS_PROP_VOLBLOCKSIZE), &volblocksize) != 0)
		volblocksize = zfs_prop_default_numeric(ZFS_PROP_VOLBLOCKSIZE);

	/*
	 * These properties must be removed from the list so the generic
	 * property setting step won't apply to them.
	 */
	VERIFY(nvlist_remove_all(nvprops,
	    zfs_prop_to_name(ZFS_PROP_VOLSIZE)) == 0);
	(void) nvlist_remove_all(nvprops,
	    zfs_prop_to_name(ZFS_PROP_VOLBLOCKSIZE));

	error = dmu_object_claim(os, ZVOL_OBJ, DMU_OT_ZVOL, volblocksize,
	    DMU_OT_NONE, 0, tx);
	ASSERT(error == 0);

	error = zap_create_claim(os, ZVOL_ZAP_OBJ, DMU_OT_ZVOL_PROP,
	    DMU_OT_NONE, 0, tx);
	ASSERT(error == 0);

	error = zap_update(os, ZVOL_ZAP_OBJ, "size", 8, 1, &volsize, tx);
	ASSERT(error == 0);
}

/*
 * ZFS_IOC_OBJSET_STATS entry point.
 */
int
zvol_get_stats(objset_t *os, nvlist_t *nv)
{
	int error;
	dmu_object_info_t *doi;
	uint64_t val;

	error = zap_lookup(os, ZVOL_ZAP_OBJ, "size", 8, 1, &val);
	if (error)
		return (SET_ERROR(error));

	dsl_prop_nvlist_add_uint64(nv, ZFS_PROP_VOLSIZE, val);
	doi = kmem_alloc(sizeof (dmu_object_info_t), KM_SLEEP);
	error = dmu_object_info(os, ZVOL_OBJ, doi);

	if (error == 0) {
		dsl_prop_nvlist_add_uint64(nv, ZFS_PROP_VOLBLOCKSIZE,
		    doi->doi_data_block_size);
	}

	kmem_free(doi, sizeof (dmu_object_info_t));

	return (SET_ERROR(error));
}

/*
 * Sanity check volume size.
 */
int
zvol_check_volsize(uint64_t volsize, uint64_t blocksize)
{
	if (volsize == 0)
		return (SET_ERROR(EINVAL));

	if (volsize % blocksize != 0)
		return (SET_ERROR(EINVAL));

#ifdef _ILP32
	if (volsize - 1 > SPEC_MAXOFFSET_T)
		return (SET_ERROR(EOVERFLOW));
#endif
	return (0);
}

/*
 * Ensure the zap is flushed then inform the VFS of the capacity change.
 */
static int
zvol_update_volsize(uint64_t volsize, objset_t *os)
{
	dmu_tx_t *tx;
	int error;
	uint64_t txg;

	tx = dmu_tx_create(os);
	dmu_tx_hold_zap(tx, ZVOL_ZAP_OBJ, TRUE, NULL);
	dmu_tx_mark_netfree(tx);
	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		return (SET_ERROR(error));
	}
	txg = dmu_tx_get_txg(tx);

	error = zap_update(os, ZVOL_ZAP_OBJ, "size", 8, 1,
	    &volsize, tx);
	dmu_tx_commit(tx);

	txg_wait_synced(dmu_objset_pool(os), txg);

	if (error == 0)
		error = dmu_free_long_range(os,
		    ZVOL_OBJ, volsize, DMU_OBJECT_END);

	return (error);
}

/*
 * Set ZFS_PROP_VOLSIZE set entry point.  Note that modifying the volume
 * size will result in a udev "change" event being generated.
 */
int
zvol_set_volsize(const char *name, uint64_t volsize)
{
	objset_t *os = NULL;
	struct gendisk *disk = NULL;
	uint64_t readonly;
	int error;
	boolean_t owned = B_FALSE;

	error = dsl_prop_get_integer(name,
	    zfs_prop_to_name(ZFS_PROP_READONLY), &readonly, NULL);
	if (error != 0)
		return (SET_ERROR(error));
	if (readonly)
		return (SET_ERROR(EROFS));

	zvol_state_t *zv = zvol_find_by_name(name, RW_READER);

	ASSERT(zv == NULL || (MUTEX_HELD(&zv->zv_state_lock) &&
	    RW_READ_HELD(&zv->zv_suspend_lock)));

	if (zv == NULL || zv->zv_objset == NULL) {
		if (zv != NULL)
			rw_exit(&zv->zv_suspend_lock);
		if ((error = dmu_objset_own(name, DMU_OST_ZVOL, B_FALSE,
		    FTAG, &os)) != 0) {
			if (zv != NULL)
				mutex_exit(&zv->zv_state_lock);
			return (SET_ERROR(error));
		}
		owned = B_TRUE;
		if (zv != NULL)
			zv->zv_objset = os;
	} else {
		os = zv->zv_objset;
	}

	dmu_object_info_t *doi = kmem_alloc(sizeof (*doi), KM_SLEEP);

	if ((error = dmu_object_info(os, ZVOL_OBJ, doi)) ||
	    (error = zvol_check_volsize(volsize, doi->doi_data_block_size)))
		goto out;

	error = zvol_update_volsize(volsize, os);
	if (error == 0 && zv != NULL) {
		zv->zv_volsize = volsize;
		zv->zv_changed = 1;
		disk = zv->zv_disk;
	}
out:
	kmem_free(doi, sizeof (dmu_object_info_t));

	if (owned) {
		dmu_objset_disown(os, FTAG);
		if (zv != NULL)
			zv->zv_objset = NULL;
	} else {
		rw_exit(&zv->zv_suspend_lock);
	}

	if (zv != NULL)
		mutex_exit(&zv->zv_state_lock);

	if (disk != NULL)
		revalidate_disk(disk);

	return (SET_ERROR(error));
}

/*
 * Sanity check volume block size.
 */
int
zvol_check_volblocksize(const char *name, uint64_t volblocksize)
{
	/* Record sizes above 128k need the feature to be enabled */
	if (volblocksize > SPA_OLD_MAXBLOCKSIZE) {
		spa_t *spa;
		int error;

		if ((error = spa_open(name, &spa, FTAG)) != 0)
			return (error);

		if (!spa_feature_is_enabled(spa, SPA_FEATURE_LARGE_BLOCKS)) {
			spa_close(spa, FTAG);
			return (SET_ERROR(ENOTSUP));
		}

		/*
		 * We don't allow setting the property above 1MB,
		 * unless the tunable has been changed.
		 */
		if (volblocksize > zfs_max_recordsize)
			return (SET_ERROR(EDOM));

		spa_close(spa, FTAG);
	}

	if (volblocksize < SPA_MINBLOCKSIZE ||
	    volblocksize > SPA_MAXBLOCKSIZE ||
	    !ISP2(volblocksize))
		return (SET_ERROR(EDOM));

	return (0);
}

/*
 * Set ZFS_PROP_VOLBLOCKSIZE set entry point.
 */
int
zvol_set_volblocksize(const char *name, uint64_t volblocksize)
{
	zvol_state_t *zv;
	dmu_tx_t *tx;
	int error;

	zv = zvol_find_by_name(name, RW_READER);

	if (zv == NULL)
		return (SET_ERROR(ENXIO));

	ASSERT(MUTEX_HELD(&zv->zv_state_lock));
	ASSERT(RW_READ_HELD(&zv->zv_suspend_lock));

	if (zv->zv_flags & ZVOL_RDONLY) {
		mutex_exit(&zv->zv_state_lock);
		rw_exit(&zv->zv_suspend_lock);
		return (SET_ERROR(EROFS));
	}

	tx = dmu_tx_create(zv->zv_objset);
	dmu_tx_hold_bonus(tx, ZVOL_OBJ);
	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error) {
		dmu_tx_abort(tx);
	} else {
		error = dmu_object_set_blocksize(zv->zv_objset, ZVOL_OBJ,
		    volblocksize, 0, tx);
		if (error == ENOTSUP)
			error = SET_ERROR(EBUSY);
		dmu_tx_commit(tx);
		if (error == 0)
			zv->zv_volblocksize = volblocksize;
	}

	mutex_exit(&zv->zv_state_lock);
	rw_exit(&zv->zv_suspend_lock);

	return (SET_ERROR(error));
}

/*
 * Replay a TX_TRUNCATE ZIL transaction if asked.  TX_TRUNCATE is how we
 * implement DKIOCFREE/free-long-range.
 */
static int
zvol_replay_truncate(zvol_state_t *zv, lr_truncate_t *lr, boolean_t byteswap)
{
	uint64_t offset, length;

	if (byteswap)
		byteswap_uint64_array(lr, sizeof (*lr));

	offset = lr->lr_offset;
	length = lr->lr_length;

	return (dmu_free_long_range(zv->zv_objset, ZVOL_OBJ, offset, length));
}

/*
 * Replay a TX_WRITE ZIL transaction that didn't get committed
 * after a system failure
 */
static int
zvol_replay_write(zvol_state_t *zv, lr_write_t *lr, boolean_t byteswap)
{
	objset_t *os = zv->zv_objset;
	char *data = (char *)(lr + 1);  /* data follows lr_write_t */
	uint64_t offset, length;
	dmu_tx_t *tx;
	int error;

	if (byteswap)
		byteswap_uint64_array(lr, sizeof (*lr));

	offset = lr->lr_offset;
	length = lr->lr_length;

	/* If it's a dmu_sync() block, write the whole block */
	if (lr->lr_common.lrc_reclen == sizeof (lr_write_t)) {
		uint64_t blocksize = BP_GET_LSIZE(&lr->lr_blkptr);
		if (length < blocksize) {
			offset -= offset % blocksize;
			length = blocksize;
		}
	}

	tx = dmu_tx_create(os);
	dmu_tx_hold_write(tx, ZVOL_OBJ, offset, length);
	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error) {
		dmu_tx_abort(tx);
	} else {
		dmu_write(os, ZVOL_OBJ, offset, length, data, tx);
		dmu_tx_commit(tx);
	}

	return (error);
}

static int
zvol_replay_err(zvol_state_t *zv, lr_t *lr, boolean_t byteswap)
{
	return (SET_ERROR(ENOTSUP));
}

/*
 * Callback vectors for replaying records.
 * Only TX_WRITE and TX_TRUNCATE are needed for zvol.
 */
zil_replay_func_t zvol_replay_vector[TX_MAX_TYPE] = {
	(zil_replay_func_t)zvol_replay_err,	/* no such transaction type */
	(zil_replay_func_t)zvol_replay_err,	/* TX_CREATE */
	(zil_replay_func_t)zvol_replay_err,	/* TX_MKDIR */
	(zil_replay_func_t)zvol_replay_err,	/* TX_MKXATTR */
	(zil_replay_func_t)zvol_replay_err,	/* TX_SYMLINK */
	(zil_replay_func_t)zvol_replay_err,	/* TX_REMOVE */
	(zil_replay_func_t)zvol_replay_err,	/* TX_RMDIR */
	(zil_replay_func_t)zvol_replay_err,	/* TX_LINK */
	(zil_replay_func_t)zvol_replay_err,	/* TX_RENAME */
	(zil_replay_func_t)zvol_replay_write,	/* TX_WRITE */
	(zil_replay_func_t)zvol_replay_truncate, /* TX_TRUNCATE */
	(zil_replay_func_t)zvol_replay_err,	/* TX_SETATTR */
	(zil_replay_func_t)zvol_replay_err,	/* TX_ACL */
};

/*
 * zvol_log_write() handles synchronous writes using TX_WRITE ZIL transactions.
 *
 * We store data in the log buffers if it's small enough.
 * Otherwise we will later flush the data out via dmu_sync().
 */
ssize_t zvol_immediate_write_sz = 32768;

static void
zvol_log_write(zvol_state_t *zv, dmu_tx_t *tx, uint64_t offset,
    uint64_t size, int sync)
{
	uint32_t blocksize = zv->zv_volblocksize;
	zilog_t *zilog = zv->zv_zilog;
	itx_wr_state_t write_state;

	if (zil_replaying(zilog, tx))
		return;

	if (zilog->zl_logbias == ZFS_LOGBIAS_THROUGHPUT)
		write_state = WR_INDIRECT;
	else if (!spa_has_slogs(zilog->zl_spa) &&
	    size >= blocksize && blocksize > zvol_immediate_write_sz)
		write_state = WR_INDIRECT;
	else if (sync)
		write_state = WR_COPIED;
	else
		write_state = WR_NEED_COPY;

	while (size) {
		itx_t *itx;
		lr_write_t *lr;
		itx_wr_state_t wr_state = write_state;
		ssize_t len = size;

		if (wr_state == WR_COPIED && size > ZIL_MAX_COPIED_DATA)
			wr_state = WR_NEED_COPY;
		else if (wr_state == WR_INDIRECT)
			len = MIN(blocksize - P2PHASE(offset, blocksize), size);

		itx = zil_itx_create(TX_WRITE, sizeof (*lr) +
		    (wr_state == WR_COPIED ? len : 0));
		lr = (lr_write_t *)&itx->itx_lr;
		if (wr_state == WR_COPIED && dmu_read_by_dnode(zv->zv_dn,
		    offset, len, lr+1, DMU_READ_NO_PREFETCH) != 0) {
			zil_itx_destroy(itx);
			itx = zil_itx_create(TX_WRITE, sizeof (*lr));
			lr = (lr_write_t *)&itx->itx_lr;
			wr_state = WR_NEED_COPY;
		}

		itx->itx_wr_state = wr_state;
		lr->lr_foid = ZVOL_OBJ;
		lr->lr_offset = offset;
		lr->lr_length = len;
		lr->lr_blkoff = 0;
		BP_ZERO(&lr->lr_blkptr);

		itx->itx_private = zv;
		itx->itx_sync = sync;

		(void) zil_itx_assign(zilog, itx, tx);

		offset += len;
		size -= len;
	}
}

typedef struct zv_request {
	zvol_state_t	*zv;
	struct bio	*bio;
	rl_t		*rl;
} zv_request_t;

static void
uio_from_bio(uio_t *uio, struct bio *bio)
{
	uio->uio_bvec = &bio->bi_io_vec[BIO_BI_IDX(bio)];
	uio->uio_skip = BIO_BI_SKIP(bio);
	uio->uio_resid = BIO_BI_SIZE(bio);
	uio->uio_iovcnt = bio->bi_vcnt - BIO_BI_IDX(bio);
	uio->uio_loffset = BIO_BI_SECTOR(bio) << 9;
	uio->uio_limit = MAXOFFSET_T;
	uio->uio_segflg = UIO_BVEC;
}

static void
zvol_write(void *arg)
{
	zv_request_t *zvr = arg;
	struct bio *bio = zvr->bio;
	uio_t uio;
	zvol_state_t *zv = zvr->zv;
	uint64_t volsize = zv->zv_volsize;
	boolean_t sync;
	int error = 0;
	unsigned long start_jif;

	uio_from_bio(&uio, bio);

	ASSERT(zv && zv->zv_open_count > 0);

	start_jif = jiffies;
	blk_generic_start_io_acct(zv->zv_queue, WRITE, bio_sectors(bio),
	    &zv->zv_disk->part0);

	sync = bio_is_fua(bio) || zv->zv_objset->os_sync == ZFS_SYNC_ALWAYS;

	while (uio.uio_resid > 0 && uio.uio_loffset < volsize) {
		uint64_t bytes = MIN(uio.uio_resid, DMU_MAX_ACCESS >> 1);
		uint64_t off = uio.uio_loffset;
		dmu_tx_t *tx = dmu_tx_create(zv->zv_objset);

		if (bytes > volsize - off)	/* don't write past the end */
			bytes = volsize - off;

		dmu_tx_hold_write(tx, ZVOL_OBJ, off, bytes);

		/* This will only fail for ENOSPC */
		error = dmu_tx_assign(tx, TXG_WAIT);
		if (error) {
			dmu_tx_abort(tx);
			break;
		}
		error = dmu_write_uio_dnode(zv->zv_dn, &uio, bytes, tx);
		if (error == 0)
			zvol_log_write(zv, tx, off, bytes, sync);
		dmu_tx_commit(tx);

		if (error)
			break;
	}
	zfs_range_unlock(zvr->rl);
	if (sync)
		zil_commit(zv->zv_zilog, ZVOL_OBJ);

	rw_exit(&zv->zv_suspend_lock);
	blk_generic_end_io_acct(zv->zv_queue, WRITE, &zv->zv_disk->part0,
	    start_jif);
	BIO_END_IO(bio, -error);
	kmem_free(zvr, sizeof (zv_request_t));
}

/*
 * Log a DKIOCFREE/free-long-range to the ZIL with TX_TRUNCATE.
 */
static void
zvol_log_truncate(zvol_state_t *zv, dmu_tx_t *tx, uint64_t off, uint64_t len,
    boolean_t sync)
{
	itx_t *itx;
	lr_truncate_t *lr;
	zilog_t *zilog = zv->zv_zilog;

	if (zil_replaying(zilog, tx))
		return;

	itx = zil_itx_create(TX_TRUNCATE, sizeof (*lr));
	lr = (lr_truncate_t *)&itx->itx_lr;
	lr->lr_foid = ZVOL_OBJ;
	lr->lr_offset = off;
	lr->lr_length = len;

	itx->itx_sync = sync;
	zil_itx_assign(zilog, itx, tx);
}

static void
zvol_discard(void *arg)
{
	zv_request_t *zvr = arg;
	struct bio *bio = zvr->bio;
	zvol_state_t *zv = zvr->zv;
	uint64_t start = BIO_BI_SECTOR(bio) << 9;
	uint64_t size = BIO_BI_SIZE(bio);
	uint64_t end = start + size;
	boolean_t sync;
	int error = 0;
	dmu_tx_t *tx;
	unsigned long start_jif;

	ASSERT(zv && zv->zv_open_count > 0);

	start_jif = jiffies;
	blk_generic_start_io_acct(zv->zv_queue, WRITE, bio_sectors(bio),
	    &zv->zv_disk->part0);

	sync = bio_is_fua(bio) || zv->zv_objset->os_sync == ZFS_SYNC_ALWAYS;

	if (end > zv->zv_volsize) {
		error = SET_ERROR(EIO);
		goto unlock;
	}

	/*
	 * Align the request to volume block boundaries when a secure erase is
	 * not required.  This will prevent dnode_free_range() from zeroing out
	 * the unaligned parts which is slow (read-modify-write) and useless
	 * since we are not freeing any space by doing so.
	 */
	if (!bio_is_secure_erase(bio)) {
		start = P2ROUNDUP(start, zv->zv_volblocksize);
		end = P2ALIGN(end, zv->zv_volblocksize);
		size = end - start;
	}

	if (start >= end)
		goto unlock;

	tx = dmu_tx_create(zv->zv_objset);
	dmu_tx_mark_netfree(tx);
	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error != 0) {
		dmu_tx_abort(tx);
	} else {
		zvol_log_truncate(zv, tx, start, size, B_TRUE);
		dmu_tx_commit(tx);
		error = dmu_free_long_range(zv->zv_objset,
		    ZVOL_OBJ, start, size);
	}
unlock:
	zfs_range_unlock(zvr->rl);
	if (error == 0 && sync)
		zil_commit(zv->zv_zilog, ZVOL_OBJ);

	rw_exit(&zv->zv_suspend_lock);
	blk_generic_end_io_acct(zv->zv_queue, WRITE, &zv->zv_disk->part0,
	    start_jif);
	BIO_END_IO(bio, -error);
	kmem_free(zvr, sizeof (zv_request_t));
}

static void
zvol_read(void *arg)
{
	zv_request_t *zvr = arg;
	struct bio *bio = zvr->bio;
	uio_t uio;
	zvol_state_t *zv = zvr->zv;
	uint64_t volsize = zv->zv_volsize;
	int error = 0;
	unsigned long start_jif;

	uio_from_bio(&uio, bio);

	ASSERT(zv && zv->zv_open_count > 0);

	start_jif = jiffies;
	blk_generic_start_io_acct(zv->zv_queue, READ, bio_sectors(bio),
	    &zv->zv_disk->part0);

	while (uio.uio_resid > 0 && uio.uio_loffset < volsize) {
		uint64_t bytes = MIN(uio.uio_resid, DMU_MAX_ACCESS >> 1);

		/* don't read past the end */
		if (bytes > volsize - uio.uio_loffset)
			bytes = volsize - uio.uio_loffset;

		error = dmu_read_uio_dnode(zv->zv_dn, &uio, bytes);
		if (error) {
			/* convert checksum errors into IO errors */
			if (error == ECKSUM)
				error = SET_ERROR(EIO);
			break;
		}
	}
	zfs_range_unlock(zvr->rl);

	rw_exit(&zv->zv_suspend_lock);
	blk_generic_end_io_acct(zv->zv_queue, READ, &zv->zv_disk->part0,
	    start_jif);
	BIO_END_IO(bio, -error);
	kmem_free(zvr, sizeof (zv_request_t));
}

static MAKE_REQUEST_FN_RET
zvol_request(struct request_queue *q, struct bio *bio)
{
	zvol_state_t *zv = q->queuedata;
	fstrans_cookie_t cookie = spl_fstrans_mark();
	uint64_t offset = BIO_BI_SECTOR(bio) << 9;
	uint64_t size = BIO_BI_SIZE(bio);
	int rw = bio_data_dir(bio);
	zv_request_t *zvr;

	if (bio_has_data(bio) && offset + size > zv->zv_volsize) {
		printk(KERN_INFO
		    "%s: bad access: offset=%llu, size=%lu\n",
		    zv->zv_disk->disk_name,
		    (long long unsigned)offset,
		    (long unsigned)size);

		BIO_END_IO(bio, -SET_ERROR(EIO));
		goto out;
	}

	if (rw == WRITE) {
		boolean_t need_sync = B_FALSE;

		if (unlikely(zv->zv_flags & ZVOL_RDONLY)) {
			BIO_END_IO(bio, -SET_ERROR(EROFS));
			goto out;
		}

		/*
		 * To be released in the I/O function. See the comment on
		 * zfs_range_lock below.
		 */
		rw_enter(&zv->zv_suspend_lock, RW_READER);

		/* bio marked as FLUSH need to flush before write */
		if (bio_is_flush(bio))
			zil_commit(zv->zv_zilog, ZVOL_OBJ);

		/* Some requests are just for flush and nothing else. */
		if (size == 0) {
			rw_exit(&zv->zv_suspend_lock);
			BIO_END_IO(bio, 0);
			goto out;
		}

		zvr = kmem_alloc(sizeof (zv_request_t), KM_SLEEP);
		zvr->zv = zv;
		zvr->bio = bio;

		/*
		 * To be released in the I/O function. Since the I/O functions
		 * are asynchronous, we take it here synchronously to make
		 * sure overlapped I/Os are properly ordered.
		 */
		zvr->rl = zfs_range_lock(&zv->zv_range_lock, offset, size,
		    RL_WRITER);
		/*
		 * Sync writes and discards execute zil_commit() which may need
		 * to take a RL_READER lock on the whole block being modified
		 * via its zillog->zl_get_data(): to avoid circular dependency
		 * issues with taskq threads execute these requests
		 * synchronously here in zvol_request().
		 */
		need_sync = bio_is_fua(bio) ||
		    zv->zv_objset->os_sync == ZFS_SYNC_ALWAYS;
		if (bio_is_discard(bio) || bio_is_secure_erase(bio)) {
			if (zvol_request_sync || need_sync ||
			    taskq_dispatch(zvol_taskq, zvol_discard, zvr,
			    TQ_SLEEP) == TASKQID_INVALID)
				zvol_discard(zvr);
		} else {
			if (zvol_request_sync || need_sync ||
			    taskq_dispatch(zvol_taskq, zvol_write, zvr,
			    TQ_SLEEP) == TASKQID_INVALID)
				zvol_write(zvr);
		}
	} else {
		zvr = kmem_alloc(sizeof (zv_request_t), KM_SLEEP);
		zvr->zv = zv;
		zvr->bio = bio;

		rw_enter(&zv->zv_suspend_lock, RW_READER);

		zvr->rl = zfs_range_lock(&zv->zv_range_lock, offset, size,
		    RL_READER);
		if (zvol_request_sync || taskq_dispatch(zvol_taskq,
		    zvol_read, zvr, TQ_SLEEP) == TASKQID_INVALID)
			zvol_read(zvr);
	}

out:
	spl_fstrans_unmark(cookie);
#ifdef HAVE_MAKE_REQUEST_FN_RET_INT
	return (0);
#elif defined(HAVE_MAKE_REQUEST_FN_RET_QC)
	return (BLK_QC_T_NONE);
#endif
}

static void
zvol_get_done(zgd_t *zgd, int error)
{
	if (zgd->zgd_db)
		dmu_buf_rele(zgd->zgd_db, zgd);

	zfs_range_unlock(zgd->zgd_rl);

	if (error == 0 && zgd->zgd_bp)
		zil_add_block(zgd->zgd_zilog, zgd->zgd_bp);

	kmem_free(zgd, sizeof (zgd_t));
}

/*
 * Get data to generate a TX_WRITE intent log record.
 */
static int
zvol_get_data(void *arg, lr_write_t *lr, char *buf, zio_t *zio)
{
	zvol_state_t *zv = arg;
	uint64_t offset = lr->lr_offset;
	uint64_t size = lr->lr_length;
	dmu_buf_t *db;
	zgd_t *zgd;
	int error;

	ASSERT(zio != NULL);
	ASSERT(size != 0);

	zgd = (zgd_t *)kmem_zalloc(sizeof (zgd_t), KM_SLEEP);
	zgd->zgd_zilog = zv->zv_zilog;

	/*
	 * Write records come in two flavors: immediate and indirect.
	 * For small writes it's cheaper to store the data with the
	 * log record (immediate); for large writes it's cheaper to
	 * sync the data and get a pointer to it (indirect) so that
	 * we don't have to write the data twice.
	 */
	if (buf != NULL) { /* immediate write */
		zgd->zgd_rl = zfs_range_lock(&zv->zv_range_lock, offset, size,
		    RL_READER);
		error = dmu_read_by_dnode(zv->zv_dn, offset, size, buf,
		    DMU_READ_NO_PREFETCH);
	} else { /* indirect write */
		/*
		 * Have to lock the whole block to ensure when it's written out
		 * and its checksum is being calculated that no one can change
		 * the data. Contrarily to zfs_get_data we need not re-check
		 * blocksize after we get the lock because it cannot be changed.
		 */
		size = zv->zv_volblocksize;
		offset = P2ALIGN_TYPED(offset, size, uint64_t);
		zgd->zgd_rl = zfs_range_lock(&zv->zv_range_lock, offset, size,
		    RL_READER);
		error = dmu_buf_hold_by_dnode(zv->zv_dn, offset, zgd, &db,
		    DMU_READ_NO_PREFETCH);
		if (error == 0) {
			blkptr_t *bp = &lr->lr_blkptr;

			zgd->zgd_db = db;
			zgd->zgd_bp = bp;

			ASSERT(db != NULL);
			ASSERT(db->db_offset == offset);
			ASSERT(db->db_size == size);

			error = dmu_sync(zio, lr->lr_common.lrc_txg,
			    zvol_get_done, zgd);

			if (error == 0)
				return (0);
		}
	}

	zvol_get_done(zgd, error);

	return (SET_ERROR(error));
}

/*
 * The zvol_state_t's are inserted into zvol_state_list and zvol_htable.
 */
static void
zvol_insert(zvol_state_t *zv)
{
	ASSERT(RW_WRITE_HELD(&zvol_state_lock));
	ASSERT3U(MINOR(zv->zv_dev) & ZVOL_MINOR_MASK, ==, 0);
	list_insert_head(&zvol_state_list, zv);
	hlist_add_head(&zv->zv_hlink, ZVOL_HT_HEAD(zv->zv_hash));
}

/*
 * Simply remove the zvol from to list of zvols.
 */
static void
zvol_remove(zvol_state_t *zv)
{
	ASSERT(RW_WRITE_HELD(&zvol_state_lock));
	list_remove(&zvol_state_list, zv);
	hlist_del(&zv->zv_hlink);
}

/*
 * Setup zv after we just own the zv->objset
 */
static int
zvol_setup_zv(zvol_state_t *zv)
{
	uint64_t volsize;
	int error;
	uint64_t ro;
	objset_t *os = zv->zv_objset;

	ASSERT(MUTEX_HELD(&zv->zv_state_lock));
	ASSERT(RW_LOCK_HELD(&zv->zv_suspend_lock));

	error = dsl_prop_get_integer(zv->zv_name, "readonly", &ro, NULL);
	if (error)
		return (SET_ERROR(error));

	error = zap_lookup(os, ZVOL_ZAP_OBJ, "size", 8, 1, &volsize);
	if (error)
		return (SET_ERROR(error));

	error = dnode_hold(os, ZVOL_OBJ, FTAG, &zv->zv_dn);
	if (error)
		return (SET_ERROR(error));

	set_capacity(zv->zv_disk, volsize >> 9);
	zv->zv_volsize = volsize;
	zv->zv_zilog = zil_open(os, zvol_get_data);

	if (ro || dmu_objset_is_snapshot(os) ||
	    !spa_writeable(dmu_objset_spa(os))) {
		set_disk_ro(zv->zv_disk, 1);
		zv->zv_flags |= ZVOL_RDONLY;
	} else {
		set_disk_ro(zv->zv_disk, 0);
		zv->zv_flags &= ~ZVOL_RDONLY;
	}
	return (0);
}

/*
 * Shutdown every zv_objset related stuff except zv_objset itself.
 * The is the reverse of zvol_setup_zv.
 */
static void
zvol_shutdown_zv(zvol_state_t *zv)
{
	ASSERT(MUTEX_HELD(&zv->zv_state_lock) &&
	    RW_LOCK_HELD(&zv->zv_suspend_lock));

	zil_close(zv->zv_zilog);
	zv->zv_zilog = NULL;

	dnode_rele(zv->zv_dn, FTAG);
	zv->zv_dn = NULL;

	/*
	 * Evict cached data
	 */
	if (dsl_dataset_is_dirty(dmu_objset_ds(zv->zv_objset)) &&
	    !(zv->zv_flags & ZVOL_RDONLY))
		txg_wait_synced(dmu_objset_pool(zv->zv_objset), 0);
	(void) dmu_objset_evict_dbufs(zv->zv_objset);
}

/*
 * return the proper tag for rollback and recv
 */
void *
zvol_tag(zvol_state_t *zv)
{
	ASSERT(RW_WRITE_HELD(&zv->zv_suspend_lock));
	return (zv->zv_open_count > 0 ? zv : NULL);
}

/*
 * Suspend the zvol for recv and rollback.
 */
zvol_state_t *
zvol_suspend(const char *name)
{
	zvol_state_t *zv;

	zv = zvol_find_by_name(name, RW_WRITER);

	if (zv == NULL)
		return (NULL);

	/* block all I/O, release in zvol_resume. */
	ASSERT(MUTEX_HELD(&zv->zv_state_lock));
	ASSERT(RW_WRITE_HELD(&zv->zv_suspend_lock));

	atomic_inc(&zv->zv_suspend_ref);

	if (zv->zv_open_count > 0)
		zvol_shutdown_zv(zv);

	/*
	 * do not hold zv_state_lock across suspend/resume to
	 * avoid locking up zvol lookups
	 */
	mutex_exit(&zv->zv_state_lock);

	/* zv_suspend_lock is released in zvol_resume() */
	return (zv);
}

int
zvol_resume(zvol_state_t *zv)
{
	int error = 0;

	ASSERT(RW_WRITE_HELD(&zv->zv_suspend_lock));

	mutex_enter(&zv->zv_state_lock);

	if (zv->zv_open_count > 0) {
		VERIFY0(dmu_objset_hold(zv->zv_name, zv, &zv->zv_objset));
		VERIFY3P(zv->zv_objset->os_dsl_dataset->ds_owner, ==, zv);
		VERIFY(dsl_dataset_long_held(zv->zv_objset->os_dsl_dataset));
		dmu_objset_rele(zv->zv_objset, zv);

		error = zvol_setup_zv(zv);
	}

	mutex_exit(&zv->zv_state_lock);

	rw_exit(&zv->zv_suspend_lock);
	/*
	 * We need this because we don't hold zvol_state_lock while releasing
	 * zv_suspend_lock. zvol_remove_minors_impl thus cannot check
	 * zv_suspend_lock to determine it is safe to free because rwlock is
	 * not inherent atomic.
	 */
	atomic_dec(&zv->zv_suspend_ref);

	return (SET_ERROR(error));
}

static int
zvol_first_open(zvol_state_t *zv)
{
	objset_t *os;
	int error, locked = 0;

	ASSERT(RW_READ_HELD(&zv->zv_suspend_lock));
	ASSERT(MUTEX_HELD(&zv->zv_state_lock));

	/*
	 * In all other cases the spa_namespace_lock is taken before the
	 * bdev->bd_mutex lock.	 But in this case the Linux __blkdev_get()
	 * function calls fops->open() with the bdev->bd_mutex lock held.
	 * This deadlock can be easily observed with zvols used as vdevs.
	 *
	 * To avoid a potential lock inversion deadlock we preemptively
	 * try to take the spa_namespace_lock().  Normally it will not
	 * be contended and this is safe because spa_open_common() handles
	 * the case where the caller already holds the spa_namespace_lock.
	 *
	 * When it is contended we risk a lock inversion if we were to
	 * block waiting for the lock.	Luckily, the __blkdev_get()
	 * function allows us to return -ERESTARTSYS which will result in
	 * bdev->bd_mutex being dropped, reacquired, and fops->open() being
	 * called again.  This process can be repeated safely until both
	 * locks are acquired.
	 */
	if (!mutex_owned(&spa_namespace_lock)) {
		locked = mutex_tryenter(&spa_namespace_lock);
		if (!locked)
			return (-SET_ERROR(ERESTARTSYS));
	}

	/* lie and say we're read-only */
	error = dmu_objset_own(zv->zv_name, DMU_OST_ZVOL, 1, zv, &os);
	if (error)
		goto out_mutex;

	zv->zv_objset = os;

	error = zvol_setup_zv(zv);

	if (error) {
		dmu_objset_disown(os, zv);
		zv->zv_objset = NULL;
	}

out_mutex:
	if (locked)
		mutex_exit(&spa_namespace_lock);
	return (SET_ERROR(-error));
}

static void
zvol_last_close(zvol_state_t *zv)
{
	ASSERT(RW_READ_HELD(&zv->zv_suspend_lock));
	ASSERT(MUTEX_HELD(&zv->zv_state_lock));

	zvol_shutdown_zv(zv);

	dmu_objset_disown(zv->zv_objset, zv);
	zv->zv_objset = NULL;
}

static int
zvol_open(struct block_device *bdev, fmode_t flag)
{
	zvol_state_t *zv;
	int error = 0;
	boolean_t drop_suspend = B_TRUE;

	rw_enter(&zvol_state_lock, RW_READER);
	/*
	 * Obtain a copy of private_data under the zvol_state_lock to make
	 * sure that either the result of zvol free code path setting
	 * bdev->bd_disk->private_data to NULL is observed, or zvol_free()
	 * is not called on this zv because of the positive zv_open_count.
	 */
	zv = bdev->bd_disk->private_data;
	if (zv == NULL) {
		rw_exit(&zvol_state_lock);
		return (SET_ERROR(-ENXIO));
	}

	mutex_enter(&zv->zv_state_lock);
	/*
	 * make sure zvol is not suspended during first open
	 * (hold zv_suspend_lock) and respect proper lock acquisition
	 * ordering - zv_suspend_lock before zv_state_lock
	 */
	if (zv->zv_open_count == 0) {
		if (!rw_tryenter(&zv->zv_suspend_lock, RW_READER)) {
			mutex_exit(&zv->zv_state_lock);
			rw_enter(&zv->zv_suspend_lock, RW_READER);
			mutex_enter(&zv->zv_state_lock);
			/* check to see if zv_suspend_lock is needed */
			if (zv->zv_open_count != 0) {
				rw_exit(&zv->zv_suspend_lock);
				drop_suspend = B_FALSE;
			}
		}
	} else {
		drop_suspend = B_FALSE;
	}
	rw_exit(&zvol_state_lock);

	ASSERT(MUTEX_HELD(&zv->zv_state_lock));
	ASSERT(zv->zv_open_count != 0 || RW_READ_HELD(&zv->zv_suspend_lock));

	if (zv->zv_open_count == 0) {
		error = zvol_first_open(zv);
		if (error)
			goto out_mutex;
	}

	if ((flag & FMODE_WRITE) && (zv->zv_flags & ZVOL_RDONLY)) {
		error = -EROFS;
		goto out_open_count;
	}

	zv->zv_open_count++;

	mutex_exit(&zv->zv_state_lock);
	if (drop_suspend)
		rw_exit(&zv->zv_suspend_lock);

	check_disk_change(bdev);

	return (0);

out_open_count:
	if (zv->zv_open_count == 0)
		zvol_last_close(zv);

out_mutex:
	mutex_exit(&zv->zv_state_lock);
	if (drop_suspend)
		rw_exit(&zv->zv_suspend_lock);
	if (error == -ERESTARTSYS)
		schedule();

	return (SET_ERROR(error));
}

#ifdef HAVE_BLOCK_DEVICE_OPERATIONS_RELEASE_VOID
static void
#else
static int
#endif
zvol_release(struct gendisk *disk, fmode_t mode)
{
	zvol_state_t *zv;
	boolean_t drop_suspend = B_TRUE;

	rw_enter(&zvol_state_lock, RW_READER);
	zv = disk->private_data;

	mutex_enter(&zv->zv_state_lock);
	ASSERT(zv->zv_open_count > 0);
	/*
	 * make sure zvol is not suspended during last close
	 * (hold zv_suspend_lock) and respect proper lock acquisition
	 * ordering - zv_suspend_lock before zv_state_lock
	 */
	if (zv->zv_open_count == 1) {
		if (!rw_tryenter(&zv->zv_suspend_lock, RW_READER)) {
			mutex_exit(&zv->zv_state_lock);
			rw_enter(&zv->zv_suspend_lock, RW_READER);
			mutex_enter(&zv->zv_state_lock);
			/* check to see if zv_suspend_lock is needed */
			if (zv->zv_open_count != 1) {
				rw_exit(&zv->zv_suspend_lock);
				drop_suspend = B_FALSE;
			}
		}
	} else {
		drop_suspend = B_FALSE;
	}
	rw_exit(&zvol_state_lock);

	ASSERT(MUTEX_HELD(&zv->zv_state_lock));
	ASSERT(zv->zv_open_count != 1 || RW_READ_HELD(&zv->zv_suspend_lock));

	zv->zv_open_count--;
	if (zv->zv_open_count == 0)
		zvol_last_close(zv);

	mutex_exit(&zv->zv_state_lock);

	if (drop_suspend)
		rw_exit(&zv->zv_suspend_lock);

#ifndef HAVE_BLOCK_DEVICE_OPERATIONS_RELEASE_VOID
	return (0);
#endif
}

static int
zvol_ioctl(struct block_device *bdev, fmode_t mode,
    unsigned int cmd, unsigned long arg)
{
	zvol_state_t *zv = bdev->bd_disk->private_data;
	int error = 0;

	ASSERT3U(zv->zv_open_count, >, 0);

	switch (cmd) {
	case BLKFLSBUF:
		fsync_bdev(bdev);
		invalidate_bdev(bdev);
		rw_enter(&zv->zv_suspend_lock, RW_READER);

		if (dsl_dataset_is_dirty(dmu_objset_ds(zv->zv_objset)) &&
		    !(zv->zv_flags & ZVOL_RDONLY))
			txg_wait_synced(dmu_objset_pool(zv->zv_objset), 0);

		rw_exit(&zv->zv_suspend_lock);
		break;

	case BLKZNAME:
		mutex_enter(&zv->zv_state_lock);
		error = copy_to_user((void *)arg, zv->zv_name, MAXNAMELEN);
		mutex_exit(&zv->zv_state_lock);
		break;

	default:
		error = -ENOTTY;
		break;
	}

	return (SET_ERROR(error));
}

#ifdef CONFIG_COMPAT
static int
zvol_compat_ioctl(struct block_device *bdev, fmode_t mode,
    unsigned cmd, unsigned long arg)
{
	return (zvol_ioctl(bdev, mode, cmd, arg));
}
#else
#define	zvol_compat_ioctl	NULL
#endif

/*
 * Linux 2.6.38 preferred interface.
 */
#ifdef HAVE_BLOCK_DEVICE_OPERATIONS_CHECK_EVENTS
static unsigned int
zvol_check_events(struct gendisk *disk, unsigned int clearing)
{
	unsigned int mask = 0;

	rw_enter(&zvol_state_lock, RW_READER);

	zvol_state_t *zv = disk->private_data;
	if (zv != NULL) {
		mutex_enter(&zv->zv_state_lock);
		mask = zv->zv_changed ? DISK_EVENT_MEDIA_CHANGE : 0;
		zv->zv_changed = 0;
		mutex_exit(&zv->zv_state_lock);
	}

	rw_exit(&zvol_state_lock);

	return (mask);
}
#else
static int zvol_media_changed(struct gendisk *disk)
{
	int changed = 0;

	rw_enter(&zvol_state_lock, RW_READER);

	zvol_state_t *zv = disk->private_data;
	if (zv != NULL) {
		mutex_enter(&zv->zv_state_lock);
		changed = zv->zv_changed;
		zv->zv_changed = 0;
		mutex_exit(&zv->zv_state_lock);
	}

	rw_exit(&zvol_state_lock);

	return (changed);
}
#endif

static int zvol_revalidate_disk(struct gendisk *disk)
{
	rw_enter(&zvol_state_lock, RW_READER);

	zvol_state_t *zv = disk->private_data;
	if (zv != NULL) {
		mutex_enter(&zv->zv_state_lock);
		set_capacity(zv->zv_disk, zv->zv_volsize >> SECTOR_BITS);
		mutex_exit(&zv->zv_state_lock);
	}

	rw_exit(&zvol_state_lock);

	return (0);
}

/*
 * Provide a simple virtual geometry for legacy compatibility.  For devices
 * smaller than 1 MiB a small head and sector count is used to allow very
 * tiny devices.  For devices over 1 Mib a standard head and sector count
 * is used to keep the cylinders count reasonable.
 */
static int
zvol_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	zvol_state_t *zv = bdev->bd_disk->private_data;
	sector_t sectors;

	ASSERT3U(zv->zv_open_count, >, 0);

	sectors = get_capacity(zv->zv_disk);

	if (sectors > 2048) {
		geo->heads = 16;
		geo->sectors = 63;
	} else {
		geo->heads = 2;
		geo->sectors = 4;
	}

	geo->start = 0;
	geo->cylinders = sectors / (geo->heads * geo->sectors);

	return (0);
}

static struct kobject *
zvol_probe(dev_t dev, int *part, void *arg)
{
	zvol_state_t *zv;
	struct kobject *kobj;

	zv = zvol_find_by_dev(dev);
	kobj = zv ? get_disk_and_module(zv->zv_disk) : NULL;
	ASSERT(zv == NULL || MUTEX_HELD(&zv->zv_state_lock));
	if (zv)
		mutex_exit(&zv->zv_state_lock);

	return (kobj);
}

static struct block_device_operations zvol_ops = {
	.open			= zvol_open,
	.release		= zvol_release,
	.ioctl			= zvol_ioctl,
	.compat_ioctl		= zvol_compat_ioctl,
#ifdef HAVE_BLOCK_DEVICE_OPERATIONS_CHECK_EVENTS
	.check_events		= zvol_check_events,
#else
	.media_changed		= zvol_media_changed,
#endif
	.revalidate_disk	= zvol_revalidate_disk,
	.getgeo			= zvol_getgeo,
	.owner			= THIS_MODULE,
};

/*
 * Allocate memory for a new zvol_state_t and setup the required
 * request queue and generic disk structures for the block device.
 */
static zvol_state_t *
zvol_alloc(dev_t dev, const char *name)
{
	zvol_state_t *zv;
	uint64_t volmode;

	if (dsl_prop_get_integer(name, "volmode", &volmode, NULL) != 0)
		return (NULL);

	if (volmode == ZFS_VOLMODE_DEFAULT)
		volmode = zvol_volmode;

	if (volmode == ZFS_VOLMODE_NONE)
		return (NULL);

	zv = kmem_zalloc(sizeof (zvol_state_t), KM_SLEEP);

	list_link_init(&zv->zv_next);

	mutex_init(&zv->zv_state_lock, NULL, MUTEX_DEFAULT, NULL);

	zv->zv_queue = blk_alloc_queue(GFP_ATOMIC);
	if (zv->zv_queue == NULL)
		goto out_kmem;

	blk_queue_make_request(zv->zv_queue, zvol_request);
	blk_queue_set_write_cache(zv->zv_queue, B_TRUE, B_TRUE);

	/* Limit read-ahead to a single page to prevent over-prefetching. */
	blk_queue_set_read_ahead(zv->zv_queue, 1);

	/* Disable write merging in favor of the ZIO pipeline. */
	blk_queue_flag_set(QUEUE_FLAG_NOMERGES, zv->zv_queue);

	zv->zv_disk = alloc_disk(ZVOL_MINORS);
	if (zv->zv_disk == NULL)
		goto out_queue;

	zv->zv_queue->queuedata = zv;
	zv->zv_dev = dev;
	zv->zv_open_count = 0;
	strlcpy(zv->zv_name, name, MAXNAMELEN);

	zfs_rlock_init(&zv->zv_range_lock);
	rw_init(&zv->zv_suspend_lock, NULL, RW_DEFAULT, NULL);

	zv->zv_disk->major = zvol_major;
#ifdef HAVE_BLOCK_DEVICE_OPERATIONS_CHECK_EVENTS
	zv->zv_disk->events = DISK_EVENT_MEDIA_CHANGE;
#endif

	if (volmode == ZFS_VOLMODE_DEV) {
		/*
		 * ZFS_VOLMODE_DEV disable partitioning on ZVOL devices: set
		 * gendisk->minors = 1 as noted in include/linux/genhd.h.
		 * Also disable extended partition numbers (GENHD_FL_EXT_DEVT)
		 * and suppresses partition scanning (GENHD_FL_NO_PART_SCAN)
		 * setting gendisk->flags accordingly.
		 */
		zv->zv_disk->minors = 1;
#if defined(GENHD_FL_EXT_DEVT)
		zv->zv_disk->flags &= ~GENHD_FL_EXT_DEVT;
#endif
#if defined(GENHD_FL_NO_PART_SCAN)
		zv->zv_disk->flags |= GENHD_FL_NO_PART_SCAN;
#endif
	}
	zv->zv_disk->first_minor = (dev & MINORMASK);
	zv->zv_disk->fops = &zvol_ops;
	zv->zv_disk->private_data = zv;
	zv->zv_disk->queue = zv->zv_queue;
	snprintf(zv->zv_disk->disk_name, DISK_NAME_LEN, "%s%d",
	    ZVOL_DEV_NAME, (dev & MINORMASK));

	return (zv);

out_queue:
	blk_cleanup_queue(zv->zv_queue);
out_kmem:
	kmem_free(zv, sizeof (zvol_state_t));

	return (NULL);
}

/*
 * Cleanup then free a zvol_state_t which was created by zvol_alloc().
 * At this time, the structure is not opened by anyone, is taken off
 * the zvol_state_list, and has its private data set to NULL.
 * The zvol_state_lock is dropped.
 */
static void
zvol_free(void *arg)
{
	zvol_state_t *zv = arg;

	ASSERT(!RW_LOCK_HELD(&zv->zv_suspend_lock));
	ASSERT(!MUTEX_HELD(&zv->zv_state_lock));
	ASSERT(zv->zv_open_count == 0);
	ASSERT(zv->zv_disk->private_data == NULL);

	rw_destroy(&zv->zv_suspend_lock);
	zfs_rlock_destroy(&zv->zv_range_lock);

	del_gendisk(zv->zv_disk);
	blk_cleanup_queue(zv->zv_queue);
	put_disk(zv->zv_disk);

	ida_simple_remove(&zvol_ida, MINOR(zv->zv_dev) >> ZVOL_MINOR_BITS);

	mutex_destroy(&zv->zv_state_lock);

	kmem_free(zv, sizeof (zvol_state_t));
}

/*
 * Create a block device minor node and setup the linkage between it
 * and the specified volume.  Once this function returns the block
 * device is live and ready for use.
 */
static int
zvol_create_minor_impl(const char *name)
{
	zvol_state_t *zv;
	objset_t *os;
	dmu_object_info_t *doi;
	uint64_t volsize;
	uint64_t len;
	unsigned minor = 0;
	int error = 0;
	int idx;
	uint64_t hash = zvol_name_hash(name);

	if (zvol_inhibit_dev)
		return (0);

	idx = ida_simple_get(&zvol_ida, 0, 0, kmem_flags_convert(KM_SLEEP));
	if (idx < 0)
		return (SET_ERROR(-idx));
	minor = idx << ZVOL_MINOR_BITS;

	zv = zvol_find_by_name_hash(name, hash, RW_NONE);
	if (zv) {
		ASSERT(MUTEX_HELD(&zv->zv_state_lock));
		mutex_exit(&zv->zv_state_lock);
		ida_simple_remove(&zvol_ida, idx);
		return (SET_ERROR(EEXIST));
	}

	doi = kmem_alloc(sizeof (dmu_object_info_t), KM_SLEEP);

	error = dmu_objset_own(name, DMU_OST_ZVOL, B_TRUE, FTAG, &os);
	if (error)
		goto out_doi;

	error = dmu_object_info(os, ZVOL_OBJ, doi);
	if (error)
		goto out_dmu_objset_disown;

	error = zap_lookup(os, ZVOL_ZAP_OBJ, "size", 8, 1, &volsize);
	if (error)
		goto out_dmu_objset_disown;

	zv = zvol_alloc(MKDEV(zvol_major, minor), name);
	if (zv == NULL) {
		error = SET_ERROR(EAGAIN);
		goto out_dmu_objset_disown;
	}
	zv->zv_hash = hash;

	if (dmu_objset_is_snapshot(os))
		zv->zv_flags |= ZVOL_RDONLY;

	zv->zv_volblocksize = doi->doi_data_block_size;
	zv->zv_volsize = volsize;
	zv->zv_objset = os;

	set_capacity(zv->zv_disk, zv->zv_volsize >> 9);

	blk_queue_max_hw_sectors(zv->zv_queue, (DMU_MAX_ACCESS / 4) >> 9);
	blk_queue_max_segments(zv->zv_queue, UINT16_MAX);
	blk_queue_max_segment_size(zv->zv_queue, UINT_MAX);
	blk_queue_physical_block_size(zv->zv_queue, zv->zv_volblocksize);
	blk_queue_io_opt(zv->zv_queue, zv->zv_volblocksize);
	blk_queue_max_discard_sectors(zv->zv_queue,
	    (zvol_max_discard_blocks * zv->zv_volblocksize) >> 9);
	blk_queue_discard_granularity(zv->zv_queue, zv->zv_volblocksize);
	blk_queue_flag_set(QUEUE_FLAG_DISCARD, zv->zv_queue);
#ifdef QUEUE_FLAG_NONROT
	blk_queue_flag_set(QUEUE_FLAG_NONROT, zv->zv_queue);
#endif
#ifdef QUEUE_FLAG_ADD_RANDOM
	blk_queue_flag_clear(QUEUE_FLAG_ADD_RANDOM, zv->zv_queue);
#endif

	if (spa_writeable(dmu_objset_spa(os))) {
		if (zil_replay_disable)
			zil_destroy(dmu_objset_zil(os), B_FALSE);
		else
			zil_replay(os, zv, zvol_replay_vector);
	}

	/*
	 * When udev detects the addition of the device it will immediately
	 * invoke blkid(8) to determine the type of content on the device.
	 * Prefetching the blocks commonly scanned by blkid(8) will speed
	 * up this process.
	 */
	len = MIN(MAX(zvol_prefetch_bytes, 0), SPA_MAXBLOCKSIZE);
	if (len > 0) {
		dmu_prefetch(os, ZVOL_OBJ, 0, 0, len, ZIO_PRIORITY_SYNC_READ);
		dmu_prefetch(os, ZVOL_OBJ, 0, volsize - len, len,
		    ZIO_PRIORITY_SYNC_READ);
	}

	zv->zv_objset = NULL;
out_dmu_objset_disown:
	dmu_objset_disown(os, FTAG);
out_doi:
	kmem_free(doi, sizeof (dmu_object_info_t));

	if (error == 0) {
		rw_enter(&zvol_state_lock, RW_WRITER);
		zvol_insert(zv);
		rw_exit(&zvol_state_lock);
		add_disk(zv->zv_disk);
	} else {
		ida_simple_remove(&zvol_ida, idx);
	}

	return (SET_ERROR(error));
}

/*
 * Rename a block device minor mode for the specified volume.
 */
static void
zvol_rename_minor(zvol_state_t *zv, const char *newname)
{
	int readonly = get_disk_ro(zv->zv_disk);

	ASSERT(RW_LOCK_HELD(&zvol_state_lock));
	ASSERT(MUTEX_HELD(&zv->zv_state_lock));

	strlcpy(zv->zv_name, newname, sizeof (zv->zv_name));

	/* move to new hashtable entry  */
	zv->zv_hash = zvol_name_hash(zv->zv_name);
	hlist_del(&zv->zv_hlink);
	hlist_add_head(&zv->zv_hlink, ZVOL_HT_HEAD(zv->zv_hash));

	/*
	 * The block device's read-only state is briefly changed causing
	 * a KOBJ_CHANGE uevent to be issued.  This ensures udev detects
	 * the name change and fixes the symlinks.  This does not change
	 * ZVOL_RDONLY in zv->zv_flags so the actual read-only state never
	 * changes.  This would normally be done using kobject_uevent() but
	 * that is a GPL-only symbol which is why we need this workaround.
	 */
	set_disk_ro(zv->zv_disk, !readonly);
	set_disk_ro(zv->zv_disk, readonly);
}

typedef struct minors_job {
	list_t *list;
	list_node_t link;
	/* input */
	char *name;
	/* output */
	int error;
} minors_job_t;

/*
 * Prefetch zvol dnodes for the minors_job
 */
static void
zvol_prefetch_minors_impl(void *arg)
{
	minors_job_t *job = arg;
	char *dsname = job->name;
	objset_t *os = NULL;

	job->error = dmu_objset_own(dsname, DMU_OST_ZVOL, B_TRUE, FTAG,
	    &os);
	if (job->error == 0) {
		dmu_prefetch(os, ZVOL_OBJ, 0, 0, 0, ZIO_PRIORITY_SYNC_READ);
		dmu_objset_disown(os, FTAG);
	}
}

/*
 * Mask errors to continue dmu_objset_find() traversal
 */
static int
zvol_create_snap_minor_cb(const char *dsname, void *arg)
{
	minors_job_t *j = arg;
	list_t *minors_list = j->list;
	const char *name = j->name;

	ASSERT0(MUTEX_HELD(&spa_namespace_lock));

	/* skip the designated dataset */
	if (name && strcmp(dsname, name) == 0)
		return (0);

	/* at this point, the dsname should name a snapshot */
	if (strchr(dsname, '@') == 0) {
		dprintf("zvol_create_snap_minor_cb(): "
		    "%s is not a shapshot name\n", dsname);
	} else {
		minors_job_t *job;
		char *n = strdup(dsname);
		if (n == NULL)
			return (0);

		job = kmem_alloc(sizeof (minors_job_t), KM_SLEEP);
		job->name = n;
		job->list = minors_list;
		job->error = 0;
		list_insert_tail(minors_list, job);
		/* don't care if dispatch fails, because job->error is 0 */
		taskq_dispatch(system_taskq, zvol_prefetch_minors_impl, job,
		    TQ_SLEEP);
	}

	return (0);
}

/*
 * Mask errors to continue dmu_objset_find() traversal
 */
static int
zvol_create_minors_cb(const char *dsname, void *arg)
{
	uint64_t snapdev;
	int error;
	list_t *minors_list = arg;

	ASSERT0(MUTEX_HELD(&spa_namespace_lock));

	error = dsl_prop_get_integer(dsname, "snapdev", &snapdev, NULL);
	if (error)
		return (0);

	/*
	 * Given the name and the 'snapdev' property, create device minor nodes
	 * with the linkages to zvols/snapshots as needed.
	 * If the name represents a zvol, create a minor node for the zvol, then
	 * check if its snapshots are 'visible', and if so, iterate over the
	 * snapshots and create device minor nodes for those.
	 */
	if (strchr(dsname, '@') == 0) {
		minors_job_t *job;
		char *n = strdup(dsname);
		if (n == NULL)
			return (0);

		job = kmem_alloc(sizeof (minors_job_t), KM_SLEEP);
		job->name = n;
		job->list = minors_list;
		job->error = 0;
		list_insert_tail(minors_list, job);
		/* don't care if dispatch fails, because job->error is 0 */
		taskq_dispatch(system_taskq, zvol_prefetch_minors_impl, job,
		    TQ_SLEEP);

		if (snapdev == ZFS_SNAPDEV_VISIBLE) {
			/*
			 * traverse snapshots only, do not traverse children,
			 * and skip the 'dsname'
			 */
			error = dmu_objset_find((char *)dsname,
			    zvol_create_snap_minor_cb, (void *)job,
			    DS_FIND_SNAPSHOTS);
		}
	} else {
		dprintf("zvol_create_minors_cb(): %s is not a zvol name\n",
		    dsname);
	}

	return (0);
}

/*
 * Create minors for the specified dataset, including children and snapshots.
 * Pay attention to the 'snapdev' property and iterate over the snapshots
 * only if they are 'visible'. This approach allows one to assure that the
 * snapshot metadata is read from disk only if it is needed.
 *
 * The name can represent a dataset to be recursively scanned for zvols and
 * their snapshots, or a single zvol snapshot. If the name represents a
 * dataset, the scan is performed in two nested stages:
 * - scan the dataset for zvols, and
 * - for each zvol, create a minor node, then check if the zvol's snapshots
 *   are 'visible', and only then iterate over the snapshots if needed
 *
 * If the name represents a snapshot, a check is performed if the snapshot is
 * 'visible' (which also verifies that the parent is a zvol), and if so,
 * a minor node for that snapshot is created.
 */
static int
zvol_create_minors_impl(const char *name)
{
	int error = 0;
	fstrans_cookie_t cookie;
	char *atp, *parent;
	list_t minors_list;
	minors_job_t *job;

	if (zvol_inhibit_dev)
		return (0);

	/*
	 * This is the list for prefetch jobs. Whenever we found a match
	 * during dmu_objset_find, we insert a minors_job to the list and do
	 * taskq_dispatch to parallel prefetch zvol dnodes. Note we don't need
	 * any lock because all list operation is done on the current thread.
	 *
	 * We will use this list to do zvol_create_minor_impl after prefetch
	 * so we don't have to traverse using dmu_objset_find again.
	 */
	list_create(&minors_list, sizeof (minors_job_t),
	    offsetof(minors_job_t, link));

	parent = kmem_alloc(MAXPATHLEN, KM_SLEEP);
	(void) strlcpy(parent, name, MAXPATHLEN);

	if ((atp = strrchr(parent, '@')) != NULL) {
		uint64_t snapdev;

		*atp = '\0';
		error = dsl_prop_get_integer(parent, "snapdev",
		    &snapdev, NULL);

		if (error == 0 && snapdev == ZFS_SNAPDEV_VISIBLE)
			error = zvol_create_minor_impl(name);
	} else {
		cookie = spl_fstrans_mark();
		error = dmu_objset_find(parent, zvol_create_minors_cb,
		    &minors_list, DS_FIND_CHILDREN);
		spl_fstrans_unmark(cookie);
	}

	kmem_free(parent, MAXPATHLEN);
	taskq_wait_outstanding(system_taskq, 0);

	/*
	 * Prefetch is completed, we can do zvol_create_minor_impl
	 * sequentially.
	 */
	while ((job = list_head(&minors_list)) != NULL) {
		list_remove(&minors_list, job);
		if (!job->error)
			zvol_create_minor_impl(job->name);
		strfree(job->name);
		kmem_free(job, sizeof (minors_job_t));
	}

	list_destroy(&minors_list);

	return (SET_ERROR(error));
}

/*
 * Remove minors for specified dataset including children and snapshots.
 */
static void
zvol_remove_minors_impl(const char *name)
{
	zvol_state_t *zv, *zv_next;
	int namelen = ((name) ? strlen(name) : 0);
	taskqid_t t, tid = TASKQID_INVALID;
	list_t free_list;

	if (zvol_inhibit_dev)
		return;

	list_create(&free_list, sizeof (zvol_state_t),
	    offsetof(zvol_state_t, zv_next));

	rw_enter(&zvol_state_lock, RW_WRITER);

	for (zv = list_head(&zvol_state_list); zv != NULL; zv = zv_next) {
		zv_next = list_next(&zvol_state_list, zv);

		mutex_enter(&zv->zv_state_lock);
		if (name == NULL || strcmp(zv->zv_name, name) == 0 ||
		    (strncmp(zv->zv_name, name, namelen) == 0 &&
		    (zv->zv_name[namelen] == '/' ||
		    zv->zv_name[namelen] == '@'))) {
			/*
			 * By holding zv_state_lock here, we guarantee that no
			 * one is currently using this zv
			 */

			/* If in use, leave alone */
			if (zv->zv_open_count > 0 ||
			    atomic_read(&zv->zv_suspend_ref)) {
				mutex_exit(&zv->zv_state_lock);
				continue;
			}

			zvol_remove(zv);

			/*
			 * Cleared while holding zvol_state_lock as a writer
			 * which will prevent zvol_open() from opening it.
			 */
			zv->zv_disk->private_data = NULL;

			/* Drop zv_state_lock before zvol_free() */
			mutex_exit(&zv->zv_state_lock);

			/* Try parallel zv_free, if failed do it in place */
			t = taskq_dispatch(system_taskq, zvol_free, zv,
			    TQ_SLEEP);
			if (t == TASKQID_INVALID)
				list_insert_head(&free_list, zv);
			else
				tid = t;
		} else {
			mutex_exit(&zv->zv_state_lock);
		}
	}
	rw_exit(&zvol_state_lock);

	/* Drop zvol_state_lock before calling zvol_free() */
	while ((zv = list_head(&free_list)) != NULL) {
		list_remove(&free_list, zv);
		zvol_free(zv);
	}

	if (tid != TASKQID_INVALID)
		taskq_wait_outstanding(system_taskq, tid);
}

/* Remove minor for this specific volume only */
static void
zvol_remove_minor_impl(const char *name)
{
	zvol_state_t *zv = NULL, *zv_next;

	if (zvol_inhibit_dev)
		return;

	rw_enter(&zvol_state_lock, RW_WRITER);

	for (zv = list_head(&zvol_state_list); zv != NULL; zv = zv_next) {
		zv_next = list_next(&zvol_state_list, zv);

		mutex_enter(&zv->zv_state_lock);
		if (strcmp(zv->zv_name, name) == 0) {
			/*
			 * By holding zv_state_lock here, we guarantee that no
			 * one is currently using this zv
			 */

			/* If in use, leave alone */
			if (zv->zv_open_count > 0 ||
			    atomic_read(&zv->zv_suspend_ref)) {
				mutex_exit(&zv->zv_state_lock);
				continue;
			}
			zvol_remove(zv);

			/*
			 * Cleared while holding zvol_state_lock as a writer
			 * which will prevent zvol_open() from opening it.
			 */
			zv->zv_disk->private_data = NULL;

			mutex_exit(&zv->zv_state_lock);
			break;
		} else {
			mutex_exit(&zv->zv_state_lock);
		}
	}

	/* Drop zvol_state_lock before calling zvol_free() */
	rw_exit(&zvol_state_lock);

	if (zv != NULL)
		zvol_free(zv);
}

/*
 * Rename minors for specified dataset including children and snapshots.
 */
static void
zvol_rename_minors_impl(const char *oldname, const char *newname)
{
	zvol_state_t *zv, *zv_next;
	int oldnamelen, newnamelen;

	if (zvol_inhibit_dev)
		return;

	oldnamelen = strlen(oldname);
	newnamelen = strlen(newname);

	rw_enter(&zvol_state_lock, RW_READER);

	for (zv = list_head(&zvol_state_list); zv != NULL; zv = zv_next) {
		zv_next = list_next(&zvol_state_list, zv);

		mutex_enter(&zv->zv_state_lock);

		/* If in use, leave alone */
		if (zv->zv_open_count > 0) {
			mutex_exit(&zv->zv_state_lock);
			continue;
		}

		if (strcmp(zv->zv_name, oldname) == 0) {
			zvol_rename_minor(zv, newname);
		} else if (strncmp(zv->zv_name, oldname, oldnamelen) == 0 &&
		    (zv->zv_name[oldnamelen] == '/' ||
		    zv->zv_name[oldnamelen] == '@')) {
			char *name = kmem_asprintf("%s%c%s", newname,
			    zv->zv_name[oldnamelen],
			    zv->zv_name + oldnamelen + 1);
			zvol_rename_minor(zv, name);
			kmem_free(name, strlen(name + 1));
		}

		mutex_exit(&zv->zv_state_lock);
	}

	rw_exit(&zvol_state_lock);
}

typedef struct zvol_snapdev_cb_arg {
	uint64_t snapdev;
} zvol_snapdev_cb_arg_t;

static int
zvol_set_snapdev_cb(const char *dsname, void *param)
{
	zvol_snapdev_cb_arg_t *arg = param;

	if (strchr(dsname, '@') == NULL)
		return (0);

	switch (arg->snapdev) {
		case ZFS_SNAPDEV_VISIBLE:
			(void) zvol_create_minor_impl(dsname);
			break;
		case ZFS_SNAPDEV_HIDDEN:
			(void) zvol_remove_minor_impl(dsname);
			break;
	}

	return (0);
}

static void
zvol_set_snapdev_impl(char *name, uint64_t snapdev)
{
	zvol_snapdev_cb_arg_t arg = {snapdev};
	fstrans_cookie_t cookie = spl_fstrans_mark();
	/*
	 * The zvol_set_snapdev_sync() sets snapdev appropriately
	 * in the dataset hierarchy. Here, we only scan snapshots.
	 */
	dmu_objset_find(name, zvol_set_snapdev_cb, &arg, DS_FIND_SNAPSHOTS);
	spl_fstrans_unmark(cookie);
}

typedef struct zvol_volmode_cb_arg {
	uint64_t volmode;
} zvol_volmode_cb_arg_t;

static void
zvol_set_volmode_impl(char *name, uint64_t volmode)
{
	fstrans_cookie_t cookie = spl_fstrans_mark();

	if (strchr(name, '@') != NULL)
		return;

	/*
	 * It's unfortunate we need to remove minors before we create new ones:
	 * this is necessary because our backing gendisk (zvol_state->zv_disk)
	 * coule be different when we set, for instance, volmode from "geom"
	 * to "dev" (or vice versa).
	 * A possible optimization is to modify our consumers so we don't get
	 * called when "volmode" does not change.
	 */
	switch (volmode) {
		case ZFS_VOLMODE_NONE:
			(void) zvol_remove_minor_impl(name);
			break;
		case ZFS_VOLMODE_GEOM:
		case ZFS_VOLMODE_DEV:
			(void) zvol_remove_minor_impl(name);
			(void) zvol_create_minor_impl(name);
			break;
		case ZFS_VOLMODE_DEFAULT:
			(void) zvol_remove_minor_impl(name);
			if (zvol_volmode == ZFS_VOLMODE_NONE)
				break;
			else /* if zvol_volmode is invalid defaults to "geom" */
				(void) zvol_create_minor_impl(name);
			break;
	}

	spl_fstrans_unmark(cookie);
}

static zvol_task_t *
zvol_task_alloc(zvol_async_op_t op, const char *name1, const char *name2,
    uint64_t value)
{
	zvol_task_t *task;
	char *delim;

	/* Never allow tasks on hidden names. */
	if (name1[0] == '$')
		return (NULL);

	task = kmem_zalloc(sizeof (zvol_task_t), KM_SLEEP);
	task->op = op;
	task->value = value;
	delim = strchr(name1, '/');
	strlcpy(task->pool, name1, delim ? (delim - name1 + 1) : MAXNAMELEN);

	strlcpy(task->name1, name1, MAXNAMELEN);
	if (name2 != NULL)
		strlcpy(task->name2, name2, MAXNAMELEN);

	return (task);
}

static void
zvol_task_free(zvol_task_t *task)
{
	kmem_free(task, sizeof (zvol_task_t));
}

/*
 * The worker thread function performed asynchronously.
 */
static void
zvol_task_cb(void *param)
{
	zvol_task_t *task = (zvol_task_t *)param;

	switch (task->op) {
	case ZVOL_ASYNC_CREATE_MINORS:
		(void) zvol_create_minors_impl(task->name1);
		break;
	case ZVOL_ASYNC_REMOVE_MINORS:
		zvol_remove_minors_impl(task->name1);
		break;
	case ZVOL_ASYNC_RENAME_MINORS:
		zvol_rename_minors_impl(task->name1, task->name2);
		break;
	case ZVOL_ASYNC_SET_SNAPDEV:
		zvol_set_snapdev_impl(task->name1, task->value);
		break;
	case ZVOL_ASYNC_SET_VOLMODE:
		zvol_set_volmode_impl(task->name1, task->value);
		break;
	default:
		VERIFY(0);
		break;
	}

	zvol_task_free(task);
}

typedef struct zvol_set_prop_int_arg {
	const char *zsda_name;
	uint64_t zsda_value;
	zprop_source_t zsda_source;
	dmu_tx_t *zsda_tx;
} zvol_set_prop_int_arg_t;

/*
 * Sanity check the dataset for safe use by the sync task.  No additional
 * conditions are imposed.
 */
static int
zvol_set_snapdev_check(void *arg, dmu_tx_t *tx)
{
	zvol_set_prop_int_arg_t *zsda = arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	dsl_dir_t *dd;
	int error;

	error = dsl_dir_hold(dp, zsda->zsda_name, FTAG, &dd, NULL);
	if (error != 0)
		return (error);

	dsl_dir_rele(dd, FTAG);

	return (error);
}

/* ARGSUSED */
static int
zvol_set_snapdev_sync_cb(dsl_pool_t *dp, dsl_dataset_t *ds, void *arg)
{
	char dsname[MAXNAMELEN];
	zvol_task_t *task;
	uint64_t snapdev;

	dsl_dataset_name(ds, dsname);
	if (dsl_prop_get_int_ds(ds, "snapdev", &snapdev) != 0)
		return (0);
	task = zvol_task_alloc(ZVOL_ASYNC_SET_SNAPDEV, dsname, NULL, snapdev);
	if (task == NULL)
		return (0);

	(void) taskq_dispatch(dp->dp_spa->spa_zvol_taskq, zvol_task_cb,
	    task, TQ_SLEEP);
	return (0);
}

/*
 * Traverse all child datasets and apply snapdev appropriately.
 * We call dsl_prop_set_sync_impl() here to set the value only on the toplevel
 * dataset and read the effective "snapdev" on every child in the callback
 * function: this is because the value is not guaranteed to be the same in the
 * whole dataset hierarchy.
 */
static void
zvol_set_snapdev_sync(void *arg, dmu_tx_t *tx)
{
	zvol_set_prop_int_arg_t *zsda = arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	dsl_dir_t *dd;
	dsl_dataset_t *ds;
	int error;

	VERIFY0(dsl_dir_hold(dp, zsda->zsda_name, FTAG, &dd, NULL));
	zsda->zsda_tx = tx;

	error = dsl_dataset_hold(dp, zsda->zsda_name, FTAG, &ds);
	if (error == 0) {
		dsl_prop_set_sync_impl(ds, zfs_prop_to_name(ZFS_PROP_SNAPDEV),
		    zsda->zsda_source, sizeof (zsda->zsda_value), 1,
		    &zsda->zsda_value, zsda->zsda_tx);
		dsl_dataset_rele(ds, FTAG);
	}
	dmu_objset_find_dp(dp, dd->dd_object, zvol_set_snapdev_sync_cb,
	    zsda, DS_FIND_CHILDREN);

	dsl_dir_rele(dd, FTAG);
}

int
zvol_set_snapdev(const char *ddname, zprop_source_t source, uint64_t snapdev)
{
	zvol_set_prop_int_arg_t zsda;

	zsda.zsda_name = ddname;
	zsda.zsda_source = source;
	zsda.zsda_value = snapdev;

	return (dsl_sync_task(ddname, zvol_set_snapdev_check,
	    zvol_set_snapdev_sync, &zsda, 0, ZFS_SPACE_CHECK_NONE));
}

/*
 * Sanity check the dataset for safe use by the sync task.  No additional
 * conditions are imposed.
 */
static int
zvol_set_volmode_check(void *arg, dmu_tx_t *tx)
{
	zvol_set_prop_int_arg_t *zsda = arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	dsl_dir_t *dd;
	int error;

	error = dsl_dir_hold(dp, zsda->zsda_name, FTAG, &dd, NULL);
	if (error != 0)
		return (error);

	dsl_dir_rele(dd, FTAG);

	return (error);
}

/* ARGSUSED */
static int
zvol_set_volmode_sync_cb(dsl_pool_t *dp, dsl_dataset_t *ds, void *arg)
{
	char dsname[MAXNAMELEN];
	zvol_task_t *task;
	uint64_t volmode;

	dsl_dataset_name(ds, dsname);
	if (dsl_prop_get_int_ds(ds, "volmode", &volmode) != 0)
		return (0);
	task = zvol_task_alloc(ZVOL_ASYNC_SET_VOLMODE, dsname, NULL, volmode);
	if (task == NULL)
		return (0);

	(void) taskq_dispatch(dp->dp_spa->spa_zvol_taskq, zvol_task_cb,
	    task, TQ_SLEEP);
	return (0);
}

/*
 * Traverse all child datasets and apply volmode appropriately.
 * We call dsl_prop_set_sync_impl() here to set the value only on the toplevel
 * dataset and read the effective "volmode" on every child in the callback
 * function: this is because the value is not guaranteed to be the same in the
 * whole dataset hierarchy.
 */
static void
zvol_set_volmode_sync(void *arg, dmu_tx_t *tx)
{
	zvol_set_prop_int_arg_t *zsda = arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	dsl_dir_t *dd;
	dsl_dataset_t *ds;
	int error;

	VERIFY0(dsl_dir_hold(dp, zsda->zsda_name, FTAG, &dd, NULL));
	zsda->zsda_tx = tx;

	error = dsl_dataset_hold(dp, zsda->zsda_name, FTAG, &ds);
	if (error == 0) {
		dsl_prop_set_sync_impl(ds, zfs_prop_to_name(ZFS_PROP_VOLMODE),
		    zsda->zsda_source, sizeof (zsda->zsda_value), 1,
		    &zsda->zsda_value, zsda->zsda_tx);
		dsl_dataset_rele(ds, FTAG);
	}

	dmu_objset_find_dp(dp, dd->dd_object, zvol_set_volmode_sync_cb,
	    zsda, DS_FIND_CHILDREN);

	dsl_dir_rele(dd, FTAG);
}

int
zvol_set_volmode(const char *ddname, zprop_source_t source, uint64_t volmode)
{
	zvol_set_prop_int_arg_t zsda;

	zsda.zsda_name = ddname;
	zsda.zsda_source = source;
	zsda.zsda_value = volmode;

	return (dsl_sync_task(ddname, zvol_set_volmode_check,
	    zvol_set_volmode_sync, &zsda, 0, ZFS_SPACE_CHECK_NONE));
}

void
zvol_create_minors(spa_t *spa, const char *name, boolean_t async)
{
	zvol_task_t *task;
	taskqid_t id;

	task = zvol_task_alloc(ZVOL_ASYNC_CREATE_MINORS, name, NULL, ~0ULL);
	if (task == NULL)
		return;

	id = taskq_dispatch(spa->spa_zvol_taskq, zvol_task_cb, task, TQ_SLEEP);
	if ((async == B_FALSE) && (id != TASKQID_INVALID))
		taskq_wait_id(spa->spa_zvol_taskq, id);
}

void
zvol_remove_minors(spa_t *spa, const char *name, boolean_t async)
{
	zvol_task_t *task;
	taskqid_t id;

	task = zvol_task_alloc(ZVOL_ASYNC_REMOVE_MINORS, name, NULL, ~0ULL);
	if (task == NULL)
		return;

	id = taskq_dispatch(spa->spa_zvol_taskq, zvol_task_cb, task, TQ_SLEEP);
	if ((async == B_FALSE) && (id != TASKQID_INVALID))
		taskq_wait_id(spa->spa_zvol_taskq, id);
}

void
zvol_rename_minors(spa_t *spa, const char *name1, const char *name2,
    boolean_t async)
{
	zvol_task_t *task;
	taskqid_t id;

	task = zvol_task_alloc(ZVOL_ASYNC_RENAME_MINORS, name1, name2, ~0ULL);
	if (task == NULL)
		return;

	id = taskq_dispatch(spa->spa_zvol_taskq, zvol_task_cb, task, TQ_SLEEP);
	if ((async == B_FALSE) && (id != TASKQID_INVALID))
		taskq_wait_id(spa->spa_zvol_taskq, id);
}

int
zvol_init(void)
{
	int threads = MIN(MAX(zvol_threads, 1), 1024);
	int i, error;

	list_create(&zvol_state_list, sizeof (zvol_state_t),
	    offsetof(zvol_state_t, zv_next));
	rw_init(&zvol_state_lock, NULL, RW_DEFAULT, NULL);
	ida_init(&zvol_ida);

	zvol_taskq = taskq_create(ZVOL_DRIVER, threads, maxclsyspri,
	    threads * 2, INT_MAX, TASKQ_PREPOPULATE | TASKQ_DYNAMIC);
	if (zvol_taskq == NULL) {
		printk(KERN_INFO "ZFS: taskq_create() failed\n");
		error = -ENOMEM;
		goto out;
	}

	zvol_htable = kmem_alloc(ZVOL_HT_SIZE * sizeof (struct hlist_head),
	    KM_SLEEP);
	if (!zvol_htable) {
		error = -ENOMEM;
		goto out_taskq;
	}
	for (i = 0; i < ZVOL_HT_SIZE; i++)
		INIT_HLIST_HEAD(&zvol_htable[i]);

	error = register_blkdev(zvol_major, ZVOL_DRIVER);
	if (error) {
		printk(KERN_INFO "ZFS: register_blkdev() failed %d\n", error);
		goto out_free;
	}

	blk_register_region(MKDEV(zvol_major, 0), 1UL << MINORBITS,
	    THIS_MODULE, zvol_probe, NULL, NULL);

	return (0);

out_free:
	kmem_free(zvol_htable, ZVOL_HT_SIZE * sizeof (struct hlist_head));
out_taskq:
	taskq_destroy(zvol_taskq);
out:
	ida_destroy(&zvol_ida);
	rw_destroy(&zvol_state_lock);
	list_destroy(&zvol_state_list);

	return (SET_ERROR(error));
}

void
zvol_fini(void)
{
	zvol_remove_minors_impl(NULL);

	blk_unregister_region(MKDEV(zvol_major, 0), 1UL << MINORBITS);
	unregister_blkdev(zvol_major, ZVOL_DRIVER);
	kmem_free(zvol_htable, ZVOL_HT_SIZE * sizeof (struct hlist_head));

	taskq_destroy(zvol_taskq);
	list_destroy(&zvol_state_list);
	rw_destroy(&zvol_state_lock);

	ida_destroy(&zvol_ida);
}

/* BEGIN CSTYLED */
module_param(zvol_inhibit_dev, uint, 0644);
MODULE_PARM_DESC(zvol_inhibit_dev, "Do not create zvol device nodes");

module_param(zvol_major, uint, 0444);
MODULE_PARM_DESC(zvol_major, "Major number for zvol device");

module_param(zvol_threads, uint, 0444);
MODULE_PARM_DESC(zvol_threads, "Max number of threads to handle I/O requests");

module_param(zvol_request_sync, uint, 0644);
MODULE_PARM_DESC(zvol_request_sync, "Synchronously handle bio requests");

module_param(zvol_max_discard_blocks, ulong, 0444);
MODULE_PARM_DESC(zvol_max_discard_blocks, "Max number of blocks to discard");

module_param(zvol_prefetch_bytes, uint, 0644);
MODULE_PARM_DESC(zvol_prefetch_bytes, "Prefetch N bytes at zvol start+end");

module_param(zvol_volmode, uint, 0644);
MODULE_PARM_DESC(zvol_volmode, "Default volmode property value");
/* END CSTYLED */
