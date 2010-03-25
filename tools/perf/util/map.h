#ifndef __PERF_MAP_H
#define __PERF_MAP_H

#include <linux/compiler.h>
#include <linux/list.h>
#include <linux/rbtree.h>
#include <stdio.h>
#include "types.h"

enum map_type {
	MAP__FUNCTION = 0,
	MAP__VARIABLE,
};

#define MAP__NR_TYPES (MAP__VARIABLE + 1)

extern const char *map_type__name[MAP__NR_TYPES];

struct dso;
struct ref_reloc_sym;
struct map_groups;

struct map {
	union {
		struct rb_node	rb_node;
		struct list_head node;
	};
	u64			start;
	u64			end;
	enum map_type		type;
	u64			pgoff;

	/* ip -> dso rip */
	u64			(*map_ip)(struct map *, u64);
	/* dso rip -> ip */
	u64			(*unmap_ip)(struct map *, u64);

	struct dso		*dso;
};

struct kmap {
	struct ref_reloc_sym	*ref_reloc_sym;
	struct map_groups	*kmaps;
};

static inline struct kmap *map__kmap(struct map *self)
{
	return (struct kmap *)(self + 1);
}

static inline u64 map__map_ip(struct map *map, u64 ip)
{
	return ip - map->start + map->pgoff;
}

static inline u64 map__unmap_ip(struct map *map, u64 ip)
{
	return ip + map->start - map->pgoff;
}

static inline u64 identity__map_ip(struct map *map __used, u64 ip)
{
	return ip;
}


/* rip/ip <-> addr suitable for passing to `objdump --start-address=` */
u64 map__rip_2objdump(struct map *map, u64 rip);
u64 map__objdump_2ip(struct map *map, u64 addr);

struct symbol;

typedef int (*symbol_filter_t)(struct map *map, struct symbol *sym);

void map__init(struct map *self, enum map_type type,
	       u64 start, u64 end, u64 pgoff, struct dso *dso);
struct map *map__new(u64 start, u64 len, u64 pgoff, u32 pid, char *filename,
		     enum map_type type, char *cwd, int cwdlen);
void map__delete(struct map *self);
struct map *map__clone(struct map *self);
int map__overlap(struct map *l, struct map *r);
size_t map__fprintf(struct map *self, FILE *fp);

int map__load(struct map *self, symbol_filter_t filter);
struct symbol *map__find_symbol(struct map *self,
				u64 addr, symbol_filter_t filter);
struct symbol *map__find_symbol_by_name(struct map *self, const char *name,
					symbol_filter_t filter);
void map__fixup_start(struct map *self);
void map__fixup_end(struct map *self);

void map__reloc_vmlinux(struct map *self);

struct map_groups {
	struct rb_root		maps[MAP__NR_TYPES];
	struct list_head	removed_maps[MAP__NR_TYPES];
};

size_t __map_groups__fprintf_maps(struct map_groups *self,
				  enum map_type type, FILE *fp);
void maps__insert(struct rb_root *maps, struct map *map);
struct map *maps__find(struct rb_root *maps, u64 addr);
void map_groups__init(struct map_groups *self);
size_t map_groups__fprintf_maps(struct map_groups *self, FILE *fp);

static inline void map_groups__insert(struct map_groups *self, struct map *map)
{
	 maps__insert(&self->maps[map->type], map);
}

static inline struct map *map_groups__find(struct map_groups *self,
					   enum map_type type, u64 addr)
{
	return maps__find(&self->maps[type], addr);
}

struct symbol *map_groups__find_symbol(struct map_groups *self,
				       enum map_type type, u64 addr,
				       symbol_filter_t filter);

static inline struct symbol *map_groups__find_function(struct map_groups *self,
						       u64 addr,
						       symbol_filter_t filter)
{
	return map_groups__find_symbol(self, MAP__FUNCTION, addr, filter);
}

struct map *map_groups__find_by_name(struct map_groups *self,
				     enum map_type type, const char *name);
int __map_groups__create_kernel_maps(struct map_groups *self,
				     struct map *vmlinux_maps[MAP__NR_TYPES],
				     struct dso *kernel);
int map_groups__create_kernel_maps(struct map_groups *self,
				   struct map *vmlinux_maps[MAP__NR_TYPES]);
struct map *map_groups__new_module(struct map_groups *self, u64 start,
				   const char *filename);

#endif /* __PERF_MAP_H */
