/*
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
#include <ck_hs.h>
#include <ck_limits.h>
#include <ck_md.h>
#include <ck_pr.h>
#include <ck_stdint.h>
#include <ck_stdbool.h>
#include <ck_string.h>

#include "ck_internal.h"

#ifndef CK_HS_PROBE_L1_SHIFT
#define CK_HS_PROBE_L1_SHIFT 3ULL
#endif /* CK_HS_PROBE_L1_SHIFT */

#define CK_HS_PROBE_L1 (1 << CK_HS_PROBE_L1_SHIFT)
#define CK_HS_PROBE_L1_MASK (CK_HS_PROBE_L1 - 1)

#ifndef CK_HS_PROBE_L1_DEFAULT
#define CK_HS_PROBE_L1_DEFAULT CK_MD_CACHELINE
#endif

#define CK_HS_VMA_MASK ((uintptr_t)((1ULL << CK_MD_VMA_BITS) - 1))
#define CK_HS_VMA(x)	\
	((void *)((uintptr_t)(x) & CK_HS_VMA_MASK))

#define CK_HS_EMPTY     NULL
#define CK_HS_TOMBSTONE ((void *)~(uintptr_t)0)
#define CK_HS_G		(2)
#define CK_HS_G_MASK	(CK_HS_G - 1)

#if defined(CK_F_PR_LOAD_8) && defined(CK_F_PR_STORE_8)
#define CK_HS_WORD          uint8_t
#define CK_HS_WORD_MAX	    UINT8_MAX
#define CK_HS_STORE(x, y)   ck_pr_store_8(x, y)
#define CK_HS_LOAD(x)       ck_pr_load_8(x)
#elif defined(CK_F_PR_LOAD_16) && defined(CK_F_PR_STORE_16)
#define CK_HS_WORD          uint16_t
#define CK_HS_WORD_MAX	    UINT16_MAX
#define CK_HS_STORE(x, y)   ck_pr_store_16(x, y)
#define CK_HS_LOAD(x)       ck_pr_load_16(x)
#elif defined(CK_F_PR_LOAD_32) && defined(CK_F_PR_STORE_32)
#define CK_HS_WORD          uint32_t
#define CK_HS_WORD_MAX	    UINT32_MAX
#define CK_HS_STORE(x, y)   ck_pr_store_32(x, y)
#define CK_HS_LOAD(x)       ck_pr_load_32(x)
#else
#error "ck_hs is not supported on your platform."
#endif

enum ck_hs_probe_behavior {
	CK_HS_PROBE = 0,	/* Default behavior. */
	CK_HS_PROBE_TOMBSTONE,	/* Short-circuit on tombstone. */
	CK_HS_PROBE_INSERT	/* Short-circuit on probe bound if tombstone found. */
};

struct ck_hs_map {
	unsigned int generation[CK_HS_G];
	unsigned int probe_maximum;
	unsigned long mask;
	unsigned long step;
	unsigned int probe_limit;
	unsigned int tombstones;
	unsigned long n_entries;
	unsigned long capacity;
	unsigned long size;
	CK_HS_WORD *probe_bound;
	const void **entries;
};

static inline void
ck_hs_map_signal(struct ck_hs_map *map, unsigned long h)
{

	h &= CK_HS_G_MASK;
	ck_pr_store_uint(&map->generation[h],
	    map->generation[h] + 1);
	ck_pr_fence_store();
	return;
}

static bool 
_ck_hs_next(struct ck_hs *hs, struct ck_hs_map *map, struct ck_hs_iterator *i, void **key)
{
	void *value;
	if (i->offset >= map->capacity)
		return false;

	do {
		value = CK_CC_DECONST_PTR(map->entries[i->offset]);
		if (value != CK_HS_EMPTY && value != CK_HS_TOMBSTONE) {
#ifdef CK_HS_PP
			if (hs->mode & CK_HS_MODE_OBJECT)
				value = CK_HS_VMA(value);
#else
			(void)hs; /* Avoid unused parameter warning. */
#endif
			i->offset++;
			*key = value;
			return true;
		}
	} while (++i->offset < map->capacity);

	return false;
}

void
ck_hs_iterator_init(struct ck_hs_iterator *iterator)
{

	iterator->cursor = NULL;
	iterator->offset = 0;
	iterator->map = NULL;
	return;
}

