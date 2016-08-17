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
 * Copyright (c) 2006, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2011, 2015 by Delphix. All rights reserved.
 * Copyright (c) 2014 Integros [integros.com]
 * Copyright 2017 Joyent, Inc.
 */

#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/zap.h>
#include <sys/dsl_synctask.h>
#include <sys/dmu_tx.h>
#include <sys/dmu_objset.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_dir.h>
#include <sys/cmn_err.h>
#include <sys/sunddi.h>
#include <sys/cred.h>
#include "zfs_comutil.h"
#ifdef _KERNEL
#include <sys/zone.h>
#endif

/*
 * Routines to manage the on-disk history log.
 *
 * The history log is stored as a dmu object containing
 * <packed record length, record nvlist> tuples.
 *
 * Where "record nvlist" is an nvlist containing uint64_ts and strings, and
 * "packed record length" is the packed length of the "record nvlist" stored
 * as a little endian uint64_t.
 *
 * The log is implemented as a ring buffer, though the original creation
 * of the pool ('zpool create') is never overwritten.
 *
 * The history log is tracked as object 'spa_t::spa_history'.  The bonus buffer
 * of 'spa_history' stores the offsets for logging/retrieving history as
 * 'spa_history_phys_t'.  'sh_pool_create_len' is the ending offset in bytes of
 * where the 'zpool create' record is stored.  This allows us to never
 * overwrite the original creation of the pool.  'sh_phys_max_off' is the
 * physical ending offset in bytes of the log.  This tells you the length of
 * the buffer. 'sh_eof' is the logical EOF (in bytes).  Whenever a record
 * is added, 'sh_eof' is incremented by the the size of the record.
 * 'sh_eof' is never decremented.  'sh_bof' is the logical BOF (in bytes).
 * This is where the consumer should start reading from after reading in
 * the 'zpool create' portion of the log.
 *
 * 'sh_records_lost' keeps track of how many records have been overwritten
 * and permanently lost.
 */

/* convert a logical offset to physical */
static uint64_t
spa_history_log_to_phys(uint64_t log_off, spa_history_phys_t *shpp)
{
	uint64_t phys_len;

	phys_len = shpp->sh_phys_max_off - shpp->sh_pool_create_len;
	return ((log_off - shpp->sh_pool_create_len) % phys_len
	    + shpp->sh_pool_create_len);
}

void
spa_history_create_obj(spa_t *spa, dmu_tx_t *tx)
{
	dmu_buf_t *dbp;
	spa_history_phys_t *shpp;
	objset_t *mos = spa->spa_meta_objset;

	ASSERT0(spa->spa_history);
	spa->spa_history = dmu_object_alloc(mos, DMU_OT_SPA_HISTORY,
	    SPA_OLD_MAXBLOCKSIZE, DMU_OT_SPA_HISTORY_OFFSETS,
	    sizeof (spa_history_phys_t), tx);

	VERIFY0(zap_add(mos, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_HISTORY, sizeof (uint64_t), 1,
	    &spa->spa_history, tx));

	VERIFY0(dmu_bonus_hold(mos, spa->spa_history, FTAG, &dbp));
	ASSERT3U(dbp->db_size, >=, sizeof (spa_history_phys_t));

	shpp = dbp->db_data;
	dmu_buf_will_dirty(dbp, tx);

	/*
	 * Figure out maximum size of history log.  We set it at
	 * 0.1% of pool size, with a max of 1G and min of 128KB.
	 */
	shpp->sh_phys_max_off =
	    metaslab_class_get_dspace(spa_normal_class(spa)) / 1000;
	shpp->sh_phys_max_off = MIN(shpp->sh_phys_max_off, 1<<30);
	shpp->sh_phys_max_off = MAX(shpp->sh_phys_max_off, 128<<10);

	dmu_buf_rele(dbp, FTAG);
}

/*
 * Change 'sh_bof' to the beginning of the next record.
 */
