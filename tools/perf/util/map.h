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
struct ref_reloc_sym;
struct map_groups;
struct machine;

struct map {
	union {
		struct rb_node	rb_node;
		struct list_head node;
	};
	u64			start;
	u64			end;
	u8 /* enum map_type */	type;
	bool			referenced;
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

/* Native host kernel uses -1 as pid index in machine */
#define	HOST_KERNEL_ID			(-1)
#define	DEFAULT_GUEST_KERNEL_ID		(0)

struct machine {
	struct rb_node	  rb_node;
	pid_t		  pid;
	char		  *root_dir;
	struct list_head  user_dsos;
	struct list_head  kernel_dsos;
	struct map_groups kmaps;
	struct map	  *vmlinux_maps[MAP__NR_TYPES];
};

static inline
struct map *machine__kernel_map(struct machine *self, enum map_type type)
{
	return self->vmlinux_maps[type];
}

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
struct map *map__new(struct list_head *dsos__list, u64 start, u64 len,
		     u64 pgoff, u32 pid, char *filename,
		     enum map_type type);
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

typedef void (*machine__process_t)(struct machine *self, void *data);

void machines__process(struct rb_root *self, machine__process_t process, void *data);
struct machine *machines__add(struct rb_root *self, pid_t pid,
			      const char *root_dir);
struct machine *machines__find_host(struct rb_root *self);
struct machine *machines__find(struct rb_root *self, pid_t pid);
struct machine *machines__findnew(struct rb_root *self, pid_t pid);
char *machine__mmap_name(struct machine *self, char *bf, size_t size);
int machine__init(struct machine *self, const char *root_dir, pid_t pid);
void machine__exit(struct machine *self);
void machine__delete(struct machine *self);

/*
 * Default guest kernel is defined by parameter --guestkallsyms
 * and --guestmodules
 */
static inline bool machine__is_default_guest(struct machine *self)
{
	return self ? self->pid == DEFAULT_GUEST_KERNEL_ID : false;
}

static inline bool machine__is_host(struct machine *self)
{
	return self ? self->pid == HOST_KERNEL_ID : false;
}

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
struct symbol *machine__find_kernel_symbol(struct machine *self,
					   enum map_type type, u64 addr,
					   struct map **mapp,
					   symbol_filter_t filter)
{
	return map_groups__find_symbol(&self->kmaps, type, addr, mapp, filter);
}

static inline
struct symbol *machine__find_kernel_function(struct machine *self, u64 addr,
					     struct map **mapp,
					     symbol_filter_t filter)
{
	return machine__find_kernel_symbol(self, MAP__FUNCTION, addr, mapp, filter);
}

static inline
struct symbol *map_groups__find_function_by_name(struct map_groups *mg,
						 const char *name, struct map **mapp,
						 symbol_filter_t filter)
{
	return map_groups__find_symbol_by_name(mg, MAP__FUNCTION, name, mapp, filter);
}

static inline
struct symbol *machine__find_kernel_function_by_name(struct machine *self,
						     const char *name,
						     struct map **mapp,
						     symbol_filter_t filter)
{
	return map_groups__find_function_by_name(&self->kmaps, name, mapp,
						 filter);
}

int map_groups__fixup_overlappings(struct map_groups *mg, struct map *map,
				   int verbose, FILE *fp);

struct map *map_groups__find_by_name(struct map_groups *mg,
				     enum map_type type, const char *name);
struct map *machine__new_module(struct machine *self, u64 start, const char *filename);

void map_groups__flush(struct map_groups *mg);

#endif /* __PERF_MAP_H */
