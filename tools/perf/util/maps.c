// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <stdlib.h>
#include <linux/zalloc.h>
#include "debug.h"
#include "dso.h"
#include "map.h"
#include "maps.h"
#include "rwsem.h"
#include "thread.h"
#include "ui/ui.h"
#include "unwind.h"
#include <internal/rc_check.h>

/*
 * Locking/sorting note:
 *
 * Sorting is done with the write lock, iteration and binary searching happens
 * under the read lock requiring being sorted. There is a race between sorting
 * releasing the write lock and acquiring the read lock for iteration/searching
 * where another thread could insert and break the sorting of the maps. In
 * practice inserting maps should be rare meaning that the race shouldn't lead
 * to live lock. Removal of maps doesn't break being sorted.
 */

DECLARE_RC_STRUCT(maps) {
	struct rw_semaphore lock;
	/**
	 * @maps_by_address: array of maps sorted by their starting address if
	 * maps_by_address_sorted is true.
	 */
	struct map	 **maps_by_address;
	/**
	 * @maps_by_name: optional array of maps sorted by their dso name if
	 * maps_by_name_sorted is true.
	 */
	struct map	 **maps_by_name;
	struct machine	 *machine;
#ifdef HAVE_LIBUNWIND_SUPPORT
	void		*addr_space;
	const struct unwind_libunwind_ops *unwind_libunwind_ops;
#endif
	refcount_t	 refcnt;
	/**
	 * @nr_maps: number of maps_by_address, and possibly maps_by_name,
	 * entries that contain maps.
	 */
	unsigned int	 nr_maps;
	/**
	 * @nr_maps_allocated: number of entries in maps_by_address and possibly
	 * maps_by_name.
	 */
	unsigned int	 nr_maps_allocated;
	/**
	 * @last_search_by_name_idx: cache of last found by name entry's index
	 * as frequent searches for the same dso name are common.
	 */
	unsigned int	 last_search_by_name_idx;
	/** @maps_by_address_sorted: is maps_by_address sorted. */
	bool		 maps_by_address_sorted;
	/** @maps_by_name_sorted: is maps_by_name sorted. */
	bool		 maps_by_name_sorted;
	/** @ends_broken: does the map contain a map where end values are unset/unsorted? */
	bool		 ends_broken;
};

static void check_invariants(const struct maps *maps __maybe_unused)
{
#ifndef NDEBUG
	assert(RC_CHK_ACCESS(maps)->nr_maps <= RC_CHK_ACCESS(maps)->nr_maps_allocated);
	for (unsigned int i = 0; i < RC_CHK_ACCESS(maps)->nr_maps; i++) {
		struct map *map = RC_CHK_ACCESS(maps)->maps_by_address[i];

		/* Check map is well-formed. */
		assert(map__end(map) == 0 || map__start(map) <= map__end(map));
		/* Expect at least 1 reference count. */
		assert(refcount_read(map__refcnt(map)) > 0);

		if (map__dso(map) && dso__kernel(map__dso(map)))
			assert(RC_CHK_EQUAL(map__kmap(map)->kmaps, maps));

		if (i > 0) {
			struct map *prev = RC_CHK_ACCESS(maps)->maps_by_address[i - 1];

			/* If addresses are sorted... */
			if (RC_CHK_ACCESS(maps)->maps_by_address_sorted) {
				/* Maps should be in start address order. */
				assert(map__start(prev) <= map__start(map));
				/*
				 * If the ends of maps aren't broken (during
				 * construction) then they should be ordered
				 * too.
				 */
				if (!RC_CHK_ACCESS(maps)->ends_broken) {
					assert(map__end(prev) <= map__end(map));
					assert(map__end(prev) <= map__start(map) ||
					       map__start(prev) == map__start(map));
				}
			}
		}
	}
	if (RC_CHK_ACCESS(maps)->maps_by_name) {
		for (unsigned int i = 0; i < RC_CHK_ACCESS(maps)->nr_maps; i++) {
			struct map *map = RC_CHK_ACCESS(maps)->maps_by_name[i];

			/*
			 * Maps by name maps should be in maps_by_address, so
			 * the reference count should be higher.
			 */
			assert(refcount_read(map__refcnt(map)) > 1);
		}
	}
#endif
}

static struct map **maps__maps_by_address(const struct maps *maps)
{
	return RC_CHK_ACCESS(maps)->maps_by_address;
}

static void maps__set_maps_by_address(struct maps *maps, struct map **new)
{
	RC_CHK_ACCESS(maps)->maps_by_address = new;

}

static void maps__set_nr_maps_allocated(struct maps *maps, unsigned int nr_maps_allocated)
{
	RC_CHK_ACCESS(maps)->nr_maps_allocated = nr_maps_allocated;
}

static void maps__set_nr_maps(struct maps *maps, unsigned int nr_maps)
{
	RC_CHK_ACCESS(maps)->nr_maps = nr_maps;
}

/* Not in the header, to aid reference counting. */
static struct map **maps__maps_by_name(const struct maps *maps)
{
	return RC_CHK_ACCESS(maps)->maps_by_name;

}

static void maps__set_maps_by_name(struct maps *maps, struct map **new)
{
	RC_CHK_ACCESS(maps)->maps_by_name = new;

}

static bool maps__maps_by_address_sorted(const struct maps *maps)
{
	return RC_CHK_ACCESS(maps)->maps_by_address_sorted;
}