static int
spa_history_advance_bof(spa_t *spa, spa_history_phys_t *shpp)
{
	objset_t *mos = spa->spa_meta_objset;
	uint64_t firstread, reclen, phys_bof;
	char buf[sizeof (reclen)];
	int err;

	phys_bof = spa_history_log_to_phys(shpp->sh_bof, shpp);
	firstread = MIN(sizeof (reclen), shpp->sh_phys_max_off - phys_bof);

	if ((err = dmu_read(mos, spa->spa_history, phys_bof, firstread,
	    buf, DMU_READ_PREFETCH)) != 0)
		return (err);
	if (firstread != sizeof (reclen)) {
		if ((err = dmu_read(mos, spa->spa_history,
		    shpp->sh_pool_create_len, sizeof (reclen) - firstread,
		    buf + firstread, DMU_READ_PREFETCH)) != 0)
			return (err);
	}

	reclen = LE_64(*((uint64_t *)buf));
	shpp->sh_bof += reclen + sizeof (reclen);
	shpp->sh_records_lost++;
	return (0);
}

static int
spa_history_write(spa_t *spa, void *buf, uint64_t len, spa_history_phys_t *shpp,
    dmu_tx_t *tx)
{
	uint64_t firstwrite, phys_eof;
	objset_t *mos = spa->spa_meta_objset;
	int err;

	ASSERT(MUTEX_HELD(&spa->spa_history_lock));

	/* see if we need to reset logical BOF */
	while (shpp->sh_phys_max_off - shpp->sh_pool_create_len -
	    (shpp->sh_eof - shpp->sh_bof) <= len) {
		if ((err = spa_history_advance_bof(spa, shpp)) != 0) {
			return (err);
		}
	}

	phys_eof = spa_history_log_to_phys(shpp->sh_eof, shpp);
	firstwrite = MIN(len, shpp->sh_phys_max_off - phys_eof);
	shpp->sh_eof += len;
	dmu_write(mos, spa->spa_history, phys_eof, firstwrite, buf, tx);

	len -= firstwrite;
	if (len > 0) {
		/* write out the rest at the beginning of physical file */
		dmu_write(mos, spa->spa_history, shpp->sh_pool_create_len,
		    len, (char *)buf + firstwrite, tx);
	}

	return (0);
}

static char *
spa_history_zone(void)
{
#ifdef _KERNEL
#ifdef HAVE_SPL
	return ("linux");
#else
	return (curproc->p_zone->zone_name);
#endif
#else
	return (NULL);
#endif
}

/*
 * Post a history sysevent.
 *
 * The nvlist_t* passed into this function will be transformed into a new
 * nvlist where:
 *
 * 1. Nested nvlists will be flattened to a single level
 * 2. Keys will have their names normalized (to remove any problematic
 * characters, such as whitespace)
 *
 * The nvlist_t passed into this function will duplicated and should be freed
 * by caller.
 *
 */