bool
ck_hs_next(struct ck_hs *hs, struct ck_hs_iterator *i, void **key)
{
	return _ck_hs_next(hs, hs->map, i, key);
}

bool
ck_hs_next_spmc(struct ck_hs *hs, struct ck_hs_iterator *i, void **key)
{
	struct ck_hs_map *m = i->map;
	if (m == NULL) {
		m = i->map = ck_pr_load_ptr(&hs->map);
	}
	return _ck_hs_next(hs, m, i, key);
}

void
ck_hs_stat(struct ck_hs *hs, struct ck_hs_stat *st)
{
	struct ck_hs_map *map = hs->map;

	st->n_entries = map->n_entries;
	st->tombstones = map->tombstones;
	st->probe_maximum = map->probe_maximum;
	return;
}

unsigned long
ck_hs_count(struct ck_hs *hs)
{

	return hs->map->n_entries;
}

static void
ck_hs_map_destroy(struct ck_malloc *m, struct ck_hs_map *map, bool defer)
{

	m->free(map, map->size, defer);
	return;
}

void
ck_hs_destroy(struct ck_hs *hs)
{

	ck_hs_map_destroy(hs->m, hs->map, false);
	return;
}

static struct ck_hs_map *
ck_hs_map_create(struct ck_hs *hs, unsigned long entries)
{
	struct ck_hs_map *map;
	unsigned long size, n_entries, prefix, limit;

	n_entries = ck_internal_power_2(entries);
	if (n_entries < CK_HS_PROBE_L1)
		n_entries = CK_HS_PROBE_L1;

	size = sizeof(struct ck_hs_map) + (sizeof(void *) * n_entries + CK_MD_CACHELINE - 1);

	if (hs->mode & CK_HS_MODE_DELETE) {
		prefix = sizeof(CK_HS_WORD) * n_entries;
		size += prefix;
	} else {
		prefix = 0;
	}

	map = hs->m->malloc(size);
	if (map == NULL)
		return NULL;

	map->size = size;

	/* We should probably use a more intelligent heuristic for default probe length. */
	limit = ck_internal_max(n_entries >> (CK_HS_PROBE_L1_SHIFT + 2), CK_HS_PROBE_L1_DEFAULT);
	if (limit > UINT_MAX)
		limit = UINT_MAX;

	map->probe_limit = (unsigned int)limit;
	map->probe_maximum = 0;
	map->capacity = n_entries;
	map->step = ck_cc_ffsl(n_entries);
	map->mask = n_entries - 1;
	map->n_entries = 0;

	/* Align map allocation to cache line. */
	map->entries = (void *)(((uintptr_t)&map[1] + prefix +
	    CK_MD_CACHELINE - 1) & ~(CK_MD_CACHELINE - 1));

	memset(map->entries, 0, sizeof(void *) * n_entries);
	memset(map->generation, 0, sizeof map->generation);

	if (hs->mode & CK_HS_MODE_DELETE) {
		map->probe_bound = (CK_HS_WORD *)&map[1];
		memset(map->probe_bound, 0, prefix);
	} else {
		map->probe_bound = NULL;
	}

	/* Commit entries purge with respect to map publication. */
	ck_pr_fence_store();
	return map;
}

bool
ck_hs_reset_size(struct ck_hs *hs, unsigned long capacity)
{
	struct ck_hs_map *map, *previous;

	previous = hs->map;
	map = ck_hs_map_create(hs, capacity);
	if (map == NULL)
		return false;

	ck_pr_store_ptr(&hs->map, map);
	ck_hs_map_destroy(hs->m, previous, true);
	return true;
}

bool
ck_hs_reset(struct ck_hs *hs)
{
	struct ck_hs_map *previous;

	previous = hs->map;
	return ck_hs_reset_size(hs, previous->capacity);
}

static inline unsigned long
ck_hs_map_probe_next(struct ck_hs_map *map,
    unsigned long offset,
    unsigned long h,
    unsigned long level,
    unsigned long probes)
{
	unsigned long r, stride;

	r = (h >> map->step) >> level;
	stride = (r & ~CK_HS_PROBE_L1_MASK) << 1 | (r & CK_HS_PROBE_L1_MASK);

	return (offset + (probes >> CK_HS_PROBE_L1_SHIFT) +
	    (stride | CK_HS_PROBE_L1)) & map->mask;
}

