/*
 * Copyright 2014-2015 Olivier Houchard.
 * Copyright 2012-2015 Samy Al Bahra.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <ck_cc.h>
#include <ck_rhs.h>
#include <ck_limits.h>
#include <ck_md.h>
#include <ck_pr.h>
#include <ck_stdint.h>
#include <ck_stdbool.h>
#include <ck_string.h>

#include "ck_internal.h"

#ifndef CK_RHS_PROBE_L1_SHIFT
#define CK_RHS_PROBE_L1_SHIFT 3ULL
#endif /* CK_RHS_PROBE_L1_SHIFT */

#define CK_RHS_PROBE_L1 (1 << CK_RHS_PROBE_L1_SHIFT)
#define CK_RHS_PROBE_L1_MASK (CK_RHS_PROBE_L1 - 1)

#ifndef CK_RHS_PROBE_L1_DEFAULT
#define CK_RHS_PROBE_L1_DEFAULT CK_MD_CACHELINE
#endif

#define CK_RHS_VMA_MASK ((uintptr_t)((1ULL << CK_MD_VMA_BITS) - 1))
#define CK_RHS_VMA(x)	\
	((void *)((uintptr_t)(x) & CK_RHS_VMA_MASK))

#define CK_RHS_EMPTY     NULL
#define CK_RHS_G		(1024)
#define CK_RHS_G_MASK	(CK_RHS_G - 1)

#if defined(CK_F_PR_LOAD_8) && defined(CK_F_PR_STORE_8)
#define CK_RHS_WORD          uint8_t
#define CK_RHS_WORD_MAX	    UINT8_MAX
#define CK_RHS_STORE(x, y)   ck_pr_store_8(x, y)
#define CK_RHS_LOAD(x)       ck_pr_load_8(x)
#elif defined(CK_F_PR_LOAD_16) && defined(CK_F_PR_STORE_16)
#define CK_RHS_WORD          uint16_t
#define CK_RHS_WORD_MAX	    UINT16_MAX
#define CK_RHS_STORE(x, y)   ck_pr_store_16(x, y)
#define CK_RHS_LOAD(x)       ck_pr_load_16(x)
#elif defined(CK_F_PR_LOAD_32) && defined(CK_F_PR_STORE_32)
#define CK_RHS_WORD          uint32_t
#define CK_RHS_WORD_MAX	    UINT32_MAX
#define CK_RHS_STORE(x, y)   ck_pr_store_32(x, y)
#define CK_RHS_LOAD(x)       ck_pr_load_32(x)
#else
#error "ck_rhs is not supported on your platform."
#endif

#define CK_RHS_MAX_WANTED	0xffff

enum ck_rhs_probe_behavior {
	CK_RHS_PROBE = 0,	/* Default behavior. */
	CK_RHS_PROBE_RH,	/* Short-circuit if RH slot found. */
	CK_RHS_PROBE_INSERT,	/* Short-circuit on probe bound if tombstone found. */

	CK_RHS_PROBE_ROBIN_HOOD,/* Look for the first slot available for the entry we are about to replace, only used to internally implement Robin Hood */
	CK_RHS_PROBE_NO_RH,	/* Don't do the RH dance */
};
struct ck_rhs_entry_desc {
	unsigned int probes;
	unsigned short wanted;
	CK_RHS_WORD probe_bound;
	bool in_rh;
	const void *entry;
} CK_CC_ALIGN(16);

struct ck_rhs_no_entry_desc {
	unsigned int probes;
	unsigned short wanted;
	CK_RHS_WORD probe_bound;
	bool in_rh;
} CK_CC_ALIGN(8);

typedef long ck_rhs_probe_cb_t(struct ck_rhs *hs,
    struct ck_rhs_map *map,
    unsigned long *n_probes,
    long *priority,
    unsigned long h,
    const void *key,
    const void **object,
    unsigned long probe_limit,
    enum ck_rhs_probe_behavior behavior);

struct ck_rhs_map {
	unsigned int generation[CK_RHS_G];
	unsigned int probe_maximum;
	unsigned long mask;
	unsigned long step;
	unsigned int probe_limit;
	unsigned long n_entries;
	unsigned long capacity;
	unsigned long size;
	unsigned long max_entries;
	char offset_mask;
	union {
		struct ck_rhs_entry_desc *descs;
		struct ck_rhs_no_entry {
			const void **entries;
			struct ck_rhs_no_entry_desc *descs;
		} no_entries;
	} entries;
	bool read_mostly;
	ck_rhs_probe_cb_t *probe_func;
};

static CK_CC_INLINE const void *
ck_rhs_entry(struct ck_rhs_map *map, long offset)
{

	if (map->read_mostly)
		return (map->entries.no_entries.entries[offset]);
	else
		return (map->entries.descs[offset].entry);
}

static CK_CC_INLINE const void **
ck_rhs_entry_addr(struct ck_rhs_map *map, long offset)
{

	if (map->read_mostly)
		return (&map->entries.no_entries.entries[offset]);
	else
		return (&map->entries.descs[offset].entry);
}

static CK_CC_INLINE struct ck_rhs_entry_desc *
ck_rhs_desc(struct ck_rhs_map *map, long offset)
{

	if (CK_CC_UNLIKELY(map->read_mostly))
		return ((struct ck_rhs_entry_desc *)(void *)&map->entries.no_entries.descs[offset]);
	else
		return (&map->entries.descs[offset]);
}

static CK_CC_INLINE void
ck_rhs_wanted_inc(struct ck_rhs_map *map, long offset)
{

	if (CK_CC_UNLIKELY(map->read_mostly))
		map->entries.no_entries.descs[offset].wanted++;
	else
		map->entries.descs[offset].wanted++;
}

static CK_CC_INLINE unsigned int
ck_rhs_probes(struct ck_rhs_map *map, long offset)
{

	if (CK_CC_UNLIKELY(map->read_mostly))
		return (map->entries.no_entries.descs[offset].probes);
	else
		return (map->entries.descs[offset].probes);
}

