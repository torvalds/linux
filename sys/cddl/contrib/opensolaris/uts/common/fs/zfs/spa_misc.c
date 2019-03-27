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
 * Copyright (c) 2011, 2018 by Delphix. All rights reserved.
 * Copyright 2015 Nexenta Systems, Inc.  All rights reserved.
 * Copyright 2013 Martin Matuska <mm@FreeBSD.org>. All rights reserved.
 * Copyright (c) 2014 Spectra Logic Corporation, All rights reserved.
 * Copyright 2013 Saso Kiselkov. All rights reserved.
 * Copyright (c) 2014 Integros [integros.com]
 * Copyright (c) 2017 Datto Inc.
 */

#include <sys/zfs_context.h>
#include <sys/spa_impl.h>
#include <sys/spa_boot.h>
#include <sys/zio.h>
#include <sys/zio_checksum.h>
#include <sys/zio_compress.h>
#include <sys/dmu.h>
#include <sys/dmu_tx.h>
#include <sys/zap.h>
#include <sys/zil.h>
#include <sys/vdev_impl.h>
#include <sys/vdev_file.h>
#include <sys/vdev_initialize.h>
#include <sys/metaslab.h>
#include <sys/uberblock_impl.h>
#include <sys/txg.h>
#include <sys/avl.h>
#include <sys/unique.h>
#include <sys/dsl_pool.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_prop.h>
#include <sys/dsl_scan.h>
#include <sys/fs/zfs.h>
#include <sys/metaslab_impl.h>
#include <sys/arc.h>
#include <sys/ddt.h>
#include "zfs_prop.h"
#include <sys/zfeature.h>

#if defined(__FreeBSD__) && defined(_KERNEL)
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

/*
 * SPA locking
 *
 * There are four basic locks for managing spa_t structures:
 *
 * spa_namespace_lock (global mutex)
 *
 *	This lock must be acquired to do any of the following:
 *
 *		- Lookup a spa_t by name
 *		- Add or remove a spa_t from the namespace
 *		- Increase spa_refcount from non-zero
 *		- Check if spa_refcount is zero
 *		- Rename a spa_t
 *		- add/remove/attach/detach devices
 *		- Held for the duration of create/destroy/import/export
 *
 *	It does not need to handle recursion.  A create or destroy may
 *	reference objects (files or zvols) in other pools, but by
 *	definition they must have an existing reference, and will never need
 *	to lookup a spa_t by name.
 *
 * spa_refcount (per-spa refcount_t protected by mutex)
 *
 *	This reference count keep track of any active users of the spa_t.  The
 *	spa_t cannot be destroyed or freed while this is non-zero.  Internally,
 *	the refcount is never really 'zero' - opening a pool implicitly keeps
 *	some references in the DMU.  Internally we check against spa_minref, but
 *	present the image of a zero/non-zero value to consumers.
 *
 * spa_config_lock[] (per-spa array of rwlocks)
 *
 *	This protects the spa_t from config changes, and must be held in
 *	the following circumstances:
 *
 *		- RW_READER to perform I/O to the spa
 *		- RW_WRITER to change the vdev config
 *
 * The locking order is fairly straightforward:
 *
 *		spa_namespace_lock	->	spa_refcount
 *
 *	The namespace lock must be acquired to increase the refcount from 0
 *	or to check if it is zero.
 *
 *		spa_refcount		->	spa_config_lock[]
 *
 *	There must be at least one valid reference on the spa_t to acquire
 *	the config lock.
 *
 *		spa_namespace_lock	->	spa_config_lock[]
 *
 *	The namespace lock must always be taken before the config lock.
 *
 *
 * The spa_namespace_lock can be acquired directly and is globally visible.
 *
 * The namespace is manipulated using the following functions, all of which
 * require the spa_namespace_lock to be held.
 *
 *	spa_lookup()		Lookup a spa_t by name.
 *
 *	spa_add()		Create a new spa_t in the namespace.
 *
 *	spa_remove()		Remove a spa_t from the namespace.  This also
 *				frees up any memory associated with the spa_t.
 *
 *	spa_next()		Returns the next spa_t in the system, or the
 *				first if NULL is passed.
 *
 *	spa_evict_all()		Shutdown and remove all spa_t structures in
 *				the system.
 *
 *	spa_guid_exists()	Determine whether a pool/device guid exists.
 *
 * The spa_refcount is manipulated using the following functions:
 *
 *	spa_open_ref()		Adds a reference to the given spa_t.  Must be
 *				called with spa_namespace_lock held if the
 *				refcount is currently zero.
 *
 *	spa_close()		Remove a reference from the spa_t.  This will
 *				not free the spa_t or remove it from the
 *				namespace.  No locking is required.
 *
 *	spa_refcount_zero()	Returns true if the refcount is currently
 *				zero.  Must be called with spa_namespace_lock
 *				held.
 *
 * The spa_config_lock[] is an array of rwlocks, ordered as follows:
 * SCL_CONFIG > SCL_STATE > SCL_ALLOC > SCL_ZIO > SCL_FREE > SCL_VDEV.
 * spa_config_lock[] is manipulated with spa_config_{enter,exit,held}().
 *
 * To read the configuration, it suffices to hold one of these locks as reader.
 * To modify the configuration, you must hold all locks as writer.  To modify
 * vdev state without altering the vdev tree's topology (e.g. online/offline),
 * you must hold SCL_STATE and SCL_ZIO as writer.
 *
 * We use these distinct config locks to avoid recursive lock entry.
 * For example, spa_sync() (which holds SCL_CONFIG as reader) induces
 * block allocations (SCL_ALLOC), which may require reading space maps
 * from disk (dmu_read() -> zio_read() -> SCL_ZIO).
 *
 * The spa config locks cannot be normal rwlocks because we need the
 * ability to hand off ownership.  For example, SCL_ZIO is acquired
 * by the issuing thread and later released by an interrupt thread.
 * They do, however, obey the usual write-wanted semantics to prevent
 * writer (i.e. system administrator) starvation.
 *
 * The lock acquisition rules are as follows:
 *
 * SCL_CONFIG
 *	Protects changes to the vdev tree topology, such as vdev
 *	add/remove/attach/detach.  Protects the dirty config list
 *	(spa_config_dirty_list) and the set of spares and l2arc devices.
 *
 * SCL_STATE
 *	Protects changes to pool state and vdev state, such as vdev
 *	online/offline/fault/degrade/clear.  Protects the dirty state list
 *	(spa_state_dirty_list) and global pool state (spa_state).
 *
 * SCL_ALLOC
 *	Protects changes to metaslab groups and classes.
 *	Held as reader by metaslab_alloc() and metaslab_claim().
 *
 * SCL_ZIO
 *	Held by bp-level zios (those which have no io_vd upon entry)
 *	to prevent changes to the vdev tree.  The bp-level zio implicitly
 *	protects all of its vdev child zios, which do not hold SCL_ZIO.
 *
 * SCL_FREE
 *	Protects changes to metaslab groups and classes.
 *	Held as reader by metaslab_free().  SCL_FREE is distinct from
 *	SCL_ALLOC, and lower than SCL_ZIO, so that we can safely free
 *	blocks in zio_done() while another i/o that holds either
 *	SCL_ALLOC or SCL_ZIO is waiting for this i/o to complete.
 *
 * SCL_VDEV
 *	Held as reader to prevent changes to the vdev tree during trivial
 *	inquiries such as bp_get_dsize().  SCL_VDEV is distinct from the
 *	other locks, and lower than all of them, to ensure that it's safe
 *	to acquire regardless of caller context.
 *
 * In addition, the following rules apply:
 *
 * (a)	spa_props_lock protects pool properties, spa_config and spa_config_list.
 *	The lock ordering is SCL_CONFIG > spa_props_lock.
 *
 * (b)	I/O operations on leaf vdevs.  For any zio operation that takes
 *	an explicit vdev_t argument -- such as zio_ioctl(), zio_read_phys(),
 *	or zio_write_phys() -- the caller must ensure that the config cannot
 *	cannot change in the interim, and that the vdev cannot be reopened.
 *	SCL_STATE as reader suffices for both.
 *
 * The vdev configuration is protected by spa_vdev_enter() / spa_vdev_exit().
 *
 *	spa_vdev_enter()	Acquire the namespace lock and the config lock
 *				for writing.
 *
 *	spa_vdev_exit()		Release the config lock, wait for all I/O
 *				to complete, sync the updated configs to the
 *				cache, and release the namespace lock.
 *
 * vdev state is protected by spa_vdev_state_enter() / spa_vdev_state_exit().
 * Like spa_vdev_enter/exit, these are convenience wrappers -- the actual
 * locking is, always, based on spa_namespace_lock and spa_config_lock[].
 *
 * spa_rename() is also implemented within this file since it requires
 * manipulation of the namespace.
 */

static avl_tree_t spa_namespace_avl;
kmutex_t spa_namespace_lock;
static kcondvar_t spa_namespace_cv;
static int spa_active_count;
int spa_max_replication_override = SPA_DVAS_PER_BP;

static kmutex_t spa_spare_lock;
static avl_tree_t spa_spare_avl;
static kmutex_t spa_l2cache_lock;
static avl_tree_t spa_l2cache_avl;

kmem_cache_t *spa_buffer_pool;
int spa_mode_global;

#ifdef ZFS_DEBUG
/*
 * Everything except dprintf, spa, and indirect_remap is on by default
 * in debug builds.
 */
int zfs_flags = ~(ZFS_DEBUG_DPRINTF | ZFS_DEBUG_INDIRECT_REMAP);
#else
int zfs_flags = 0;
#endif

/*
 * zfs_recover can be set to nonzero to attempt to recover from
 * otherwise-fatal errors, typically caused by on-disk corruption.  When
 * set, calls to zfs_panic_recover() will turn into warning messages.
 * This should only be used as a last resort, as it typically results
 * in leaked space, or worse.
 */
