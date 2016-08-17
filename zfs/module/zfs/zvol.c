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
 * Copyright (c) 2016 Actifio, Inc. All rights reserved.
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
unsigned int zvol_prefetch_bytes = (128 * 1024);
unsigned long zvol_max_discard_blocks = 16384;

static kmutex_t zvol_state_lock;
static list_t zvol_state_list;
static char *zvol_tag = "zvol_tag";

/*
 * The in-core state of each volume.
 */
typedef struct zvol_state {
	char			zv_name[MAXNAMELEN];	/* name */
	uint64_t		zv_volsize;		/* advertised space */
	uint64_t		zv_volblocksize;	/* volume block size */
	objset_t		*zv_objset;	/* objset handle */
	uint32_t		zv_flags;	/* ZVOL_* flags */
	uint32_t		zv_open_count;	/* open counts */
	uint32_t		zv_changed;	/* disk changed */
	zilog_t			*zv_zilog;	/* ZIL handle */
	zfs_rlock_t		zv_range_lock;	/* range lock */
	dmu_buf_t		*zv_dbuf;	/* bonus handle */
	dev_t			zv_dev;		/* device id */
	struct gendisk		*zv_disk;	/* generic disk */
	struct request_queue	*zv_queue;	/* request queue */
	spinlock_t		zv_lock;	/* request queue lock */
	list_node_t		zv_next;	/* next zvol_state_t linkage */
} zvol_state_t;

typedef enum {
	ZVOL_ASYNC_CREATE_MINORS,
	ZVOL_ASYNC_REMOVE_MINORS,
	ZVOL_ASYNC_RENAME_MINORS,
	ZVOL_ASYNC_SET_SNAPDEV,
	ZVOL_ASYNC_MAX
} zvol_async_op_t;

typedef struct {
	zvol_async_op_t op;
	char pool[MAXNAMELEN];
	char name1[MAXNAMELEN];
	char name2[MAXNAMELEN];
	zprop_source_t source;
	uint64_t snapdev;
} zvol_task_t;

#define	ZVOL_RDONLY	0x1

/*
 * Find the next available range of ZVOL_MINORS minor numbers.  The
 * zvol_state_list is kept in ascending minor order so we simply need
 * to scan the list for the first gap in the sequence.  This allows us
 * to recycle minor number as devices are created and removed.
 */
static int
zvol_find_minor(unsigned *minor)
{
	zvol_state_t *zv;

	*minor = 0;
	ASSERT(MUTEX_HELD(&zvol_state_lock));
	for (zv = list_head(&zvol_state_list); zv != NULL;
	    zv = list_next(&zvol_state_list, zv), *minor += ZVOL_MINORS) {
		if (MINOR(zv->zv_dev) != MINOR(*minor))
			break;
	}

	/* All minors are in use */
	if (*minor >= (1 << MINORBITS))
		return (SET_ERROR(ENXIO));

	return (0);
}

/*
 * Find a zvol_state_t given the full major+minor dev_t.
 */
static zvol_state_t *
zvol_find_by_dev(dev_t dev)
{
	zvol_state_t *zv;

	ASSERT(MUTEX_HELD(&zvol_state_lock));
	for (zv = list_head(&zvol_state_list); zv != NULL;
	    zv = list_next(&zvol_state_list, zv)) {
		if (zv->zv_dev == dev)
			return (zv);
	}

	return (NULL);
}

/*
 * Find a zvol_state_t given the name provided at zvol_alloc() time.
 */
static zvol_state_t *
zvol_find_by_name(const char *name)
{
	zvol_state_t *zv;

	ASSERT(MUTEX_HELD(&zvol_state_lock));
	for (zv = list_head(&zvol_state_list); zv != NULL;
	    zv = list_next(&zvol_state_list, zv)) {
		if (strncmp(zv->zv_name, name, MAXNAMELEN) == 0)
			return (zv);
	}

	return (NULL);
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

static void
zvol_size_changed(zvol_state_t *zv, uint64_t volsize)
{
	struct block_device *bdev;

	bdev = bdget_disk(zv->zv_disk, 0);
	if (bdev == NULL)
		return;
/*
 * 2.6.28 API change
 * Added check_disk_size_change() helper function.
 */
#ifdef HAVE_CHECK_DISK_SIZE_CHANGE
	set_capacity(zv->zv_disk, volsize >> 9);
	zv->zv_volsize = volsize;
	check_disk_size_change(zv->zv_disk, bdev);
#else
	zv->zv_volsize = volsize;
	zv->zv_changed = 1;
	(void) check_disk_change(bdev);
#endif /* HAVE_CHECK_DISK_SIZE_CHANGE */

	bdput(bdev);
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
	if (volsize - 1 > MAXOFFSET_T)
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

	ASSERT(MUTEX_HELD(&zvol_state_lock));

	tx = dmu_tx_create(os);
	dmu_tx_hold_zap(tx, ZVOL_ZAP_OBJ, TRUE, NULL);
	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		return (SET_ERROR(error));
	}

	error = zap_update(os, ZVOL_ZAP_OBJ, "size", 8, 1,
	    &volsize, tx);
	dmu_tx_commit(tx);

	if (error == 0)
		error = dmu_free_long_range(os,
		    ZVOL_OBJ, volsize, DMU_OBJECT_END);

	return (error);
}

