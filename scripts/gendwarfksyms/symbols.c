// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 Google LLC
 */

#include "gendwarfksyms.h"

#define SYMBOL_HASH_BITS 12

/* name -> struct symbol */
static HASHTABLE_DEFINE(symbol_names, 1 << SYMBOL_HASH_BITS);

static unsigned int for_each(const char *name, symbol_callback_t func,
			     void *data)
{
	struct hlist_node *tmp;
	struct symbol *match;

	if (!name || !*name)
		return 0;

	hash_for_each_possible_safe(symbol_names, match, tmp, name_hash,
				    hash_str(name)) {
		if (strcmp(match->name, name))
			continue;

		if (func)
			func(match, data);

		return 1;
	}

	return 0;
}

static bool is_exported(const char *name)
{
	return for_each(name, NULL, NULL) > 0;
}

void symbol_read_exports(FILE *file)
{
	struct symbol *sym;
	char *line = NULL;
	char *name = NULL;
	size_t size = 0;
	int nsym = 0;

	while (getline(&line, &size, file) > 0) {
		if (sscanf(line, "%ms\n", &name) != 1)
			error("malformed input line: %s", line);

		if (is_exported(name)) {
			/* Ignore duplicates */
			free(name);
			continue;
		}

		sym = xcalloc(1, sizeof(struct symbol));
		sym->name = name;

		hash_add(symbol_names, &sym->name_hash, hash_str(sym->name));
		++nsym;

		debug("%s", sym->name);
	}

	free(line);
	debug("%d exported symbols", nsym);
}

static void get_symbol(struct symbol *sym, void *arg)
{
	struct symbol **res = arg;

	*res = sym;
}

struct symbol *symbol_get(const char *name)
{
	struct symbol *sym = NULL;

	for_each(name, get_symbol, &sym);
	return sym;
}

void symbol_free(void)
{
	struct hlist_node *tmp;
	struct symbol *sym;

	hash_for_each_safe(symbol_names, sym, tmp, name_hash) {
		free((void *)sym->name);
		free(sym);
	}

	hash_init(symbol_names);
}
