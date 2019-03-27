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
 * Copyright (c) 2013, 2014 by Delphix. All rights reserved.
 */

/*
 * Routines to manage the on-disk persistent error log.
 *
 * Each pool stores a log of all logical data errors seen during normal
 * operation.  This is actually the union of two distinct logs: the last log,
 * and the current log.  All errors seen are logged to the current log.  When a
 * scrub completes, the current log becomes the last log, the last log is thrown
 * out, and the current log is reinitialized.  This way, if an error is somehow
 * corrected, a new scrub will show that that it no longer exists, and will be
 * deleted from the log when the scrub completes.
 *
 * The log is stored using a ZAP object whose key is a string form of the
 * zbookmark_phys tuple (objset, object, level, blkid), and whose contents is an
 * optional 'objset:object' human-readable string describing the data.  When an
 * error is first logged, this string will be empty, indicating that no name is
 * known.  This prevents us from having to issue a potentially large amount of
 * I/O to discover the object name during an error path.  Instead, we do the
 * calculation when the data is requested, storing the result so future queries
 * will be faster.
 *
 * This log is then shipped into an nvlist where the key is the dataset name and
 * the value is the object name.  Userland is then responsible for uniquifying
 * this list and displaying it to the user.
 */

#include <sys/dmu_tx.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/zap.h>
#include <sys/zio.h>


/*
 * Convert a bookmark to a string.
 */
static void
bookmark_to_name(zbookmark_phys_t *zb, char *buf, size_t len)
{
	(void) snprintf(buf, len, "%llx:%llx:%llx:%llx",
	    (u_longlong_t)zb->zb_objset, (u_longlong_t)zb->zb_object,
	    (u_longlong_t)zb->zb_level, (u_longlong_t)zb->zb_blkid);
}

/*
 * Convert a string to a bookmark
 */
#ifdef _KERNEL
static void
name_to_bookmark(char *buf, zbookmark_phys_t *zb)
{
	zb->zb_objset = zfs_strtonum(buf, &buf);
	ASSERT(*buf == ':');
	zb->zb_object = zfs_strtonum(buf + 1, &buf);
	ASSERT(*buf == ':');
	zb->zb_level = (int)zfs_strtonum(buf + 1, &buf);
	ASSERT(*buf == ':');
	zb->zb_blkid = zfs_strtonum(buf + 1, &buf);
	ASSERT(*buf == '\0');
}
#endif

/*
 * Log an uncorrectable error to the persistent error log.  We add it to the
 * spa's list of pending errors.  The changes are actually synced out to disk
 * during spa_errlog_sync().
 */
void
spa_log_error(spa_t *spa, zio_t *zio)
{
	zbookmark_phys_t *zb = &zio->io_logical->io_bookmark;
	spa_error_entry_t search;
	spa_error_entry_t *new;
	avl_tree_t *tree;
	avl_index_t where;

	/*
	 * If we are trying to import a pool, ignore any errors, as we won't be
	 * writing to the pool any time soon.
	 */
	if (spa_load_state(spa) == SPA_LOAD_TRYIMPORT)
		return;

	mutex_enter(&spa->spa_errlist_lock);

	/*
	 * If we have had a request to rotate the log, log it to the next list
	 * instead of the current one.
	 */
	if (spa->spa_scrub_active || spa->spa_scrub_finished)
		tree = &spa->spa_errlist_scrub;
	else
		tree = &spa->spa_errlist_last;

	search.se_bookmark = *zb;
	if (avl_find(tree, &search, &where) != NULL) {
		mutex_exit(&spa->spa_errlist_lock);
		return;
	}

	new = kmem_zalloc(sizeof (spa_error_entry_t), KM_SLEEP);
	new->se_bookmark = *zb;
	avl_insert(tree, new, where);

	mutex_exit(&spa->spa_errlist_lock);
}

/*
 * Return the number of errors currently in the error log.  This is actually the
 * sum of both the last log and the current log, since we don't know the union
 * of these logs until we reach userland.
 */