static int
zvol_update_live_volsize(zvol_state_t *zv, uint64_t volsize)
{
	zvol_size_changed(zv, volsize);

	/*
	 * We should post a event here describing the expansion.  However,
	 * the zfs_ereport_post() interface doesn't nicely support posting
	 * events for zvols, it assumes events relate to vdevs or zios.
	 */

	return (0);
}

/*
 * Set ZFS_PROP_VOLSIZE set entry point.
 */
int
zvol_set_volsize(const char *name, uint64_t volsize)
{
	zvol_state_t *zv = NULL;
	objset_t *os = NULL;
	int error;
	dmu_object_info_t *doi;
	uint64_t readonly;
	boolean_t owned = B_FALSE;

	error = dsl_prop_get_integer(name,
	    zfs_prop_to_name(ZFS_PROP_READONLY), &readonly, NULL);
	if (error != 0)
		return (SET_ERROR(error));
	if (readonly)
		return (SET_ERROR(EROFS));

	mutex_enter(&zvol_state_lock);
	zv = zvol_find_by_name(name);

	if (zv == NULL || zv->zv_objset == NULL) {
		if ((error = dmu_objset_own(name, DMU_OST_ZVOL, B_FALSE,
		    FTAG, &os)) != 0) {
			mutex_exit(&zvol_state_lock);
			return (SET_ERROR(error));
		}
		owned = B_TRUE;
		if (zv != NULL)
			zv->zv_objset = os;
	} else {
		os = zv->zv_objset;
	}

	doi = kmem_alloc(sizeof (dmu_object_info_t), KM_SLEEP);

	if ((error = dmu_object_info(os, ZVOL_OBJ, doi)) ||
	    (error = zvol_check_volsize(volsize, doi->doi_data_block_size)))
		goto out;

	error = zvol_update_volsize(volsize, os);
	kmem_free(doi, sizeof (dmu_object_info_t));

	if (error == 0 && zv != NULL)
		error = zvol_update_live_volsize(zv, volsize);
out:
	if (owned) {
		dmu_objset_disown(os, FTAG);
		if (zv != NULL)
			zv->zv_objset = NULL;
	}
	mutex_exit(&zvol_state_lock);
	return (error);
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

	mutex_enter(&zvol_state_lock);

	zv = zvol_find_by_name(name);
	if (zv == NULL) {
		error = SET_ERROR(ENXIO);
		goto out;
	}

	if (zv->zv_flags & ZVOL_RDONLY) {
		error = SET_ERROR(EROFS);
		goto out;
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
out:
	mutex_exit(&zvol_state_lock);

	return (SET_ERROR(error));
}

/*
 * Replay a TX_WRITE ZIL transaction that didn't get committed
 * after a system failure
 */
static int
zvol_replay_write(zvol_state_t *zv, lr_write_t *lr, boolean_t byteswap)
{
	objset_t *os = zv->zv_objset;
	char *data = (char *)(lr + 1);	/* data follows lr_write_t */
	uint64_t off = lr->lr_offset;
	uint64_t len = lr->lr_length;
	dmu_tx_t *tx;
	int error;

	if (byteswap)
		byteswap_uint64_array(lr, sizeof (*lr));

	tx = dmu_tx_create(os);
	dmu_tx_hold_write(tx, ZVOL_OBJ, off, len);
	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error) {
		dmu_tx_abort(tx);
	} else {
		dmu_write(os, ZVOL_OBJ, off, len, data, tx);
		dmu_tx_commit(tx);
	}

	return (SET_ERROR(error));
}

static int
zvol_replay_err(zvol_state_t *zv, lr_t *lr, boolean_t byteswap)
{
	return (SET_ERROR(ENOTSUP));
}

