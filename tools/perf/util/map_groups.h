/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_MAP_GROUPS_H
#define __PERF_MAP_GROUPS_H

#include <linux/refcount.h>
#include <linux/rbtree.h>
#include <stdio.h>
#include <stdbool.h>
#include <linux/types.h>
#include "rwsem.h"

struct ref_reloc_sym;
struct machine;
struct map;
struct thread;

struct maps {
	struct rb_root      entries;
	struct rb_root	    names;
	struct rw_semaphore lock;
};

void maps__insert(struct maps *maps, struct map *map);
void maps__remove(struct maps *maps, struct map *map);
struct map *maps__find(struct maps *maps, u64 addr);
struct map *maps__first(struct maps *maps);
struct map *map__next(struct map *map);
struct symbol *maps__find_symbol_by_name(struct maps *maps, const char *name, struct map **mapp);

struct map_groups {
	struct maps	 maps;
	struct machine	 *machine;
	refcount_t	 refcnt;
};

#define KMAP_NAME_LEN 256

struct kmap {
	struct ref_reloc_sym *ref_reloc_sym;
	struct map_groups    *kmaps;
	char		     name[KMAP_NAME_LEN];
};

struct map_groups *map_groups__new(struct machine *machine);
void map_groups__delete(struct map_groups *mg);
bool map_groups__empty(struct map_groups *mg);

static inline struct map_groups *map_groups__get(struct map_groups *mg)
{
	if (mg)
		refcount_inc(&mg->refcnt);
	return mg;
}

void map_groups__put(struct map_groups *mg);
void map_groups__init(struct map_groups *mg, struct machine *machine);
void map_groups__exit(struct map_groups *mg);
int map_groups__clone(struct thread *thread, struct map_groups *parent);
size_t map_groups__fprintf(struct map_groups *mg, FILE *fp);

void map_groups__insert(struct map_groups *mg, struct map *map);

static inline void map_groups__remove(struct map_groups *mg, struct map *map)
{
	maps__remove(&mg->maps, map);
}

static inline struct map *map_groups__find(struct map_groups *mg, u64 addr)
{
	return maps__find(&mg->maps, addr);
}

struct map *map_groups__first(struct map_groups *mg);

static inline struct map *map_groups__next(struct map *map)
{
	return map__next(map);
}

struct symbol *map_groups__find_symbol(struct map_groups *mg, u64 addr, struct map **mapp);
struct symbol *map_groups__find_symbol_by_name(struct map_groups *mg, const char *name, struct map **mapp);

struct addr_map_symbol;

int map_groups__find_ams(struct addr_map_symbol *ams);

int map_groups__fixup_overlappings(struct map_groups *mg, struct map *map, FILE *fp);

struct map *map_groups__find_by_name(struct map_groups *mg, const char *name);

#endif // __PERF_MAP_GROUPS_H