static void maps__set_maps_by_address_sorted(struct maps *maps, bool value)
{
	RC_CHK_ACCESS(maps)->maps_by_address_sorted = value;
}

static bool maps__maps_by_name_sorted(const struct maps *maps)
{
	return RC_CHK_ACCESS(maps)->maps_by_name_sorted;
}

static void maps__set_maps_by_name_sorted(struct maps *maps, bool value)
{
	RC_CHK_ACCESS(maps)->maps_by_name_sorted = value;
}

struct machine *maps__machine(const struct maps *maps)
{
	return RC_CHK_ACCESS(maps)->machine;
}

unsigned int maps__nr_maps(const struct maps *maps)
{
	return RC_CHK_ACCESS(maps)->nr_maps;
}

refcount_t *maps__refcnt(struct maps *maps)
{
	return &RC_CHK_ACCESS(maps)->refcnt;
}

#ifdef HAVE_LIBUNWIND_SUPPORT
void *maps__addr_space(const struct maps *maps)
{
	return RC_CHK_ACCESS(maps)->addr_space;
}

void maps__set_addr_space(struct maps *maps, void *addr_space)
{
	RC_CHK_ACCESS(maps)->addr_space = addr_space;
}

const struct unwind_libunwind_ops *maps__unwind_libunwind_ops(const struct maps *maps)
{
	return RC_CHK_ACCESS(maps)->unwind_libunwind_ops;
}

void maps__set_unwind_libunwind_ops(struct maps *maps, const struct unwind_libunwind_ops *ops)
{
	RC_CHK_ACCESS(maps)->unwind_libunwind_ops = ops;
}
#endif

static struct rw_semaphore *maps__lock(struct maps *maps)
{
	return &RC_CHK_ACCESS(maps)->lock;
}

static void maps__init(struct maps *maps, struct machine *machine)
{
	init_rwsem(maps__lock(maps));
	RC_CHK_ACCESS(maps)->maps_by_address = NULL;
	RC_CHK_ACCESS(maps)->maps_by_name = NULL;
	RC_CHK_ACCESS(maps)->machine = machine;
#ifdef HAVE_LIBUNWIND_SUPPORT
	RC_CHK_ACCESS(maps)->addr_space = NULL;
	RC_CHK_ACCESS(maps)->unwind_libunwind_ops = NULL;
#endif
	refcount_set(maps__refcnt(maps), 1);
	RC_CHK_ACCESS(maps)->nr_maps = 0;
	RC_CHK_ACCESS(maps)->nr_maps_allocated = 0;
	RC_CHK_ACCESS(maps)->last_search_by_name_idx = 0;
	RC_CHK_ACCESS(maps)->maps_by_address_sorted = true;
	RC_CHK_ACCESS(maps)->maps_by_name_sorted = false;
}

static void maps__exit(struct maps *maps)
{
	struct map **maps_by_address = maps__maps_by_address(maps);
	struct map **maps_by_name = maps__maps_by_name(maps);

	for (unsigned int i = 0; i < maps__nr_maps(maps); i++) {
		map__zput(maps_by_address[i]);
		if (maps_by_name)
			map__zput(maps_by_name[i]);
	}
	zfree(&maps_by_address);
	zfree(&maps_by_name);
	unwind__finish_access(maps);
}

struct maps *maps__new(struct machine *machine)
{
	struct maps *result;
	RC_STRUCT(maps) *maps = zalloc(sizeof(*maps));

	if (ADD_RC_CHK(result, maps))
		maps__init(result, machine);

	return result;
}

static void maps__delete(struct maps *maps)
{
	maps__exit(maps);
	RC_CHK_FREE(maps);
}

struct maps *maps__get(struct maps *maps)
{
	struct maps *result;

	if (RC_CHK_GET(result, maps))
		refcount_inc(maps__refcnt(maps));

	return result;
}

void maps__put(struct maps *maps)
{
	if (maps && refcount_dec_and_test(maps__refcnt(maps)))
		maps__delete(maps);
	else
		RC_CHK_PUT(maps);
}

static void __maps__free_maps_by_name(struct maps *maps)
{
	if (!maps__maps_by_name(maps))
		return;

	/*
	 * Free everything to try to do it from the rbtree in the next search
	 */
	for (unsigned int i = 0; i < maps__nr_maps(maps); i++)
		map__put(maps__maps_by_name(maps)[i]);

	zfree(&RC_CHK_ACCESS(maps)->maps_by_name);

	/* Consistent with maps__init(). When maps_by_name == NULL, maps_by_name_sorted == false */
	maps__set_maps_by_name_sorted(maps, false);
}

static int map__start_cmp(const void *a, const void *b)
{
	const struct map *map_a = *(const struct map * const *)a;
	const struct map *map_b = *(const struct map * const *)b;
	u64 map_a_start = map__start(map_a);
	u64 map_b_start = map__start(map_b);

	if (map_a_start == map_b_start) {
		u64 map_a_end = map__end(map_a);
		u64 map_b_end = map__end(map_b);

		if  (map_a_end == map_b_end) {
			/* Ensure maps with the same addresses have a fixed order. */
			if (RC_CHK_ACCESS(map_a) == RC_CHK_ACCESS(map_b))
				return 0;
			return (intptr_t)RC_CHK_ACCESS(map_a) > (intptr_t)RC_CHK_ACCESS(map_b)
				? 1 : -1;
		}
		return map_a_end > map_b_end ? 1 : -1;
	}
	return map_a_start > map_b_start ? 1 : -1;
}

