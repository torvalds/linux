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
 * dwarf.c
 */

struct state {
	struct symbol *sym;
	Dwarf_Die die;
};

typedef int (*die_callback_t)(struct state *state, Dwarf_Die *die);
typedef bool (*die_match_callback_t)(Dwarf_Die *die);
bool match_all(Dwarf_Die *die);

int process_die_container(struct state *state, Dwarf_Die *die,
			  die_callback_t func, die_match_callback_t match);

void process_cu(Dwarf_Die *cudie);

#endif /* __GENDWARFKSYMS_H */
