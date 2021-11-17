// SPDX-License-Identifier: GPL-2.0-only
/*
 * snapshot.c    Ceph snapshot context utility routines (part of libceph)
 *
 * Copyright (C) 2013 Inktank Storage, Inc.
 */

#include <linux/types.h>
#include <linux/export.h>
#include <linux/ceph/libceph.h>

/*
 * Ceph snapshot contexts are reference counted objects, and the
 * returned structure holds a single reference.  Acquire additional
 * references with ceph_get_snap_context(), and release them with
 * ceph_put_snap_context().  When the reference count reaches zero
 * the entire structure is freed.
 */

/*
 * Create a new ceph snapshot context large enough to hold the
 * indicated number of snapshot ids (which can be 0).  Caller has
 * to fill in snapc->seq and snapc->snaps[0..snap_count-1].
 *
 * Returns a null pointer if an error occurs.
 */
struct ceph_snap_context *ceph_create_snap_context(u32 snap_count,
						gfp_t gfp_flags)
{
	struct ceph_snap_context *snapc;
	size_t size;

	size = sizeof (struct ceph_snap_context);
	size += snap_count * sizeof (snapc->snaps[0]);
	snapc = kzalloc(size, gfp_flags);
	if (!snapc)
		return NULL;

	refcount_set(&snapc->nref, 1);
	snapc->num_snaps = snap_count;

	return snapc;
}
EXPORT_SYMBOL(ceph_create_snap_context);

struct ceph_snap_context *ceph_get_snap_context(struct ceph_snap_context *sc)
{
	if (sc)
		refcount_inc(&sc->nref);
	return sc;
}
EXPORT_SYMBOL(ceph_get_snap_context);

void ceph_put_snap_context(struct ceph_snap_context *sc)
{
	if (!sc)
		return;
	if (refcount_dec_and_test(&sc->nref)) {
		/*printk(" deleting snap_context %p\n", sc);*/
		kfree(sc);
	}
}
EXPORT_SYMBOL(ceph_put_snap_context);
