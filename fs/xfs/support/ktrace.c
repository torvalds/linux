/*
 * Copyright (c) 2000-2003,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include <xfs.h>

static kmem_zone_t *ktrace_hdr_zone;
static kmem_zone_t *ktrace_ent_zone;
static int          ktrace_zentries;

void
ktrace_init(int zentries)
{
	ktrace_zentries = zentries;

	ktrace_hdr_zone = kmem_zone_init(sizeof(ktrace_t),
					"ktrace_hdr");
	ASSERT(ktrace_hdr_zone);

	ktrace_ent_zone = kmem_zone_init(ktrace_zentries
					* sizeof(ktrace_entry_t),
					"ktrace_ent");
	ASSERT(ktrace_ent_zone);
}

void
ktrace_uninit(void)
{
	kmem_cache_destroy(ktrace_hdr_zone);
	kmem_cache_destroy(ktrace_ent_zone);
}

/*
 * ktrace_alloc()
 *
 * Allocate a ktrace header and enough buffering for the given
 * number of entries.
 */
ktrace_t *
ktrace_alloc(int nentries, unsigned int __nocast sleep)
{
	ktrace_t        *ktp;
	ktrace_entry_t  *ktep;

	ktp = (ktrace_t*)kmem_zone_alloc(ktrace_hdr_zone, sleep);

	if (ktp == (ktrace_t*)NULL) {
		/*
		 * KM_SLEEP callers don't expect failure.
		 */
		if (sleep & KM_SLEEP)
			panic("ktrace_alloc: NULL memory on KM_SLEEP request!");

		return NULL;
	}

	/*
	 * Special treatment for buffers with the ktrace_zentries entries
	 */
	if (nentries == ktrace_zentries) {
		ktep = (ktrace_entry_t*)kmem_zone_zalloc(ktrace_ent_zone,
							    sleep);
	} else {
		ktep = (ktrace_entry_t*)kmem_zalloc((nentries * sizeof(*ktep)),
							    sleep);
	}

	if (ktep == NULL) {
		/*
		 * KM_SLEEP callers don't expect failure.
		 */
		if (sleep & KM_SLEEP)
			panic("ktrace_alloc: NULL memory on KM_SLEEP request!");

		kmem_free(ktp, sizeof(*ktp));

		return NULL;
	}

	spinlock_init(&(ktp->kt_lock), "kt_lock");

	ktp->kt_entries  = ktep;
	ktp->kt_nentries = nentries;
	ktp->kt_index    = 0;
	ktp->kt_rollover = 0;
	return ktp;
}


/*
 * ktrace_free()
 *
 * Free up the ktrace header and buffer.  It is up to the caller
 * to ensure that no-one is referencing it.
 */
void
ktrace_free(ktrace_t *ktp)
{
	int     entries_size;

	if (ktp == (ktrace_t *)NULL)
		return;

	spinlock_destroy(&ktp->kt_lock);

	/*
	 * Special treatment for the Vnode trace buffer.
	 */
	if (ktp->kt_nentries == ktrace_zentries) {
		kmem_zone_free(ktrace_ent_zone, ktp->kt_entries);
	} else {
		entries_size = (int)(ktp->kt_nentries * sizeof(ktrace_entry_t));

		kmem_free(ktp->kt_entries, entries_size);
	}

	kmem_zone_free(ktrace_hdr_zone, ktp);
}


/*
 * Enter the given values into the "next" entry in the trace buffer.
 * kt_index is always the index of the next entry to be filled.
 */