static void
spa_history_log_notify(spa_t *spa, nvlist_t *nvl)
{
	nvlist_t *hist_nvl = fnvlist_alloc();
	uint64_t uint64;
	char *string;

	if (nvlist_lookup_string(nvl, ZPOOL_HIST_CMD, &string) == 0)
		fnvlist_add_string(hist_nvl, ZFS_EV_HIST_CMD, string);

	if (nvlist_lookup_string(nvl, ZPOOL_HIST_INT_NAME, &string) == 0)
		fnvlist_add_string(hist_nvl, ZFS_EV_HIST_INT_NAME, string);

	if (nvlist_lookup_string(nvl, ZPOOL_HIST_ZONE, &string) == 0)
		fnvlist_add_string(hist_nvl, ZFS_EV_HIST_ZONE, string);

	if (nvlist_lookup_string(nvl, ZPOOL_HIST_HOST, &string) == 0)
		fnvlist_add_string(hist_nvl, ZFS_EV_HIST_HOST, string);

	if (nvlist_lookup_string(nvl, ZPOOL_HIST_DSNAME, &string) == 0)
		fnvlist_add_string(hist_nvl, ZFS_EV_HIST_DSNAME, string);

	if (nvlist_lookup_string(nvl, ZPOOL_HIST_INT_STR, &string) == 0)
		fnvlist_add_string(hist_nvl, ZFS_EV_HIST_INT_STR, string);

	if (nvlist_lookup_string(nvl, ZPOOL_HIST_IOCTL, &string) == 0)
		fnvlist_add_string(hist_nvl, ZFS_EV_HIST_IOCTL, string);

	if (nvlist_lookup_string(nvl, ZPOOL_HIST_INT_NAME, &string) == 0)
		fnvlist_add_string(hist_nvl, ZFS_EV_HIST_INT_NAME, string);

	if (nvlist_lookup_uint64(nvl, ZPOOL_HIST_DSID, &uint64) == 0)
		fnvlist_add_uint64(hist_nvl, ZFS_EV_HIST_DSID, uint64);

	if (nvlist_lookup_uint64(nvl, ZPOOL_HIST_TXG, &uint64) == 0)
		fnvlist_add_uint64(hist_nvl, ZFS_EV_HIST_TXG, uint64);

	if (nvlist_lookup_uint64(nvl, ZPOOL_HIST_TIME, &uint64) == 0)
		fnvlist_add_uint64(hist_nvl, ZFS_EV_HIST_TIME, uint64);

	if (nvlist_lookup_uint64(nvl, ZPOOL_HIST_WHO, &uint64) == 0)
		fnvlist_add_uint64(hist_nvl, ZFS_EV_HIST_WHO, uint64);

	if (nvlist_lookup_uint64(nvl, ZPOOL_HIST_INT_EVENT, &uint64) == 0)
		fnvlist_add_uint64(hist_nvl, ZFS_EV_HIST_INT_EVENT, uint64);

	spa_event_notify(spa, NULL, hist_nvl, ESC_ZFS_HISTORY_EVENT);

	nvlist_free(hist_nvl);
}

/*
 * Write out a history event.
 */