static CK_CC_INLINE void
ck_rhs_set_probes(struct ck_rhs_map *map, long offset, unsigned int value)
{

	if (CK_CC_UNLIKELY(map->read_mostly))
		map->entries.no_entries.descs[offset].probes = value;
	else
		map->entries.descs[offset].probes = value;
}

static CK_CC_INLINE CK_RHS_WORD
ck_rhs_probe_bound(struct ck_rhs_map *map, long offset)
{

	if (CK_CC_UNLIKELY(map->read_mostly))
		return (map->entries.no_entries.descs[offset].probe_bound);
	else
		return (map->entries.descs[offset].probe_bound);
}

static CK_CC_INLINE CK_RHS_WORD *
ck_rhs_probe_bound_addr(struct ck_rhs_map *map, long offset)
{

	if (CK_CC_UNLIKELY(map->read_mostly))
		return (&map->entries.no_entries.descs[offset].probe_bound);
	else
		return (&map->entries.descs[offset].probe_bound);
}


static CK_CC_INLINE bool
ck_rhs_in_rh(struct ck_rhs_map *map, long offset)
{

	if (CK_CC_UNLIKELY(map->read_mostly))
		return (map->entries.no_entries.descs[offset].in_rh);
	else
		return (map->entries.descs[offset].in_rh);
}

static CK_CC_INLINE void
ck_rhs_set_rh(struct ck_rhs_map *map, long offset)
{

	if (CK_CC_UNLIKELY(map->read_mostly))
		map->entries.no_entries.descs[offset].in_rh = true;
	else
		map->entries.descs[offset].in_rh = true;
}

static CK_CC_INLINE void
ck_rhs_unset_rh(struct ck_rhs_map *map, long offset)
{

	if (CK_CC_UNLIKELY(map->read_mostly))
		map->entries.no_entries.descs[offset].in_rh = false;
	else
		map->entries.descs[offset].in_rh = false;
}


#define CK_RHS_DEFAULT_LOAD_FACTOR	50

static ck_rhs_probe_cb_t ck_rhs_map_probe;
static ck_rhs_probe_cb_t ck_rhs_map_probe_rm;

bool
ck_rhs_set_load_factor(struct ck_rhs *hs, unsigned int load_factor)
{
	struct ck_rhs_map *map = hs->map;

	if (load_factor == 0 || load_factor > 100)
		return false;

	hs->load_factor = load_factor;
	map->max_entries = (map->capacity * (unsigned long)hs->load_factor) / 100;
	while (map->n_entries > map->max_entries) {
		if (ck_rhs_grow(hs, map->capacity << 1) == false)
			return false;
		map = hs->map;
	}
	return true;
}

void
ck_rhs_iterator_init(struct ck_rhs_iterator *iterator)
{

	iterator->cursor = NULL;
	iterator->offset = 0;
	return;
}

bool
ck_rhs_next(struct ck_rhs *hs, struct ck_rhs_iterator *i, void **key)
{
	struct ck_rhs_map *map = hs->map;
	void *value;

	if (i->offset >= map->capacity)
		return false;

	do {
		value = CK_CC_DECONST_PTR(ck_rhs_entry(map, i->offset));
		if (value != CK_RHS_EMPTY) {
#ifdef CK_RHS_PP
			if (hs->mode & CK_RHS_MODE_OBJECT)
				value = CK_RHS_VMA(value);
#endif
			i->offset++;
			*key = value;
			return true;
		}
	} while (++i->offset < map->capacity);

	return false;
}

void
ck_rhs_stat(struct ck_rhs *hs, struct ck_rhs_stat *st)
{
	struct ck_rhs_map *map = hs->map;

	st->n_entries = map->n_entries;
	st->probe_maximum = map->probe_maximum;
	return;
}

unsigned long
ck_rhs_count(struct ck_rhs *hs)
{

	return hs->map->n_entries;
}

static void
ck_rhs_map_destroy(struct ck_malloc *m, struct ck_rhs_map *map, bool defer)
{

	m->free(map, map->size, defer);
	return;
}

void
ck_rhs_destroy(struct ck_rhs *hs)
{

	ck_rhs_map_destroy(hs->m, hs->map, false);
	return;
}