boolean_t zfs_recover = B_FALSE;

/*
 * If destroy encounters an EIO while reading metadata (e.g. indirect
 * blocks), space referenced by the missing metadata can not be freed.
 * Normally this causes the background destroy to become "stalled", as
 * it is unable to make forward progress.  While in this stalled state,
 * all remaining space to free from the error-encountering filesystem is
 * "temporarily leaked".  Set this flag to cause it to ignore the EIO,
 * permanently leak the space from indirect blocks that can not be read,
 * and continue to free everything else that it can.
 *
 * The default, "stalling" behavior is useful if the storage partially
 * fails (i.e. some but not all i/os fail), and then later recovers.  In
 * this case, we will be able to continue pool operations while it is
 * partially failed, and when it recovers, we can continue to free the
 * space, with no leaks.  However, note that this case is actually
 * fairly rare.
 *
 * Typically pools either (a) fail completely (but perhaps temporarily,
 * e.g. a top-level vdev going offline), or (b) have localized,
 * permanent errors (e.g. disk returns the wrong data due to bit flip or
 * firmware bug).  In case (a), this setting does not matter because the
 * pool will be suspended and the sync thread will not be able to make
 * forward progress regardless.  In case (b), because the error is
 * permanent, the best we can do is leak the minimum amount of space,
 * which is what setting this flag will do.  Therefore, it is reasonable
 * for this flag to normally be set, but we chose the more conservative
 * approach of not setting it, so that there is no possibility of
 * leaking space in the "partial temporary" failure case.
 */
boolean_t zfs_free_leak_on_eio = B_FALSE;

/*
 * Expiration time in milliseconds. This value has two meanings. First it is
 * used to determine when the spa_deadman() logic should fire. By default the
 * spa_deadman() will fire if spa_sync() has not completed in 1000 seconds.
 * Secondly, the value determines if an I/O is considered "hung". Any I/O that
 * has not completed in zfs_deadman_synctime_ms is considered "hung" resulting
 * in a system panic.
 */
uint64_t zfs_deadman_synctime_ms = 1000000ULL;

/*
 * Check time in milliseconds. This defines the frequency at which we check
 * for hung I/O.
 */
uint64_t zfs_deadman_checktime_ms = 5000ULL;

/*
 * Default value of -1 for zfs_deadman_enabled is resolved in
 * zfs_deadman_init()
 */
int zfs_deadman_enabled = -1;

/*
 * The worst case is single-sector max-parity RAID-Z blocks, in which
 * case the space requirement is exactly (VDEV_RAIDZ_MAXPARITY + 1)
 * times the size; so just assume that.  Add to this the fact that
 * we can have up to 3 DVAs per bp, and one more factor of 2 because
 * the block may be dittoed with up to 3 DVAs by ddt_sync().  All together,
 * the worst case is:
 *     (VDEV_RAIDZ_MAXPARITY + 1) * SPA_DVAS_PER_BP * 2 == 24
 */
int spa_asize_inflation = 24;

#if defined(__FreeBSD__) && defined(_KERNEL)
SYSCTL_DECL(_vfs_zfs);
SYSCTL_INT(_vfs_zfs, OID_AUTO, recover, CTLFLAG_RWTUN, &zfs_recover, 0,
    "Try to recover from otherwise-fatal errors.");

static int
sysctl_vfs_zfs_debug_flags(SYSCTL_HANDLER_ARGS)
{
	int err, val;

	val = zfs_flags;
	err = sysctl_handle_int(oidp, &val, 0, req);
	if (err != 0 || req->newptr == NULL)
		return (err);

	/*
	 * ZFS_DEBUG_MODIFY must be enabled prior to boot so all
	 * arc buffers in the system have the necessary additional
	 * checksum data.  However, it is safe to disable at any
	 * time.
	 */
	if (!(zfs_flags & ZFS_DEBUG_MODIFY))
		val &= ~ZFS_DEBUG_MODIFY;
	zfs_flags = val;

	return (0);
}

SYSCTL_PROC(_vfs_zfs, OID_AUTO, debugflags,
    CTLTYPE_UINT | CTLFLAG_MPSAFE | CTLFLAG_RWTUN, 0, sizeof(int),
    sysctl_vfs_zfs_debug_flags, "IU", "Debug flags for ZFS testing.");

SYSCTL_UQUAD(_vfs_zfs, OID_AUTO, deadman_synctime_ms, CTLFLAG_RDTUN,
    &zfs_deadman_synctime_ms, 0,
    "Stalled ZFS I/O expiration time in milliseconds");
SYSCTL_UQUAD(_vfs_zfs, OID_AUTO, deadman_checktime_ms, CTLFLAG_RDTUN,
    &zfs_deadman_checktime_ms, 0,
    "Period of checks for stalled ZFS I/O in milliseconds");
SYSCTL_INT(_vfs_zfs, OID_AUTO, deadman_enabled, CTLFLAG_RDTUN,
    &zfs_deadman_enabled, 0, "Kernel panic on stalled ZFS I/O");
SYSCTL_INT(_vfs_zfs, OID_AUTO, spa_asize_inflation, CTLFLAG_RWTUN,
    &spa_asize_inflation, 0, "Worst case inflation factor for single sector writes");
#endif

#ifndef illumos
#ifdef _KERNEL
static void
zfs_deadman_init()
{
	/*
	 * If we are not i386 or amd64 or in a virtual machine,
	 * disable ZFS deadman thread by default
	 */
	if (zfs_deadman_enabled == -1) {
#if defined(__amd64__) || defined(__i386__)
		zfs_deadman_enabled = (vm_guest == VM_GUEST_NO) ? 1 : 0;
#else
		zfs_deadman_enabled = 0;
#endif
	}
}
#endif	/* _KERNEL */
#endif	/* !illumos */

/*
 * Normally, we don't allow the last 3.2% (1/(2^spa_slop_shift)) of space in
 * the pool to be consumed.  This ensures that we don't run the pool
 * completely out of space, due to unaccounted changes (e.g. to the MOS).
 * It also limits the worst-case time to allocate space.  If we have
 * less than this amount of free space, most ZPL operations (e.g. write,
 * create) will return ENOSPC.
 *
 * Certain operations (e.g. file removal, most administrative actions) can
 * use half the slop space.  They will only return ENOSPC if less than half
 * the slop space is free.  Typically, once the pool has less than the slop
 * space free, the user will use these operations to free up space in the pool.
 * These are the operations that call dsl_pool_adjustedsize() with the netfree
 * argument set to TRUE.
 *
 * Operations that are almost guaranteed to free up space in the absence of
 * a pool checkpoint can use up to three quarters of the slop space
 * (e.g zfs destroy).
 *
 * A very restricted set of operations are always permitted, regardless of
 * the amount of free space.  These are the operations that call
 * dsl_sync_task(ZFS_SPACE_CHECK_NONE). If these operations result in a net
 * increase in the amount of space used, it is possible to run the pool
 * completely out of space, causing it to be permanently read-only.
 *
 * Note that on very small pools, the slop space will be larger than
 * 3.2%, in an effort to have it be at least spa_min_slop (128MB),
 * but we never allow it to be more than half the pool size.
 *
 * See also the comments in zfs_space_check_t.
 */
int spa_slop_shift = 5;
SYSCTL_INT(_vfs_zfs, OID_AUTO, spa_slop_shift, CTLFLAG_RWTUN,
    &spa_slop_shift, 0,
    "Shift value of reserved space (1/(2^spa_slop_shift)).");
uint64_t spa_min_slop = 128 * 1024 * 1024;
SYSCTL_UQUAD(_vfs_zfs, OID_AUTO, spa_min_slop, CTLFLAG_RWTUN,
    &spa_min_slop, 0,
    "Minimal value of reserved space");

int spa_allocators = 4;

SYSCTL_INT(_vfs_zfs, OID_AUTO, spa_allocators, CTLFLAG_RWTUN,
    &spa_allocators, 0,
    "Number of allocators per metaslab group");

/*PRINTFLIKE2*/
void
spa_load_failed(spa_t *spa, const char *fmt, ...)
{
	va_list adx;
	char buf[256];

	va_start(adx, fmt);
	(void) vsnprintf(buf, sizeof (buf), fmt, adx);
	va_end(adx);

	zfs_dbgmsg("spa_load(%s, config %s): FAILED: %s", spa->spa_name,
	    spa->spa_trust_config ? "trusted" : "untrusted", buf);
}

/*PRINTFLIKE2*/
void
spa_load_note(spa_t *spa, const char *fmt, ...)
{
	va_list adx;
	char buf[256];

	va_start(adx, fmt);
	(void) vsnprintf(buf, sizeof (buf), fmt, adx);
	va_end(adx);

	zfs_dbgmsg("spa_load(%s, config %s): %s", spa->spa_name,
	    spa->spa_trust_config ? "trusted" : "untrusted", buf);
}

/*
 * ==========================================================================
 * SPA config locking
 * ==========================================================================
 */
static void
spa_config_lock_init(spa_t *spa)
{
	for (int i = 0; i < SCL_LOCKS; i++) {
		spa_config_lock_t *scl = &spa->spa_config_lock[i];
		mutex_init(&scl->scl_lock, NULL, MUTEX_DEFAULT, NULL);
		cv_init(&scl->scl_cv, NULL, CV_DEFAULT, NULL);
		refcount_create_untracked(&scl->scl_count);
		scl->scl_writer = NULL;
		scl->scl_write_wanted = 0;
	}
}

static void
spa_config_lock_destroy(spa_t *spa)
{
	for (int i = 0; i < SCL_LOCKS; i++) {
		spa_config_lock_t *scl = &spa->spa_config_lock[i];
		mutex_destroy(&scl->scl_lock);
		cv_destroy(&scl->scl_cv);
		refcount_destroy(&scl->scl_count);
		ASSERT(scl->scl_writer == NULL);
		ASSERT(scl->scl_write_wanted == 0);
	}
}