/*ARGSUSED*/
static void
spa_history_log_sync(void *arg, dmu_tx_t *tx)
{
	nvlist_t	*nvl = arg;
	spa_t		*spa = dmu_tx_pool(tx)->dp_spa;
	objset_t	*mos = spa->spa_meta_objset;
	dmu_buf_t	*dbp;
	spa_history_phys_t *shpp;
	size_t		reclen;
	uint64_t	le_len;
	char		*record_packed = NULL;
	int		ret;

	/*
	 * If we have an older pool that doesn't have a command
	 * history object, create it now.
	 */
	mutex_enter(&spa->spa_history_lock);
	if (!spa->spa_history)
		spa_history_create_obj(spa, tx);
	mutex_exit(&spa->spa_history_lock);

	/*
	 * Get the offset of where we need to write via the bonus buffer.
	 * Update the offset when the write completes.
	 */
	VERIFY0(dmu_bonus_hold(mos, spa->spa_history, FTAG, &dbp));
	shpp = dbp->db_data;

	dmu_buf_will_dirty(dbp, tx);

#ifdef ZFS_DEBUG
	{
		dmu_object_info_t doi;
		dmu_object_info_from_db(dbp, &doi);
		ASSERT3U(doi.doi_bonus_type, ==, DMU_OT_SPA_HISTORY_OFFSETS);
	}
#endif

	fnvlist_add_uint64(nvl, ZPOOL_HIST_TIME, gethrestime_sec());
	fnvlist_add_string(nvl, ZPOOL_HIST_HOST, utsname()->nodename);

	if (nvlist_exists(nvl, ZPOOL_HIST_CMD)) {
		zfs_dbgmsg("command: %s",
		    fnvlist_lookup_string(nvl, ZPOOL_HIST_CMD));
	} else if (nvlist_exists(nvl, ZPOOL_HIST_INT_NAME)) {
		if (nvlist_exists(nvl, ZPOOL_HIST_DSNAME)) {
			zfs_dbgmsg("txg %lld %s %s (id %llu) %s",
			    fnvlist_lookup_uint64(nvl, ZPOOL_HIST_TXG),
			    fnvlist_lookup_string(nvl, ZPOOL_HIST_INT_NAME),
			    fnvlist_lookup_string(nvl, ZPOOL_HIST_DSNAME),
			    fnvlist_lookup_uint64(nvl, ZPOOL_HIST_DSID),
			    fnvlist_lookup_string(nvl, ZPOOL_HIST_INT_STR));
		} else {
			zfs_dbgmsg("txg %lld %s %s",
			    fnvlist_lookup_uint64(nvl, ZPOOL_HIST_TXG),
			    fnvlist_lookup_string(nvl, ZPOOL_HIST_INT_NAME),
			    fnvlist_lookup_string(nvl, ZPOOL_HIST_INT_STR));
		}
		/*
		 * The history sysevent is posted only for internal history
		 * messages to show what has happened, not how it happened. For
		 * example, the following command:
		 *
		 * # zfs destroy -r tank/foo
		 *
		 * will result in one sysevent posted per dataset that is
		 * destroyed as a result of the command - which could be more
		 * than one event in total.  By contrast, if the sysevent was
		 * posted as a result of the ZPOOL_HIST_CMD key being present
		 * it would result in only one sysevent being posted with the
		 * full command line arguments, requiring the consumer to know
		 * how to parse and understand zfs(1M) command invocations.
		 */
		spa_history_log_notify(spa, nvl);
	} else if (nvlist_exists(nvl, ZPOOL_HIST_IOCTL)) {
		zfs_dbgmsg("ioctl %s",
		    fnvlist_lookup_string(nvl, ZPOOL_HIST_IOCTL));
	}

	VERIFY3U(nvlist_pack(nvl, &record_packed, &reclen, NV_ENCODE_NATIVE,
	    KM_SLEEP), ==, 0);

	mutex_enter(&spa->spa_history_lock);

	/* write out the packed length as little endian */
	le_len = LE_64((uint64_t)reclen);
	ret = spa_history_write(spa, &le_len, sizeof (le_len), shpp, tx);
	if (!ret)
		ret = spa_history_write(spa, record_packed, reclen, shpp, tx);

	/* The first command is the create, which we keep forever */
	if (ret == 0 && shpp->sh_pool_create_len == 0 &&
	    nvlist_exists(nvl, ZPOOL_HIST_CMD)) {
		shpp->sh_pool_create_len = shpp->sh_bof = shpp->sh_eof;
	}

	mutex_exit(&spa->spa_history_lock);
	fnvlist_pack_free(record_packed, reclen);
	dmu_buf_rele(dbp, FTAG);
	fnvlist_free(nvl);
}

/*
 * Write out a history event.
 */
int
spa_history_log(spa_t *spa, const char *msg)
{
	int err;
	nvlist_t *nvl = fnvlist_alloc();

	fnvlist_add_string(nvl, ZPOOL_HIST_CMD, msg);
	err = spa_history_log_nvl(spa, nvl);
	fnvlist_free(nvl);
	return (err);
}

int
spa_history_log_nvl(spa_t *spa, nvlist_t *nvl)
{
	int err = 0;
	dmu_tx_t *tx;
	nvlist_t *nvarg;

	if (spa_version(spa) < SPA_VERSION_ZPOOL_HISTORY || !spa_writeable(spa))
		return (SET_ERROR(EINVAL));

	tx = dmu_tx_create_dd(spa_get_dsl(spa)->dp_mos_dir);
	err = dmu_tx_assign(tx, TXG_WAIT);
	if (err) {
		dmu_tx_abort(tx);
		return (err);
	}

	VERIFY0(nvlist_dup(nvl, &nvarg, KM_SLEEP));
	if (spa_history_zone() != NULL) {
		fnvlist_add_string(nvarg, ZPOOL_HIST_ZONE,
		    spa_history_zone());
	}
	fnvlist_add_uint64(nvarg, ZPOOL_HIST_WHO, crgetruid(CRED()));

	/* Kick this off asynchronously; errors are ignored. */
	dsl_sync_task_nowait(spa_get_dsl(spa), spa_history_log_sync,
	    nvarg, 0, ZFS_SPACE_CHECK_NONE, tx);
	dmu_tx_commit(tx);

	/* spa_history_log_sync will free nvl */
	return (err);

}