static struct ck_rhs_map *
ck_rhs_map_create(struct ck_rhs *hs, unsigned long entries)
{
	struct ck_rhs_map *map;
	unsigned long size, n_entries, limit;

	n_entries = ck_internal_power_2(entries);
	if (n_entries < CK_RHS_PROBE_L1)
		n_entries = CK_RHS_PROBE_L1;

	if (hs->mode & CK_RHS_MODE_READ_MOSTLY)
		size = sizeof(struct ck_rhs_map) +
		    (sizeof(void *) * n_entries +
		     sizeof(struct ck_rhs_no_entry_desc) * n_entries +
		     2 * CK_MD_CACHELINE - 1);
	else
		size = sizeof(struct ck_rhs_map) +
		    (sizeof(struct ck_rhs_entry_desc) * n_entries +
		     CK_MD_CACHELINE - 1);
	map = hs->m->malloc(size);
	if (map == NULL)
		return NULL;
	map->read_mostly = !!(hs->mode & CK_RHS_MODE_READ_MOSTLY);

	map->size = size;
	/* We should probably use a more intelligent heuristic for default probe length. */
	limit = ck_internal_max(n_entries >> (CK_RHS_PROBE_L1_SHIFT + 2), CK_RHS_PROBE_L1_DEFAULT);
	if (limit > UINT_MAX)
		limit = UINT_MAX;

	map->probe_limit = (unsigned int)limit;
	map->probe_maximum = 0;
	map->capacity = n_entries;
	map->step = ck_cc_ffsl(n_entries);
	map->mask = n_entries - 1;
	map->n_entries = 0;

	map->max_entries = (map->capacity * (unsigned long)hs->load_factor) / 100;
	/* Align map allocation to cache line. */
	if (map->read_mostly) {
		map->entries.no_entries.entries = (void *)(((uintptr_t)&map[1] +
		    CK_MD_CACHELINE - 1) & ~(CK_MD_CACHELINE - 1));
		map->entries.no_entries.descs = (void *)(((uintptr_t)map->entries.no_entries.entries + (sizeof(void *) * n_entries) + CK_MD_CACHELINE - 1) &~ (CK_MD_CACHELINE - 1));
		memset(map->entries.no_entries.entries, 0,
		    sizeof(void *) * n_entries);
		memset(map->entries.no_entries.descs, 0,
		    sizeof(struct ck_rhs_no_entry_desc) * n_entries);
		map->offset_mask = (CK_MD_CACHELINE / sizeof(void *)) - 1;
		map->probe_func = ck_rhs_map_probe_rm;

	} else {
		map->entries.descs = (void *)(((uintptr_t)&map[1] +
		    CK_MD_CACHELINE - 1) & ~(CK_MD_CACHELINE - 1));
		memset(map->entries.descs, 0, sizeof(struct ck_rhs_entry_desc) * n_entries);
		map->offset_mask = (CK_MD_CACHELINE / sizeof(struct ck_rhs_entry_desc)) - 1;
		map->probe_func = ck_rhs_map_probe;
	}
	memset(map->generation, 0, sizeof map->generation);

	/* Commit entries purge with respect to map publication. */
	ck_pr_fence_store();
	return map;
}

bool
ck_rhs_reset_size(struct ck_rhs *hs, unsigned long capacity)
{
	struct ck_rhs_map *map, *previous;

	previous = hs->map;
	map = ck_rhs_map_create(hs, capacity);
	if (map == NULL)
		return false;

	ck_pr_store_ptr(&hs->map, map);
	ck_rhs_map_destroy(hs->m, previous, true);
	return true;
}

bool
ck_rhs_reset(struct ck_rhs *hs)
{
	struct ck_rhs_map *previous;

	previous = hs->map;
	return ck_rhs_reset_size(hs, previous->capacity);
}

static inline unsigned long
ck_rhs_map_probe_next(struct ck_rhs_map *map,
    unsigned long offset,
    unsigned long probes)
{

	if (probes & map->offset_mask) {
		offset = (offset &~ map->offset_mask) +
		    ((offset + 1) & map->offset_mask);
		return offset;
	} else
		return (offset + probes) & map->mask;
}

static inline unsigned long
ck_rhs_map_probe_prev(struct ck_rhs_map *map, unsigned long offset,
    unsigned long probes)
{

	if (probes & map->offset_mask) {
		offset = (offset &~ map->offset_mask) + ((offset - 1) &
		    map->offset_mask);
		return offset;
	} else
		return ((offset - probes) & map->mask);
}


static inline void
ck_rhs_map_bound_set(struct ck_rhs_map *m,
    unsigned long h,
    unsigned long n_probes)
{
	unsigned long offset = h & m->mask;
	struct ck_rhs_entry_desc *desc;

	if (n_probes > m->probe_maximum)
		ck_pr_store_uint(&m->probe_maximum, n_probes);
	if (!(m->read_mostly)) {
		desc = &m->entries.descs[offset];

		if (desc->probe_bound < n_probes) {
			if (n_probes > CK_RHS_WORD_MAX)
				n_probes = CK_RHS_WORD_MAX;

			CK_RHS_STORE(&desc->probe_bound, n_probes);
			ck_pr_fence_store();
		}
	}

	return;
}

static inline unsigned int
ck_rhs_map_bound_get(struct ck_rhs_map *m, unsigned long h)
{
	unsigned long offset = h & m->mask;
	unsigned int r = CK_RHS_WORD_MAX;

	if (m->read_mostly)
		r = ck_pr_load_uint(&m->probe_maximum);
	else {
		r = CK_RHS_LOAD(&m->entries.descs[offset].probe_bound);
		if (r == CK_RHS_WORD_MAX)
			r = ck_pr_load_uint(&m->probe_maximum);
	}
	return r;
}

bool
ck_rhs_grow(struct ck_rhs *hs,
    unsigned long capacity)
{
	struct ck_rhs_map *map, *update;
	const void *previous, *prev_saved;
	unsigned long k, offset, probes;

restart:
	map = hs->map;
	if (map->capacity > capacity)
		return false;

	update = ck_rhs_map_create(hs, capacity);
	if (update == NULL)
		return false;

	for (k = 0; k < map->capacity; k++) {
		unsigned long h;

		prev_saved = previous = ck_rhs_entry(map, k);
		if (previous == CK_RHS_EMPTY)
			continue;

#ifdef CK_RHS_PP
		if (hs->mode & CK_RHS_MODE_OBJECT)
			previous = CK_RHS_VMA(previous);
#endif

		h = hs->hf(previous, hs->seed);
		offset = h & update->mask;
		probes = 0;

		for (;;) {
			const void **cursor = ck_rhs_entry_addr(update, offset);

			if (probes++ == update->probe_limit) {
				/*
				 * We have hit the probe limit, map needs to be even larger.
				 */
				ck_rhs_map_destroy(hs->m, update, false);
				capacity <<= 1;
				goto restart;
			}

			if (CK_CC_LIKELY(*cursor == CK_RHS_EMPTY)) {
				*cursor = prev_saved;
				update->n_entries++;
				ck_rhs_set_probes(update, offset, probes);
				ck_rhs_map_bound_set(update, h, probes);
				break;
			} else if (ck_rhs_probes(update, offset) < probes) {
				const void *tmp = prev_saved;
				unsigned int old_probes;
				prev_saved = previous = *cursor;
#ifdef CK_RHS_PP
				if (hs->mode & CK_RHS_MODE_OBJECT)
					previous = CK_RHS_VMA(previous);
#endif
				*cursor = tmp;
				ck_rhs_map_bound_set(update, h, probes);
				h = hs->hf(previous, hs->seed);
				old_probes = ck_rhs_probes(update, offset);
				ck_rhs_set_probes(update, offset, probes);
				probes = old_probes - 1;
				continue;
			}
			ck_rhs_wanted_inc(update, offset);
			offset = ck_rhs_map_probe_next(update, offset,  probes);
		}

	}

	ck_pr_fence_store();
	ck_pr_store_ptr(&hs->map, update);
	ck_rhs_map_destroy(hs->m, map, true);
	return true;
}

