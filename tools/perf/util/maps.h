/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_MAPS_H
#define __PERF_MAPS_H

#include <linux/refcount.h>
#include <linux/rbtree.h>
#include <stdio.h>
#include <stdbool.h>
#include <linux/types.h>
#include "rwsem.h"

struct ref_reloc_sym;
struct machine;
struct map;
struct maps;
struct thread;

struct map *maps__find(struct maps *maps, u64 addr);
struct map *maps__first(struct maps *maps);
struct map *map__next(struct map *map);

#define maps__for_each_entry(maps, map) \
	for (map = maps__first(maps); map; map = map__next(map))

#define maps__for_each_entry_safe(maps, map, next) \
	for (map = maps__first(maps), next = map__next(map); map; map = next, next = map__next(map))

struct maps {
	struct rb_root      entries;
	struct rw_semaphore lock;
	struct machine	 *machine;
	struct map	 *last_search_by_name;
	struct map	 **maps_by_name;
	refcount_t	 refcnt;
	unsigned int	 nr_maps;
	unsigned int	 nr_maps_allocated;
#ifdef HAVE_LIBUNWIND_SUPPORT
	void				*addr_space;
	struct unwind_libunwind_ops	*unwind_libunwind_ops;
#endif
};

#define KMAP_NAME_LEN 256

struct kmap {
	struct ref_reloc_sym *ref_reloc_sym;
	struct maps	     *kmaps;
	char		     name[KMAP_NAME_LEN];
};

struct maps *maps__new(struct machine *machine);
void maps__delete(struct maps *maps);
bool maps__empty(struct maps *maps);

static inline struct maps *maps__get(struct maps *maps)
{
	if (maps)
		refcount_inc(&maps->refcnt);
	return maps;
}

void maps__put(struct maps *maps);
void maps__init(struct maps *maps, struct machine *machine);
void maps__exit(struct maps *maps);
int maps__clone(struct thread *thread, struct maps *parent);
size_t maps__fprintf(struct maps *maps, FILE *fp);

void maps__insert(struct maps *maps, struct map *map);

void maps__remove(struct maps *maps, struct map *map);

struct symbol *maps__find_symbol(struct maps *maps, u64 addr, struct map **mapp);
struct symbol *maps__find_symbol_by_name(struct maps *maps, const char *name, struct map **mapp);

struct addr_map_symbol;

int maps__find_ams(struct maps *maps, struct addr_map_symbol *ams);

int maps__fixup_overlappings(struct maps *maps, struct map *map, FILE *fp);

struct map *maps__find_by_name(struct maps *maps, const char *name);

int maps__merge_in(struct maps *kmaps, struct map *new_map);

void __maps__sort_by_name(struct maps *maps);

#endif // __PERF_MAPS_H