/*
 * Callback vectors for replaying records.
 * Only TX_WRITE is needed for zvol.
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
	(zil_replay_func_t)zvol_replay_err,	/* TX_TRUNCATE */
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
	boolean_t slogging;
	ssize_t immediate_write_sz;

	if (zil_replaying(zilog, tx))
		return;

	immediate_write_sz = (zilog->zl_logbias == ZFS_LOGBIAS_THROUGHPUT)
		? 0 : zvol_immediate_write_sz;
	slogging = spa_has_slogs(zilog->zl_spa) &&
		(zilog->zl_logbias == ZFS_LOGBIAS_LATENCY);

	while (size) {
		itx_t *itx;
		lr_write_t *lr;
		ssize_t len;
		itx_wr_state_t write_state;

		/*
		 * Unlike zfs_log_write() we can be called with
		 * up to DMU_MAX_ACCESS/2 (5MB) writes.
		 */
		if (blocksize > immediate_write_sz && !slogging &&
		    size >= blocksize && offset % blocksize == 0) {
			write_state = WR_INDIRECT; /* uses dmu_sync */
			len = blocksize;
		} else if (sync) {
			write_state = WR_COPIED;
			len = MIN(ZIL_MAX_LOG_DATA, size);
		} else {
			write_state = WR_NEED_COPY;
			len = MIN(ZIL_MAX_LOG_DATA, size);
		}

		itx = zil_itx_create(TX_WRITE, sizeof (*lr) +
		    (write_state == WR_COPIED ? len : 0));
		lr = (lr_write_t *)&itx->itx_lr;
		if (write_state == WR_COPIED && dmu_read(zv->zv_objset,
		    ZVOL_OBJ, offset, len, lr+1, DMU_READ_NO_PREFETCH) != 0) {
			zil_itx_destroy(itx);
			itx = zil_itx_create(TX_WRITE, sizeof (*lr));
			lr = (lr_write_t *)&itx->itx_lr;
			write_state = WR_NEED_COPY;
		}

		itx->itx_wr_state = write_state;
		if (write_state == WR_NEED_COPY)
			itx->itx_sod += len;
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

static int
zvol_write(struct bio *bio)
{
	zvol_state_t *zv = bio->bi_bdev->bd_disk->private_data;
	uint64_t offset = BIO_BI_SECTOR(bio) << 9;
	uint64_t size = BIO_BI_SIZE(bio);
	int error = 0;
	dmu_tx_t *tx;
	rl_t *rl;

	ASSERT(zv && zv->zv_open_count > 0);

	if (bio_is_flush(bio))
		zil_commit(zv->zv_zilog, ZVOL_OBJ);

	/*
	 * Some requests are just for flush and nothing else.
	 */
	if (size == 0)
		goto out;

	rl = zfs_range_lock(&zv->zv_range_lock, offset, size, RL_WRITER);

	tx = dmu_tx_create(zv->zv_objset);
	dmu_tx_hold_write(tx, ZVOL_OBJ, offset, size);

	/* This will only fail for ENOSPC */
	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		zfs_range_unlock(rl);
		goto out;
	}

	error = dmu_write_bio(zv->zv_objset, ZVOL_OBJ, bio, tx);
	if (error == 0)
		zvol_log_write(zv, tx, offset, size,
		    !!(bio_is_fua(bio)));

	dmu_tx_commit(tx);
	zfs_range_unlock(rl);

	if ((bio_is_fua(bio)) ||
	    zv->zv_objset->os_sync == ZFS_SYNC_ALWAYS)
		zil_commit(zv->zv_zilog, ZVOL_OBJ);

out:
	return (error);
}

static int
zvol_discard(struct bio *bio)
{
	zvol_state_t *zv = bio->bi_bdev->bd_disk->private_data;
	uint64_t start = BIO_BI_SECTOR(bio) << 9;
	uint64_t size = BIO_BI_SIZE(bio);
	uint64_t end = start + size;
	int error;
	rl_t *rl;

	ASSERT(zv && zv->zv_open_count > 0);

	if (end > zv->zv_volsize)
		return (SET_ERROR(EIO));

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
		return (0);

	rl = zfs_range_lock(&zv->zv_range_lock, start, size, RL_WRITER);

	error = dmu_free_long_range(zv->zv_objset, ZVOL_OBJ, start, size);

	/*
	 * TODO: maybe we should add the operation to the log.
	 */
	zfs_range_unlock(rl);

	return (error);
}

static int
zvol_read(struct bio *bio)
{
	zvol_state_t *zv = bio->bi_bdev->bd_disk->private_data;
	uint64_t offset = BIO_BI_SECTOR(bio) << 9;
	uint64_t len = BIO_BI_SIZE(bio);
	int error;
	rl_t *rl;

	ASSERT(zv && zv->zv_open_count > 0);

	if (len == 0)
		return (0);

	rl = zfs_range_lock(&zv->zv_range_lock, offset, len, RL_READER);

	error = dmu_read_bio(zv->zv_objset, ZVOL_OBJ, bio);

	zfs_range_unlock(rl);

	/* convert checksum errors into IO errors */
	if (error == ECKSUM)
		error = SET_ERROR(EIO);

	return (error);
}