bool
ck_rhs_rebuild(struct ck_rhs *hs)
{

	return ck_rhs_grow(hs, hs->map->capacity);
}

static long
ck_rhs_map_probe_rm(struct ck_rhs *hs,
    struct ck_rhs_map *map,
    unsigned long *n_probes,
    long *priority,
    unsigned long h,
    const void *key,
    const void **object,
    unsigned long probe_limit,
    enum ck_rhs_probe_behavior behavior)
{
	const void *k;
	const void *compare;
	long pr = -1;
	unsigned long offset, probes, opl;

#ifdef CK_RHS_PP
	/* If we are storing object pointers, then we may leverage pointer packing. */
	unsigned long hv = 0;

	if (hs->mode & CK_RHS_MODE_OBJECT) {
		hv = (h >> 25) & CK_RHS_KEY_MASK;
		compare = CK_RHS_VMA(key);
	} else {
		compare = key;
	}
#else
	compare = key;
#endif
 	*object = NULL;
	if (behavior != CK_RHS_PROBE_ROBIN_HOOD) {
		probes = 0;
		offset = h & map->mask;
	} else {
		/* Restart from the bucket we were previously in */
		probes = *n_probes;
		offset = ck_rhs_map_probe_next(map, *priority,
		    probes);
	}
	opl = probe_limit;

	for (;;) {
		if (probes++ == probe_limit) {
			if (probe_limit == opl || pr != -1) {
				k = CK_RHS_EMPTY;
				goto leave;
			}
			/*
			 * If no eligible slot has been found yet, continue probe
			 * sequence with original probe limit.
			 */
			probe_limit = opl;
		}
		k = ck_pr_load_ptr(&map->entries.no_entries.entries[offset]);
		if (k == CK_RHS_EMPTY)
			goto leave;

		if (behavior != CK_RHS_PROBE_NO_RH) {
			struct ck_rhs_entry_desc *desc = (void *)&map->entries.no_entries.descs[offset];

			if (pr == -1 &&
			    desc->in_rh == false && desc->probes < probes) {
				pr = offset;
				*n_probes = probes;

				if (behavior == CK_RHS_PROBE_RH ||
				    behavior == CK_RHS_PROBE_ROBIN_HOOD) {
					k = CK_RHS_EMPTY;
					goto leave;
				}
			}
		}

		if (behavior != CK_RHS_PROBE_ROBIN_HOOD) {
#ifdef CK_RHS_PP
			if (hs->mode & CK_RHS_MODE_OBJECT) {
				if (((uintptr_t)k >> CK_MD_VMA_BITS) != hv) {
					offset = ck_rhs_map_probe_next(map, offset, probes);
					continue;
				}

				k = CK_RHS_VMA(k);
			}
#endif

			if (k == compare)
				goto leave;

			if (hs->compare == NULL) {
				offset = ck_rhs_map_probe_next(map, offset, probes);
				continue;
			}

			if (hs->compare(k, key) == true)
				goto leave;
		}
		offset = ck_rhs_map_probe_next(map, offset, probes);
	}
leave:
	if (probes > probe_limit) {
		offset = -1;
	} else {
		*object = k;
	}

	if (pr == -1)
		*n_probes = probes;

	*priority = pr;
	return offset;
}

static long
ck_rhs_map_probe(struct ck_rhs *hs,
    struct ck_rhs_map *map,
    unsigned long *n_probes,
    long *priority,
    unsigned long h,
    const void *key,
    const void **object,
    unsigned long probe_limit,
    enum ck_rhs_probe_behavior behavior)
{
	const void *k;
	const void *compare;
	long pr = -1;
	unsigned long offset, probes, opl;

#ifdef CK_RHS_PP
	/* If we are storing object pointers, then we may leverage pointer packing. */
	unsigned long hv = 0;

	if (hs->mode & CK_RHS_MODE_OBJECT) {
		hv = (h >> 25) & CK_RHS_KEY_MASK;
		compare = CK_RHS_VMA(key);
	} else {
		compare = key;
	}
#else
	compare = key;
#endif

 	*object = NULL;
	if (behavior != CK_RHS_PROBE_ROBIN_HOOD) {
		probes = 0;
		offset = h & map->mask;
	} else {
		/* Restart from the bucket we were previously in */
		probes = *n_probes;
		offset = ck_rhs_map_probe_next(map, *priority,
		    probes);
	}
	opl = probe_limit;
	if (behavior == CK_RHS_PROBE_INSERT)
		probe_limit = ck_rhs_map_bound_get(map, h);