static void __maps__sort_by_address(struct maps *maps)
{
	if (maps__maps_by_address_sorted(maps))
		return;

	qsort(maps__maps_by_address(maps),
		maps__nr_maps(maps),
		sizeof(struct map *),
		map__start_cmp);
	maps__set_maps_by_address_sorted(maps, true);
}

static void maps__sort_by_address(struct maps *maps)
{
	down_write(maps__lock(maps));
	__maps__sort_by_address(maps);
	up_write(maps__lock(maps));
}

static int map__strcmp(const void *a, const void *b)
{
	const struct map *map_a = *(const struct map * const *)a;
	const struct map *map_b = *(const struct map * const *)b;
	const struct dso *dso_a = map__dso(map_a);
	const struct dso *dso_b = map__dso(map_b);
	int ret = strcmp(dso__short_name(dso_a), dso__short_name(dso_b));

	if (ret == 0 && RC_CHK_ACCESS(map_a) != RC_CHK_ACCESS(map_b)) {
		/* Ensure distinct but name equal maps have an order. */
		return map__start_cmp(a, b);
	}
	return ret;
}

static int maps__sort_by_name(struct maps *maps)
{
	int err = 0;

	down_write(maps__lock(maps));
	if (!maps__maps_by_name_sorted(maps)) {
		struct map **maps_by_name = maps__maps_by_name(maps);

		if (!maps_by_name) {
			maps_by_name = malloc(RC_CHK_ACCESS(maps)->nr_maps_allocated *
					sizeof(*maps_by_name));
			if (!maps_by_name)
				err = -ENOMEM;
			else {
				struct map **maps_by_address = maps__maps_by_address(maps);
				unsigned int n = maps__nr_maps(maps);

				maps__set_maps_by_name(maps, maps_by_name);
				for (unsigned int i = 0; i < n; i++)
					maps_by_name[i] = map__get(maps_by_address[i]);
			}
		}
		if (!err) {
			qsort(maps_by_name,
				maps__nr_maps(maps),
				sizeof(struct map *),
				map__strcmp);
			maps__set_maps_by_name_sorted(maps, true);
		}
	}
	check_invariants(maps);
	up_write(maps__lock(maps));
	return err;
}

static unsigned int maps__by_address_index(const struct maps *maps, const struct map *map)
{
	struct map **maps_by_address = maps__maps_by_address(maps);

	if (maps__maps_by_address_sorted(maps)) {
		struct map **mapp =
			bsearch(&map, maps__maps_by_address(maps), maps__nr_maps(maps),
				sizeof(*mapp), map__start_cmp);

		if (mapp)
			return mapp - maps_by_address;
	} else {
		for (unsigned int i = 0; i < maps__nr_maps(maps); i++) {
			if (RC_CHK_ACCESS(maps_by_address[i]) == RC_CHK_ACCESS(map))
				return i;
		}
	}
	pr_err("Map missing from maps");
	return -1;
}

static unsigned int maps__by_name_index(const struct maps *maps, const struct map *map)
{
	struct map **maps_by_name = maps__maps_by_name(maps);

	if (maps__maps_by_name_sorted(maps)) {
		struct map **mapp =
			bsearch(&map, maps_by_name, maps__nr_maps(maps),
				sizeof(*mapp), map__strcmp);

		if (mapp)
			return mapp - maps_by_name;
	} else {
		for (unsigned int i = 0; i < maps__nr_maps(maps); i++) {
			if (RC_CHK_ACCESS(maps_by_name[i]) == RC_CHK_ACCESS(map))
				return i;
		}
	}
	pr_err("Map missing from maps");
	return -1;
}

static void map__set_kmap_maps(struct map *map, struct maps *maps)
{
	struct dso *dso;

	if (map == NULL)
		return;

	dso = map__dso(map);

	if (dso && dso__kernel(dso)) {
                struct kmap *kmap = map__kmap(map);

                if (kmap)
                        kmap->kmaps = maps;
                else
                        pr_err("Internal error: kernel dso with non kernel map\n");
        }
}

