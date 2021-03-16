/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_MAP_H
#define __PERF_MAP_H

#include <linux/refcount.h>
#include <linux/compiler.h>
#include <linux/list.h>
#include <linux/rbtree.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <linux/types.h>

struct dso;
struct maps;
struct machine;

struct map {
	union {
		struct rb_node	rb_node;
		struct list_head node;
	};
	u64			start;
	u64			end;
	bool			erange_warned:1;
	bool			priv:1;
	u32			prot;
	u64			pgoff;
	u64			reloc;

	/* ip -> dso rip */
	u64			(*map_ip)(struct map *, u64);
	/* dso rip -> ip */
	u64			(*unmap_ip)(struct map *, u64);

	struct dso		*dso;
	refcount_t		refcnt;
	u32			flags;
};

struct kmap;

struct kmap *__map__kmap(struct map *map);
struct kmap *map__kmap(struct map *map);
struct maps *map__kmaps(struct map *map);

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

static inline size_t map__size(const struct map *map)
{
	return map->end - map->start;
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
	dso__for_each_symbol(map->dso, pos, n)

/* map__for_each_symbol_with_name - iterate over the symbols in the given map
 *                                  that have the given name
 *
 * @map: the 'struct map *' in which symbols itereated
 * @sym_name: the symbol name
 * @pos: the 'struct symbol *' to use as a loop cursor
 */
#define __map__for_each_symbol_by_name(map, sym_name, pos)	\
	for (pos = map__find_symbol_by_name(map, sym_name);	\
	     pos &&						\
	     !symbol__match_symbol_name(pos->name, sym_name,	\
					SYMBOL_TAG_INCLUDE__DEFAULT_ONLY); \
	     pos = symbol__next_by_name(pos))

#define map__for_each_symbol_by_name(map, sym_name, pos)		\
	__map__for_each_symbol_by_name(map, sym_name, (pos))

void map__init(struct map *map,
	       u64 start, u64 end, u64 pgoff, struct dso *dso);

struct dso_id;
struct build_id;

struct map *map__new(struct machine *machine, u64 start, u64 len,
		     u64 pgoff, struct dso_id *id, u32 prot, u32 flags,
		     struct build_id *bid, char *filename, struct thread *thread);
struct map *map__new2(u64 start, struct dso *dso);
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

size_t map__fprintf(struct map *map, FILE *fp);
size_t map__fprintf_dsoname(struct map *map, FILE *fp);
char *map__srcline(struct map *map, u64 addr, struct symbol *sym);
int map__fprintf_srcline(struct map *map, u64 addr, const char *prefix,
			 FILE *fp);

int map__load(struct map *map);
struct symbol *map__find_symbol(struct map *map, u64 addr);
struct symbol *map__find_symbol_by_name(struct map *map, const char *name);
void map__fixup_start(struct map *map);
void map__fixup_end(struct map *map);

int map__set_kallsyms_ref_reloc_sym(struct map *map, const char *symbol_name,
				    u64 addr);

bool __map__is_kernel(const struct map *map);
bool __map__is_extra_kernel_map(const struct map *map);
bool __map__is_bpf_prog(const struct map *map);
bool __map__is_bpf_image(const struct map *map);
bool __map__is_ool(const struct map *map);

static inline bool __map__is_kmodule(const struct map *map)
{
	return !__map__is_kernel(map) && !__map__is_extra_kernel_map(map) &&
	       !__map__is_bpf_prog(map) && !__map__is_ool(map) &&
	       !__map__is_bpf_image(map);
}

bool map__has_symbols(const struct map *map);

#define ENTRY_TRAMPOLINE_NAME "__entry_SYSCALL_64_trampoline"

static inline bool is_entry_trampoline(const char *name)
{
	return !strcmp(name, ENTRY_TRAMPOLINE_NAME);
}

static inline bool is_bpf_image(const char *name)
{
	return strncmp(name, "bpf_trampoline_", sizeof("bpf_trampoline_") - 1) == 0 ||
	       strncmp(name, "bpf_dispatcher_", sizeof("bpf_dispatcher_") - 1) == 0;
}

static inline int is_anon_memory(const char *filename)
{
	return !strcmp(filename, "//anon") ||
	       !strncmp(filename, "/dev/zero", sizeof("/dev/zero") - 1) ||
	       !strncmp(filename, "/anon_hugepage", sizeof("/anon_hugepage") - 1);
}

static inline int is_no_dso_memory(const char *filename)
{
	return !strncmp(filename, "[stack", 6) ||
	       !strncmp(filename, "/SYSV", 5)  ||
	       !strcmp(filename, "[heap]");
}
#endif /* __PERF_MAP_H */