	for (;;) {
		if (probes++ == probe_limit) {
			if (probe_limit == opl || pr != -1) {
				k = CK_RHS_EMPTY;
				goto leave;
			}
			/*
			 * If no eligible slot has been found yet, continue probe
			 * sequence with original probe limit.
			 */
			probe_limit = opl;
		}
		k = ck_pr_load_ptr(&map->entries.descs[offset].entry);
		if (k == CK_RHS_EMPTY)
			goto leave;
		if ((behavior != CK_RHS_PROBE_NO_RH)) {
			struct ck_rhs_entry_desc *desc = &map->entries.descs[offset];

			if (pr == -1 &&
			    desc->in_rh == false && desc->probes < probes) {
				pr = offset;
				*n_probes = probes;

				if (behavior == CK_RHS_PROBE_RH ||
				    behavior == CK_RHS_PROBE_ROBIN_HOOD) {
					k = CK_RHS_EMPTY;
					goto leave;
				}
			}
		}

		if (behavior != CK_RHS_PROBE_ROBIN_HOOD) {
#ifdef CK_RHS_PP
			if (hs->mode & CK_RHS_MODE_OBJECT) {
				if (((uintptr_t)k >> CK_MD_VMA_BITS) != hv) {
					offset = ck_rhs_map_probe_next(map, offset, probes);
					continue;
				}

				k = CK_RHS_VMA(k);
			}
#endif

			if (k == compare)
				goto leave;

			if (hs->compare == NULL) {
				offset = ck_rhs_map_probe_next(map, offset, probes);
				continue;
			}

			if (hs->compare(k, key) == true)
				goto leave;
		}
		offset = ck_rhs_map_probe_next(map, offset, probes);
	}
leave:
	if (probes > probe_limit) {
		offset = -1;
	} else {
		*object = k;
	}

	if (pr == -1)
		*n_probes = probes;

	*priority = pr;
	return offset;
}

static inline const void *
ck_rhs_marshal(unsigned int mode, const void *key, unsigned long h)
{
#ifdef CK_RHS_PP
	const void *insert;

	if (mode & CK_RHS_MODE_OBJECT) {
		insert = (void *)((uintptr_t)CK_RHS_VMA(key) | ((h >> 25) << CK_MD_VMA_BITS));
	} else {
		insert = key;
	}

	return insert;
#else
	(void)mode;
	(void)h;

	return key;
#endif
}

bool
ck_rhs_gc(struct ck_rhs *hs)
{
	unsigned long i;
	struct ck_rhs_map *map = hs->map;

	unsigned int max_probes = 0;
	for (i = 0; i < map->capacity; i++) {
		if (ck_rhs_probes(map, i) > max_probes)
			max_probes = ck_rhs_probes(map, i);
	}
	map->probe_maximum = max_probes;
	return true;
}

static void
ck_rhs_add_wanted(struct ck_rhs *hs, long end_offset, long old_slot,
	unsigned long h)
{
	struct ck_rhs_map *map = hs->map;
	long offset;
	unsigned int probes = 1;
	bool found_slot = false;
	struct ck_rhs_entry_desc *desc;

	offset = h & map->mask;

	if (old_slot == -1)
		found_slot = true;
	while (offset != end_offset) {
		if (offset == old_slot)
			found_slot = true;
		if (found_slot) {
			desc = ck_rhs_desc(map, offset);
			if (desc->wanted < CK_RHS_MAX_WANTED)
				desc->wanted++;
		}
		offset = ck_rhs_map_probe_next(map, offset, probes);
		probes++;
	}
}

static unsigned long
ck_rhs_remove_wanted(struct ck_rhs *hs, long offset, long limit)
{
	struct ck_rhs_map *map = hs->map;
	int probes = ck_rhs_probes(map, offset);
	bool do_remove = true;
	struct ck_rhs_entry_desc *desc;

	while (probes > 1) {
		probes--;
		offset = ck_rhs_map_probe_prev(map, offset, probes);
		if (offset == limit)
			do_remove = false;
		if (do_remove) {
			desc = ck_rhs_desc(map, offset);
			if (desc->wanted != CK_RHS_MAX_WANTED)
				desc->wanted--;
		}
	}
	return offset;
}

static long
ck_rhs_get_first_offset(struct ck_rhs_map *map, unsigned long offset, unsigned int probes)
{
	while (probes > (unsigned long)map->offset_mask + 1) {
		offset -= ((probes - 1) &~ map->offset_mask);
		offset &= map->mask;
		offset = (offset &~ map->offset_mask) +
		    ((offset - map->offset_mask) & map->offset_mask);
		probes -= map->offset_mask + 1;
	}
	return ((offset &~ map->offset_mask) + ((offset - (probes - 1)) & map->offset_mask));
}

#define CK_RHS_MAX_RH	512

static int
ck_rhs_put_robin_hood(struct ck_rhs *hs,
    long orig_slot, struct ck_rhs_entry_desc *desc)
{
	long slot, first;
	const void *object, *insert;
	unsigned long n_probes;
	struct ck_rhs_map *map;
	unsigned long h = 0;
	long prev;
	void *key;
	long prevs[CK_RHS_MAX_RH];
	unsigned int prevs_nb = 0;
	unsigned int i;

	map = hs->map;
	first = orig_slot;
	n_probes = desc->probes;
restart:
	key = CK_CC_DECONST_PTR(ck_rhs_entry(map, first));
	insert = key;
#ifdef CK_RHS_PP
	if (hs->mode & CK_RHS_MODE_OBJECT)
	    key = CK_RHS_VMA(key);
#endif
	orig_slot = first;
	ck_rhs_set_rh(map, orig_slot);

	slot = map->probe_func(hs, map, &n_probes, &first, h, key, &object,
	    map->probe_limit, prevs_nb == CK_RHS_MAX_RH ?
	    CK_RHS_PROBE_NO_RH : CK_RHS_PROBE_ROBIN_HOOD);

	if (slot == -1 && first == -1) {
		if (ck_rhs_grow(hs, map->capacity << 1) == false) {
			desc->in_rh = false;

			for (i = 0; i < prevs_nb; i++)
				ck_rhs_unset_rh(map, prevs[i]);

			return -1;
		}

		return 1;
	}

	if (first != -1) {
		desc = ck_rhs_desc(map, first);
		int old_probes = desc->probes;

		desc->probes = n_probes;
		h = ck_rhs_get_first_offset(map, first, n_probes);
		ck_rhs_map_bound_set(map, h, n_probes);
		prev = orig_slot;
		prevs[prevs_nb++] = prev;
		n_probes = old_probes;
		goto restart;
	} else {
		/* An empty slot was found. */
		h =  ck_rhs_get_first_offset(map, slot, n_probes);
		ck_rhs_map_bound_set(map, h, n_probes);
		ck_pr_store_ptr(ck_rhs_entry_addr(map, slot), insert);
		ck_pr_inc_uint(&map->generation[h & CK_RHS_G_MASK]);
		ck_pr_fence_atomic_store();
		ck_rhs_set_probes(map, slot, n_probes);
		desc->in_rh = 0;
		ck_rhs_add_wanted(hs, slot, orig_slot, h);
	}
	while (prevs_nb > 0) {
		prev = prevs[--prevs_nb];
		ck_pr_store_ptr(ck_rhs_entry_addr(map, orig_slot),
		    ck_rhs_entry(map, prev));
		h = ck_rhs_get_first_offset(map, orig_slot,
		    desc->probes);
		ck_rhs_add_wanted(hs, orig_slot, prev, h);
		ck_pr_inc_uint(&map->generation[h & CK_RHS_G_MASK]);
		ck_pr_fence_atomic_store();
		orig_slot = prev;
		desc->in_rh = false;
		desc = ck_rhs_desc(map, orig_slot);
	}
	return 0;
}