static inline void
ck_hs_map_bound_set(struct ck_hs_map *m,
    unsigned long h,
    unsigned long n_probes)
{
	unsigned long offset = h & m->mask;

	if (n_probes > m->probe_maximum)
		ck_pr_store_uint(&m->probe_maximum, n_probes);

	if (m->probe_bound != NULL && m->probe_bound[offset] < n_probes) {
		if (n_probes > CK_HS_WORD_MAX)
			n_probes = CK_HS_WORD_MAX;

		CK_HS_STORE(&m->probe_bound[offset], n_probes);
		ck_pr_fence_store();
	}

	return;
}

static inline unsigned int
ck_hs_map_bound_get(struct ck_hs_map *m, unsigned long h)
{
	unsigned long offset = h & m->mask;
	unsigned int r = CK_HS_WORD_MAX;

	if (m->probe_bound != NULL) {
		r = CK_HS_LOAD(&m->probe_bound[offset]);
		if (r == CK_HS_WORD_MAX)
			r = ck_pr_load_uint(&m->probe_maximum);
	} else {
		r = ck_pr_load_uint(&m->probe_maximum);
	}

	return r;
}

bool
ck_hs_grow(struct ck_hs *hs,
    unsigned long capacity)
{
	struct ck_hs_map *map, *update;
	unsigned long k, i, j, offset, probes;
	const void *previous, **bucket;

restart:
	map = hs->map;
	if (map->capacity > capacity)
		return false;

	update = ck_hs_map_create(hs, capacity);
	if (update == NULL)
		return false;

	for (k = 0; k < map->capacity; k++) {
		unsigned long h;

		previous = map->entries[k];
		if (previous == CK_HS_EMPTY || previous == CK_HS_TOMBSTONE)
			continue;

#ifdef CK_HS_PP
		if (hs->mode & CK_HS_MODE_OBJECT)
			previous = CK_HS_VMA(previous);
#endif

		h = hs->hf(previous, hs->seed);
		offset = h & update->mask;
		i = probes = 0;

		for (;;) {
			bucket = (const void **)((uintptr_t)&update->entries[offset] & ~(CK_MD_CACHELINE - 1));

			for (j = 0; j < CK_HS_PROBE_L1; j++) {
				const void **cursor = bucket + ((j + offset) & (CK_HS_PROBE_L1 - 1));

				if (probes++ == update->probe_limit)
					break;

				if (CK_CC_LIKELY(*cursor == CK_HS_EMPTY)) {
					*cursor = map->entries[k];
					update->n_entries++;

					ck_hs_map_bound_set(update, h, probes);
					break;
				}
			}

			if (j < CK_HS_PROBE_L1)
				break;

			offset = ck_hs_map_probe_next(update, offset, h, i++, probes);
		}

		if (probes > update->probe_limit) {
			/*
			 * We have hit the probe limit, map needs to be even larger.
			 */
			ck_hs_map_destroy(hs->m, update, false);
			capacity <<= 1;
			goto restart;
		}
	}

	ck_pr_fence_store();
	ck_pr_store_ptr(&hs->map, update);
	ck_hs_map_destroy(hs->m, map, true);
	return true;
}

static void
ck_hs_map_postinsert(struct ck_hs *hs, struct ck_hs_map *map)
{

	map->n_entries++;
	if ((map->n_entries << 1) > map->capacity)
		ck_hs_grow(hs, map->capacity << 1);

	return;
}

bool
ck_hs_rebuild(struct ck_hs *hs)
{

	return ck_hs_grow(hs, hs->map->capacity);
}

static const void **
ck_hs_map_probe(struct ck_hs *hs,
    struct ck_hs_map *map,
    unsigned long *n_probes,
    const void ***priority,
    unsigned long h,
    const void *key,
    const void **object,
    unsigned long probe_limit,
    enum ck_hs_probe_behavior behavior)
{
	const void **bucket, **cursor, *k, *compare;
	const void **pr = NULL;
	unsigned long offset, j, i, probes, opl;

#ifdef CK_HS_PP
	/* If we are storing object pointers, then we may leverage pointer packing. */
	unsigned long hv = 0;

	if (hs->mode & CK_HS_MODE_OBJECT) {
		hv = (h >> 25) & CK_HS_KEY_MASK;
		compare = CK_HS_VMA(key);
	} else {
		compare = key;
	}
#else
	compare = key;
#endif

	offset = h & map->mask;
	*object = NULL;
	i = probes = 0;