uint64_t
spa_get_errlog_size(spa_t *spa)
{
	uint64_t total = 0, count;

	mutex_enter(&spa->spa_errlog_lock);
	if (spa->spa_errlog_scrub != 0 &&
	    zap_count(spa->spa_meta_objset, spa->spa_errlog_scrub,
	    &count) == 0)
		total += count;

	if (spa->spa_errlog_last != 0 && !spa->spa_scrub_finished &&
	    zap_count(spa->spa_meta_objset, spa->spa_errlog_last,
	    &count) == 0)
		total += count;
	mutex_exit(&spa->spa_errlog_lock);

	mutex_enter(&spa->spa_errlist_lock);
	total += avl_numnodes(&spa->spa_errlist_last);
	total += avl_numnodes(&spa->spa_errlist_scrub);
	mutex_exit(&spa->spa_errlist_lock);

	return (total);
}

#ifdef _KERNEL
static int
process_error_log(spa_t *spa, uint64_t obj, void *addr, size_t *count)
{
	zap_cursor_t zc;
	zap_attribute_t za;
	zbookmark_phys_t zb;

	if (obj == 0)
		return (0);

	for (zap_cursor_init(&zc, spa->spa_meta_objset, obj);
	    zap_cursor_retrieve(&zc, &za) == 0;
	    zap_cursor_advance(&zc)) {

		if (*count == 0) {
			zap_cursor_fini(&zc);
			return (SET_ERROR(ENOMEM));
		}

		name_to_bookmark(za.za_name, &zb);

		if (copyout(&zb, (char *)addr +
		    (*count - 1) * sizeof (zbookmark_phys_t),
		    sizeof (zbookmark_phys_t)) != 0) {
			zap_cursor_fini(&zc);
			return (SET_ERROR(EFAULT));
		}

		*count -= 1;
	}

	zap_cursor_fini(&zc);

	return (0);
}

static int
process_error_list(avl_tree_t *list, void *addr, size_t *count)
{
	spa_error_entry_t *se;

	for (se = avl_first(list); se != NULL; se = AVL_NEXT(list, se)) {

		if (*count == 0)
			return (SET_ERROR(ENOMEM));

		if (copyout(&se->se_bookmark, (char *)addr +
		    (*count - 1) * sizeof (zbookmark_phys_t),
		    sizeof (zbookmark_phys_t)) != 0)
			return (SET_ERROR(EFAULT));

		*count -= 1;
	}

	return (0);
}
#endif

/*
 * Copy all known errors to userland as an array of bookmarks.  This is
 * actually a union of the on-disk last log and current log, as well as any
 * pending error requests.
 *
 * Because the act of reading the on-disk log could cause errors to be
 * generated, we have two separate locks: one for the error log and one for the
 * in-core error lists.  We only need the error list lock to log and error, so
 * we grab the error log lock while we read the on-disk logs, and only pick up
 * the error list lock when we are finished.
 */
int
spa_get_errlog(spa_t *spa, void *uaddr, size_t *count)
{
	int ret = 0;

#ifdef _KERNEL
	mutex_enter(&spa->spa_errlog_lock);

	ret = process_error_log(spa, spa->spa_errlog_scrub, uaddr, count);

	if (!ret && !spa->spa_scrub_finished)
		ret = process_error_log(spa, spa->spa_errlog_last, uaddr,
		    count);

	mutex_enter(&spa->spa_errlist_lock);
	if (!ret)
		ret = process_error_list(&spa->spa_errlist_scrub, uaddr,
		    count);
	if (!ret)
		ret = process_error_list(&spa->spa_errlist_last, uaddr,
		    count);
	mutex_exit(&spa->spa_errlist_lock);

	mutex_exit(&spa->spa_errlog_lock);
#endif

	return (ret);
}

/*
 * Called when a scrub completes.  This simply set a bit which tells which AVL
 * tree to add new errors.  spa_errlog_sync() is responsible for actually
 * syncing the changes to the underlying objects.
 */
void
spa_errlog_rotate(spa_t *spa)
{
	mutex_enter(&spa->spa_errlist_lock);
	spa->spa_scrub_finished = B_TRUE;
	mutex_exit(&spa->spa_errlist_lock);
}

/*
 * Discard any pending errors from the spa_t.  Called when unloading a faulted
 * pool, as the errors encountered during the open cannot be synced to disk.
 */