static void
ck_rhs_do_backward_shift_delete(struct ck_rhs *hs, long slot)
{
	struct ck_rhs_map *map = hs->map;
	struct ck_rhs_entry_desc *desc, *new_desc = NULL;
	unsigned long h;

	desc = ck_rhs_desc(map, slot);
	h = ck_rhs_remove_wanted(hs, slot, -1);
	while (desc->wanted > 0) {
		unsigned long offset = 0, tmp_offset;
		unsigned long wanted_probes = 1;
		unsigned int probe = 0;
		unsigned int max_probes;

		/* Find a successor */
		while (wanted_probes < map->probe_maximum) {
			probe = wanted_probes;
			offset = ck_rhs_map_probe_next(map, slot, probe);
			while (probe < map->probe_maximum) {
				new_desc = ck_rhs_desc(map, offset);
				if (new_desc->probes == probe + 1)
					break;
				probe++;
				offset = ck_rhs_map_probe_next(map, offset,
				    probe);
			}
			if (probe < map->probe_maximum)
				break;
			wanted_probes++;
		}
		if (!(wanted_probes < map->probe_maximum)) {
			desc->wanted = 0;
			break;
		}
		desc->probes = wanted_probes;
		h = ck_rhs_remove_wanted(hs, offset, slot);
		ck_pr_store_ptr(ck_rhs_entry_addr(map, slot),
		    ck_rhs_entry(map, offset));
		ck_pr_inc_uint(&map->generation[h & CK_RHS_G_MASK]);
		ck_pr_fence_atomic_store();
		if (wanted_probes < CK_RHS_WORD_MAX) {
			struct ck_rhs_entry_desc *hdesc = ck_rhs_desc(map, h);
			if (hdesc->wanted == 1)
				CK_RHS_STORE(&hdesc->probe_bound,
				    wanted_probes);
			else if (hdesc->probe_bound == CK_RHS_WORD_MAX ||
			    hdesc->probe_bound == new_desc->probes) {
				probe++;
				if (hdesc->probe_bound == CK_RHS_WORD_MAX)
					max_probes = map->probe_maximum;
				else {
					max_probes = hdesc->probe_bound;
					max_probes--;
				}
				tmp_offset = ck_rhs_map_probe_next(map, offset,
				    probe);
				while (probe < max_probes) {
					if (h == (unsigned long)ck_rhs_get_first_offset(map, tmp_offset, probe))
						break;
					probe++;
					tmp_offset = ck_rhs_map_probe_next(map, tmp_offset, probe);
				}
				if (probe == max_probes)
					CK_RHS_STORE(&hdesc->probe_bound,
					    wanted_probes);
			}
		}
		if (desc->wanted < CK_RHS_MAX_WANTED)
			desc->wanted--;
		slot = offset;
		desc = new_desc;
	}
	ck_pr_store_ptr(ck_rhs_entry_addr(map, slot), CK_RHS_EMPTY);
	if ((desc->probes - 1) < CK_RHS_WORD_MAX)
		CK_RHS_STORE(ck_rhs_probe_bound_addr(map, h),
		    desc->probes - 1);
	desc->probes = 0;
}

bool
ck_rhs_fas(struct ck_rhs *hs,
    unsigned long h,
    const void *key,
    void **previous)
{
	long slot, first;
	const void *object;
	const void *insert;
	unsigned long n_probes;
	struct ck_rhs_map *map = hs->map;
	struct ck_rhs_entry_desc *desc, *desc2;

	*previous = NULL;
restart:
	slot = map->probe_func(hs, map, &n_probes, &first, h, key, &object,
	    ck_rhs_map_bound_get(map, h), CK_RHS_PROBE);

	/* Replacement semantics presume existence. */
	if (object == NULL)
		return false;

	insert = ck_rhs_marshal(hs->mode, key, h);

	if (first != -1) {
		int ret;

		desc = ck_rhs_desc(map, slot);
		desc2 = ck_rhs_desc(map, first);
		desc->in_rh = true;
		ret = ck_rhs_put_robin_hood(hs, first, desc2);
		desc->in_rh = false;
		if (CK_CC_UNLIKELY(ret == 1))
			goto restart;
		else if (CK_CC_UNLIKELY(ret != 0))
			return false;
		ck_pr_store_ptr(ck_rhs_entry_addr(map, first), insert);
		ck_pr_inc_uint(&map->generation[h & CK_RHS_G_MASK]);
		ck_pr_fence_atomic_store();
		desc2->probes = n_probes;
		ck_rhs_add_wanted(hs, first, -1, h);
		ck_rhs_do_backward_shift_delete(hs, slot);
	} else {
		ck_pr_store_ptr(ck_rhs_entry_addr(map, slot), insert);
		ck_rhs_set_probes(map, slot, n_probes);
	}
	*previous = CK_CC_DECONST_PTR(object);
	return true;
}