static int __maps__insert(struct maps *maps, struct map *new)
{
	struct map **maps_by_address = maps__maps_by_address(maps);
	struct map **maps_by_name = maps__maps_by_name(maps);
	unsigned int nr_maps = maps__nr_maps(maps);
	unsigned int nr_allocate = RC_CHK_ACCESS(maps)->nr_maps_allocated;

	if (nr_maps + 1 > nr_allocate) {
		nr_allocate = !nr_allocate ? 32 : nr_allocate * 2;

		maps_by_address = realloc(maps_by_address, nr_allocate * sizeof(new));
		if (!maps_by_address)
			return -ENOMEM;

		maps__set_maps_by_address(maps, maps_by_address);
		if (maps_by_name) {
			maps_by_name = realloc(maps_by_name, nr_allocate * sizeof(new));
			if (!maps_by_name) {
				/*
				 * If by name fails, just disable by name and it will
				 * recompute next time it is required.
				 */
				__maps__free_maps_by_name(maps);
			}
			maps__set_maps_by_name(maps, maps_by_name);
		}
		RC_CHK_ACCESS(maps)->nr_maps_allocated = nr_allocate;
	}
	/* Insert the value at the end. */
	maps_by_address[nr_maps] = map__get(new);
	if (maps_by_name)
		maps_by_name[nr_maps] = map__get(new);

	nr_maps++;
	RC_CHK_ACCESS(maps)->nr_maps = nr_maps;

	/*
	 * Recompute if things are sorted. If things are inserted in a sorted
	 * manner, for example by processing /proc/pid/maps, then no
	 * sorting/resorting will be necessary.
	 */
	if (nr_maps == 1) {
		/* If there's just 1 entry then maps are sorted. */
		maps__set_maps_by_address_sorted(maps, true);
		maps__set_maps_by_name_sorted(maps, maps_by_name != NULL);
	} else {
		/* Sorted if maps were already sorted and this map starts after the last one. */
		maps__set_maps_by_address_sorted(maps,
			maps__maps_by_address_sorted(maps) &&
			map__end(maps_by_address[nr_maps - 2]) <= map__start(new));
		maps__set_maps_by_name_sorted(maps, false);
	}
	if (map__end(new) < map__start(new))
		RC_CHK_ACCESS(maps)->ends_broken = true;

	map__set_kmap_maps(new, maps);

	return 0;
}

int maps__insert(struct maps *maps, struct map *map)
{
	int ret;

	down_write(maps__lock(maps));
	ret = __maps__insert(maps, map);
	check_invariants(maps);
	up_write(maps__lock(maps));
	return ret;
}

static void __maps__remove(struct maps *maps, struct map *map)
{
	struct map **maps_by_address = maps__maps_by_address(maps);
	struct map **maps_by_name = maps__maps_by_name(maps);
	unsigned int nr_maps = maps__nr_maps(maps);
	unsigned int address_idx;

	/* Slide later mappings over the one to remove */
	address_idx = maps__by_address_index(maps, map);
	map__put(maps_by_address[address_idx]);
	memmove(&maps_by_address[address_idx],
		&maps_by_address[address_idx + 1],
		(nr_maps - address_idx - 1) * sizeof(*maps_by_address));

	if (maps_by_name) {
		unsigned int name_idx = maps__by_name_index(maps, map);

		map__put(maps_by_name[name_idx]);
		memmove(&maps_by_name[name_idx],
			&maps_by_name[name_idx + 1],
			(nr_maps - name_idx - 1) *  sizeof(*maps_by_name));
	}

	--RC_CHK_ACCESS(maps)->nr_maps;
}

void maps__remove(struct maps *maps, struct map *map)
{
	down_write(maps__lock(maps));
	__maps__remove(maps, map);
	check_invariants(maps);
	up_write(maps__lock(maps));
}

bool maps__empty(struct maps *maps)
{
	bool res;

	down_read(maps__lock(maps));
	res = maps__nr_maps(maps) == 0;
	up_read(maps__lock(maps));

	return res;
}

bool maps__equal(struct maps *a, struct maps *b)
{
	return RC_CHK_EQUAL(a, b);
}

int maps__for_each_map(struct maps *maps, int (*cb)(struct map *map, void *data), void *data)
{
	bool done = false;
	int ret = 0;

	/* See locking/sorting note. */
	while (!done) {
		down_read(maps__lock(maps));
		if (maps__maps_by_address_sorted(maps)) {
			/*
			 * maps__for_each_map callbacks may buggily/unsafely
			 * insert into maps_by_address. Deliberately reload
			 * maps__nr_maps and maps_by_address on each iteration
			 * to avoid using memory freed by maps__insert growing
			 * the array - this may cause maps to be skipped or
			 * repeated.
			 */
			for (unsigned int i = 0; i < maps__nr_maps(maps); i++) {
				struct map **maps_by_address = maps__maps_by_address(maps);
				struct map *map = maps_by_address[i];

				ret = cb(map, data);
				if (ret)
					break;
			}
			done = true;
		}
		up_read(maps__lock(maps));
		if (!done)
			maps__sort_by_address(maps);
	}
	return ret;
}

void maps__remove_maps(struct maps *maps, bool (*cb)(struct map *map, void *data), void *data)
{
	struct map **maps_by_address;

	down_write(maps__lock(maps));

	maps_by_address = maps__maps_by_address(maps);
	for (unsigned int i = 0; i < maps__nr_maps(maps);) {
		if (cb(maps_by_address[i], data))
			__maps__remove(maps, maps_by_address[i]);
		else
			i++;
	}
	check_invariants(maps);
	up_write(maps__lock(maps));
}

struct symbol *maps__find_symbol(struct maps *maps, u64 addr, struct map **mapp)
{
	struct map *map = maps__find(maps, addr);
	struct symbol *result = NULL;

	/* Ensure map is loaded before using map->map_ip */
	if (map != NULL && map__load(map) >= 0)
		result = map__find_symbol(map, map__map_ip(map, addr));

	if (mapp)
		*mapp = map;
	else
		map__put(map);

	return result;
}

struct maps__find_symbol_by_name_args {
	struct map **mapp;
	const char *name;
	struct symbol *sym;
};

static int maps__find_symbol_by_name_cb(struct map *map, void *data)
{
	struct maps__find_symbol_by_name_args *args = data;

	args->sym = map__find_symbol_by_name(map, args->name);
	if (!args->sym)
		return 0;

	if (!map__contains_symbol(map, args->sym)) {
		args->sym = NULL;
		return 0;
	}

	if (args->mapp != NULL)
		*args->mapp = map__get(map);
	return 1;
}

