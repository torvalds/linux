#ifndef __PERF_MAP_H
#define __PERF_MAP_H

#include <linux/refcount.h>
#include <linux/compiler.h>
#include <linux/list.h>
#include <linux/rbtree.h>
#include <pthread.h>
#include <stdio.h>
#include <stdbool.h>
#include <linux/types.h>

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
	bool			erange_warned;
	u32			priv;
	u32			prot;
	u32			flags;
	u64			pgoff;
	u64			reloc;
	u32			maj, min; /* only valid for MMAP2 record */
	u64			ino;      /* only valid for MMAP2 record */
	u64			ino_generation;/* only valid for MMAP2 record */

	/* ip -> dso rip */
	u64			(*map_ip)(struct map *, u64);
	/* dso rip -> ip */
	u64			(*unmap_ip)(struct map *, u64);

	struct dso		*dso;
	struct map_groups	*groups;
	refcount_t		refcnt;
};

struct kmap {
	struct ref_reloc_sym	*ref_reloc_sym;
	struct map_groups	*kmaps;
};

struct maps {
	struct rb_root	 entries;
	pthread_rwlock_t lock;
};

struct map_groups {
	struct maps	 maps[MAP__NR_TYPES];
	struct machine	 *machine;
	atomic_t	 refcnt;
};

struct map_groups *map_groups__new(struct machine *machine);
void map_groups__delete(struct map_groups *mg);
bool map_groups__empty(struct map_groups *mg);

static inline struct map_groups *map_groups__get(struct map_groups *mg)
{
	if (mg)
		atomic_inc(&mg->refcnt);
	return mg;
}

void map_groups__put(struct map_groups *mg);

struct kmap *map__kmap(struct map *map);
struct map_groups *map__kmaps(struct map *map);

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

/* objdump address -> memory address */
u64 map__objdump_2mem(struct map *map, u64 ip);

struct symbol;
struct thread;

/* map__for_each_symbol - iterate over the symbols in the given map
 *
 * @map: the 'struct map *' in which symbols itereated
 * @pos: the 'struct symbol *' to use as a loop cursor
 * @n: the 'struct rb_node *' to use as a temporary storage
 * Note: caller must ensure map->dso is not NULL (map is loaded).
 */
#define map__for_each_symbol(map, pos, n)	\
	dso__for_each_symbol(map->dso, pos, n, map->type)

/* map__for_each_symbol_with_name - iterate over the symbols in the given map
 *                                  that have the given name
 *
 * @map: the 'struct map *' in which symbols itereated
 * @sym_name: the symbol name
 * @pos: the 'struct symbol *' to use as a loop cursor
 */
#define __map__for_each_symbol_by_name(map, sym_name, pos)	\
	for (pos = map__find_symbol_by_name(map, sym_name);	\
	     pos && arch__compare_symbol_names(pos->name, sym_name) == 0;	\
	     pos = symbol__next_by_name(pos))

#define map__for_each_symbol_by_name(map, sym_name, pos)		\
	__map__for_each_symbol_by_name(map, sym_name, (pos))

int arch__compare_symbol_names(const char *namea, const char *nameb);
void map__init(struct map *map, enum map_type type,
	       u64 start, u64 end, u64 pgoff, struct dso *dso);
struct map *map__new(struct machine *machine, u64 start, u64 len,
		     u64 pgoff, u32 pid, u32 d_maj, u32 d_min, u64 ino,
		     u64 ino_gen, u32 prot, u32 flags,
		     char *filename, enum map_type type, struct thread *thread);
struct map *map__new2(u64 start, struct dso *dso, enum map_type type);
void map__delete(struct map *map);
struct map *map__clone(struct map *map);

static inline struct map *map__get(struct map *map)
{
	if (map)
		refcount_inc(&map->refcnt);
	return map;
}

void map__put(struct map *map);

static inline void __map__zput(struct map **map)
{
	map__put(*map);
	*map = NULL;
}

#define map__zput(map) __map__zput(&map)

int map__overlap(struct map *l, struct map *r);
size_t map__fprintf(struct map *map, FILE *fp);
size_t map__fprintf_dsoname(struct map *map, FILE *fp);
int map__fprintf_srcline(struct map *map, u64 addr, const char *prefix,
			 FILE *fp);

int map__load(struct map *map);
struct symbol *map__find_symbol(struct map *map, u64 addr);
struct symbol *map__find_symbol_by_name(struct map *map, const char *name);
void map__fixup_start(struct map *map);
void map__fixup_end(struct map *map);

void map__reloc_vmlinux(struct map *map);

size_t __map_groups__fprintf_maps(struct map_groups *mg, enum map_type type,
				  FILE *fp);
void maps__insert(struct maps *maps, struct map *map);
void maps__remove(struct maps *maps, struct map *map);
struct map *maps__find(struct maps *maps, u64 addr);
struct map *maps__first(struct maps *maps);
struct map *map__next(struct map *map);
struct symbol *maps__find_symbol_by_name(struct maps *maps, const char *name,
                                         struct map **mapp);
void map_groups__init(struct map_groups *mg, struct machine *machine);
void map_groups__exit(struct map_groups *mg);
int map_groups__clone(struct thread *thread,
		      struct map_groups *parent, enum map_type type);
size_t map_groups__fprintf(struct map_groups *mg, FILE *fp);

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

static inline struct map *map_groups__first(struct map_groups *mg,
					    enum map_type type)
{
	return maps__first(&mg->maps[type]);
}

static inline struct map *map_groups__next(struct map *map)
{
	return map__next(map);
}

struct symbol *map_groups__find_symbol(struct map_groups *mg,
				       enum map_type type, u64 addr,
				       struct map **mapp);

struct symbol *map_groups__find_symbol_by_name(struct map_groups *mg,
					       enum map_type type,
					       const char *name,
					       struct map **mapp);

struct addr_map_symbol;

int map_groups__find_ams(struct addr_map_symbol *ams);

static inline
struct symbol *map_groups__find_function_by_name(struct map_groups *mg,
						 const char *name, struct map **mapp)
{
	return map_groups__find_symbol_by_name(mg, MAP__FUNCTION, name, mapp);
}

int map_groups__fixup_overlappings(struct map_groups *mg, struct map *map,
				   FILE *fp);

struct map *map_groups__find_by_name(struct map_groups *mg,
				     enum map_type type, const char *name);

bool __map__is_kernel(const struct map *map);

static inline bool __map__is_kmodule(const struct map *map)
{
	return !__map__is_kernel(map);
}

#endif /* __PERF_MAP_H */