/*
 * An apply function takes two arguments. The first argument is a pointer to a
 * pre-existing object. The second argument is a pointer to the fifth argument
 * passed to ck_hs_apply. If a non-NULL pointer is passed to the first argument
 * and the return value of the apply function is NULL, then the pre-existing
 * value is deleted. If the return pointer is the same as the one passed to the
 * apply function then no changes are made to the hash table.  If the first
 * argument is non-NULL and the return pointer is different than that passed to
 * the apply function, then the pre-existing value is replaced. For
 * replacement, it is required that the value itself is identical to the
 * previous value.
 */
bool
ck_rhs_apply(struct ck_rhs *hs,
    unsigned long h,
    const void *key,
    ck_rhs_apply_fn_t *fn,
    void *cl)
{
	const void *insert;
	const void  *object, *delta = false;
	unsigned long n_probes;
	long slot, first;
	struct ck_rhs_map *map;
	bool delta_set = false;

restart:
	map = hs->map;

	slot = map->probe_func(hs, map, &n_probes, &first, h, key, &object, map->probe_limit, CK_RHS_PROBE_INSERT);
	if (slot == -1 && first == -1) {
		if (ck_rhs_grow(hs, map->capacity << 1) == false)
			return false;

		goto restart;
	}
	if (!delta_set) {
		delta = fn(CK_CC_DECONST_PTR(object), cl);
		delta_set = true;
	}

	if (delta == NULL) {
		/*
		 * The apply function has requested deletion. If the object doesn't exist,
		 * then exit early.
		 */
		if (CK_CC_UNLIKELY(object == NULL))
			return true;

		/* Otherwise, delete it. */
		ck_rhs_do_backward_shift_delete(hs, slot);
		return true;
	}

	/* The apply function has not requested hash set modification so exit early. */
	if (delta == object)
		return true;

	/* A modification or insertion has been requested. */
	ck_rhs_map_bound_set(map, h, n_probes);

	insert = ck_rhs_marshal(hs->mode, delta, h);
	if (first != -1) {
		/*
		 * This follows the same semantics as ck_hs_set, please refer to that
		 * function for documentation.
		 */
		struct ck_rhs_entry_desc *desc = NULL, *desc2;
		if (slot != -1) {
			desc = ck_rhs_desc(map, slot);
			desc->in_rh = true;
		}
		desc2 = ck_rhs_desc(map, first);
		int ret = ck_rhs_put_robin_hood(hs, first, desc2);
		if (slot != -1)
			desc->in_rh = false;

		if (CK_CC_UNLIKELY(ret == 1))
			goto restart;
		if (CK_CC_UNLIKELY(ret == -1))
			return false;
		/* If an earlier bucket was found, then store entry there. */
		ck_pr_store_ptr(ck_rhs_entry_addr(map, first), insert);
		desc2->probes = n_probes;
		/*
		 * If a duplicate key was found, then delete it after
		 * signaling concurrent probes to restart. Optionally,
		 * it is possible to install tombstone after grace
		 * period if we can guarantee earlier position of
		 * duplicate key.
		 */
		ck_rhs_add_wanted(hs, first, -1, h);
		if (object != NULL) {
			ck_pr_inc_uint(&map->generation[h & CK_RHS_G_MASK]);
			ck_pr_fence_atomic_store();
			ck_rhs_do_backward_shift_delete(hs, slot);
		}
	} else {
		/*
		 * If we are storing into same slot, then atomic store is sufficient
		 * for replacement.
		 */
		ck_pr_store_ptr(ck_rhs_entry_addr(map, slot), insert);
		ck_rhs_set_probes(map, slot, n_probes);
		if (object == NULL)
			ck_rhs_add_wanted(hs, slot, -1, h);
	}

	if (object == NULL) {
		map->n_entries++;
		if ((map->n_entries ) > map->max_entries)
			ck_rhs_grow(hs, map->capacity << 1);
	}
	return true;
}

bool
ck_rhs_set(struct ck_rhs *hs,
    unsigned long h,
    const void *key,
    void **previous)
{
	long slot, first;
	const void *object;
	const void *insert;
	unsigned long n_probes;
	struct ck_rhs_map *map;

	*previous = NULL;

restart:
	map = hs->map;

	slot = map->probe_func(hs, map, &n_probes, &first, h, key, &object, map->probe_limit, CK_RHS_PROBE_INSERT);
	if (slot == -1 && first == -1) {
		if (ck_rhs_grow(hs, map->capacity << 1) == false)
			return false;

		goto restart;
	}
	ck_rhs_map_bound_set(map, h, n_probes);
	insert = ck_rhs_marshal(hs->mode, key, h);

	if (first != -1) {
		struct ck_rhs_entry_desc *desc = NULL, *desc2;
		if (slot != -1) {
			desc = ck_rhs_desc(map, slot);
			desc->in_rh = true;
		}
		desc2 = ck_rhs_desc(map, first);
		int ret = ck_rhs_put_robin_hood(hs, first, desc2);
		if (slot != -1)
			desc->in_rh = false;

		if (CK_CC_UNLIKELY(ret == 1))
			goto restart;
		if (CK_CC_UNLIKELY(ret == -1))
			return false;
		/* If an earlier bucket was found, then store entry there. */
		ck_pr_store_ptr(ck_rhs_entry_addr(map, first), insert);
		desc2->probes = n_probes;
		/*
		 * If a duplicate key was found, then delete it after
		 * signaling concurrent probes to restart. Optionally,
		 * it is possible to install tombstone after grace
		 * period if we can guarantee earlier position of
		 * duplicate key.
		 */
		ck_rhs_add_wanted(hs, first, -1, h);
		if (object != NULL) {
			ck_pr_inc_uint(&map->generation[h & CK_RHS_G_MASK]);
			ck_pr_fence_atomic_store();
			ck_rhs_do_backward_shift_delete(hs, slot);
		}

	} else {
		/*
		 * If we are storing into same slot, then atomic store is sufficient
		 * for replacement.
		 */
		ck_pr_store_ptr(ck_rhs_entry_addr(map, slot), insert);
		ck_rhs_set_probes(map, slot, n_probes);
		if (object == NULL)
			ck_rhs_add_wanted(hs, slot, -1, h);
	}

	if (object == NULL) {
		map->n_entries++;
		if ((map->n_entries ) > map->max_entries)
			ck_rhs_grow(hs, map->capacity << 1);
	}

	*previous = CK_CC_DECONST_PTR(object);
	return true;
}