static MAKE_REQUEST_FN_RET
zvol_request(struct request_queue *q, struct bio *bio)
{
	zvol_state_t *zv = q->queuedata;
	fstrans_cookie_t cookie = spl_fstrans_mark();
	uint64_t offset = BIO_BI_SECTOR(bio);
	unsigned int sectors = bio_sectors(bio);
	int rw = bio_data_dir(bio);
#ifdef HAVE_GENERIC_IO_ACCT
	unsigned long start = jiffies;
#endif
	int error = 0;

	if (bio_has_data(bio) && offset + sectors >
	    get_capacity(zv->zv_disk)) {
		printk(KERN_INFO
		    "%s: bad access: block=%llu, count=%lu\n",
		    zv->zv_disk->disk_name,
		    (long long unsigned)offset,
		    (long unsigned)sectors);
		error = SET_ERROR(EIO);
		goto out1;
	}

	generic_start_io_acct(rw, sectors, &zv->zv_disk->part0);

	if (rw == WRITE) {
		if (unlikely(zv->zv_flags & ZVOL_RDONLY)) {
			error = SET_ERROR(EROFS);
			goto out2;
		}

		if (bio_is_discard(bio) || bio_is_secure_erase(bio)) {
			error = zvol_discard(bio);
			goto out2;
		}

		error = zvol_write(bio);
	} else
		error = zvol_read(bio);

out2:
	generic_end_io_acct(rw, &zv->zv_disk->part0, start);
out1:
	BIO_END_IO(bio, -error);
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
	objset_t *os = zv->zv_objset;
	uint64_t object = ZVOL_OBJ;
	uint64_t offset = lr->lr_offset;
	uint64_t size = lr->lr_length;
	blkptr_t *bp = &lr->lr_blkptr;
	dmu_buf_t *db;
	zgd_t *zgd;
	int error;

	ASSERT(zio != NULL);
	ASSERT(size != 0);

	zgd = (zgd_t *)kmem_zalloc(sizeof (zgd_t), KM_SLEEP);
	zgd->zgd_zilog = zv->zv_zilog;
	zgd->zgd_rl = zfs_range_lock(&zv->zv_range_lock, offset, size,
	    RL_READER);

	/*
	 * Write records come in two flavors: immediate and indirect.
	 * For small writes it's cheaper to store the data with the
	 * log record (immediate); for large writes it's cheaper to
	 * sync the data and get a pointer to it (indirect) so that
	 * we don't have to write the data twice.
	 */
	if (buf != NULL) { /* immediate write */
		error = dmu_read(os, object, offset, size, buf,
		    DMU_READ_NO_PREFETCH);
	} else {
		size = zv->zv_volblocksize;
		offset = P2ALIGN_TYPED(offset, size, uint64_t);
		error = dmu_buf_hold(os, object, offset, zgd, &db,
		    DMU_READ_NO_PREFETCH);
		if (error == 0) {
			blkptr_t *obp = dmu_buf_get_blkptr(db);
			if (obp) {
				ASSERT(BP_IS_HOLE(bp));
				*bp = *obp;
			}

			zgd->zgd_db = db;
			zgd->zgd_bp = &lr->lr_blkptr;

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
 * The zvol_state_t's are inserted in increasing MINOR(dev_t) order.
 */
static void
zvol_insert(zvol_state_t *zv_insert)
{
	zvol_state_t *zv = NULL;

	ASSERT(MUTEX_HELD(&zvol_state_lock));
	ASSERT3U(MINOR(zv_insert->zv_dev) & ZVOL_MINOR_MASK, ==, 0);
	for (zv = list_head(&zvol_state_list); zv != NULL;
	    zv = list_next(&zvol_state_list, zv)) {
		if (MINOR(zv->zv_dev) > MINOR(zv_insert->zv_dev))
			break;
	}

	list_insert_before(&zvol_state_list, zv, zv_insert);
}

/*
 * Simply remove the zvol from to list of zvols.
 */
static void
zvol_remove(zvol_state_t *zv_remove)
{
	ASSERT(MUTEX_HELD(&zvol_state_lock));
	list_remove(&zvol_state_list, zv_remove);
}

static int
zvol_first_open(zvol_state_t *zv)
{
	objset_t *os;
	uint64_t volsize;
	int error;
	uint64_t ro;

	/* lie and say we're read-only */
	error = dmu_objset_own(zv->zv_name, DMU_OST_ZVOL, 1, zvol_tag, &os);
	if (error)
		return (SET_ERROR(-error));

	zv->zv_objset = os;

	error = dsl_prop_get_integer(zv->zv_name, "readonly", &ro, NULL);
	if (error)
		goto out_owned;

	error = zap_lookup(os, ZVOL_ZAP_OBJ, "size", 8, 1, &volsize);
	if (error)
		goto out_owned;

	error = dmu_bonus_hold(os, ZVOL_OBJ, zvol_tag, &zv->zv_dbuf);
	if (error)
		goto out_owned;

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

out_owned:
	if (error) {
		dmu_objset_disown(os, zvol_tag);
		zv->zv_objset = NULL;
	}

	return (SET_ERROR(-error));
}

static void
zvol_last_close(zvol_state_t *zv)
{
	zil_close(zv->zv_zilog);
	zv->zv_zilog = NULL;

	dmu_buf_rele(zv->zv_dbuf, zvol_tag);
	zv->zv_dbuf = NULL;

	/*
	 * Evict cached data
	 */
	if (dsl_dataset_is_dirty(dmu_objset_ds(zv->zv_objset)) &&
	    !(zv->zv_flags & ZVOL_RDONLY))
		txg_wait_synced(dmu_objset_pool(zv->zv_objset), 0);
	(void) dmu_objset_evict_dbufs(zv->zv_objset);

	dmu_objset_disown(zv->zv_objset, zvol_tag);
	zv->zv_objset = NULL;
}

static int
zvol_open(struct block_device *bdev, fmode_t flag)
{
	zvol_state_t *zv;
	int error = 0, drop_mutex = 0;

	/*
	 * If the caller is already holding the mutex do not take it
	 * again, this will happen as part of zvol_create_minor_impl().
	 * Once add_disk() is called the device is live and the kernel
	 * will attempt to open it to read the partition information.
	 */
	if (!mutex_owned(&zvol_state_lock)) {
		mutex_enter(&zvol_state_lock);
		drop_mutex = 1;
	}

	/*
	 * Obtain a copy of private_data under the lock to make sure
	 * that either the result of zvol_freeg() setting
	 * bdev->bd_disk->private_data to NULL is observed, or zvol_free()
	 * is not called on this zv because of the positive zv_open_count.
	 */
	zv = bdev->bd_disk->private_data;
	if (zv == NULL) {
		error = -ENXIO;
		goto out_mutex;
	}

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

	check_disk_change(bdev);

out_open_count:
	if (zv->zv_open_count == 0)
		zvol_last_close(zv);

out_mutex:
	if (drop_mutex)
		mutex_exit(&zvol_state_lock);

	return (SET_ERROR(error));
}

#ifdef HAVE_BLOCK_DEVICE_OPERATIONS_RELEASE_VOID
static void
#else
static int
#endif
zvol_release(struct gendisk *disk, fmode_t mode)
{
	zvol_state_t *zv = disk->private_data;
	int drop_mutex = 0;

	ASSERT(zv && zv->zv_open_count > 0);

	if (!mutex_owned(&zvol_state_lock)) {
		mutex_enter(&zvol_state_lock);
		drop_mutex = 1;
	}

	zv->zv_open_count--;
	if (zv->zv_open_count == 0)
		zvol_last_close(zv);

	if (drop_mutex)
		mutex_exit(&zvol_state_lock);

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

	ASSERT(zv && zv->zv_open_count > 0);

	switch (cmd) {
	case BLKFLSBUF:
		zil_commit(zv->zv_zilog, ZVOL_OBJ);
		break;
	case BLKZNAME:
		error = copy_to_user((void *)arg, zv->zv_name, MAXNAMELEN);
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

static int zvol_media_changed(struct gendisk *disk)
{
	zvol_state_t *zv = disk->private_data;

	ASSERT(zv && zv->zv_open_count > 0);

	return (zv->zv_changed);
}

static int zvol_revalidate_disk(struct gendisk *disk)
{
	zvol_state_t *zv = disk->private_data;

	ASSERT(zv && zv->zv_open_count > 0);

	zv->zv_changed = 0;
	set_capacity(zv->zv_disk, zv->zv_volsize >> 9);

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

	ASSERT(zv && zv->zv_open_count > 0);

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

	mutex_enter(&zvol_state_lock);
	zv = zvol_find_by_dev(dev);
	kobj = zv ? get_disk(zv->zv_disk) : NULL;
	mutex_exit(&zvol_state_lock);

	return (kobj);
}

#ifdef HAVE_BDEV_BLOCK_DEVICE_OPERATIONS
static struct block_device_operations zvol_ops = {
	.open			= zvol_open,
	.release		= zvol_release,
	.ioctl			= zvol_ioctl,
	.compat_ioctl		= zvol_compat_ioctl,
	.media_changed		= zvol_media_changed,
	.revalidate_disk	= zvol_revalidate_disk,
	.getgeo			= zvol_getgeo,
	.owner			= THIS_MODULE,
};

#else /* HAVE_BDEV_BLOCK_DEVICE_OPERATIONS */

static int
zvol_open_by_inode(struct inode *inode, struct file *file)
{
	return (zvol_open(inode->i_bdev, file->f_mode));
}

static int
zvol_release_by_inode(struct inode *inode, struct file *file)
{
	return (zvol_release(inode->i_bdev->bd_disk, file->f_mode));
}

static int
zvol_ioctl_by_inode(struct inode *inode, struct file *file,
    unsigned int cmd, unsigned long arg)
{
	if (file == NULL || inode == NULL)
		return (SET_ERROR(-EINVAL));

	return (zvol_ioctl(inode->i_bdev, file->f_mode, cmd, arg));
}

#ifdef CONFIG_COMPAT
static long
zvol_compat_ioctl_by_inode(struct file *file,
    unsigned int cmd, unsigned long arg)
{
	if (file == NULL)
		return (SET_ERROR(-EINVAL));

	return (zvol_compat_ioctl(file->f_dentry->d_inode->i_bdev,
	    file->f_mode, cmd, arg));
}
#else
#define	zvol_compat_ioctl_by_inode	NULL
#endif

static struct block_device_operations zvol_ops = {
	.open			= zvol_open_by_inode,
	.release		= zvol_release_by_inode,
	.ioctl			= zvol_ioctl_by_inode,
	.compat_ioctl		= zvol_compat_ioctl_by_inode,
	.media_changed		= zvol_media_changed,
	.revalidate_disk	= zvol_revalidate_disk,
	.getgeo			= zvol_getgeo,
	.owner			= THIS_MODULE,
};
#endif /* HAVE_BDEV_BLOCK_DEVICE_OPERATIONS */

/*
 * Allocate memory for a new zvol_state_t and setup the required
 * request queue and generic disk structures for the block device.
 */
static zvol_state_t *
zvol_alloc(dev_t dev, const char *name)
{
	zvol_state_t *zv;

	zv = kmem_zalloc(sizeof (zvol_state_t), KM_SLEEP);

	spin_lock_init(&zv->zv_lock);
	list_link_init(&zv->zv_next);

	zv->zv_queue = blk_alloc_queue(GFP_ATOMIC);
	if (zv->zv_queue == NULL)
		goto out_kmem;

	blk_queue_make_request(zv->zv_queue, zvol_request);
	blk_queue_set_write_cache(zv->zv_queue, B_TRUE, B_TRUE);

	zv->zv_disk = alloc_disk(ZVOL_MINORS);
	if (zv->zv_disk == NULL)
		goto out_queue;

	zv->zv_queue->queuedata = zv;
	zv->zv_dev = dev;
	zv->zv_open_count = 0;
	strlcpy(zv->zv_name, name, MAXNAMELEN);

	zfs_rlock_init(&zv->zv_range_lock);

	zv->zv_disk->major = zvol_major;
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
 */
static void
zvol_free(zvol_state_t *zv)
{
	ASSERT(MUTEX_HELD(&zvol_state_lock));
	ASSERT(zv->zv_open_count == 0);

	zfs_rlock_destroy(&zv->zv_range_lock);

	zv->zv_disk->private_data = NULL;

	del_gendisk(zv->zv_disk);
	blk_cleanup_queue(zv->zv_queue);
	put_disk(zv->zv_disk);

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

	mutex_enter(&zvol_state_lock);

	zv = zvol_find_by_name(name);
	if (zv) {
		error = SET_ERROR(EEXIST);
		goto out;
	}

	doi = kmem_alloc(sizeof (dmu_object_info_t), KM_SLEEP);

	error = dmu_objset_own(name, DMU_OST_ZVOL, B_TRUE, zvol_tag, &os);
	if (error)
		goto out_doi;

	error = dmu_object_info(os, ZVOL_OBJ, doi);
	if (error)
		goto out_dmu_objset_disown;

	error = zap_lookup(os, ZVOL_ZAP_OBJ, "size", 8, 1, &volsize);
	if (error)
		goto out_dmu_objset_disown;

	error = zvol_find_minor(&minor);
	if (error)
		goto out_dmu_objset_disown;

	zv = zvol_alloc(MKDEV(zvol_major, minor), name);
	if (zv == NULL) {
		error = SET_ERROR(EAGAIN);
		goto out_dmu_objset_disown;
	}

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
	queue_flag_set_unlocked(QUEUE_FLAG_DISCARD, zv->zv_queue);
#ifdef QUEUE_FLAG_NONROT
	queue_flag_set_unlocked(QUEUE_FLAG_NONROT, zv->zv_queue);
#endif
#ifdef QUEUE_FLAG_ADD_RANDOM
	queue_flag_clear_unlocked(QUEUE_FLAG_ADD_RANDOM, zv->zv_queue);
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
		dmu_prefetch(os, ZVOL_OBJ, 0, len);
		dmu_prefetch(os, ZVOL_OBJ, volsize - len, len);
	}

	zv->zv_objset = NULL;
out_dmu_objset_disown:
	dmu_objset_disown(os, zvol_tag);
out_doi:
	kmem_free(doi, sizeof (dmu_object_info_t));
out:

	if (error == 0) {
		zvol_insert(zv);
		/*
		 * Drop the lock to prevent deadlock with sys_open() ->
		 * zvol_open(), which first takes bd_disk->bd_mutex and then
		 * takes zvol_state_lock, whereas this code path first takes
		 * zvol_state_lock, and then takes bd_disk->bd_mutex.
		 */
		mutex_exit(&zvol_state_lock);
		add_disk(zv->zv_disk);
	} else {
		mutex_exit(&zvol_state_lock);
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

	ASSERT(MUTEX_HELD(&zvol_state_lock));

	strlcpy(zv->zv_name, newname, sizeof (zv->zv_name));

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


/*
 * Mask errors to continue dmu_objset_find() traversal
 */
static int
zvol_create_snap_minor_cb(const char *dsname, void *arg)
{
	const char *name = (const char *)arg;

	ASSERT0(MUTEX_HELD(&spa_namespace_lock));

	/* skip the designated dataset */
	if (name && strcmp(dsname, name) == 0)
		return (0);

	/* at this point, the dsname should name a snapshot */
	if (strchr(dsname, '@') == 0) {
		dprintf("zvol_create_snap_minor_cb(): "
			"%s is not a shapshot name\n", dsname);
	} else {
		(void) zvol_create_minor_impl(dsname);
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
		/* create minor for the 'dsname' explicitly */
		error = zvol_create_minor_impl(dsname);
		if ((error == 0 || error == EEXIST) &&
		    (snapdev == ZFS_SNAPDEV_VISIBLE)) {
			fstrans_cookie_t cookie = spl_fstrans_mark();
			/*
			 * traverse snapshots only, do not traverse children,
			 * and skip the 'dsname'
			 */
			error = dmu_objset_find((char *)dsname,
			    zvol_create_snap_minor_cb, (void *)dsname,
			    DS_FIND_SNAPSHOTS);
			spl_fstrans_unmark(cookie);
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
 * If the name represents a snapshot, a check is perfromed if the snapshot is
 * 'visible' (which also verifies that the parent is a zvol), and if so,
 * a minor node for that snapshot is created.
 */
static int
zvol_create_minors_impl(const char *name)
{
	int error = 0;
	fstrans_cookie_t cookie;
	char *atp, *parent;

	if (zvol_inhibit_dev)
		return (0);

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
		    NULL, DS_FIND_CHILDREN);
		spl_fstrans_unmark(cookie);
	}

	kmem_free(parent, MAXPATHLEN);

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

	if (zvol_inhibit_dev)
		return;

	mutex_enter(&zvol_state_lock);

	for (zv = list_head(&zvol_state_list); zv != NULL; zv = zv_next) {
		zv_next = list_next(&zvol_state_list, zv);

		if (name == NULL || strcmp(zv->zv_name, name) == 0 ||
		    (strncmp(zv->zv_name, name, namelen) == 0 &&
		    (zv->zv_name[namelen] == '/' ||
		    zv->zv_name[namelen] == '@'))) {

			/* If in use, leave alone */
			if (zv->zv_open_count > 0)
				continue;

			zvol_remove(zv);
			zvol_free(zv);
		}
	}

	mutex_exit(&zvol_state_lock);
}

/* Remove minor for this specific snapshot only */
static void
zvol_remove_minor_impl(const char *name)
{
	zvol_state_t *zv, *zv_next;

	if (zvol_inhibit_dev)
		return;

	if (strchr(name, '@') == NULL)
		return;

	mutex_enter(&zvol_state_lock);

	for (zv = list_head(&zvol_state_list); zv != NULL; zv = zv_next) {
		zv_next = list_next(&zvol_state_list, zv);

		if (strcmp(zv->zv_name, name) == 0) {
			/* If in use, leave alone */
			if (zv->zv_open_count > 0)
				continue;
			zvol_remove(zv);
			zvol_free(zv);
			break;
		}
	}

	mutex_exit(&zvol_state_lock);
}

/*
 * Rename minors for specified dataset including children and snapshots.
 */
static void
zvol_rename_minors_impl(const char *oldname, const char *newname)
{
	zvol_state_t *zv, *zv_next;
	int oldnamelen, newnamelen;
	char *name;

	if (zvol_inhibit_dev)
		return;

	oldnamelen = strlen(oldname);
	newnamelen = strlen(newname);
	name = kmem_alloc(MAXNAMELEN, KM_SLEEP);

	mutex_enter(&zvol_state_lock);

	for (zv = list_head(&zvol_state_list); zv != NULL; zv = zv_next) {
		zv_next = list_next(&zvol_state_list, zv);

		/* If in use, leave alone */
		if (zv->zv_open_count > 0)
			continue;

		if (strcmp(zv->zv_name, oldname) == 0) {
			zvol_rename_minor(zv, newname);
		} else if (strncmp(zv->zv_name, oldname, oldnamelen) == 0 &&
		    (zv->zv_name[oldnamelen] == '/' ||
		    zv->zv_name[oldnamelen] == '@')) {
			snprintf(name, MAXNAMELEN, "%s%c%s", newname,
			    zv->zv_name[oldnamelen],
			    zv->zv_name + oldnamelen + 1);
			zvol_rename_minor(zv, name);
		}
	}

	mutex_exit(&zvol_state_lock);

	kmem_free(name, MAXNAMELEN);
}

typedef struct zvol_snapdev_cb_arg {
	uint64_t snapdev;
} zvol_snapdev_cb_arg_t;

static int
zvol_set_snapdev_cb(const char *dsname, void *param) {
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

static zvol_task_t *
zvol_task_alloc(zvol_async_op_t op, const char *name1, const char *name2,
    uint64_t snapdev)
{
	zvol_task_t *task;
	char *delim;

	/* Never allow tasks on hidden names. */
	if (name1[0] == '$')
		return (NULL);

	task = kmem_zalloc(sizeof (zvol_task_t), KM_SLEEP);
	task->op = op;
	task->snapdev = snapdev;
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
		zvol_set_snapdev_impl(task->name1, task->snapdev);
		break;
	default:
		VERIFY(0);
		break;
	}

	zvol_task_free(task);
}

typedef struct zvol_set_snapdev_arg {
	const char *zsda_name;
	uint64_t zsda_value;
	zprop_source_t zsda_source;
	dmu_tx_t *zsda_tx;
} zvol_set_snapdev_arg_t;

/*
 * Sanity check the dataset for safe use by the sync task.  No additional
 * conditions are imposed.
 */
static int
zvol_set_snapdev_check(void *arg, dmu_tx_t *tx)
{
	zvol_set_snapdev_arg_t *zsda = arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	dsl_dir_t *dd;
	int error;

	error = dsl_dir_hold(dp, zsda->zsda_name, FTAG, &dd, NULL);
	if (error != 0)
		return (error);

	dsl_dir_rele(dd, FTAG);

	return (error);
}

static int
zvol_set_snapdev_sync_cb(dsl_pool_t *dp, dsl_dataset_t *ds, void *arg)
{
	zvol_set_snapdev_arg_t *zsda = arg;
	char dsname[MAXNAMELEN];
	zvol_task_t *task;

	dsl_dataset_name(ds, dsname);
	dsl_prop_set_sync_impl(ds, zfs_prop_to_name(ZFS_PROP_SNAPDEV),
	    zsda->zsda_source, sizeof (zsda->zsda_value), 1,
	    &zsda->zsda_value, zsda->zsda_tx);

	task = zvol_task_alloc(ZVOL_ASYNC_SET_SNAPDEV, dsname,
	    NULL, zsda->zsda_value);
	if (task == NULL)
		return (0);

	(void) taskq_dispatch(dp->dp_spa->spa_zvol_taskq, zvol_task_cb,
		task, TQ_SLEEP);
	return (0);
}

/*
 * Traverse all child snapshot datasets and apply snapdev appropriately.
 */
static void
zvol_set_snapdev_sync(void *arg, dmu_tx_t *tx)
{
	zvol_set_snapdev_arg_t *zsda = arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	dsl_dir_t *dd;

	VERIFY0(dsl_dir_hold(dp, zsda->zsda_name, FTAG, &dd, NULL));
	zsda->zsda_tx = tx;

	dmu_objset_find_dp(dp, dd->dd_object, zvol_set_snapdev_sync_cb,
	    zsda, DS_FIND_CHILDREN);

	dsl_dir_rele(dd, FTAG);
}

int
zvol_set_snapdev(const char *ddname, zprop_source_t source, uint64_t snapdev)
{
	zvol_set_snapdev_arg_t zsda;

	zsda.zsda_name = ddname;
	zsda.zsda_source = source;
	zsda.zsda_value = snapdev;

	return (dsl_sync_task(ddname, zvol_set_snapdev_check,
	    zvol_set_snapdev_sync, &zsda, 0, ZFS_SPACE_CHECK_NONE));
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
	if ((async == B_FALSE) && (id != 0))
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
	if ((async == B_FALSE) && (id != 0))
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
	if ((async == B_FALSE) && (id != 0))
		taskq_wait_id(spa->spa_zvol_taskq, id);
}

int
zvol_init(void)
{
	int error;

	list_create(&zvol_state_list, sizeof (zvol_state_t),
	    offsetof(zvol_state_t, zv_next));
	mutex_init(&zvol_state_lock, NULL, MUTEX_DEFAULT, NULL);

	error = register_blkdev(zvol_major, ZVOL_DRIVER);
	if (error) {
		printk(KERN_INFO "ZFS: register_blkdev() failed %d\n", error);
		goto out;
	}

	blk_register_region(MKDEV(zvol_major, 0), 1UL << MINORBITS,
	    THIS_MODULE, zvol_probe, NULL, NULL);

	return (0);

out:
	mutex_destroy(&zvol_state_lock);
	list_destroy(&zvol_state_list);

	return (SET_ERROR(error));
}

void
zvol_fini(void)
{
	zvol_remove_minors_impl(NULL);

	blk_unregister_region(MKDEV(zvol_major, 0), 1UL << MINORBITS);
	unregister_blkdev(zvol_major, ZVOL_DRIVER);

	list_destroy(&zvol_state_list);
	mutex_destroy(&zvol_state_lock);
}

module_param(zvol_inhibit_dev, uint, 0644);
MODULE_PARM_DESC(zvol_inhibit_dev, "Do not create zvol device nodes");

module_param(zvol_major, uint, 0444);
MODULE_PARM_DESC(zvol_major, "Major number for zvol device");

module_param(zvol_max_discard_blocks, ulong, 0444);
MODULE_PARM_DESC(zvol_max_discard_blocks, "Max number of blocks to discard");

module_param(zvol_prefetch_bytes, uint, 0644);
MODULE_PARM_DESC(zvol_prefetch_bytes, "Prefetch N bytes at zvol start+end");
