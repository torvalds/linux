/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024 Google LLC
 */

#include <dwarf.h>
#include <elfutils/libdw.h>
#include <elfutils/libdwfl.h>
#include <stdlib.h>
#include <stdio.h>

#include <hash.h>
#include <hashtable.h>
#include <xalloc.h>

#ifndef __GENDWARFKSYMS_H
#define __GENDWARFKSYMS_H

/*
 * Options -- in gendwarfksyms.c
 */
extern int debug;
extern int dump_dies;
extern int dump_die_map;

/*
 * Output helpers
 */
#define __PREFIX "gendwarfksyms: "
#define __println(prefix, format, ...)                                \
	fprintf(stderr, prefix __PREFIX "%s: " format "\n", __func__, \
		##__VA_ARGS__)

#define debug(format, ...)                                    \
	do {                                                  \
		if (debug)                                    \
			__println("", format, ##__VA_ARGS__); \
	} while (0)

#define warn(format, ...) __println("warning: ", format, ##__VA_ARGS__)
#define error(format, ...)                                   \
	do {                                                 \
		__println("error: ", format, ##__VA_ARGS__); \
		exit(1);                                     \
	} while (0)

#define __die_debug(color, format, ...)                                 \
	do {                                                            \
		if (dump_dies && dump_die_map)                          \
			fprintf(stderr,                                 \
				"\033[" #color "m<" format ">\033[39m", \
				__VA_ARGS__);                           \
	} while (0)

#define die_debug_r(format, ...) __die_debug(91, format, __VA_ARGS__)
#define die_debug_g(format, ...) __die_debug(92, format, __VA_ARGS__)
#define die_debug_b(format, ...) __die_debug(94, format, __VA_ARGS__)

/*
 * Error handling helpers
 */
#define __check(expr, test)                                     \
	({                                                      \
		int __res = expr;                               \
		if (test)                                       \
			error("`%s` failed: %d", #expr, __res); \
		__res;                                          \
	})

/* Error == non-zero values */
#define check(expr) __check(expr, __res)
/* Error == negative values */
#define checkp(expr) __check(expr, __res < 0)

/* Consistent aliases (DW_TAG_<type>_type) for DWARF tags */
#define DW_TAG_enumerator_type DW_TAG_enumerator
#define DW_TAG_formal_parameter_type DW_TAG_formal_parameter
#define DW_TAG_member_type DW_TAG_member
#define DW_TAG_template_type_parameter_type DW_TAG_template_type_parameter
#define DW_TAG_typedef_type DW_TAG_typedef
#define DW_TAG_variant_part_type DW_TAG_variant_part
#define DW_TAG_variant_type DW_TAG_variant

/*
 * symbols.c
 */

static inline unsigned int addr_hash(uintptr_t addr)
{
	return hash_ptr((const void *)addr);
}

struct symbol_addr {
	uint32_t section;
	Elf64_Addr address;
};

struct symbol {
	const char *name;
	struct symbol_addr addr;
	struct hlist_node addr_hash;
	struct hlist_node name_hash;
};

typedef void (*symbol_callback_t)(struct symbol *, void *arg);

void symbol_read_exports(FILE *file);
void symbol_read_symtab(int fd);
struct symbol *symbol_get(const char *name);
void symbol_free(void);

/*
 * die.c
 */

enum die_state {
	DIE_INCOMPLETE,
	DIE_UNEXPANDED,
	DIE_COMPLETE,
	DIE_LAST = DIE_COMPLETE
};

enum die_fragment_type {
	FRAGMENT_EMPTY,
	FRAGMENT_STRING,
	FRAGMENT_LINEBREAK,
	FRAGMENT_DIE
};

struct die_fragment {
	enum die_fragment_type type;
	union {
		char *str;
		int linebreak;
		uintptr_t addr;
	} data;
	struct list_head list;
};

#define CASE_CONST_TO_STR(name) \
	case name:              \
		return #name;

static inline const char *die_state_name(enum die_state state)
{
	switch (state) {
	CASE_CONST_TO_STR(DIE_INCOMPLETE)
	CASE_CONST_TO_STR(DIE_UNEXPANDED)
	CASE_CONST_TO_STR(DIE_COMPLETE)
	}

	error("unexpected die_state: %d", state);
}

struct die {
	enum die_state state;
	char *fqn;
	int tag;
	uintptr_t addr;
	struct list_head fragments;
	struct hlist_node hash;
};

int __die_map_get(uintptr_t addr, enum die_state state, struct die **res);
struct die *die_map_get(Dwarf_Die *die, enum die_state state);
void die_map_add_string(struct die *pd, const char *str);
void die_map_add_linebreak(struct die *pd, int linebreak);
void die_map_add_die(struct die *pd, struct die *child);
void die_map_free(void);

/*
 * cache.c
 */

#define CACHE_HASH_BITS 10

/* A cache for addresses we've already seen. */
struct cache {
	HASHTABLE_DECLARE(cache, 1 << CACHE_HASH_BITS);
};

void cache_set(struct cache *cache, unsigned long key, int value);
int cache_get(struct cache *cache, unsigned long key);
void cache_init(struct cache *cache);
void cache_free(struct cache *cache);

static inline void __cache_mark_expanded(struct cache *cache, uintptr_t addr)
{
	cache_set(cache, addr, 1);
}

static inline bool __cache_was_expanded(struct cache *cache, uintptr_t addr)
{
	return cache_get(cache, addr) == 1;
}

static inline void cache_mark_expanded(struct cache *cache, void *addr)
{
	__cache_mark_expanded(cache, (uintptr_t)addr);
}

static inline bool cache_was_expanded(struct cache *cache, void *addr)
{
	return __cache_was_expanded(cache, (uintptr_t)addr);
}

/*
 * dwarf.c
 */

struct expansion_state {
	bool expand;
};

struct state {
	struct symbol *sym;
	Dwarf_Die die;

	/* List expansion */
	bool first_list_item;

	/* Structure expansion */
	struct expansion_state expand;
	struct cache expansion_cache;
};

typedef int (*die_callback_t)(struct state *state, struct die *cache,
			      Dwarf_Die *die);
typedef bool (*die_match_callback_t)(Dwarf_Die *die);
bool match_all(Dwarf_Die *die);

int process_die_container(struct state *state, struct die *cache,
			  Dwarf_Die *die, die_callback_t func,
			  die_match_callback_t match);

void process_cu(Dwarf_Die *cudie);

#endif /* __GENDWARFKSYMS_H */
