/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2015 by Delphix. All rights reserved.
 */

#include <sys/dmu_tx.h>
#include <sys/spa.h>
#include <sys/dmu.h>
#include <sys/dsl_pool.h>
#include <sys/vdev_indirect_births.h>

static boolean_t
vdev_indirect_births_verify(vdev_indirect_births_t *vib)
{
	ASSERT(vib != NULL);

	ASSERT(vib->vib_object != 0);
	ASSERT(vib->vib_objset != NULL);
	ASSERT(vib->vib_phys != NULL);
	ASSERT(vib->vib_dbuf != NULL);

	EQUIV(vib->vib_phys->vib_count > 0, vib->vib_entries != NULL);

	return (B_TRUE);
}

uint64_t
vdev_indirect_births_count(vdev_indirect_births_t *vib)
{
	ASSERT(vdev_indirect_births_verify(vib));

	return (vib->vib_phys->vib_count);
}

uint64_t
vdev_indirect_births_object(vdev_indirect_births_t *vib)
{
	ASSERT(vdev_indirect_births_verify(vib));

	return (vib->vib_object);
}

static uint64_t
vdev_indirect_births_size_impl(vdev_indirect_births_t *vib)
{
	return (vib->vib_phys->vib_count * sizeof (*vib->vib_entries));
}

void
vdev_indirect_births_close(vdev_indirect_births_t *vib)
{
	ASSERT(vdev_indirect_births_verify(vib));

	if (vib->vib_phys->vib_count > 0) {
		uint64_t births_size = vdev_indirect_births_size_impl(vib);

		kmem_free(vib->vib_entries, births_size);
		vib->vib_entries = NULL;
	}

	dmu_buf_rele(vib->vib_dbuf, vib);

	vib->vib_objset = NULL;
	vib->vib_object = 0;
	vib->vib_dbuf = NULL;
	vib->vib_phys = NULL;

	kmem_free(vib, sizeof (*vib));
}

uint64_t
vdev_indirect_births_alloc(objset_t *os, dmu_tx_t *tx)
{
	ASSERT(dmu_tx_is_syncing(tx));

	return (dmu_object_alloc(os,
	    DMU_OTN_UINT64_METADATA, SPA_OLD_MAXBLOCKSIZE,
	    DMU_OTN_UINT64_METADATA, sizeof (vdev_indirect_birth_phys_t),
	    tx));
}

vdev_indirect_births_t *
vdev_indirect_births_open(objset_t *os, uint64_t births_object)
{
	vdev_indirect_births_t *vib = kmem_zalloc(sizeof (*vib), KM_SLEEP);

	vib->vib_objset = os;
	vib->vib_object = births_object;

	VERIFY0(dmu_bonus_hold(os, vib->vib_object, vib, &vib->vib_dbuf));
	vib->vib_phys = vib->vib_dbuf->db_data;

	if (vib->vib_phys->vib_count > 0) {
		uint64_t births_size = vdev_indirect_births_size_impl(vib);
		vib->vib_entries = kmem_alloc(births_size, KM_SLEEP);
		VERIFY0(dmu_read(vib->vib_objset, vib->vib_object, 0,
		    births_size, vib->vib_entries, DMU_READ_PREFETCH));
	}

	ASSERT(vdev_indirect_births_verify(vib));

	return (vib);
}

void
vdev_indirect_births_free(objset_t *os, uint64_t object, dmu_tx_t *tx)
{
	VERIFY0(dmu_object_free(os, object, tx));
}

void
vdev_indirect_births_add_entry(vdev_indirect_births_t *vib,
    uint64_t max_offset, uint64_t txg, dmu_tx_t *tx)
{
	vdev_indirect_birth_entry_phys_t vibe;
	uint64_t old_size;
	uint64_t new_size;
	vdev_indirect_birth_entry_phys_t *new_entries;

	ASSERT(dmu_tx_is_syncing(tx));
	ASSERT(dsl_pool_sync_context(dmu_tx_pool(tx)));
	ASSERT(vdev_indirect_births_verify(vib));

	dmu_buf_will_dirty(vib->vib_dbuf, tx);

	vibe.vibe_offset = max_offset;
	vibe.vibe_phys_birth_txg = txg;

	old_size = vdev_indirect_births_size_impl(vib);
	dmu_write(vib->vib_objset, vib->vib_object, old_size, sizeof (vibe),
	    &vibe, tx);
	vib->vib_phys->vib_count++;
	new_size = vdev_indirect_births_size_impl(vib);

	new_entries = kmem_alloc(new_size, KM_SLEEP);
	if (old_size > 0) {
		bcopy(vib->vib_entries, new_entries, old_size);
		kmem_free(vib->vib_entries, old_size);
	}
	new_entries[vib->vib_phys->vib_count - 1] = vibe;
	vib->vib_entries = new_entries;
}

uint64_t
vdev_indirect_births_last_entry_txg(vdev_indirect_births_t *vib)
{
	ASSERT(vdev_indirect_births_verify(vib));
	ASSERT(vib->vib_phys->vib_count > 0);

	vdev_indirect_birth_entry_phys_t *last =
	    &vib->vib_entries[vib->vib_phys->vib_count - 1];
	return (last->vibe_phys_birth_txg);
}

/*
 * Return the txg in which the given range was copied (i.e. its physical
 * birth txg).  The specified offset+asize must be contiguously mapped
 * (i.e. not a split block).
 *
 * The entries are sorted by increasing phys_birth, and also by increasing
 * offset.  We find the specified offset by binary search.  Note that we
 * can not use bsearch() because looking at each entry independently is
 * insufficient to find the correct entry.  Each entry implicitly relies
 * on the previous entry: an entry indicates that the offsets from the
 * end of the previous entry to the end of this entry were written in the
 * specified txg.
 */
uint64_t
vdev_indirect_births_physbirth(vdev_indirect_births_t *vib, uint64_t offset,
    uint64_t asize)
{
	vdev_indirect_birth_entry_phys_t *base;
	vdev_indirect_birth_entry_phys_t *last;

	ASSERT(vdev_indirect_births_verify(vib));
	ASSERT(vib->vib_phys->vib_count > 0);

	base = vib->vib_entries;
	last = base + vib->vib_phys->vib_count - 1;

	ASSERT3U(offset, <, last->vibe_offset);

	while (last >= base) {
		vdev_indirect_birth_entry_phys_t *p =
		    base + ((last - base) / 2);
		if (offset >= p->vibe_offset) {
			base = p + 1;
		} else if (p == vib->vib_entries ||
		    offset >= (p - 1)->vibe_offset) {
			ASSERT3U(offset + asize, <=, p->vibe_offset);
			return (p->vibe_phys_birth_txg);
		} else {
			last = p - 1;
		}
	}
	ASSERT(!"offset not found");
	return (-1);
}