void
ktrace_enter(
	ktrace_t        *ktp,
	void            *val0,
	void            *val1,
	void            *val2,
	void            *val3,
	void            *val4,
	void            *val5,
	void            *val6,
	void            *val7,
	void            *val8,
	void            *val9,
	void            *val10,
	void            *val11,
	void            *val12,
	void            *val13,
	void            *val14,
	void            *val15)
{
	static DEFINE_SPINLOCK(wrap_lock);
	unsigned long	flags;
	int             index;
	ktrace_entry_t  *ktep;

	ASSERT(ktp != NULL);

	/*
	 * Grab an entry by pushing the index up to the next one.
	 */
	spin_lock_irqsave(&wrap_lock, flags);
	index = ktp->kt_index;
	if (++ktp->kt_index == ktp->kt_nentries)
		ktp->kt_index = 0;
	spin_unlock_irqrestore(&wrap_lock, flags);

	if (!ktp->kt_rollover && index == ktp->kt_nentries - 1)
		ktp->kt_rollover = 1;

	ASSERT((index >= 0) && (index < ktp->kt_nentries));

	ktep = &(ktp->kt_entries[index]);

	ktep->val[0]  = val0;
	ktep->val[1]  = val1;
	ktep->val[2]  = val2;
	ktep->val[3]  = val3;
	ktep->val[4]  = val4;
	ktep->val[5]  = val5;
	ktep->val[6]  = val6;
	ktep->val[7]  = val7;
	ktep->val[8]  = val8;
	ktep->val[9]  = val9;
	ktep->val[10] = val10;
	ktep->val[11] = val11;
	ktep->val[12] = val12;
	ktep->val[13] = val13;
	ktep->val[14] = val14;
	ktep->val[15] = val15;
}

/*
 * Return the number of entries in the trace buffer.
 */
int
ktrace_nentries(
	ktrace_t        *ktp)
{
	if (ktp == NULL) {
		return 0;
	}

	return (ktp->kt_rollover ? ktp->kt_nentries : ktp->kt_index);
}

/*
 * ktrace_first()
 *
 * This is used to find the start of the trace buffer.
 * In conjunction with ktrace_next() it can be used to
 * iterate through the entire trace buffer.  This code does
 * not do any locking because it is assumed that it is called
 * from the debugger.
 *
 * The caller must pass in a pointer to a ktrace_snap
 * structure in which we will keep some state used to
 * iterate through the buffer.  This state must not touched
 * by any code outside of this module.
 */
ktrace_entry_t *
ktrace_first(ktrace_t   *ktp, ktrace_snap_t     *ktsp)
{
	ktrace_entry_t  *ktep;
	int             index;
	int             nentries;

	if (ktp->kt_rollover)
		index = ktp->kt_index;
	else
		index = 0;

	ktsp->ks_start = index;
	ktep = &(ktp->kt_entries[index]);

	nentries = ktrace_nentries(ktp);
	index++;
	if (index < nentries) {
		ktsp->ks_index = index;
	} else {
		ktsp->ks_index = 0;
		if (index > nentries)
			ktep = NULL;
	}
	return ktep;
}

/*
 * ktrace_next()
 *
 * This is used to iterate through the entries of the given
 * trace buffer.  The caller must pass in the ktrace_snap_t
 * structure initialized by ktrace_first().  The return value
 * will be either a pointer to the next ktrace_entry or NULL
 * if all of the entries have been traversed.
 */
ktrace_entry_t *
ktrace_next(
	ktrace_t        *ktp,
	ktrace_snap_t   *ktsp)
{
	int             index;
	ktrace_entry_t  *ktep;

	index = ktsp->ks_index;
	if (index == ktsp->ks_start) {
		ktep = NULL;
	} else {
		ktep = &ktp->kt_entries[index];
	}

	index++;
	if (index == ktrace_nentries(ktp)) {
		ktsp->ks_index = 0;
	} else {
		ktsp->ks_index = index;
	}

	return ktep;
}

/*
 * ktrace_skip()
 *
 * Skip the next "count" entries and return the entry after that.
 * Return NULL if this causes us to iterate past the beginning again.
 */
ktrace_entry_t *
ktrace_skip(
	ktrace_t        *ktp,
	int             count,
	ktrace_snap_t   *ktsp)
{
	int             index;
	int             new_index;
	ktrace_entry_t  *ktep;
	int             nentries = ktrace_nentries(ktp);

	index = ktsp->ks_index;
	new_index = index + count;
	while (new_index >= nentries) {
		new_index -= nentries;
	}
	if (index == ktsp->ks_start) {
		/*
		 * We've iterated around to the start, so we're done.
		 */
		ktep = NULL;
	} else if ((new_index < index) && (index < ktsp->ks_index)) {
		/*
		 * We've skipped past the start again, so we're done.
		 */
		ktep = NULL;
		ktsp->ks_index = ktsp->ks_start;
	} else {
		ktep = &(ktp->kt_entries[new_index]);
		new_index++;
		if (new_index == nentries) {
			ktsp->ks_index = 0;
		} else {
			ktsp->ks_index = new_index;
		}
	}
	return ktep;
}