int
spa_config_tryenter(spa_t *spa, int locks, void *tag, krw_t rw)
{
	for (int i = 0; i < SCL_LOCKS; i++) {
		spa_config_lock_t *scl = &spa->spa_config_lock[i];
		if (!(locks & (1 << i)))
			continue;
		mutex_enter(&scl->scl_lock);
		if (rw == RW_READER) {
			if (scl->scl_writer || scl->scl_write_wanted) {
				mutex_exit(&scl->scl_lock);
				spa_config_exit(spa, locks & ((1 << i) - 1),
				    tag);
				return (0);
			}
		} else {
			ASSERT(scl->scl_writer != curthread);
			if (!refcount_is_zero(&scl->scl_count)) {
				mutex_exit(&scl->scl_lock);
				spa_config_exit(spa, locks & ((1 << i) - 1),
				    tag);
				return (0);
			}
			scl->scl_writer = curthread;
		}
		(void) refcount_add(&scl->scl_count, tag);
		mutex_exit(&scl->scl_lock);
	}
	return (1);
}

void
spa_config_enter(spa_t *spa, int locks, void *tag, krw_t rw)
{
	int wlocks_held = 0;

	ASSERT3U(SCL_LOCKS, <, sizeof (wlocks_held) * NBBY);

	for (int i = 0; i < SCL_LOCKS; i++) {
		spa_config_lock_t *scl = &spa->spa_config_lock[i];
		if (scl->scl_writer == curthread)
			wlocks_held |= (1 << i);
		if (!(locks & (1 << i)))
			continue;
		mutex_enter(&scl->scl_lock);
		if (rw == RW_READER) {
			while (scl->scl_writer || scl->scl_write_wanted) {
				cv_wait(&scl->scl_cv, &scl->scl_lock);
			}
		} else {
			ASSERT(scl->scl_writer != curthread);
			while (!refcount_is_zero(&scl->scl_count)) {
				scl->scl_write_wanted++;
				cv_wait(&scl->scl_cv, &scl->scl_lock);
				scl->scl_write_wanted--;
			}
			scl->scl_writer = curthread;
		}
		(void) refcount_add(&scl->scl_count, tag);
		mutex_exit(&scl->scl_lock);
	}
	ASSERT3U(wlocks_held, <=, locks);
}

void
spa_config_exit(spa_t *spa, int locks, void *tag)
{
	for (int i = SCL_LOCKS - 1; i >= 0; i--) {
		spa_config_lock_t *scl = &spa->spa_config_lock[i];
		if (!(locks & (1 << i)))
			continue;
		mutex_enter(&scl->scl_lock);
		ASSERT(!refcount_is_zero(&scl->scl_count));
		if (refcount_remove(&scl->scl_count, tag) == 0) {
			ASSERT(scl->scl_writer == NULL ||
			    scl->scl_writer == curthread);
			scl->scl_writer = NULL;	/* OK in either case */
			cv_broadcast(&scl->scl_cv);
		}
		mutex_exit(&scl->scl_lock);
	}
}

int
spa_config_held(spa_t *spa, int locks, krw_t rw)
{
	int locks_held = 0;

	for (int i = 0; i < SCL_LOCKS; i++) {
		spa_config_lock_t *scl = &spa->spa_config_lock[i];
		if (!(locks & (1 << i)))
			continue;
		if ((rw == RW_READER && !refcount_is_zero(&scl->scl_count)) ||
		    (rw == RW_WRITER && scl->scl_writer == curthread))
			locks_held |= 1 << i;
	}

	return (locks_held);
}

/*
 * ==========================================================================
 * SPA namespace functions
 * ==========================================================================
 */

/*
 * Lookup the named spa_t in the AVL tree.  The spa_namespace_lock must be held.
 * Returns NULL if no matching spa_t is found.
 */
spa_t *
spa_lookup(const char *name)
{
	static spa_t search;	/* spa_t is large; don't allocate on stack */
	spa_t *spa;
	avl_index_t where;
	char *cp;

	ASSERT(MUTEX_HELD(&spa_namespace_lock));

	(void) strlcpy(search.spa_name, name, sizeof (search.spa_name));

	/*
	 * If it's a full dataset name, figure out the pool name and
	 * just use that.
	 */
	cp = strpbrk(search.spa_name, "/@#");
	if (cp != NULL)
		*cp = '\0';

	spa = avl_find(&spa_namespace_avl, &search, &where);

	return (spa);
}

/*
 * Fires when spa_sync has not completed within zfs_deadman_synctime_ms.
 * If the zfs_deadman_enabled flag is set then it inspects all vdev queues
 * looking for potentially hung I/Os.
 */
static void
spa_deadman(void *arg, int pending)
{
	spa_t *spa = arg;

	/*
	 * Disable the deadman timer if the pool is suspended.
	 */
	if (spa_suspended(spa)) {
#ifdef illumos
		VERIFY(cyclic_reprogram(spa->spa_deadman_cycid, CY_INFINITY));
#else
		/* Nothing.  just don't schedule any future callouts. */
#endif
		return;
	}

	zfs_dbgmsg("slow spa_sync: started %llu seconds ago, calls %llu",
	    (gethrtime() - spa->spa_sync_starttime) / NANOSEC,
	    ++spa->spa_deadman_calls);
	if (zfs_deadman_enabled)
		vdev_deadman(spa->spa_root_vdev);
#ifdef __FreeBSD__
#ifdef _KERNEL
	callout_schedule(&spa->spa_deadman_cycid,
	    hz * zfs_deadman_checktime_ms / MILLISEC);
#endif
#endif
}

#if defined(__FreeBSD__) && defined(_KERNEL)
static void
spa_deadman_timeout(void *arg)
{
	spa_t *spa = arg;

	taskqueue_enqueue(taskqueue_thread, &spa->spa_deadman_task);
}
#endif

/*
 * Create an uninitialized spa_t with the given name.  Requires
 * spa_namespace_lock.  The caller must ensure that the spa_t doesn't already
 * exist by calling spa_lookup() first.
 */
spa_t *
spa_add(const char *name, nvlist_t *config, const char *altroot)
{
	spa_t *spa;
	spa_config_dirent_t *dp;
#ifdef illumos
	cyc_handler_t hdlr;
	cyc_time_t when;
#endif

	ASSERT(MUTEX_HELD(&spa_namespace_lock));

	spa = kmem_zalloc(sizeof (spa_t), KM_SLEEP);

	mutex_init(&spa->spa_async_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&spa->spa_errlist_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&spa->spa_errlog_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&spa->spa_evicting_os_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&spa->spa_history_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&spa->spa_proc_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&spa->spa_props_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&spa->spa_cksum_tmpls_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&spa->spa_scrub_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&spa->spa_suspend_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&spa->spa_vdev_top_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&spa->spa_feat_stats_lock, NULL, MUTEX_DEFAULT, NULL);

	cv_init(&spa->spa_async_cv, NULL, CV_DEFAULT, NULL);
	cv_init(&spa->spa_evicting_os_cv, NULL, CV_DEFAULT, NULL);
	cv_init(&spa->spa_proc_cv, NULL, CV_DEFAULT, NULL);
	cv_init(&spa->spa_scrub_io_cv, NULL, CV_DEFAULT, NULL);
	cv_init(&spa->spa_suspend_cv, NULL, CV_DEFAULT, NULL);

	for (int t = 0; t < TXG_SIZE; t++)
		bplist_create(&spa->spa_free_bplist[t]);

	(void) strlcpy(spa->spa_name, name, sizeof (spa->spa_name));
	spa->spa_state = POOL_STATE_UNINITIALIZED;
	spa->spa_freeze_txg = UINT64_MAX;
	spa->spa_final_txg = UINT64_MAX;
	spa->spa_load_max_txg = UINT64_MAX;
	spa->spa_proc = &p0;
	spa->spa_proc_state = SPA_PROC_NONE;
	spa->spa_trust_config = B_TRUE;

#ifdef illumos
	hdlr.cyh_func = spa_deadman;
	hdlr.cyh_arg = spa;
	hdlr.cyh_level = CY_LOW_LEVEL;
#endif

	spa->spa_deadman_synctime = MSEC2NSEC(zfs_deadman_synctime_ms);

#ifdef illumos
	/*
	 * This determines how often we need to check for hung I/Os after
	 * the cyclic has already fired. Since checking for hung I/Os is
	 * an expensive operation we don't want to check too frequently.
	 * Instead wait for 5 seconds before checking again.
	 */
	when.cyt_interval = MSEC2NSEC(zfs_deadman_checktime_ms);
	when.cyt_when = CY_INFINITY;
	mutex_enter(&cpu_lock);
	spa->spa_deadman_cycid = cyclic_add(&hdlr, &when);
	mutex_exit(&cpu_lock);
#else	/* !illumos */
#ifdef _KERNEL
	/*
	 * callout(9) does not provide a way to initialize a callout with
	 * a function and an argument, so we use callout_reset() to schedule
	 * the callout in the very distant future.  Even if that event ever
	 * fires, it should be okayas we won't have any active zio-s.
	 * But normally spa_sync() will reschedule the callout with a proper
	 * timeout.
	 * callout(9) does not allow the callback function to sleep but
	 * vdev_deadman() needs to acquire vq_lock and illumos mutexes are
	 * emulated using sx(9).  For this reason spa_deadman_timeout()
	 * will schedule spa_deadman() as task on a taskqueue that allows
	 * sleeping.
	 */
	TASK_INIT(&spa->spa_deadman_task, 0, spa_deadman, spa);
	callout_init(&spa->spa_deadman_cycid, 1);
	callout_reset_sbt(&spa->spa_deadman_cycid, SBT_MAX, 0,
	    spa_deadman_timeout, spa, 0);
#endif
#endif
	refcount_create(&spa->spa_refcount);
	spa_config_lock_init(spa);

	avl_add(&spa_namespace_avl, spa);

	/*
	 * Set the alternate root, if there is one.
	 */
	if (altroot) {
		spa->spa_root = spa_strdup(altroot);
		spa_active_count++;
	}

	spa->spa_alloc_count = spa_allocators;
	spa->spa_alloc_locks = kmem_zalloc(spa->spa_alloc_count *
	    sizeof (kmutex_t), KM_SLEEP);
	spa->spa_alloc_trees = kmem_zalloc(spa->spa_alloc_count *
	    sizeof (avl_tree_t), KM_SLEEP);
	for (int i = 0; i < spa->spa_alloc_count; i++) {
		mutex_init(&spa->spa_alloc_locks[i], NULL, MUTEX_DEFAULT, NULL);
		avl_create(&spa->spa_alloc_trees[i], zio_bookmark_compare,
		    sizeof (zio_t), offsetof(zio_t, io_alloc_node));
	}

	/*
	 * Every pool starts with the default cachefile
	 */
	list_create(&spa->spa_config_list, sizeof (spa_config_dirent_t),
	    offsetof(spa_config_dirent_t, scd_link));

	dp = kmem_zalloc(sizeof (spa_config_dirent_t), KM_SLEEP);
	dp->scd_path = altroot ? NULL : spa_strdup(spa_config_path);
	list_insert_head(&spa->spa_config_list, dp);

	VERIFY(nvlist_alloc(&spa->spa_load_info, NV_UNIQUE_NAME,
	    KM_SLEEP) == 0);

	if (config != NULL) {
		nvlist_t *features;

		if (nvlist_lookup_nvlist(config, ZPOOL_CONFIG_FEATURES_FOR_READ,
		    &features) == 0) {
			VERIFY(nvlist_dup(features, &spa->spa_label_features,
			    0) == 0);
		}

		VERIFY(nvlist_dup(config, &spa->spa_config, 0) == 0);
	}

	if (spa->spa_label_features == NULL) {
		VERIFY(nvlist_alloc(&spa->spa_label_features, NV_UNIQUE_NAME,
		    KM_SLEEP) == 0);
	}

	spa->spa_min_ashift = INT_MAX;
	spa->spa_max_ashift = 0;

	/*
	 * As a pool is being created, treat all features as disabled by
	 * setting SPA_FEATURE_DISABLED for all entries in the feature
	 * refcount cache.
	 */
	for (int i = 0; i < SPA_FEATURES; i++) {
		spa->spa_feat_refcount_cache[i] = SPA_FEATURE_DISABLED;
	}

	return (spa);
}