struct symbol *maps__find_symbol_by_name(struct maps *maps, const char *name, struct map **mapp)
{
	struct maps__find_symbol_by_name_args args = {
		.mapp = mapp,
		.name = name,
		.sym = NULL,
	};

	maps__for_each_map(maps, maps__find_symbol_by_name_cb, &args);
	return args.sym;
}

int maps__find_ams(struct maps *maps, struct addr_map_symbol *ams)
{
	if (ams->addr < map__start(ams->ms.map) || ams->addr >= map__end(ams->ms.map)) {
		if (maps == NULL)
			return -1;
		ams->ms.map = maps__find(maps, ams->addr);
		if (ams->ms.map == NULL)
			return -1;
	}

	ams->al_addr = map__map_ip(ams->ms.map, ams->addr);
	ams->ms.sym = map__find_symbol(ams->ms.map, ams->al_addr);

	return ams->ms.sym ? 0 : -1;
}

struct maps__fprintf_args {
	FILE *fp;
	size_t printed;
};

static int maps__fprintf_cb(struct map *map, void *data)
{
	struct maps__fprintf_args *args = data;

	args->printed += fprintf(args->fp, "Map:");
	args->printed += map__fprintf(map, args->fp);
	if (verbose > 2) {
		args->printed += dso__fprintf(map__dso(map), args->fp);
		args->printed += fprintf(args->fp, "--\n");
	}
	return 0;
}

size_t maps__fprintf(struct maps *maps, FILE *fp)
{
	struct maps__fprintf_args args = {
		.fp = fp,
		.printed = 0,
	};

	maps__for_each_map(maps, maps__fprintf_cb, &args);

	return args.printed;
}

/*
 * Find first map where end > map->start.
 * Same as find_vma() in kernel.
 */
static unsigned int first_ending_after(struct maps *maps, const struct map *map)
{
	struct map **maps_by_address = maps__maps_by_address(maps);
	int low = 0, high = (int)maps__nr_maps(maps) - 1, first = high + 1;

	assert(maps__maps_by_address_sorted(maps));
	if (low <= high && map__end(maps_by_address[0]) > map__start(map))
		return 0;

	while (low <= high) {
		int mid = (low + high) / 2;
		struct map *pos = maps_by_address[mid];

		if (map__end(pos) > map__start(map)) {
			first = mid;
			if (map__start(pos) <= map__start(map)) {
				/* Entry overlaps map. */
				break;
			}
			high = mid - 1;
		} else
			low = mid + 1;
	}
	return first;
}

static int __maps__insert_sorted(struct maps *maps, unsigned int first_after_index,
				 struct map *new1, struct map *new2)
{
	struct map **maps_by_address = maps__maps_by_address(maps);
	struct map **maps_by_name = maps__maps_by_name(maps);
	unsigned int nr_maps = maps__nr_maps(maps);
	unsigned int nr_allocate = RC_CHK_ACCESS(maps)->nr_maps_allocated;
	unsigned int to_add = new2 ? 2 : 1;

	assert(maps__maps_by_address_sorted(maps));
	assert(first_after_index == nr_maps ||
	       map__end(new1) <= map__start(maps_by_address[first_after_index]));
	assert(!new2 || map__end(new1) <= map__start(new2));
	assert(first_after_index == nr_maps || !new2 ||
	       map__end(new2) <= map__start(maps_by_address[first_after_index]));

	if (nr_maps + to_add > nr_allocate) {
		nr_allocate = !nr_allocate ? 32 : nr_allocate * 2;

		maps_by_address = realloc(maps_by_address, nr_allocate * sizeof(new1));
		if (!maps_by_address)
			return -ENOMEM;

		maps__set_maps_by_address(maps, maps_by_address);
		if (maps_by_name) {
			maps_by_name = realloc(maps_by_name, nr_allocate * sizeof(new1));
			if (!maps_by_name) {
				/*
				 * If by name fails, just disable by name and it will
				 * recompute next time it is required.
				 */
				__maps__free_maps_by_name(maps);
			}
			maps__set_maps_by_name(maps, maps_by_name);
		}
		RC_CHK_ACCESS(maps)->nr_maps_allocated = nr_allocate;
	}
	memmove(&maps_by_address[first_after_index+to_add],
		&maps_by_address[first_after_index],
		(nr_maps - first_after_index) * sizeof(new1));
	maps_by_address[first_after_index] = map__get(new1);
	if (maps_by_name)
		maps_by_name[nr_maps] = map__get(new1);
	if (new2) {
		maps_by_address[first_after_index + 1] = map__get(new2);
		if (maps_by_name)
			maps_by_name[nr_maps + 1] = map__get(new2);
	}
	RC_CHK_ACCESS(maps)->nr_maps = nr_maps + to_add;
	maps__set_maps_by_name_sorted(maps, false);
	map__set_kmap_maps(new1, maps);
	map__set_kmap_maps(new2, maps);

	check_invariants(maps);
	return 0;
}

/*
 * Adds new to maps, if new overlaps existing entries then the existing maps are
 * adjusted or removed so that new fits without overlapping any entries.
 */