void
spa_errlog_drain(spa_t *spa)
{
	spa_error_entry_t *se;
	void *cookie;

	mutex_enter(&spa->spa_errlist_lock);

	cookie = NULL;
	while ((se = avl_destroy_nodes(&spa->spa_errlist_last,
	    &cookie)) != NULL)
		kmem_free(se, sizeof (spa_error_entry_t));
	cookie = NULL;
	while ((se = avl_destroy_nodes(&spa->spa_errlist_scrub,
	    &cookie)) != NULL)
		kmem_free(se, sizeof (spa_error_entry_t));

	mutex_exit(&spa->spa_errlist_lock);
}

/*
 * Process a list of errors into the current on-disk log.
 */
static void
sync_error_list(spa_t *spa, avl_tree_t *t, uint64_t *obj, dmu_tx_t *tx)
{
	spa_error_entry_t *se;
	char buf[64];
	void *cookie;

	if (avl_numnodes(t) != 0) {
		/* create log if necessary */
		if (*obj == 0)
			*obj = zap_create(spa->spa_meta_objset,
			    DMU_OT_ERROR_LOG, DMU_OT_NONE,
			    0, tx);

		/* add errors to the current log */
		for (se = avl_first(t); se != NULL; se = AVL_NEXT(t, se)) {
			char *name = se->se_name ? se->se_name : "";

			bookmark_to_name(&se->se_bookmark, buf, sizeof (buf));

			(void) zap_update(spa->spa_meta_objset,
			    *obj, buf, 1, strlen(name) + 1, name, tx);
		}

		/* purge the error list */
		cookie = NULL;
		while ((se = avl_destroy_nodes(t, &cookie)) != NULL)
			kmem_free(se, sizeof (spa_error_entry_t));
	}
}

/*
 * Sync the error log out to disk.  This is a little tricky because the act of
 * writing the error log requires the spa_errlist_lock.  So, we need to lock the
 * error lists, take a copy of the lists, and then reinitialize them.  Then, we
 * drop the error list lock and take the error log lock, at which point we
 * do the errlog processing.  Then, if we encounter an I/O error during this
 * process, we can successfully add the error to the list.  Note that this will
 * result in the perpetual recycling of errors, but it is an unlikely situation
 * and not a performance critical operation.
 */
void
spa_errlog_sync(spa_t *spa, uint64_t txg)
{
	dmu_tx_t *tx;
	avl_tree_t scrub, last;
	int scrub_finished;

	mutex_enter(&spa->spa_errlist_lock);

	/*
	 * Bail out early under normal circumstances.
	 */
	if (avl_numnodes(&spa->spa_errlist_scrub) == 0 &&
	    avl_numnodes(&spa->spa_errlist_last) == 0 &&
	    !spa->spa_scrub_finished) {
		mutex_exit(&spa->spa_errlist_lock);
		return;
	}

	spa_get_errlists(spa, &last, &scrub);
	scrub_finished = spa->spa_scrub_finished;
	spa->spa_scrub_finished = B_FALSE;

	mutex_exit(&spa->spa_errlist_lock);
	mutex_enter(&spa->spa_errlog_lock);

	tx = dmu_tx_create_assigned(spa->spa_dsl_pool, txg);

	/*
	 * Sync out the current list of errors.
	 */
	sync_error_list(spa, &last, &spa->spa_errlog_last, tx);

	/*
	 * Rotate the log if necessary.
	 */
	if (scrub_finished) {
		if (spa->spa_errlog_last != 0)
			VERIFY(dmu_object_free(spa->spa_meta_objset,
			    spa->spa_errlog_last, tx) == 0);
		spa->spa_errlog_last = spa->spa_errlog_scrub;
		spa->spa_errlog_scrub = 0;

		sync_error_list(spa, &scrub, &spa->spa_errlog_last, tx);
	}

	/*
	 * Sync out any pending scrub errors.
	 */
	sync_error_list(spa, &scrub, &spa->spa_errlog_scrub, tx);

	/*
	 * Update the MOS to reflect the new values.
	 */
	(void) zap_update(spa->spa_meta_objset, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_ERRLOG_LAST, sizeof (uint64_t), 1,
	    &spa->spa_errlog_last, tx);
	(void) zap_update(spa->spa_meta_objset, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_ERRLOG_SCRUB, sizeof (uint64_t), 1,
	    &spa->spa_errlog_scrub, tx);

	dmu_tx_commit(tx);

	mutex_exit(&spa->spa_errlog_lock);
}