/*
 * Removes a spa_t from the namespace, freeing up any memory used.  Requires
 * spa_namespace_lock.  This is called only after the spa_t has been closed and
 * deactivated.
 */
void
spa_remove(spa_t *spa)
{
	spa_config_dirent_t *dp;

	ASSERT(MUTEX_HELD(&spa_namespace_lock));
	ASSERT(spa->spa_state == POOL_STATE_UNINITIALIZED);
	ASSERT3U(refcount_count(&spa->spa_refcount), ==, 0);

	nvlist_free(spa->spa_config_splitting);

	avl_remove(&spa_namespace_avl, spa);
	cv_broadcast(&spa_namespace_cv);

	if (spa->spa_root) {
		spa_strfree(spa->spa_root);
		spa_active_count--;
	}

	while ((dp = list_head(&spa->spa_config_list)) != NULL) {
		list_remove(&spa->spa_config_list, dp);
		if (dp->scd_path != NULL)
			spa_strfree(dp->scd_path);
		kmem_free(dp, sizeof (spa_config_dirent_t));
	}

	for (int i = 0; i < spa->spa_alloc_count; i++) {
		avl_destroy(&spa->spa_alloc_trees[i]);
		mutex_destroy(&spa->spa_alloc_locks[i]);
	}
	kmem_free(spa->spa_alloc_locks, spa->spa_alloc_count *
	    sizeof (kmutex_t));
	kmem_free(spa->spa_alloc_trees, spa->spa_alloc_count *
	    sizeof (avl_tree_t));

	list_destroy(&spa->spa_config_list);

	nvlist_free(spa->spa_label_features);
	nvlist_free(spa->spa_load_info);
	nvlist_free(spa->spa_feat_stats);
	spa_config_set(spa, NULL);

#ifdef illumos
	mutex_enter(&cpu_lock);
	if (spa->spa_deadman_cycid != CYCLIC_NONE)
		cyclic_remove(spa->spa_deadman_cycid);
	mutex_exit(&cpu_lock);
	spa->spa_deadman_cycid = CYCLIC_NONE;
#else	/* !illumos */
#ifdef _KERNEL
	callout_drain(&spa->spa_deadman_cycid);
	taskqueue_drain(taskqueue_thread, &spa->spa_deadman_task);
#endif
#endif

	refcount_destroy(&spa->spa_refcount);

	spa_config_lock_destroy(spa);

	for (int t = 0; t < TXG_SIZE; t++)
		bplist_destroy(&spa->spa_free_bplist[t]);

	zio_checksum_templates_free(spa);

	cv_destroy(&spa->spa_async_cv);
	cv_destroy(&spa->spa_evicting_os_cv);
	cv_destroy(&spa->spa_proc_cv);
	cv_destroy(&spa->spa_scrub_io_cv);
	cv_destroy(&spa->spa_suspend_cv);

	mutex_destroy(&spa->spa_async_lock);
	mutex_destroy(&spa->spa_errlist_lock);
	mutex_destroy(&spa->spa_errlog_lock);
	mutex_destroy(&spa->spa_evicting_os_lock);
	mutex_destroy(&spa->spa_history_lock);
	mutex_destroy(&spa->spa_proc_lock);
	mutex_destroy(&spa->spa_props_lock);
	mutex_destroy(&spa->spa_cksum_tmpls_lock);
	mutex_destroy(&spa->spa_scrub_lock);
	mutex_destroy(&spa->spa_suspend_lock);
	mutex_destroy(&spa->spa_vdev_top_lock);
	mutex_destroy(&spa->spa_feat_stats_lock);

	kmem_free(spa, sizeof (spa_t));
}

/*
 * Given a pool, return the next pool in the namespace, or NULL if there is
 * none.  If 'prev' is NULL, return the first pool.
 */
spa_t *
spa_next(spa_t *prev)
{
	ASSERT(MUTEX_HELD(&spa_namespace_lock));

	if (prev)
		return (AVL_NEXT(&spa_namespace_avl, prev));
	else
		return (avl_first(&spa_namespace_avl));
}

/*
 * ==========================================================================
 * SPA refcount functions
 * ==========================================================================
 */

/*
 * Add a reference to the given spa_t.  Must have at least one reference, or
 * have the namespace lock held.
 */
void
spa_open_ref(spa_t *spa, void *tag)
{
	ASSERT(refcount_count(&spa->spa_refcount) >= spa->spa_minref ||
	    MUTEX_HELD(&spa_namespace_lock));
	(void) refcount_add(&spa->spa_refcount, tag);
}

/*
 * Remove a reference to the given spa_t.  Must have at least one reference, or
 * have the namespace lock held.
 */
void
spa_close(spa_t *spa, void *tag)
{
	ASSERT(refcount_count(&spa->spa_refcount) > spa->spa_minref ||
	    MUTEX_HELD(&spa_namespace_lock));
	(void) refcount_remove(&spa->spa_refcount, tag);
}

/*
 * Remove a reference to the given spa_t held by a dsl dir that is
 * being asynchronously released.  Async releases occur from a taskq
 * performing eviction of dsl datasets and dirs.  The namespace lock
 * isn't held and the hold by the object being evicted may contribute to
 * spa_minref (e.g. dataset or directory released during pool export),
 * so the asserts in spa_close() do not apply.
 */
void
spa_async_close(spa_t *spa, void *tag)
{
	(void) refcount_remove(&spa->spa_refcount, tag);
}

/*
 * Check to see if the spa refcount is zero.  Must be called with
 * spa_namespace_lock held.  We really compare against spa_minref, which is the
 * number of references acquired when opening a pool
 */
boolean_t
spa_refcount_zero(spa_t *spa)
{
	ASSERT(MUTEX_HELD(&spa_namespace_lock));

	return (refcount_count(&spa->spa_refcount) == spa->spa_minref);
}

/*
 * ==========================================================================
 * SPA spare and l2cache tracking
 * ==========================================================================
 */

/*
 * Hot spares and cache devices are tracked using the same code below,
 * for 'auxiliary' devices.
 */

typedef struct spa_aux {
	uint64_t	aux_guid;
	uint64_t	aux_pool;
	avl_node_t	aux_avl;
	int		aux_count;
} spa_aux_t;

static inline int
spa_aux_compare(const void *a, const void *b)
{
	const spa_aux_t *sa = (const spa_aux_t *)a;
	const spa_aux_t *sb = (const spa_aux_t *)b;

	return (AVL_CMP(sa->aux_guid, sb->aux_guid));
}

void
spa_aux_add(vdev_t *vd, avl_tree_t *avl)
{
	avl_index_t where;
	spa_aux_t search;
	spa_aux_t *aux;

	search.aux_guid = vd->vdev_guid;
	if ((aux = avl_find(avl, &search, &where)) != NULL) {
		aux->aux_count++;
	} else {
		aux = kmem_zalloc(sizeof (spa_aux_t), KM_SLEEP);
		aux->aux_guid = vd->vdev_guid;
		aux->aux_count = 1;
		avl_insert(avl, aux, where);
	}
}