static int __maps__fixup_overlap_and_insert(struct maps *maps, struct map *new)
{
	int err = 0;
	FILE *fp = debug_file();
	unsigned int i, ni = INT_MAX; // Some gcc complain, but depends on maps_by_name...

	if (!maps__maps_by_address_sorted(maps))
		__maps__sort_by_address(maps);

	/*
	 * Iterate through entries where the end of the existing entry is
	 * greater-than the new map's start.
	 */
	for (i = first_ending_after(maps, new); i < maps__nr_maps(maps); ) {
		struct map **maps_by_address = maps__maps_by_address(maps);
		struct map **maps_by_name = maps__maps_by_name(maps);
		struct map *pos = maps_by_address[i];
		struct map *before = NULL, *after = NULL;

		/*
		 * Stop if current map starts after map->end.
		 * Maps are ordered by start: next will not overlap for sure.
		 */
		if (map__start(pos) >= map__end(new))
			break;

		if (use_browser) {
			pr_debug("overlapping maps in %s (disable tui for more info)\n",
				dso__name(map__dso(new)));
		} else if (verbose >= 2) {
			pr_debug("overlapping maps:\n");
			map__fprintf(new, fp);
			map__fprintf(pos, fp);
		}

		if (maps_by_name)
			ni = maps__by_name_index(maps, pos);

		/*
		 * Now check if we need to create new maps for areas not
		 * overlapped by the new map:
		 */
		if (map__start(new) > map__start(pos)) {
			/* Map starts within existing map. Need to shorten the existing map. */
			before = map__clone(pos);

			if (before == NULL) {
				err = -ENOMEM;
				goto out_err;
			}
			map__set_end(before, map__start(new));

			if (verbose >= 2 && !use_browser)
				map__fprintf(before, fp);
		}
		if (map__end(new) < map__end(pos)) {
			/* The new map isn't as long as the existing map. */
			after = map__clone(pos);

			if (after == NULL) {
				map__zput(before);
				err = -ENOMEM;
				goto out_err;
			}

			map__set_start(after, map__end(new));
			map__add_pgoff(after, map__end(new) - map__start(pos));
			assert(map__map_ip(pos, map__end(new)) ==
			       map__map_ip(after, map__end(new)));

			if (verbose >= 2 && !use_browser)
				map__fprintf(after, fp);
		}
		/*
		 * If adding one entry, for `before` or `after`, we can replace
		 * the existing entry. If both `before` and `after` are
		 * necessary than an insert is needed. If the existing entry
		 * entirely overlaps the existing entry it can just be removed.
		 */
		if (before) {
			map__put(maps_by_address[i]);
			maps_by_address[i] = before;

			if (maps_by_name) {
				map__put(maps_by_name[ni]);
				maps_by_name[ni] = map__get(before);
			}

			/* Maps are still ordered, go to next one. */
			i++;
			if (after) {
				/*
				 * 'before' and 'after' mean 'new' split the
				 * 'pos' mapping and therefore there are no
				 * later mappings.
				 */
				err = __maps__insert_sorted(maps, i, new, after);
				map__put(after);
				check_invariants(maps);
				return err;
			}
			check_invariants(maps);
		} else if (after) {
			/*
			 * 'after' means 'new' split 'pos' and there are no
			 * later mappings.
			 */
			map__put(maps_by_address[i]);
			maps_by_address[i] = map__get(new);

			if (maps_by_name) {
				map__put(maps_by_name[ni]);
				maps_by_name[ni] = map__get(new);
			}

			err = __maps__insert_sorted(maps, i + 1, after, NULL);
			map__put(after);
			check_invariants(maps);
			return err;
		} else {
			struct map *next = NULL;

			if (i + 1 < maps__nr_maps(maps))
				next = maps_by_address[i + 1];

			if (!next  || map__start(next) >= map__end(new)) {
				/*
				 * Replace existing mapping and end knowing
				 * there aren't later overlapping or any
				 * mappings.
				 */
				map__put(maps_by_address[i]);
				maps_by_address[i] = map__get(new);

				if (maps_by_name) {
					map__put(maps_by_name[ni]);
					maps_by_name[ni] = map__get(new);
				}

				map__set_kmap_maps(new, maps);

				check_invariants(maps);
				return err;
			}
			__maps__remove(maps, pos);
			check_invariants(maps);
			/*
			 * Maps are ordered but no need to increase `i` as the
			 * later maps were moved down.
			 */
		}
	}
	/* Add the map. */
	err = __maps__insert_sorted(maps, i, new, NULL);
out_err:
	return err;
}

int maps__fixup_overlap_and_insert(struct maps *maps, struct map *new)
{
	int err;

	down_write(maps__lock(maps));
	err =  __maps__fixup_overlap_and_insert(maps, new);
	up_write(maps__lock(maps));
	return err;
}