	opl = probe_limit;
	if (behavior == CK_HS_PROBE_INSERT)
		probe_limit = ck_hs_map_bound_get(map, h);

	for (;;) {
		bucket = (const void **)((uintptr_t)&map->entries[offset] & ~(CK_MD_CACHELINE - 1));

		for (j = 0; j < CK_HS_PROBE_L1; j++) {
			cursor = bucket + ((j + offset) & (CK_HS_PROBE_L1 - 1));

			if (probes++ == probe_limit) {
				if (probe_limit == opl || pr != NULL) {
					k = CK_HS_EMPTY;
					goto leave;
				}

				/*
				 * If no eligible slot has been found yet, continue probe
				 * sequence with original probe limit.
				 */
				probe_limit = opl;
			}

			k = ck_pr_load_ptr(cursor);
			if (k == CK_HS_EMPTY)
				goto leave;

			if (k == CK_HS_TOMBSTONE) {
				if (pr == NULL) {
					pr = cursor;
					*n_probes = probes;

					if (behavior == CK_HS_PROBE_TOMBSTONE) {
						k = CK_HS_EMPTY;
						goto leave;
					}
				}

				continue;
			}

#ifdef CK_HS_PP
			if (hs->mode & CK_HS_MODE_OBJECT) {
				if (((uintptr_t)k >> CK_MD_VMA_BITS) != hv)
					continue;

				k = CK_HS_VMA(k);
			}
#endif

			if (k == compare)
				goto leave;

			if (hs->compare == NULL)
				continue;

			if (hs->compare(k, key) == true)
				goto leave;
		}

		offset = ck_hs_map_probe_next(map, offset, h, i++, probes);
	}

leave:
	if (probes > probe_limit) {
		cursor = NULL;
	} else {
		*object = k;
	}

	if (pr == NULL)
		*n_probes = probes;

	*priority = pr;
	return cursor;
}