void
spa_aux_remove(vdev_t *vd, avl_tree_t *avl)
{
	spa_aux_t search;
	spa_aux_t *aux;
	avl_index_t where;

	search.aux_guid = vd->vdev_guid;
	aux = avl_find(avl, &search, &where);

	ASSERT(aux != NULL);

	if (--aux->aux_count == 0) {
		avl_remove(avl, aux);
		kmem_free(aux, sizeof (spa_aux_t));
	} else if (aux->aux_pool == spa_guid(vd->vdev_spa)) {
		aux->aux_pool = 0ULL;
	}
}

boolean_t
spa_aux_exists(uint64_t guid, uint64_t *pool, int *refcnt, avl_tree_t *avl)
{
	spa_aux_t search, *found;

	search.aux_guid = guid;
	found = avl_find(avl, &search, NULL);

	if (pool) {
		if (found)
			*pool = found->aux_pool;
		else
			*pool = 0ULL;
	}

	if (refcnt) {
		if (found)
			*refcnt = found->aux_count;
		else
			*refcnt = 0;
	}

	return (found != NULL);
}

void
spa_aux_activate(vdev_t *vd, avl_tree_t *avl)
{
	spa_aux_t search, *found;
	avl_index_t where;

	search.aux_guid = vd->vdev_guid;
	found = avl_find(avl, &search, &where);
	ASSERT(found != NULL);
	ASSERT(found->aux_pool == 0ULL);

	found->aux_pool = spa_guid(vd->vdev_spa);
}

/*
 * Spares are tracked globally due to the following constraints:
 *
 * 	- A spare may be part of multiple pools.
 * 	- A spare may be added to a pool even if it's actively in use within
 *	  another pool.
 * 	- A spare in use in any pool can only be the source of a replacement if
 *	  the target is a spare in the same pool.
 *
 * We keep track of all spares on the system through the use of a reference
 * counted AVL tree.  When a vdev is added as a spare, or used as a replacement
 * spare, then we bump the reference count in the AVL tree.  In addition, we set
 * the 'vdev_isspare' member to indicate that the device is a spare (active or
 * inactive).  When a spare is made active (used to replace a device in the
 * pool), we also keep track of which pool its been made a part of.
 *
 * The 'spa_spare_lock' protects the AVL tree.  These functions are normally
 * called under the spa_namespace lock as part of vdev reconfiguration.  The
 * separate spare lock exists for the status query path, which does not need to
 * be completely consistent with respect to other vdev configuration changes.
 */

static int
spa_spare_compare(const void *a, const void *b)
{
	return (spa_aux_compare(a, b));
}

void
spa_spare_add(vdev_t *vd)
{
	mutex_enter(&spa_spare_lock);
	ASSERT(!vd->vdev_isspare);
	spa_aux_add(vd, &spa_spare_avl);
	vd->vdev_isspare = B_TRUE;
	mutex_exit(&spa_spare_lock);
}

void
spa_spare_remove(vdev_t *vd)
{
	mutex_enter(&spa_spare_lock);
	ASSERT(vd->vdev_isspare);
	spa_aux_remove(vd, &spa_spare_avl);
	vd->vdev_isspare = B_FALSE;
	mutex_exit(&spa_spare_lock);
}

boolean_t
spa_spare_exists(uint64_t guid, uint64_t *pool, int *refcnt)
{
	boolean_t found;

	mutex_enter(&spa_spare_lock);
	found = spa_aux_exists(guid, pool, refcnt, &spa_spare_avl);
	mutex_exit(&spa_spare_lock);

	return (found);
}

void
spa_spare_activate(vdev_t *vd)
{
	mutex_enter(&spa_spare_lock);
	ASSERT(vd->vdev_isspare);
	spa_aux_activate(vd, &spa_spare_avl);
	mutex_exit(&spa_spare_lock);
}

/*
 * Level 2 ARC devices are tracked globally for the same reasons as spares.
 * Cache devices currently only support one pool per cache device, and so
 * for these devices the aux reference count is currently unused beyond 1.
 */

static int
spa_l2cache_compare(const void *a, const void *b)
{
	return (spa_aux_compare(a, b));
}

void
spa_l2cache_add(vdev_t *vd)
{
	mutex_enter(&spa_l2cache_lock);
	ASSERT(!vd->vdev_isl2cache);
	spa_aux_add(vd, &spa_l2cache_avl);
	vd->vdev_isl2cache = B_TRUE;
	mutex_exit(&spa_l2cache_lock);
}

void
spa_l2cache_remove(vdev_t *vd)
{
	mutex_enter(&spa_l2cache_lock);
	ASSERT(vd->vdev_isl2cache);
	spa_aux_remove(vd, &spa_l2cache_avl);
	vd->vdev_isl2cache = B_FALSE;
	mutex_exit(&spa_l2cache_lock);
}

boolean_t
spa_l2cache_exists(uint64_t guid, uint64_t *pool)
{
	boolean_t found;

	mutex_enter(&spa_l2cache_lock);
	found = spa_aux_exists(guid, pool, NULL, &spa_l2cache_avl);
	mutex_exit(&spa_l2cache_lock);

	return (found);
}

void
spa_l2cache_activate(vdev_t *vd)
{
	mutex_enter(&spa_l2cache_lock);
	ASSERT(vd->vdev_isl2cache);
	spa_aux_activate(vd, &spa_l2cache_avl);
	mutex_exit(&spa_l2cache_lock);
}

/*
 * ==========================================================================
 * SPA vdev locking
 * ==========================================================================
 */

/*
 * Lock the given spa_t for the purpose of adding or removing a vdev.
 * Grabs the global spa_namespace_lock plus the spa config lock for writing.
 * It returns the next transaction group for the spa_t.
 */
uint64_t
spa_vdev_enter(spa_t *spa)
{
	mutex_enter(&spa->spa_vdev_top_lock);
	mutex_enter(&spa_namespace_lock);
	return (spa_vdev_config_enter(spa));
}

/*
 * Internal implementation for spa_vdev_enter().  Used when a vdev
 * operation requires multiple syncs (i.e. removing a device) while
 * keeping the spa_namespace_lock held.
 */
uint64_t
spa_vdev_config_enter(spa_t *spa)
{
	ASSERT(MUTEX_HELD(&spa_namespace_lock));

	spa_config_enter(spa, SCL_ALL, spa, RW_WRITER);

	return (spa_last_synced_txg(spa) + 1);
}

/*
 * Used in combination with spa_vdev_config_enter() to allow the syncing
 * of multiple transactions without releasing the spa_namespace_lock.
 */
void
spa_vdev_config_exit(spa_t *spa, vdev_t *vd, uint64_t txg, int error, char *tag)
{
	ASSERT(MUTEX_HELD(&spa_namespace_lock));

	int config_changed = B_FALSE;

	ASSERT(txg > spa_last_synced_txg(spa));

	spa->spa_pending_vdev = NULL;

	/*
	 * Reassess the DTLs.
	 */
	vdev_dtl_reassess(spa->spa_root_vdev, 0, 0, B_FALSE);

	if (error == 0 && !list_is_empty(&spa->spa_config_dirty_list)) {
		config_changed = B_TRUE;
		spa->spa_config_generation++;
	}

	/*
	 * Verify the metaslab classes.
	 */
	ASSERT(metaslab_class_validate(spa_normal_class(spa)) == 0);
	ASSERT(metaslab_class_validate(spa_log_class(spa)) == 0);

	spa_config_exit(spa, SCL_ALL, spa);

	/*
	 * Panic the system if the specified tag requires it.  This
	 * is useful for ensuring that configurations are updated
	 * transactionally.
	 */
	if (zio_injection_enabled)
		zio_handle_panic_injection(spa, tag, 0);

	/*
	 * Note: this txg_wait_synced() is important because it ensures
	 * that there won't be more than one config change per txg.
	 * This allows us to use the txg as the generation number.
	 */
	if (error == 0)
		txg_wait_synced(spa->spa_dsl_pool, txg);

	if (vd != NULL) {
		ASSERT(!vd->vdev_detached || vd->vdev_dtl_sm == NULL);
		if (vd->vdev_ops->vdev_op_leaf) {
			mutex_enter(&vd->vdev_initialize_lock);
			vdev_initialize_stop(vd, VDEV_INITIALIZE_CANCELED);
			mutex_exit(&vd->vdev_initialize_lock);
		}

		spa_config_enter(spa, SCL_ALL, spa, RW_WRITER);
		vdev_free(vd);
		spa_config_exit(spa, SCL_ALL, spa);
	}

	/*
	 * If the config changed, update the config cache.
	 */
	if (config_changed)
		spa_write_cachefile(spa, B_FALSE, B_TRUE);
}

/*
 * Unlock the spa_t after adding or removing a vdev.  Besides undoing the
 * locking of spa_vdev_enter(), we also want make sure the transactions have
 * synced to disk, and then update the global configuration cache with the new
 * information.
 */
int
spa_vdev_exit(spa_t *spa, vdev_t *vd, uint64_t txg, int error)
{
	spa_vdev_config_exit(spa, vd, txg, error, FTAG);
	mutex_exit(&spa_namespace_lock);
	mutex_exit(&spa->spa_vdev_top_lock);

	return (error);
}

/*
 * Lock the given spa_t for the purpose of changing vdev state.
 */
void
spa_vdev_state_enter(spa_t *spa, int oplocks)
{
	int locks = SCL_STATE_ALL | oplocks;

	/*
	 * Root pools may need to read of the underlying devfs filesystem
	 * when opening up a vdev.  Unfortunately if we're holding the
	 * SCL_ZIO lock it will result in a deadlock when we try to issue
	 * the read from the root filesystem.  Instead we "prefetch"
	 * the associated vnodes that we need prior to opening the
	 * underlying devices and cache them so that we can prevent
	 * any I/O when we are doing the actual open.
	 */
	if (spa_is_root(spa)) {
		int low = locks & ~(SCL_ZIO - 1);
		int high = locks & ~low;

		spa_config_enter(spa, high, spa, RW_WRITER);
		vdev_hold(spa->spa_root_vdev);
		spa_config_enter(spa, low, spa, RW_WRITER);
	} else {
		spa_config_enter(spa, locks, spa, RW_WRITER);
	}
	spa->spa_vdev_locks = locks;
}