int maps__copy_from(struct maps *dest, struct maps *parent)
{
	/* Note, if struct map were immutable then cloning could use ref counts. */
	struct map **parent_maps_by_address;
	int err = 0;
	unsigned int n;

	down_write(maps__lock(dest));
	down_read(maps__lock(parent));

	parent_maps_by_address = maps__maps_by_address(parent);
	n = maps__nr_maps(parent);
	if (maps__nr_maps(dest) == 0) {
		/* No existing mappings so just copy from parent to avoid reallocs in insert. */
		unsigned int nr_maps_allocated = RC_CHK_ACCESS(parent)->nr_maps_allocated;
		struct map **dest_maps_by_address =
			malloc(nr_maps_allocated * sizeof(struct map *));
		struct map **dest_maps_by_name = NULL;

		if (!dest_maps_by_address)
			err = -ENOMEM;
		else {
			if (maps__maps_by_name(parent)) {
				dest_maps_by_name =
					malloc(nr_maps_allocated * sizeof(struct map *));
			}

			RC_CHK_ACCESS(dest)->maps_by_address = dest_maps_by_address;
			RC_CHK_ACCESS(dest)->maps_by_name = dest_maps_by_name;
			RC_CHK_ACCESS(dest)->nr_maps_allocated = nr_maps_allocated;
		}

		for (unsigned int i = 0; !err && i < n; i++) {
			struct map *pos = parent_maps_by_address[i];
			struct map *new = map__clone(pos);

			if (!new)
				err = -ENOMEM;
			else {
				err = unwind__prepare_access(dest, new, NULL);
				if (!err) {
					dest_maps_by_address[i] = new;
					if (dest_maps_by_name)
						dest_maps_by_name[i] = map__get(new);
					RC_CHK_ACCESS(dest)->nr_maps = i + 1;
				}
			}
			if (err)
				map__put(new);
		}
		maps__set_maps_by_address_sorted(dest, maps__maps_by_address_sorted(parent));
		if (!err) {
			RC_CHK_ACCESS(dest)->last_search_by_name_idx =
				RC_CHK_ACCESS(parent)->last_search_by_name_idx;
			maps__set_maps_by_name_sorted(dest,
						dest_maps_by_name &&
						maps__maps_by_name_sorted(parent));
		} else {
			RC_CHK_ACCESS(dest)->last_search_by_name_idx = 0;
			maps__set_maps_by_name_sorted(dest, false);
		}
	} else {
		/* Unexpected copying to a maps containing entries. */
		for (unsigned int i = 0; !err && i < n; i++) {
			struct map *pos = parent_maps_by_address[i];
			struct map *new = map__clone(pos);

			if (!new)
				err = -ENOMEM;
			else {
				err = unwind__prepare_access(dest, new, NULL);
				if (!err)
					err = __maps__insert(dest, new);
			}
			map__put(new);
		}
	}
	check_invariants(dest);

	up_read(maps__lock(parent));
	up_write(maps__lock(dest));
	return err;
}

static int map__addr_cmp(const void *key, const void *entry)
{
	const u64 ip = *(const u64 *)key;
	const struct map *map = *(const struct map * const *)entry;

	if (ip < map__start(map))
		return -1;
	if (ip >= map__end(map))
		return 1;
	return 0;
}

struct map *maps__find(struct maps *maps, u64 ip)
{
	struct map *result = NULL;
	bool done = false;

	/* See locking/sorting note. */
	while (!done) {
		down_read(maps__lock(maps));
		if (maps__maps_by_address_sorted(maps)) {
			struct map **mapp =
				bsearch(&ip, maps__maps_by_address(maps), maps__nr_maps(maps),
					sizeof(*mapp), map__addr_cmp);

			if (mapp)
				result = map__get(*mapp);
			done = true;
		}
		up_read(maps__lock(maps));
		if (!done)
			maps__sort_by_address(maps);
	}
	return result;
}

static int map__strcmp_name(const void *name, const void *b)
{
	const struct dso *dso = map__dso(*(const struct map **)b);

	return strcmp(name, dso__short_name(dso));
}

struct map *maps__find_by_name(struct maps *maps, const char *name)
{
	struct map *result = NULL;
	bool done = false;

	/* See locking/sorting note. */
	while (!done) {
		unsigned int i;

		down_read(maps__lock(maps));

		/* First check last found entry. */
		i = RC_CHK_ACCESS(maps)->last_search_by_name_idx;
		if (i < maps__nr_maps(maps) && maps__maps_by_name(maps)) {
			struct dso *dso = map__dso(maps__maps_by_name(maps)[i]);

			if (dso && strcmp(dso__short_name(dso), name) == 0) {
				result = map__get(maps__maps_by_name(maps)[i]);
				done = true;
			}
		}

		/* Second search sorted array. */
		if (!done && maps__maps_by_name_sorted(maps)) {
			struct map **mapp =
				bsearch(name, maps__maps_by_name(maps), maps__nr_maps(maps),
					sizeof(*mapp), map__strcmp_name);

			if (mapp) {
				result = map__get(*mapp);
				i = mapp - maps__maps_by_name(maps);
				RC_CHK_ACCESS(maps)->last_search_by_name_idx = i;
			}
			done = true;
		}
		up_read(maps__lock(maps));
		if (!done) {
			/* Sort and retry binary search. */
			if (maps__sort_by_name(maps)) {
				/*
				 * Memory allocation failed do linear search
				 * through address sorted maps.
				 */
				struct map **maps_by_address;
				unsigned int n;

				down_read(maps__lock(maps));
				maps_by_address =  maps__maps_by_address(maps);
				n = maps__nr_maps(maps);
				for (i = 0; i < n; i++) {
					struct map *pos = maps_by_address[i];
					struct dso *dso = map__dso(pos);

					if (dso && strcmp(dso__short_name(dso), name) == 0) {
						result = map__get(pos);
						break;
					}
				}
				up_read(maps__lock(maps));
				done = true;
			}
		}
	}
	return result;
}

