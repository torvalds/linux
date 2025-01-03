// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 Google LLC
 */

#define _GNU_SOURCE
#include <inttypes.h>
#include <stdio.h>

#include "gendwarfksyms.h"

static struct cache expansion_cache;

/*
 * A simple linked list of shared or owned strings to avoid copying strings
 * around when not necessary.
 */
struct type_list_entry {
	const char *str;
	void *owned;
	struct list_head list;
};

static void type_list_free(struct list_head *list)
{
	struct type_list_entry *entry;
	struct type_list_entry *tmp;

	list_for_each_entry_safe(entry, tmp, list, list) {
		if (entry->owned)
			free(entry->owned);
		free(entry);
	}

	INIT_LIST_HEAD(list);
}

static int type_list_append(struct list_head *list, const char *s, void *owned)
{
	struct type_list_entry *entry;

	if (!s)
		return 0;

	entry = xmalloc(sizeof(struct type_list_entry));
	entry->str = s;
	entry->owned = owned;
	list_add_tail(&entry->list, list);

	return strlen(entry->str);
}

static void type_list_write(struct list_head *list, FILE *file)
{
	struct type_list_entry *entry;

	list_for_each_entry(entry, list, list) {
		if (entry->str)
			checkp(fputs(entry->str, file));
	}
}

/*
 * An expanded type string in symtypes format.
 */
struct type_expansion {
	char *name;
	size_t len;
	struct list_head expanded;
	struct hlist_node hash;
};

static void type_expansion_init(struct type_expansion *type)
{
	type->name = NULL;
	type->len = 0;
	INIT_LIST_HEAD(&type->expanded);
}

static inline void type_expansion_free(struct type_expansion *type)
{
	free(type->name);
	type->name = NULL;
	type->len = 0;
	type_list_free(&type->expanded);
}

static void type_expansion_append(struct type_expansion *type, const char *s,
				  void *owned)
{
	type->len += type_list_append(&type->expanded, s, owned);
}

/*
 * type_map -- the longest expansions for each type.
 *
 * const char *name -> struct type_expansion *
 */
#define TYPE_HASH_BITS 12
static HASHTABLE_DEFINE(type_map, 1 << TYPE_HASH_BITS);

static int type_map_get(const char *name, struct type_expansion **res)
{
	struct type_expansion *e;

	hash_for_each_possible(type_map, e, hash, hash_str(name)) {
		if (!strcmp(name, e->name)) {
			*res = e;
			return 0;
		}
	}

	return -1;
}

static void type_map_add(const char *name, struct type_expansion *type)
{
	struct type_expansion *e;

	if (type_map_get(name, &e)) {
		e = xmalloc(sizeof(struct type_expansion));
		type_expansion_init(e);
		e->name = xstrdup(name);

		hash_add(type_map, &e->hash, hash_str(e->name));

		if (dump_types)
			debug("adding %s", e->name);
	} else {
		/* Use the longest available expansion */
		if (type->len <= e->len)
			return;

		type_list_free(&e->expanded);

		if (dump_types)
			debug("replacing %s", e->name);
	}

	/* Take ownership of type->expanded */
	list_replace_init(&type->expanded, &e->expanded);
	e->len = type->len;

	if (dump_types) {
		checkp(fputs(e->name, stderr));
		checkp(fputs(" ", stderr));
		type_list_write(&e->expanded, stderr);
		checkp(fputs("\n", stderr));
	}
}

static void type_map_write(FILE *file)
{
	struct type_expansion *e;
	struct hlist_node *tmp;

	if (!file)
		return;

	hash_for_each_safe(type_map, e, tmp, hash) {
		checkp(fputs(e->name, file));
		checkp(fputs(" ", file));
		type_list_write(&e->expanded, file);
		checkp(fputs("\n", file));
	}
}

static void type_map_free(void)
{
	struct type_expansion *e;
	struct hlist_node *tmp;

	hash_for_each_safe(type_map, e, tmp, hash) {
		type_expansion_free(e);
		free(e);
	}

	hash_init(type_map);
}

/*
 * Type reference format: <prefix>#<name>, where prefix:
 * 	s -> structure
 * 	u -> union
 * 	e -> enum
 * 	t -> typedef
 *
 * Names with spaces are additionally wrapped in single quotes.
 */