int
spa_vdev_state_exit(spa_t *spa, vdev_t *vd, int error)
{
	boolean_t config_changed = B_FALSE;

	if (vd != NULL || error == 0)
		vdev_dtl_reassess(vd ? vd->vdev_top : spa->spa_root_vdev,
		    0, 0, B_FALSE);

	if (vd != NULL) {
		vdev_state_dirty(vd->vdev_top);
		config_changed = B_TRUE;
		spa->spa_config_generation++;
	}

	if (spa_is_root(spa))
		vdev_rele(spa->spa_root_vdev);

	ASSERT3U(spa->spa_vdev_locks, >=, SCL_STATE_ALL);
	spa_config_exit(spa, spa->spa_vdev_locks, spa);

	/*
	 * If anything changed, wait for it to sync.  This ensures that,
	 * from the system administrator's perspective, zpool(1M) commands
	 * are synchronous.  This is important for things like zpool offline:
	 * when the command completes, you expect no further I/O from ZFS.
	 */
	if (vd != NULL)
		txg_wait_synced(spa->spa_dsl_pool, 0);

	/*
	 * If the config changed, update the config cache.
	 */
	if (config_changed) {
		mutex_enter(&spa_namespace_lock);
		spa_write_cachefile(spa, B_FALSE, B_TRUE);
		mutex_exit(&spa_namespace_lock);
	}

	return (error);
}

/*
 * ==========================================================================
 * Miscellaneous functions
 * ==========================================================================
 */

void
spa_activate_mos_feature(spa_t *spa, const char *feature, dmu_tx_t *tx)
{
	if (!nvlist_exists(spa->spa_label_features, feature)) {
		fnvlist_add_boolean(spa->spa_label_features, feature);
		/*
		 * When we are creating the pool (tx_txg==TXG_INITIAL), we can't
		 * dirty the vdev config because lock SCL_CONFIG is not held.
		 * Thankfully, in this case we don't need to dirty the config
		 * because it will be written out anyway when we finish
		 * creating the pool.
		 */
		if (tx->tx_txg != TXG_INITIAL)
			vdev_config_dirty(spa->spa_root_vdev);
	}
}

void
spa_deactivate_mos_feature(spa_t *spa, const char *feature)
{
	if (nvlist_remove_all(spa->spa_label_features, feature) == 0)
		vdev_config_dirty(spa->spa_root_vdev);
}

/*
 * Rename a spa_t.
 */
int
spa_rename(const char *name, const char *newname)
{
	spa_t *spa;
	int err;

	/*
	 * Lookup the spa_t and grab the config lock for writing.  We need to
	 * actually open the pool so that we can sync out the necessary labels.
	 * It's OK to call spa_open() with the namespace lock held because we
	 * allow recursive calls for other reasons.
	 */
	mutex_enter(&spa_namespace_lock);
	if ((err = spa_open(name, &spa, FTAG)) != 0) {
		mutex_exit(&spa_namespace_lock);
		return (err);
	}

	spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);

	avl_remove(&spa_namespace_avl, spa);
	(void) strlcpy(spa->spa_name, newname, sizeof (spa->spa_name));
	avl_add(&spa_namespace_avl, spa);

	/*
	 * Sync all labels to disk with the new names by marking the root vdev
	 * dirty and waiting for it to sync.  It will pick up the new pool name
	 * during the sync.
	 */
	vdev_config_dirty(spa->spa_root_vdev);

	spa_config_exit(spa, SCL_ALL, FTAG);

	txg_wait_synced(spa->spa_dsl_pool, 0);

	/*
	 * Sync the updated config cache.
	 */
	spa_write_cachefile(spa, B_FALSE, B_TRUE);

	spa_close(spa, FTAG);

	mutex_exit(&spa_namespace_lock);

	return (0);
}

/*
 * Return the spa_t associated with given pool_guid, if it exists.  If
 * device_guid is non-zero, determine whether the pool exists *and* contains
 * a device with the specified device_guid.
 */
spa_t *
spa_by_guid(uint64_t pool_guid, uint64_t device_guid)
{
	spa_t *spa;
	avl_tree_t *t = &spa_namespace_avl;

	ASSERT(MUTEX_HELD(&spa_namespace_lock));

	for (spa = avl_first(t); spa != NULL; spa = AVL_NEXT(t, spa)) {
		if (spa->spa_state == POOL_STATE_UNINITIALIZED)
			continue;
		if (spa->spa_root_vdev == NULL)
			continue;
		if (spa_guid(spa) == pool_guid) {
			if (device_guid == 0)
				break;

			if (vdev_lookup_by_guid(spa->spa_root_vdev,
			    device_guid) != NULL)
				break;

			/*
			 * Check any devices we may be in the process of adding.
			 */
			if (spa->spa_pending_vdev) {
				if (vdev_lookup_by_guid(spa->spa_pending_vdev,
				    device_guid) != NULL)
					break;
			}
		}
	}

	return (spa);
}

/*
 * Determine whether a pool with the given pool_guid exists.
 */
boolean_t
spa_guid_exists(uint64_t pool_guid, uint64_t device_guid)
{
	return (spa_by_guid(pool_guid, device_guid) != NULL);
}

char *
spa_strdup(const char *s)
{
	size_t len;
	char *new;

	len = strlen(s);
	new = kmem_alloc(len + 1, KM_SLEEP);
	bcopy(s, new, len);
	new[len] = '\0';

	return (new);
}

void
spa_strfree(char *s)
{
	kmem_free(s, strlen(s) + 1);
}

uint64_t
spa_get_random(uint64_t range)
{
	uint64_t r;

	ASSERT(range != 0);

	(void) random_get_pseudo_bytes((void *)&r, sizeof (uint64_t));

	return (r % range);
}

uint64_t
spa_generate_guid(spa_t *spa)
{
	uint64_t guid = spa_get_random(-1ULL);

	if (spa != NULL) {
		while (guid == 0 || spa_guid_exists(spa_guid(spa), guid))
			guid = spa_get_random(-1ULL);
	} else {
		while (guid == 0 || spa_guid_exists(guid, 0))
			guid = spa_get_random(-1ULL);
	}

	return (guid);
}

void
snprintf_blkptr(char *buf, size_t buflen, const blkptr_t *bp)
{
	char type[256];
	char *checksum = NULL;
	char *compress = NULL;

	if (bp != NULL) {
		if (BP_GET_TYPE(bp) & DMU_OT_NEWTYPE) {
			dmu_object_byteswap_t bswap =
			    DMU_OT_BYTESWAP(BP_GET_TYPE(bp));
			(void) snprintf(type, sizeof (type), "bswap %s %s",
			    DMU_OT_IS_METADATA(BP_GET_TYPE(bp)) ?
			    "metadata" : "data",
			    dmu_ot_byteswap[bswap].ob_name);
		} else {
			(void) strlcpy(type, dmu_ot[BP_GET_TYPE(bp)].ot_name,
			    sizeof (type));
		}
		if (!BP_IS_EMBEDDED(bp)) {
			checksum =
			    zio_checksum_table[BP_GET_CHECKSUM(bp)].ci_name;
		}
		compress = zio_compress_table[BP_GET_COMPRESS(bp)].ci_name;
	}

	SNPRINTF_BLKPTR(snprintf, ' ', buf, buflen, bp, type, checksum,
	    compress);
}

void
spa_freeze(spa_t *spa)
{
	uint64_t freeze_txg = 0;

	spa_config_enter(spa, SCL_ALL, FTAG, RW_WRITER);
	if (spa->spa_freeze_txg == UINT64_MAX) {
		freeze_txg = spa_last_synced_txg(spa) + TXG_SIZE;
		spa->spa_freeze_txg = freeze_txg;
	}
	spa_config_exit(spa, SCL_ALL, FTAG);
	if (freeze_txg != 0)
		txg_wait_synced(spa_get_dsl(spa), freeze_txg);
}

void
zfs_panic_recover(const char *fmt, ...)
{
	va_list adx;

	va_start(adx, fmt);
	vcmn_err(zfs_recover ? CE_WARN : CE_PANIC, fmt, adx);
	va_end(adx);
}

/*
 * This is a stripped-down version of strtoull, suitable only for converting
 * lowercase hexadecimal numbers that don't overflow.
 */
uint64_t
zfs_strtonum(const char *str, char **nptr)
{
	uint64_t val = 0;
	char c;
	int digit;

	while ((c = *str) != '\0') {
		if (c >= '0' && c <= '9')
			digit = c - '0';
		else if (c >= 'a' && c <= 'f')
			digit = 10 + c - 'a';
		else
			break;

		val *= 16;
		val += digit;

		str++;
	}

	if (nptr)
		*nptr = (char *)str;

	return (val);
}

/*
 * ==========================================================================
 * Accessor functions
 * ==========================================================================
 */

boolean_t
spa_shutting_down(spa_t *spa)
{
	return (spa->spa_async_suspended);
}

dsl_pool_t *
spa_get_dsl(spa_t *spa)
{
	return (spa->spa_dsl_pool);
}

boolean_t
spa_is_initializing(spa_t *spa)
{
	return (spa->spa_is_initializing);
}

boolean_t
spa_indirect_vdevs_loaded(spa_t *spa)
{
	return (spa->spa_indirect_vdevs_loaded);
}

blkptr_t *
spa_get_rootblkptr(spa_t *spa)
{
	return (&spa->spa_ubsync.ub_rootbp);
}

void
spa_set_rootblkptr(spa_t *spa, const blkptr_t *bp)
{
	spa->spa_uberblock.ub_rootbp = *bp;
}