static bool
ck_rhs_put_internal(struct ck_rhs *hs,
    unsigned long h,
    const void *key,
    enum ck_rhs_probe_behavior behavior)
{
	long slot, first;
	const void *object;
	const void *insert;
	unsigned long n_probes;
	struct ck_rhs_map *map;

restart:
	map = hs->map;

	slot = map->probe_func(hs, map, &n_probes, &first, h, key, &object,
	    map->probe_limit, behavior);

	if (slot == -1 && first == -1) {
		if (ck_rhs_grow(hs, map->capacity << 1) == false)
			return false;

		goto restart;
	}

	/* Fail operation if a match was found. */
	if (object != NULL)
		return false;

	ck_rhs_map_bound_set(map, h, n_probes);
	insert = ck_rhs_marshal(hs->mode, key, h);

	if (first != -1) {
		struct ck_rhs_entry_desc *desc = ck_rhs_desc(map, first);
		int ret = ck_rhs_put_robin_hood(hs, first, desc);
		if (CK_CC_UNLIKELY(ret == 1))
			return ck_rhs_put_internal(hs, h, key, behavior);
		else if (CK_CC_UNLIKELY(ret == -1))
			return false;
		/* Insert key into first bucket in probe sequence. */
		ck_pr_store_ptr(ck_rhs_entry_addr(map, first), insert);
		desc->probes = n_probes;
		ck_rhs_add_wanted(hs, first, -1, h);
	} else {
		/* An empty slot was found. */
		ck_pr_store_ptr(ck_rhs_entry_addr(map, slot), insert);
		ck_rhs_set_probes(map, slot, n_probes);
		ck_rhs_add_wanted(hs, slot, -1, h);
	}

	map->n_entries++;
	if ((map->n_entries ) > map->max_entries)
		ck_rhs_grow(hs, map->capacity << 1);
	return true;
}

bool
ck_rhs_put(struct ck_rhs *hs,
    unsigned long h,
    const void *key)
{

	return ck_rhs_put_internal(hs, h, key, CK_RHS_PROBE_INSERT);
}

bool
ck_rhs_put_unique(struct ck_rhs *hs,
    unsigned long h,
    const void *key)
{

	return ck_rhs_put_internal(hs, h, key, CK_RHS_PROBE_RH);
}

void *
ck_rhs_get(struct ck_rhs *hs,
    unsigned long h,
    const void *key)
{
	long first;
	const void *object;
	struct ck_rhs_map *map;
	unsigned long n_probes;
	unsigned int g, g_p, probe;
	unsigned int *generation;

	do {
		map = ck_pr_load_ptr(&hs->map);
		generation = &map->generation[h & CK_RHS_G_MASK];
		g = ck_pr_load_uint(generation);
		probe  = ck_rhs_map_bound_get(map, h);
		ck_pr_fence_load();

		first = -1;
		map->probe_func(hs, map, &n_probes, &first, h, key, &object, probe, CK_RHS_PROBE_NO_RH);

		ck_pr_fence_load();
		g_p = ck_pr_load_uint(generation);
	} while (g != g_p);

	return CK_CC_DECONST_PTR(object);
}

void *
ck_rhs_remove(struct ck_rhs *hs,
    unsigned long h,
    const void *key)
{
	long slot, first;
	const void *object;
	struct ck_rhs_map *map = hs->map;
	unsigned long n_probes;

	slot = map->probe_func(hs, map, &n_probes, &first, h, key, &object,
	    ck_rhs_map_bound_get(map, h), CK_RHS_PROBE_NO_RH);
	if (object == NULL)
		return NULL;

	map->n_entries--;
	ck_rhs_do_backward_shift_delete(hs, slot);
	return CK_CC_DECONST_PTR(object);
}

bool
ck_rhs_move(struct ck_rhs *hs,
    struct ck_rhs *source,
    ck_rhs_hash_cb_t *hf,
    ck_rhs_compare_cb_t *compare,
    struct ck_malloc *m)
{

	if (m == NULL || m->malloc == NULL || m->free == NULL || hf == NULL)
		return false;

	hs->mode = source->mode;
	hs->seed = source->seed;
	hs->map = source->map;
	hs->load_factor = source->load_factor;
	hs->m = m;
	hs->hf = hf;
	hs->compare = compare;
	return true;
}

bool
ck_rhs_init(struct ck_rhs *hs,
    unsigned int mode,
    ck_rhs_hash_cb_t *hf,
    ck_rhs_compare_cb_t *compare,
    struct ck_malloc *m,
    unsigned long n_entries,
    unsigned long seed)
{

	if (m == NULL || m->malloc == NULL || m->free == NULL || hf == NULL)
		return false;

	hs->m = m;
	hs->mode = mode;
	hs->seed = seed;
	hs->hf = hf;
	hs->compare = compare;
	hs->load_factor = CK_RHS_DEFAULT_LOAD_FACTOR;

	hs->map = ck_rhs_map_create(hs, n_entries);
	return hs->map != NULL;
}