struct map *maps__find_next_entry(struct maps *maps, struct map *map)
{
	unsigned int i;
	struct map *result = NULL;

	down_read(maps__lock(maps));
	while (!maps__maps_by_address_sorted(maps)) {
		up_read(maps__lock(maps));
		maps__sort_by_address(maps);
		down_read(maps__lock(maps));
	}
	i = maps__by_address_index(maps, map);
	if (++i < maps__nr_maps(maps))
		result = map__get(maps__maps_by_address(maps)[i]);

	up_read(maps__lock(maps));
	return result;
}

void maps__fixup_end(struct maps *maps)
{
	struct map **maps_by_address;
	unsigned int n;

	down_write(maps__lock(maps));
	if (!maps__maps_by_address_sorted(maps))
		__maps__sort_by_address(maps);

	maps_by_address = maps__maps_by_address(maps);
	n = maps__nr_maps(maps);
	for (unsigned int i = 1; i < n; i++) {
		struct map *prev = maps_by_address[i - 1];
		struct map *curr = maps_by_address[i];

		if (!map__end(prev) || map__end(prev) > map__start(curr))
			map__set_end(prev, map__start(curr));
	}

	/*
	 * We still haven't the actual symbols, so guess the
	 * last map final address.
	 */
	if (n > 0 && !map__end(maps_by_address[n - 1]))
		map__set_end(maps_by_address[n - 1], ~0ULL);

	RC_CHK_ACCESS(maps)->ends_broken = false;
	check_invariants(maps);

	up_write(maps__lock(maps));
}

/*
 * Merges map into maps by splitting the new map within the existing map
 * regions.
 */
int maps__merge_in(struct maps *kmaps, struct map *new_map)
{
	unsigned int first_after_, kmaps__nr_maps;
	struct map **kmaps_maps_by_address;
	struct map **merged_maps_by_address;
	unsigned int merged_nr_maps_allocated;

	/* First try under a read lock. */
	while (true) {
		down_read(maps__lock(kmaps));
		if (maps__maps_by_address_sorted(kmaps))
			break;

		up_read(maps__lock(kmaps));

		/* First after binary search requires sorted maps. Sort and try again. */
		maps__sort_by_address(kmaps);
	}
	first_after_ = first_ending_after(kmaps, new_map);
	kmaps_maps_by_address = maps__maps_by_address(kmaps);

	if (first_after_ >= maps__nr_maps(kmaps) ||
	    map__start(kmaps_maps_by_address[first_after_]) >= map__end(new_map)) {
		/* No overlap so regular insert suffices. */
		up_read(maps__lock(kmaps));
		return maps__insert(kmaps, new_map);
	}
	up_read(maps__lock(kmaps));

	/* Plain insert with a read-lock failed, try again now with the write lock. */
	down_write(maps__lock(kmaps));
	if (!maps__maps_by_address_sorted(kmaps))
		__maps__sort_by_address(kmaps);

	first_after_ = first_ending_after(kmaps, new_map);
	kmaps_maps_by_address = maps__maps_by_address(kmaps);
	kmaps__nr_maps = maps__nr_maps(kmaps);

	if (first_after_ >= kmaps__nr_maps ||
	    map__start(kmaps_maps_by_address[first_after_]) >= map__end(new_map)) {
		/* No overlap so regular insert suffices. */
		int ret = __maps__insert(kmaps, new_map);

		check_invariants(kmaps);
		up_write(maps__lock(kmaps));
		return ret;
	}
	/* Array to merge into, possibly 1 more for the sake of new_map. */
	merged_nr_maps_allocated = RC_CHK_ACCESS(kmaps)->nr_maps_allocated;
	if (kmaps__nr_maps + 1 == merged_nr_maps_allocated)
		merged_nr_maps_allocated++;

	merged_maps_by_address = malloc(merged_nr_maps_allocated * sizeof(*merged_maps_by_address));
	if (!merged_maps_by_address) {
		up_write(maps__lock(kmaps));
		return -ENOMEM;
	}
	maps__set_maps_by_address(kmaps, merged_maps_by_address);
	maps__set_maps_by_address_sorted(kmaps, true);
	__maps__free_maps_by_name(kmaps);
	maps__set_nr_maps_allocated(kmaps, merged_nr_maps_allocated);

	/* Copy entries before the new_map that can't overlap. */
	for (unsigned int i = 0; i < first_after_; i++)
		merged_maps_by_address[i] = map__get(kmaps_maps_by_address[i]);

	maps__set_nr_maps(kmaps, first_after_);

	/* Add the new map, it will be split when the later overlapping mappings are added. */
	__maps__insert(kmaps, new_map);

	/* Insert mappings after new_map, splitting new_map in the process. */
	for (unsigned int i = first_after_; i < kmaps__nr_maps; i++)
		__maps__fixup_overlap_and_insert(kmaps, kmaps_maps_by_address[i]);

	/* Copy the maps from merged into kmaps. */
	for (unsigned int i = 0; i < kmaps__nr_maps; i++)
		map__zput(kmaps_maps_by_address[i]);

	free(kmaps_maps_by_address);
	check_invariants(kmaps);
	up_write(maps__lock(kmaps));
	return 0;
}

void maps__load_first(struct maps *maps)
{
	down_read(maps__lock(maps));

	if (maps__nr_maps(maps) > 0)
		map__load(maps__maps_by_address(maps)[0]);

	up_read(maps__lock(maps));
}