void
spa_altroot(spa_t *spa, char *buf, size_t buflen)
{
	if (spa->spa_root == NULL)
		buf[0] = '\0';
	else
		(void) strncpy(buf, spa->spa_root, buflen);
}

int
spa_sync_pass(spa_t *spa)
{
	return (spa->spa_sync_pass);
}

char *
spa_name(spa_t *spa)
{
	return (spa->spa_name);
}

uint64_t
spa_guid(spa_t *spa)
{
	dsl_pool_t *dp = spa_get_dsl(spa);
	uint64_t guid;

	/*
	 * If we fail to parse the config during spa_load(), we can go through
	 * the error path (which posts an ereport) and end up here with no root
	 * vdev.  We stash the original pool guid in 'spa_config_guid' to handle
	 * this case.
	 */
	if (spa->spa_root_vdev == NULL)
		return (spa->spa_config_guid);

	guid = spa->spa_last_synced_guid != 0 ?
	    spa->spa_last_synced_guid : spa->spa_root_vdev->vdev_guid;

	/*
	 * Return the most recently synced out guid unless we're
	 * in syncing context.
	 */
	if (dp && dsl_pool_sync_context(dp))
		return (spa->spa_root_vdev->vdev_guid);
	else
		return (guid);
}

uint64_t
spa_load_guid(spa_t *spa)
{
	/*
	 * This is a GUID that exists solely as a reference for the
	 * purposes of the arc.  It is generated at load time, and
	 * is never written to persistent storage.
	 */
	return (spa->spa_load_guid);
}

uint64_t
spa_last_synced_txg(spa_t *spa)
{
	return (spa->spa_ubsync.ub_txg);
}

uint64_t
spa_first_txg(spa_t *spa)
{
	return (spa->spa_first_txg);
}

uint64_t
spa_syncing_txg(spa_t *spa)
{
	return (spa->spa_syncing_txg);
}

/*
 * Return the last txg where data can be dirtied. The final txgs
 * will be used to just clear out any deferred frees that remain.
 */
uint64_t
spa_final_dirty_txg(spa_t *spa)
{
	return (spa->spa_final_txg - TXG_DEFER_SIZE);
}

pool_state_t
spa_state(spa_t *spa)
{
	return (spa->spa_state);
}

spa_load_state_t
spa_load_state(spa_t *spa)
{
	return (spa->spa_load_state);
}

uint64_t
spa_freeze_txg(spa_t *spa)
{
	return (spa->spa_freeze_txg);
}

/* ARGSUSED */
uint64_t
spa_get_worst_case_asize(spa_t *spa, uint64_t lsize)
{
	return (lsize * spa_asize_inflation);
}

/*
 * Return the amount of slop space in bytes.  It is 1/32 of the pool (3.2%),
 * or at least 128MB, unless that would cause it to be more than half the
 * pool size.
 *
 * See the comment above spa_slop_shift for details.
 */
uint64_t
spa_get_slop_space(spa_t *spa)
{
	uint64_t space = spa_get_dspace(spa);
	return (MAX(space >> spa_slop_shift, MIN(space >> 1, spa_min_slop)));
}

uint64_t
spa_get_dspace(spa_t *spa)
{
	return (spa->spa_dspace);
}

uint64_t
spa_get_checkpoint_space(spa_t *spa)
{
	return (spa->spa_checkpoint_info.sci_dspace);
}

void
spa_update_dspace(spa_t *spa)
{
	spa->spa_dspace = metaslab_class_get_dspace(spa_normal_class(spa)) +
	    ddt_get_dedup_dspace(spa);
	if (spa->spa_vdev_removal != NULL) {
		/*
		 * We can't allocate from the removing device, so
		 * subtract its size.  This prevents the DMU/DSL from
		 * filling up the (now smaller) pool while we are in the
		 * middle of removing the device.
		 *
		 * Note that the DMU/DSL doesn't actually know or care
		 * how much space is allocated (it does its own tracking
		 * of how much space has been logically used).  So it
		 * doesn't matter that the data we are moving may be
		 * allocated twice (on the old device and the new
		 * device).
		 */
		spa_config_enter(spa, SCL_VDEV, FTAG, RW_READER);
		vdev_t *vd =
		    vdev_lookup_top(spa, spa->spa_vdev_removal->svr_vdev_id);
		spa->spa_dspace -= spa_deflate(spa) ?
		    vd->vdev_stat.vs_dspace : vd->vdev_stat.vs_space;
		spa_config_exit(spa, SCL_VDEV, FTAG);
	}
}

/*
 * Return the failure mode that has been set to this pool. The default
 * behavior will be to block all I/Os when a complete failure occurs.
 */
uint8_t
spa_get_failmode(spa_t *spa)
{
	return (spa->spa_failmode);
}

boolean_t
spa_suspended(spa_t *spa)
{
	return (spa->spa_suspended);
}

uint64_t
spa_version(spa_t *spa)
{
	return (spa->spa_ubsync.ub_version);
}

boolean_t
spa_deflate(spa_t *spa)
{
	return (spa->spa_deflate);
}

metaslab_class_t *
spa_normal_class(spa_t *spa)
{
	return (spa->spa_normal_class);
}

metaslab_class_t *
spa_log_class(spa_t *spa)
{
	return (spa->spa_log_class);
}

void
spa_evicting_os_register(spa_t *spa, objset_t *os)
{
	mutex_enter(&spa->spa_evicting_os_lock);
	list_insert_head(&spa->spa_evicting_os_list, os);
	mutex_exit(&spa->spa_evicting_os_lock);
}

void
spa_evicting_os_deregister(spa_t *spa, objset_t *os)
{
	mutex_enter(&spa->spa_evicting_os_lock);
	list_remove(&spa->spa_evicting_os_list, os);
	cv_broadcast(&spa->spa_evicting_os_cv);
	mutex_exit(&spa->spa_evicting_os_lock);
}

void
spa_evicting_os_wait(spa_t *spa)
{
	mutex_enter(&spa->spa_evicting_os_lock);
	while (!list_is_empty(&spa->spa_evicting_os_list))
		cv_wait(&spa->spa_evicting_os_cv, &spa->spa_evicting_os_lock);
	mutex_exit(&spa->spa_evicting_os_lock);

	dmu_buf_user_evict_wait();
}

int
spa_max_replication(spa_t *spa)
{
	/*
	 * As of SPA_VERSION == SPA_VERSION_DITTO_BLOCKS, we are able to
	 * handle BPs with more than one DVA allocated.  Set our max
	 * replication level accordingly.
	 */
	if (spa_version(spa) < SPA_VERSION_DITTO_BLOCKS)
		return (1);
	return (MIN(SPA_DVAS_PER_BP, spa_max_replication_override));
}

int
spa_prev_software_version(spa_t *spa)
{
	return (spa->spa_prev_software_version);
}

uint64_t
spa_deadman_synctime(spa_t *spa)
{
	return (spa->spa_deadman_synctime);
}

uint64_t
dva_get_dsize_sync(spa_t *spa, const dva_t *dva)
{
	uint64_t asize = DVA_GET_ASIZE(dva);
	uint64_t dsize = asize;

	ASSERT(spa_config_held(spa, SCL_ALL, RW_READER) != 0);

	if (asize != 0 && spa->spa_deflate) {
		uint64_t vdev = DVA_GET_VDEV(dva);
		vdev_t *vd = vdev_lookup_top(spa, vdev);
		if (vd == NULL) {
			panic(
			    "dva_get_dsize_sync(): bad DVA %llu:%llu",
			    (u_longlong_t)vdev, (u_longlong_t)asize);
		}
		dsize = (asize >> SPA_MINBLOCKSHIFT) * vd->vdev_deflate_ratio;
	}

	return (dsize);
}

uint64_t
bp_get_dsize_sync(spa_t *spa, const blkptr_t *bp)
{
	uint64_t dsize = 0;

	for (int d = 0; d < BP_GET_NDVAS(bp); d++)
		dsize += dva_get_dsize_sync(spa, &bp->blk_dva[d]);

	return (dsize);
}

uint64_t
bp_get_dsize(spa_t *spa, const blkptr_t *bp)
{
	uint64_t dsize = 0;

	spa_config_enter(spa, SCL_VDEV, FTAG, RW_READER);

	for (int d = 0; d < BP_GET_NDVAS(bp); d++)
		dsize += dva_get_dsize_sync(spa, &bp->blk_dva[d]);

	spa_config_exit(spa, SCL_VDEV, FTAG);

	return (dsize);
}

uint64_t
spa_dirty_data(spa_t *spa)
{
	return (spa->spa_dsl_pool->dp_dirty_total);
}

/*
 * ==========================================================================
 * Initialization and Termination
 * ==========================================================================
 */

static int
spa_name_compare(const void *a1, const void *a2)
{
	const spa_t *s1 = a1;
	const spa_t *s2 = a2;
	int s;

	s = strcmp(s1->spa_name, s2->spa_name);

	return (AVL_ISIGN(s));
}

int
spa_busy(void)
{
	return (spa_active_count);
}

void
spa_boot_init()
{
	spa_config_load();
}

#ifdef _KERNEL
EVENTHANDLER_DEFINE(mountroot, spa_boot_init, NULL, 0);
#endif