static inline const void *
ck_hs_marshal(unsigned int mode, const void *key, unsigned long h)
{
#ifdef CK_HS_PP
	const void *insert;

	if (mode & CK_HS_MODE_OBJECT) {
		insert = (void *)((uintptr_t)CK_HS_VMA(key) |
		    ((h >> 25) << CK_MD_VMA_BITS));
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
ck_hs_gc(struct ck_hs *hs, unsigned long cycles, unsigned long seed)
{
	unsigned long size = 0;
	unsigned long i;
	struct ck_hs_map *map = hs->map;
	unsigned int maximum;
	CK_HS_WORD *bounds = NULL;

	if (map->n_entries == 0) {
		ck_pr_store_uint(&map->probe_maximum, 0);
		if (map->probe_bound != NULL)
			memset(map->probe_bound, 0, sizeof(CK_HS_WORD) * map->capacity);

		return true;
	}

	if (cycles == 0) {
		maximum = 0;

		if (map->probe_bound != NULL) {
			size = sizeof(CK_HS_WORD) * map->capacity;
			bounds = hs->m->malloc(size);
			if (bounds == NULL)
				return false;

			memset(bounds, 0, size);
		}
	} else {
		maximum = map->probe_maximum;
	}

	for (i = 0; i < map->capacity; i++) {
		const void **first, *object, **slot, *entry;
		unsigned long n_probes, offset, h;

		entry = map->entries[(i + seed) & map->mask];
		if (entry == CK_HS_EMPTY || entry == CK_HS_TOMBSTONE)
			continue;

#ifdef CK_HS_PP
		if (hs->mode & CK_HS_MODE_OBJECT)
			entry = CK_HS_VMA(entry);
#endif

		h = hs->hf(entry, hs->seed);
		offset = h & map->mask;

		slot = ck_hs_map_probe(hs, map, &n_probes, &first, h, entry, &object,
		    ck_hs_map_bound_get(map, h), CK_HS_PROBE);

		if (first != NULL) {
			const void *insert = ck_hs_marshal(hs->mode, entry, h);

			ck_pr_store_ptr(first, insert);
			ck_hs_map_signal(map, h);
			ck_pr_store_ptr(slot, CK_HS_TOMBSTONE);
		}

		if (cycles == 0) {
			if (n_probes > maximum)
				maximum = n_probes;

			if (n_probes > CK_HS_WORD_MAX)
				n_probes = CK_HS_WORD_MAX;

			if (bounds != NULL && n_probes > bounds[offset])
				bounds[offset] = n_probes;
		} else if (--cycles == 0)
			break;
	}

	/*
	 * The following only apply to garbage collection involving
	 * a full scan of all entries.
	 */
	if (maximum != map->probe_maximum)
		ck_pr_store_uint(&map->probe_maximum, maximum);

	if (bounds != NULL) {
		for (i = 0; i < map->capacity; i++)
			CK_HS_STORE(&map->probe_bound[i], bounds[i]);

		hs->m->free(bounds, size, false);
	}

	return true;
}

bool
ck_hs_fas(struct ck_hs *hs,
    unsigned long h,
    const void *key,
    void **previous)
{
	const void **slot, **first, *object, *insert;
	struct ck_hs_map *map = hs->map;
	unsigned long n_probes;

	*previous = NULL;
	slot = ck_hs_map_probe(hs, map, &n_probes, &first, h, key, &object,
	    ck_hs_map_bound_get(map, h), CK_HS_PROBE);

	/* Replacement semantics presume existence. */
	if (object == NULL)
		return false;

	insert = ck_hs_marshal(hs->mode, key, h);

	if (first != NULL) {
		ck_pr_store_ptr(first, insert);
		ck_hs_map_signal(map, h);
		ck_pr_store_ptr(slot, CK_HS_TOMBSTONE);
	} else {
		ck_pr_store_ptr(slot, insert);
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
ck_hs_apply(struct ck_hs *hs,
    unsigned long h,
    const void *key,
    ck_hs_apply_fn_t *fn,
    void *cl)
{
	const void **slot, **first, *object, *delta, *insert;
	unsigned long n_probes;
	struct ck_hs_map *map;

restart:
	map = hs->map;

	slot = ck_hs_map_probe(hs, map, &n_probes, &first, h, key, &object, map->probe_limit, CK_HS_PROBE_INSERT);
	if (slot == NULL && first == NULL) {
		if (ck_hs_grow(hs, map->capacity << 1) == false)
			return false;

		goto restart;
	}

	delta = fn(CK_CC_DECONST_PTR(object), cl);
	if (delta == NULL) {
		/*
		 * The apply function has requested deletion. If the object doesn't exist,
		 * then exit early.
		 */
		if (CK_CC_UNLIKELY(object == NULL))
			return true;

		/* Otherwise, mark slot as deleted. */
		ck_pr_store_ptr(slot, CK_HS_TOMBSTONE);
		map->n_entries--;
		map->tombstones++;
		return true;
	}

	/* The apply function has not requested hash set modification so exit early. */
	if (delta == object)
		return true;

	/* A modification or insertion has been requested. */
	ck_hs_map_bound_set(map, h, n_probes);

	insert = ck_hs_marshal(hs->mode, delta, h);
	if (first != NULL) {
		/*
		 * This follows the same semantics as ck_hs_set, please refer to that
		 * function for documentation.
		 */
		ck_pr_store_ptr(first, insert);

		if (object != NULL) {
			ck_hs_map_signal(map, h);
			ck_pr_store_ptr(slot, CK_HS_TOMBSTONE);
		}
	} else {
		/*
		 * If we are storing into same slot, then atomic store is sufficient
		 * for replacement.
		 */
		ck_pr_store_ptr(slot, insert);
	}

	if (object == NULL)
		ck_hs_map_postinsert(hs, map);

	return true;
}

bool
ck_hs_set(struct ck_hs *hs,
    unsigned long h,
    const void *key,
    void **previous)
{
	const void **slot, **first, *object, *insert;
	unsigned long n_probes;
	struct ck_hs_map *map;

	*previous = NULL;

restart:
	map = hs->map;

	slot = ck_hs_map_probe(hs, map, &n_probes, &first, h, key, &object, map->probe_limit, CK_HS_PROBE_INSERT);
	if (slot == NULL && first == NULL) {
		if (ck_hs_grow(hs, map->capacity << 1) == false)
			return false;

		goto restart;
	}

	ck_hs_map_bound_set(map, h, n_probes);
	insert = ck_hs_marshal(hs->mode, key, h);

	if (first != NULL) {
		/* If an earlier bucket was found, then store entry there. */
		ck_pr_store_ptr(first, insert);

		/*
		 * If a duplicate key was found, then delete it after
		 * signaling concurrent probes to restart. Optionally,
		 * it is possible to install tombstone after grace
		 * period if we can guarantee earlier position of
		 * duplicate key.
		 */
		if (object != NULL) {
			ck_hs_map_signal(map, h);
			ck_pr_store_ptr(slot, CK_HS_TOMBSTONE);
		}
	} else {
		/*
		 * If we are storing into same slot, then atomic store is sufficient
		 * for replacement.
		 */
		ck_pr_store_ptr(slot, insert);
	}

	if (object == NULL)
		ck_hs_map_postinsert(hs, map);

	*previous = CK_CC_DECONST_PTR(object);
	return true;
}

CK_CC_INLINE static bool
ck_hs_put_internal(struct ck_hs *hs,
    unsigned long h,
    const void *key,
    enum ck_hs_probe_behavior behavior)
{
	const void **slot, **first, *object, *insert;
	unsigned long n_probes;
	struct ck_hs_map *map;

restart:
	map = hs->map;

	slot = ck_hs_map_probe(hs, map, &n_probes, &first, h, key, &object,
	    map->probe_limit, behavior);

	if (slot == NULL && first == NULL) {
		if (ck_hs_grow(hs, map->capacity << 1) == false)
			return false;

		goto restart;
	}

	/* Fail operation if a match was found. */
	if (object != NULL)
		return false;

	ck_hs_map_bound_set(map, h, n_probes);
	insert = ck_hs_marshal(hs->mode, key, h);

	if (first != NULL) {
		/* Insert key into first bucket in probe sequence. */
		ck_pr_store_ptr(first, insert);
	} else {
		/* An empty slot was found. */
		ck_pr_store_ptr(slot, insert);
	}

	ck_hs_map_postinsert(hs, map);
	return true;
}

bool
ck_hs_put(struct ck_hs *hs,
    unsigned long h,
    const void *key)
{

	return ck_hs_put_internal(hs, h, key, CK_HS_PROBE_INSERT);
}

bool
ck_hs_put_unique(struct ck_hs *hs,
    unsigned long h,
    const void *key)
{

	return ck_hs_put_internal(hs, h, key, CK_HS_PROBE_TOMBSTONE);
}

void *
ck_hs_get(struct ck_hs *hs,
    unsigned long h,
    const void *key)
{
	const void **first, *object;
	struct ck_hs_map *map;
	unsigned long n_probes;
	unsigned int g, g_p, probe;
	unsigned int *generation;

	do {
		map = ck_pr_load_ptr(&hs->map);
		generation = &map->generation[h & CK_HS_G_MASK];
		g = ck_pr_load_uint(generation);
		probe  = ck_hs_map_bound_get(map, h);
		ck_pr_fence_load();

		ck_hs_map_probe(hs, map, &n_probes, &first, h, key, &object, probe, CK_HS_PROBE);

		ck_pr_fence_load();
		g_p = ck_pr_load_uint(generation);
	} while (g != g_p);

	return CK_CC_DECONST_PTR(object);
}

void *
ck_hs_remove(struct ck_hs *hs,
    unsigned long h,
    const void *key)
{
	const void **slot, **first, *object;
	struct ck_hs_map *map = hs->map;
	unsigned long n_probes;

	slot = ck_hs_map_probe(hs, map, &n_probes, &first, h, key, &object,
	    ck_hs_map_bound_get(map, h), CK_HS_PROBE);
	if (object == NULL)
		return NULL;

	ck_pr_store_ptr(slot, CK_HS_TOMBSTONE);
	map->n_entries--;
	map->tombstones++;
	return CK_CC_DECONST_PTR(object);
}

bool
ck_hs_move(struct ck_hs *hs,
    struct ck_hs *source,
    ck_hs_hash_cb_t *hf,
    ck_hs_compare_cb_t *compare,
    struct ck_malloc *m)
{

	if (m == NULL || m->malloc == NULL || m->free == NULL || hf == NULL)
		return false;

	hs->mode = source->mode;
	hs->seed = source->seed;
	hs->map = source->map;
	hs->m = m;
	hs->hf = hf;
	hs->compare = compare;
	return true;
}

bool
ck_hs_init(struct ck_hs *hs,
    unsigned int mode,
    ck_hs_hash_cb_t *hf,
    ck_hs_compare_cb_t *compare,
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

	hs->map = ck_hs_map_create(hs, n_entries);
	return hs->map != NULL;
}
