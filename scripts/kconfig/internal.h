/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef INTERNAL_H
#define INTERNAL_H

#include <hashtable.h>

#define SYMBOL_HASHSIZE		(1U << 14)

extern HASHTABLE_DECLARE(sym_hashtable, SYMBOL_HASHSIZE);

#define for_all_symbols(sym) \
	hash_for_each(sym_hashtable, sym, node)

struct menu;

extern struct menu *current_menu, *current_entry;

extern const char *cur_filename;
extern int cur_lineno;

#endif /* INTERNAL_H */