static char get_type_prefix(int tag)
{
	switch (tag) {
	case DW_TAG_class_type:
	case DW_TAG_structure_type:
		return 's';
	case DW_TAG_union_type:
		return 'u';
	case DW_TAG_enumeration_type:
		return 'e';
	case DW_TAG_typedef_type:
		return 't';
	default:
		return 0;
	}
}

static char *get_type_name(struct die *cache)
{
	const char *quote;
	char prefix;
	char *name;

	if (cache->state == DIE_INCOMPLETE) {
		warn("found incomplete cache entry: %p", cache);
		return NULL;
	}
	if (!cache->fqn || !*cache->fqn)
		return NULL;

	prefix = get_type_prefix(cache->tag);
	if (!prefix)
		return NULL;

	/* Wrap names with spaces in single quotes */
	quote = strstr(cache->fqn, " ") ? "'" : "";

	/* <prefix>#<type_name>\0 */
	if (asprintf(&name, "%c#%s%s%s", prefix, quote, cache->fqn, quote) < 0)
		error("asprintf failed for '%s'", cache->fqn);

	return name;
}

static void __type_expand(struct die *cache, struct type_expansion *type,
			  bool recursive);

static void type_expand_child(struct die *cache, struct type_expansion *type,
			      bool recursive)
{
	struct type_expansion child;
	char *name;

	name = get_type_name(cache);
	if (!name) {
		__type_expand(cache, type, recursive);
		return;
	}

	if (recursive && !__cache_was_expanded(&expansion_cache, cache->addr)) {
		__cache_mark_expanded(&expansion_cache, cache->addr);
		type_expansion_init(&child);
		__type_expand(cache, &child, true);
		type_map_add(name, &child);
		type_expansion_free(&child);
	}

	type_expansion_append(type, name, name);
}

static void __type_expand(struct die *cache, struct type_expansion *type,
			  bool recursive)
{
	struct die_fragment *df;
	struct die *child;

	list_for_each_entry(df, &cache->fragments, list) {
		switch (df->type) {
		case FRAGMENT_STRING:
			type_expansion_append(type, df->data.str, NULL);
			break;
		case FRAGMENT_DIE:
			/* Use a complete die_map expansion if available */
			if (__die_map_get(df->data.addr, DIE_COMPLETE,
					  &child) &&
			    __die_map_get(df->data.addr, DIE_UNEXPANDED,
					  &child))
				error("unknown child: %" PRIxPTR,
				      df->data.addr);

			type_expand_child(child, type, recursive);
			break;
		case FRAGMENT_LINEBREAK:
			/*
			 * Keep whitespace in the symtypes format, but avoid
			 * repeated spaces.
			 */
			if (list_is_last(&df->list, &cache->fragments) ||
			    list_next_entry(df, list)->type !=
				    FRAGMENT_LINEBREAK)
				type_expansion_append(type, " ", NULL);
			break;
		default:
			error("empty die_fragment in %p", cache);
		}
	}
}

static void type_expand(struct die *cache, struct type_expansion *type,
			bool recursive)
{
	type_expansion_init(type);
	__type_expand(cache, type, recursive);
	cache_free(&expansion_cache);
}

static void expand_type(struct die *cache, void *arg)
{
	struct type_expansion type;
	char *name;

	if (cache->mapped)
		return;

	cache->mapped = true;

	/*
	 * Skip unexpanded die_map entries if there's a complete
	 * expansion available for this DIE.
	 */
	if (cache->state == DIE_UNEXPANDED &&
	    !__die_map_get(cache->addr, DIE_COMPLETE, &cache)) {
		if (cache->mapped)
			return;

		cache->mapped = true;
	}

	name = get_type_name(cache);
	if (!name)
		return;

	debug("%s", name);
	type_expand(cache, &type, true);
	type_map_add(name, &type);

	type_expansion_free(&type);
	free(name);
}

void generate_symtypes(FILE *file)
{
	cache_init(&expansion_cache);

	/*
	 * die_map processing:
	 *
	 *   1. die_map contains all types referenced in exported symbol
	 *      signatures, but can contain duplicates just like the original
	 *      DWARF, and some references may not be fully expanded depending
	 *      on how far we processed the DIE tree for that specific symbol.
	 *
	 *      For each die_map entry, find the longest available expansion,
	 *      and add it to type_map.
	 */
	die_map_for_each(expand_type, NULL);

	/*
	 *   2. If a symtypes file is requested, write type_map contents to
	 *      the file.
	 */
	type_map_write(file);
	type_map_free();
}