/*
 * Read out the command history.
 */
int
spa_history_get(spa_t *spa, uint64_t *offp, uint64_t *len, char *buf)
{
	objset_t *mos = spa->spa_meta_objset;
	dmu_buf_t *dbp;
	uint64_t read_len, phys_read_off, phys_eof;
	uint64_t leftover = 0;
	spa_history_phys_t *shpp;
	int err;

	/*
	 * If the command history doesn't exist (older pool),
	 * that's ok, just return ENOENT.
	 */
	if (!spa->spa_history)
		return (SET_ERROR(ENOENT));

	/*
	 * The history is logged asynchronously, so when they request
	 * the first chunk of history, make sure everything has been
	 * synced to disk so that we get it.
	 */
	if (*offp == 0 && spa_writeable(spa))
		txg_wait_synced(spa_get_dsl(spa), 0);

	if ((err = dmu_bonus_hold(mos, spa->spa_history, FTAG, &dbp)) != 0)
		return (err);
	shpp = dbp->db_data;

#ifdef ZFS_DEBUG
	{
		dmu_object_info_t doi;
		dmu_object_info_from_db(dbp, &doi);
		ASSERT3U(doi.doi_bonus_type, ==, DMU_OT_SPA_HISTORY_OFFSETS);
	}
#endif

	mutex_enter(&spa->spa_history_lock);
	phys_eof = spa_history_log_to_phys(shpp->sh_eof, shpp);

	if (*offp < shpp->sh_pool_create_len) {
		/* read in just the zpool create history */
		phys_read_off = *offp;
		read_len = MIN(*len, shpp->sh_pool_create_len -
		    phys_read_off);
	} else {
		/*
		 * Need to reset passed in offset to BOF if the passed in
		 * offset has since been overwritten.
		 */
		*offp = MAX(*offp, shpp->sh_bof);
		phys_read_off = spa_history_log_to_phys(*offp, shpp);

		/*
		 * Read up to the minimum of what the user passed down or
		 * the EOF (physical or logical).  If we hit physical EOF,
		 * use 'leftover' to read from the physical BOF.
		 */
		if (phys_read_off <= phys_eof) {
			read_len = MIN(*len, phys_eof - phys_read_off);
		} else {
			read_len = MIN(*len,
			    shpp->sh_phys_max_off - phys_read_off);
			if (phys_read_off + *len > shpp->sh_phys_max_off) {
				leftover = MIN(*len - read_len,
				    phys_eof - shpp->sh_pool_create_len);
			}
		}
	}

	/* offset for consumer to use next */
	*offp += read_len + leftover;

	/* tell the consumer how much you actually read */
	*len = read_len + leftover;

	if (read_len == 0) {
		mutex_exit(&spa->spa_history_lock);
		dmu_buf_rele(dbp, FTAG);
		return (0);
	}

	err = dmu_read(mos, spa->spa_history, phys_read_off, read_len, buf,
	    DMU_READ_PREFETCH);
	if (leftover && err == 0) {
		err = dmu_read(mos, spa->spa_history, shpp->sh_pool_create_len,
		    leftover, buf + read_len, DMU_READ_PREFETCH);
	}
	mutex_exit(&spa->spa_history_lock);

	dmu_buf_rele(dbp, FTAG);
	return (err);
}

/*
 * The nvlist will be consumed by this call.
 */