void
spa_init(int mode)
{
	mutex_init(&spa_namespace_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&spa_spare_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&spa_l2cache_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&spa_namespace_cv, NULL, CV_DEFAULT, NULL);

	avl_create(&spa_namespace_avl, spa_name_compare, sizeof (spa_t),
	    offsetof(spa_t, spa_avl));

	avl_create(&spa_spare_avl, spa_spare_compare, sizeof (spa_aux_t),
	    offsetof(spa_aux_t, aux_avl));

	avl_create(&spa_l2cache_avl, spa_l2cache_compare, sizeof (spa_aux_t),
	    offsetof(spa_aux_t, aux_avl));

	spa_mode_global = mode;

#ifdef illumos
#ifdef _KERNEL
	spa_arch_init();
#else
	if (spa_mode_global != FREAD && dprintf_find_string("watch")) {
		arc_procfd = open("/proc/self/ctl", O_WRONLY);
		if (arc_procfd == -1) {
			perror("could not enable watchpoints: "
			    "opening /proc/self/ctl failed: ");
		} else {
			arc_watch = B_TRUE;
		}
	}
#endif
#endif /* illumos */
	refcount_sysinit();
	unique_init();
	range_tree_init();
	metaslab_alloc_trace_init();
	zio_init();
	lz4_init();
	dmu_init();
	zil_init();
	vdev_cache_stat_init();
	vdev_file_init();
	zfs_prop_init();
	zpool_prop_init();
	zpool_feature_init();
	spa_config_load();
	l2arc_start();
	scan_init();
	dsl_scan_global_init();
#ifndef illumos
#ifdef _KERNEL
	zfs_deadman_init();
#endif
#endif	/* !illumos */
}

void
spa_fini(void)
{
	l2arc_stop();

	spa_evict_all();

	vdev_file_fini();
	vdev_cache_stat_fini();
	zil_fini();
	dmu_fini();
	lz4_fini();
	zio_fini();
	metaslab_alloc_trace_fini();
	range_tree_fini();
	unique_fini();
	refcount_fini();
	scan_fini();
	
	avl_destroy(&spa_namespace_avl);
	avl_destroy(&spa_spare_avl);
	avl_destroy(&spa_l2cache_avl);

	cv_destroy(&spa_namespace_cv);
	mutex_destroy(&spa_namespace_lock);
	mutex_destroy(&spa_spare_lock);
	mutex_destroy(&spa_l2cache_lock);
}

/*
 * Return whether this pool has slogs. No locking needed.
 * It's not a problem if the wrong answer is returned as it's only for
 * performance and not correctness
 */
boolean_t
spa_has_slogs(spa_t *spa)
{
	return (spa->spa_log_class->mc_rotor != NULL);
}

spa_log_state_t
spa_get_log_state(spa_t *spa)
{
	return (spa->spa_log_state);
}

void
spa_set_log_state(spa_t *spa, spa_log_state_t state)
{
	spa->spa_log_state = state;
}

boolean_t
spa_is_root(spa_t *spa)
{
	return (spa->spa_is_root);
}

boolean_t
spa_writeable(spa_t *spa)
{
	return (!!(spa->spa_mode & FWRITE) && spa->spa_trust_config);
}

/*
 * Returns true if there is a pending sync task in any of the current
 * syncing txg, the current quiescing txg, or the current open txg.
 */
boolean_t
spa_has_pending_synctask(spa_t *spa)
{
	return (!txg_all_lists_empty(&spa->spa_dsl_pool->dp_sync_tasks) ||
	    !txg_all_lists_empty(&spa->spa_dsl_pool->dp_early_sync_tasks));
}

int
spa_mode(spa_t *spa)
{
	return (spa->spa_mode);
}

uint64_t
spa_bootfs(spa_t *spa)
{
	return (spa->spa_bootfs);
}

uint64_t
spa_delegation(spa_t *spa)
{
	return (spa->spa_delegation);
}

objset_t *
spa_meta_objset(spa_t *spa)
{
	return (spa->spa_meta_objset);
}

enum zio_checksum
spa_dedup_checksum(spa_t *spa)
{
	return (spa->spa_dedup_checksum);
}

/*
 * Reset pool scan stat per scan pass (or reboot).
 */
void
spa_scan_stat_init(spa_t *spa)
{
	/* data not stored on disk */
	spa->spa_scan_pass_start = gethrestime_sec();
	if (dsl_scan_is_paused_scrub(spa->spa_dsl_pool->dp_scan))
		spa->spa_scan_pass_scrub_pause = spa->spa_scan_pass_start;
	else
		spa->spa_scan_pass_scrub_pause = 0;
	spa->spa_scan_pass_scrub_spent_paused = 0;
	spa->spa_scan_pass_exam = 0;
	spa->spa_scan_pass_issued = 0;
	vdev_scan_stat_init(spa->spa_root_vdev);
}

/*
 * Get scan stats for zpool status reports
 */
int
spa_scan_get_stats(spa_t *spa, pool_scan_stat_t *ps)
{
	dsl_scan_t *scn = spa->spa_dsl_pool ? spa->spa_dsl_pool->dp_scan : NULL;

	if (scn == NULL || scn->scn_phys.scn_func == POOL_SCAN_NONE)
		return (SET_ERROR(ENOENT));
	bzero(ps, sizeof (pool_scan_stat_t));

	/* data stored on disk */
	ps->pss_func = scn->scn_phys.scn_func;
	ps->pss_state = scn->scn_phys.scn_state;
	ps->pss_start_time = scn->scn_phys.scn_start_time;
	ps->pss_end_time = scn->scn_phys.scn_end_time;
	ps->pss_to_examine = scn->scn_phys.scn_to_examine;
	ps->pss_to_process = scn->scn_phys.scn_to_process;
	ps->pss_processed = scn->scn_phys.scn_processed;
	ps->pss_errors = scn->scn_phys.scn_errors;
	ps->pss_examined = scn->scn_phys.scn_examined;
	ps->pss_issued =
		scn->scn_issued_before_pass + spa->spa_scan_pass_issued;
	/* data not stored on disk */
	ps->pss_pass_start = spa->spa_scan_pass_start;
	ps->pss_pass_exam = spa->spa_scan_pass_exam;
	ps->pss_pass_issued = spa->spa_scan_pass_issued;
	ps->pss_pass_scrub_pause = spa->spa_scan_pass_scrub_pause;
	ps->pss_pass_scrub_spent_paused = spa->spa_scan_pass_scrub_spent_paused;

	return (0);
}

int
spa_maxblocksize(spa_t *spa)
{
	if (spa_feature_is_enabled(spa, SPA_FEATURE_LARGE_BLOCKS))
		return (SPA_MAXBLOCKSIZE);
	else
		return (SPA_OLD_MAXBLOCKSIZE);
}

int
spa_maxdnodesize(spa_t *spa)
{
	if (spa_feature_is_enabled(spa, SPA_FEATURE_LARGE_DNODE))
		return (DNODE_MAX_SIZE);
	else
		return (DNODE_MIN_SIZE);
}


/*
 * Returns the txg that the last device removal completed. No indirect mappings
 * have been added since this txg.
 */
uint64_t
spa_get_last_removal_txg(spa_t *spa)
{
	uint64_t vdevid;
	uint64_t ret = -1ULL;

	spa_config_enter(spa, SCL_VDEV, FTAG, RW_READER);
	/*
	 * sr_prev_indirect_vdev is only modified while holding all the
	 * config locks, so it is sufficient to hold SCL_VDEV as reader when
	 * examining it.
	 */
	vdevid = spa->spa_removing_phys.sr_prev_indirect_vdev;

	while (vdevid != -1ULL) {
		vdev_t *vd = vdev_lookup_top(spa, vdevid);
		vdev_indirect_births_t *vib = vd->vdev_indirect_births;

		ASSERT3P(vd->vdev_ops, ==, &vdev_indirect_ops);

		/*
		 * If the removal did not remap any data, we don't care.
		 */
		if (vdev_indirect_births_count(vib) != 0) {
			ret = vdev_indirect_births_last_entry_txg(vib);
			break;
		}

		vdevid = vd->vdev_indirect_config.vic_prev_indirect_vdev;
	}
	spa_config_exit(spa, SCL_VDEV, FTAG);

	IMPLY(ret != -1ULL,
	    spa_feature_is_active(spa, SPA_FEATURE_DEVICE_REMOVAL));

	return (ret);
}

boolean_t
spa_trust_config(spa_t *spa)
{
	return (spa->spa_trust_config);
}

uint64_t
spa_missing_tvds_allowed(spa_t *spa)
{
	return (spa->spa_missing_tvds_allowed);
}

void
spa_set_missing_tvds(spa_t *spa, uint64_t missing)
{
	spa->spa_missing_tvds = missing;
}

boolean_t
spa_top_vdevs_spacemap_addressable(spa_t *spa)
{
	vdev_t *rvd = spa->spa_root_vdev;
	for (uint64_t c = 0; c < rvd->vdev_children; c++) {
		if (!vdev_is_spacemap_addressable(rvd->vdev_child[c]))
			return (B_FALSE);
	}
	return (B_TRUE);
}

boolean_t
spa_has_checkpoint(spa_t *spa)
{
	return (spa->spa_checkpoint_txg != 0);
}

boolean_t
spa_importing_readonly_checkpoint(spa_t *spa)
{
	return ((spa->spa_import_flags & ZFS_IMPORT_CHECKPOINT) &&
	    spa->spa_mode == FREAD);
}

uint64_t
spa_min_claim_txg(spa_t *spa)
{
	uint64_t checkpoint_txg = spa->spa_uberblock.ub_checkpoint_txg;

	if (checkpoint_txg != 0)
		return (checkpoint_txg + 1);

	return (spa->spa_first_txg);
}

/*
 * If there is a checkpoint, async destroys may consume more space from
 * the pool instead of freeing it. In an attempt to save the pool from
 * getting suspended when it is about to run out of space, we stop
 * processing async destroys.
 */
boolean_t
spa_suspend_async_destroy(spa_t *spa)
{
	dsl_pool_t *dp = spa_get_dsl(spa);

	uint64_t unreserved = dsl_pool_unreserved_space(dp,
	    ZFS_SPACE_CHECK_EXTRA_RESERVED);
	uint64_t used = dsl_dir_phys(dp->dp_root_dir)->dd_used_bytes;
	uint64_t avail = (unreserved > used) ? (unreserved - used) : 0;

	if (spa_has_checkpoint(spa) && avail == 0)
		return (B_TRUE);

	return (B_FALSE);
}
