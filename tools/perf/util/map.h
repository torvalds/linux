#ifndef __PERF_MAP_H
#define __PERF_MAP_H

#include <linux/compiler.h>
#include <linux/list.h>
#include <linux/rbtree.h>
#include <stdio.h>
#include <stdbool.h>
#include "types.h"

enum map_type {
	MAP__FUNCTION = 0,
	MAP__VARIABLE,
};

#define MAP__NR_TYPES (MAP__VARIABLE + 1)

extern const char *map_type__name[MAP__NR_TYPES];

struct dso;
struct ip_callchain;
struct ref_reloc_sym;
struct map_groups;
struct machine;
struct perf_evsel;

struct map {
	union {
		struct rb_node	rb_node;
		struct list_head node;
	};
	u64			start;
	u64			end;
	u8 /* enum map_type */	type;
	bool			referenced;
	bool			erange_warned;
	u32			priv;
	u64			pgoff;

	/* ip -> dso rip */
	u64			(*map_ip)(struct map *, u64);
	/* dso rip -> ip */
	u64			(*unmap_ip)(struct map *, u64);

	struct dso		*dso;
	struct map_groups	*groups;
};

struct kmap {
	struct ref_reloc_sym	*ref_reloc_sym;
	struct map_groups	*kmaps;
};

struct map_groups {
	struct rb_root	 maps[MAP__NR_TYPES];
	struct list_head removed_maps[MAP__NR_TYPES];
	struct machine	 *machine;
};

static inline struct kmap *map__kmap(struct map *map)
{
	return (struct kmap *)(map + 1);
}

static inline u64 map__map_ip(struct map *map, u64 ip)
{
	return ip - map->start + map->pgoff;
}

static inline u64 map__unmap_ip(struct map *map, u64 ip)
{
	return ip + map->start - map->pgoff;
}

static inline u64 identity__map_ip(struct map *map __maybe_unused, u64 ip)
{
	return ip;
}


/* rip/ip <-> addr suitable for passing to `objdump --start-address=` */
u64 map__rip_2objdump(struct map *map, u64 rip);

struct symbol;

typedef int (*symbol_filter_t)(struct map *map, struct symbol *sym);

void map__init(struct map *map, enum map_type type,
	       u64 start, u64 end, u64 pgoff, struct dso *dso);
struct map *map__new(struct list_head *dsos__list, u64 start, u64 len,
		     u64 pgoff, u32 pid, char *filename,
		     enum map_type type);
struct map *map__new2(u64 start, struct dso *dso, enum map_type type);
void map__delete(struct map *map);
struct map *map__clone(struct map *map);
int map__overlap(struct map *l, struct map *r);
size_t map__fprintf(struct map *map, FILE *fp);
size_t map__fprintf_dsoname(struct map *map, FILE *fp);

int map__load(struct map *map, symbol_filter_t filter);
struct symbol *map__find_symbol(struct map *map,
				u64 addr, symbol_filter_t filter);
struct symbol *map__find_symbol_by_name(struct map *map, const char *name,
					symbol_filter_t filter);
void map__fixup_start(struct map *map);
void map__fixup_end(struct map *map);

void map__reloc_vmlinux(struct map *map);

size_t __map_groups__fprintf_maps(struct map_groups *mg,
				  enum map_type type, int verbose, FILE *fp);
void maps__insert(struct rb_root *maps, struct map *map);
void maps__remove(struct rb_root *maps, struct map *map);
struct map *maps__find(struct rb_root *maps, u64 addr);
void map_groups__init(struct map_groups *mg);
void map_groups__exit(struct map_groups *mg);
int map_groups__clone(struct map_groups *mg,
		      struct map_groups *parent, enum map_type type);
size_t map_groups__fprintf(struct map_groups *mg, int verbose, FILE *fp);
size_t map_groups__fprintf_maps(struct map_groups *mg, int verbose, FILE *fp);

int maps__set_kallsyms_ref_reloc_sym(struct map **maps, const char *symbol_name,
				     u64 addr);

static inline void map_groups__insert(struct map_groups *mg, struct map *map)
{
	maps__insert(&mg->maps[map->type], map);
	map->groups = mg;
}

static inline void map_groups__remove(struct map_groups *mg, struct map *map)
{
	maps__remove(&mg->maps[map->type], map);
}

static inline struct map *map_groups__find(struct map_groups *mg,
					   enum map_type type, u64 addr)
{
	return maps__find(&mg->maps[type], addr);
}

struct symbol *map_groups__find_symbol(struct map_groups *mg,
				       enum map_type type, u64 addr,
				       struct map **mapp,
				       symbol_filter_t filter);

struct symbol *map_groups__find_symbol_by_name(struct map_groups *mg,
					       enum map_type type,
					       const char *name,
					       struct map **mapp,
					       symbol_filter_t filter);

static inline
struct symbol *map_groups__find_function_by_name(struct map_groups *mg,
						 const char *name, struct map **mapp,
						 symbol_filter_t filter)
{
	return map_groups__find_symbol_by_name(mg, MAP__FUNCTION, name, mapp, filter);
}

int map_groups__fixup_overlappings(struct map_groups *mg, struct map *map,
				   int verbose, FILE *fp);

struct map *map_groups__find_by_name(struct map_groups *mg,
				     enum map_type type, const char *name);

void map_groups__flush(struct map_groups *mg);

#endif /* __PERF_MAP_H */