static void
log_internal(nvlist_t *nvl, const char *operation, spa_t *spa,
    dmu_tx_t *tx, const char *fmt, va_list adx)
{
	char *msg;

	/*
	 * If this is part of creating a pool, not everything is
	 * initialized yet, so don't bother logging the internal events.
	 * Likewise if the pool is not writeable.
	 */
	if (spa_is_initializing(spa) || !spa_writeable(spa)) {
		fnvlist_free(nvl);
		return;
	}

	msg = kmem_vasprintf(fmt, adx);
	fnvlist_add_string(nvl, ZPOOL_HIST_INT_STR, msg);
	strfree(msg);

	fnvlist_add_string(nvl, ZPOOL_HIST_INT_NAME, operation);
	fnvlist_add_uint64(nvl, ZPOOL_HIST_TXG, tx->tx_txg);

	if (dmu_tx_is_syncing(tx)) {
		spa_history_log_sync(nvl, tx);
	} else {
		dsl_sync_task_nowait(spa_get_dsl(spa),
		    spa_history_log_sync, nvl, 0, ZFS_SPACE_CHECK_NONE, tx);
	}
	/* spa_history_log_sync() will free nvl */
}

void
spa_history_log_internal(spa_t *spa, const char *operation,
    dmu_tx_t *tx, const char *fmt, ...)
{
	dmu_tx_t *htx = tx;
	va_list adx;

	/* create a tx if we didn't get one */
	if (tx == NULL) {
		htx = dmu_tx_create_dd(spa_get_dsl(spa)->dp_mos_dir);
		if (dmu_tx_assign(htx, TXG_WAIT) != 0) {
			dmu_tx_abort(htx);
			return;
		}
	}

	va_start(adx, fmt);
	log_internal(fnvlist_alloc(), operation, spa, htx, fmt, adx);
	va_end(adx);

	/* if we didn't get a tx from the caller, commit the one we made */
	if (tx == NULL)
		dmu_tx_commit(htx);
}

void
spa_history_log_internal_ds(dsl_dataset_t *ds, const char *operation,
    dmu_tx_t *tx, const char *fmt, ...)
{
	va_list adx;
	char namebuf[ZFS_MAX_DATASET_NAME_LEN];
	nvlist_t *nvl = fnvlist_alloc();

	ASSERT(tx != NULL);

	dsl_dataset_name(ds, namebuf);
	fnvlist_add_string(nvl, ZPOOL_HIST_DSNAME, namebuf);
	fnvlist_add_uint64(nvl, ZPOOL_HIST_DSID, ds->ds_object);

	va_start(adx, fmt);
	log_internal(nvl, operation, dsl_dataset_get_spa(ds), tx, fmt, adx);
	va_end(adx);
}

void
spa_history_log_internal_dd(dsl_dir_t *dd, const char *operation,
    dmu_tx_t *tx, const char *fmt, ...)
{
	va_list adx;
	char namebuf[ZFS_MAX_DATASET_NAME_LEN];
	nvlist_t *nvl = fnvlist_alloc();

	ASSERT(tx != NULL);

	dsl_dir_name(dd, namebuf);
	fnvlist_add_string(nvl, ZPOOL_HIST_DSNAME, namebuf);
	fnvlist_add_uint64(nvl, ZPOOL_HIST_DSID,
	    dsl_dir_phys(dd)->dd_head_dataset_obj);

	va_start(adx, fmt);
	log_internal(nvl, operation, dd->dd_pool->dp_spa, tx, fmt, adx);
	va_end(adx);
}

void
spa_history_log_version(spa_t *spa, const char *operation, dmu_tx_t *tx)
{
	utsname_t *u = utsname();

	spa_history_log_internal(spa, operation, tx,
	    "pool version %llu; software version %llu/%llu; uts %s %s %s %s",
	    (u_longlong_t)spa_version(spa), SPA_VERSION, ZPL_VERSION,
	    u->nodename, u->release, u->version, u->machine);
}

#if defined(_KERNEL) && defined(HAVE_SPL)
EXPORT_SYMBOL(spa_history_create_obj);
EXPORT_SYMBOL(spa_history_get);
EXPORT_SYMBOL(spa_history_log);
EXPORT_SYMBOL(spa_history_log_internal);
EXPORT_SYMBOL(spa_history_log_version);
#endif
