// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 Google LLC
 */

#define _GNU_SOURCE
#include <inttypes.h>
#include <stdio.h>
#include <zlib.h>

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

static int __type_map_get(const char *name, struct type_expansion **res)
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

static struct type_expansion *type_map_add(const char *name,
					   struct type_expansion *type)
{
	struct type_expansion *e;

	if (__type_map_get(name, &e)) {
		e = xmalloc(sizeof(struct type_expansion));
		type_expansion_init(e);
		e->name = xstrdup(name);

		hash_add(type_map, &e->hash, hash_str(e->name));

		if (dump_types)
			debug("adding %s", e->name);
	} else {
		/* Use the longest available expansion */
		if (type->len <= e->len)
			return e;

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

	return e;
}

static void type_parse(const char *name, const char *str,
		       struct type_expansion *type);

static int type_map_get(const char *name, struct type_expansion **res)
{
	struct type_expansion type;
	const char *override;

	if (!__type_map_get(name, res))
		return 0;

	/*
	 * If die_map didn't contain a type, we might still have
	 * a type_string kABI rule that defines it.
	 */
	if (stable && kabi_get_type_string(name, &override)) {
		type_expansion_init(&type);
		type_parse(name, override, &type);
		*res = type_map_add(name, &type);
		type_expansion_free(&type);
		return 0;
	}

	return -1;
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
 * CRC for a type, with an optional fully expanded type string for
 * debugging.
 */
struct version {
	struct type_expansion type;
	unsigned long crc;
};

static void version_init(struct version *version)
{
	version->crc = crc32(0, NULL, 0);
	type_expansion_init(&version->type);
}

static void version_free(struct version *version)
{
	type_expansion_free(&version->type);
}

static void version_add(struct version *version, const char *s)
{
	version->crc = crc32(version->crc, (void *)s, strlen(s));
	if (dump_versions)
		type_expansion_append(&version->type, s, NULL);
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
static inline bool is_type_prefix(const char *s)
{
	return (s[0] == 's' || s[0] == 'u' || s[0] == 'e' || s[0] == 't') &&
	       s[1] == '#';
}

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
	if (cache->state == DIE_SYMBOL || cache->state == DIE_FQN)
		return NULL;
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

static void __calculate_version(struct version *version,
				struct type_expansion *type)
{
	struct type_list_entry *entry;
	struct type_expansion *e;

	/* Calculate a CRC over an expanded type string */
	list_for_each_entry(entry, &type->expanded, list) {
		if (is_type_prefix(entry->str)) {
			if (type_map_get(entry->str, &e))
				error("unknown type reference to '%s' when expanding '%s'",
				      entry->str, type->name);

			/*
			 * It's sufficient to expand each type reference just
			 * once to detect changes.
			 */
			if (cache_was_expanded(&expansion_cache, e)) {
				version_add(version, entry->str);
			} else {
				cache_mark_expanded(&expansion_cache, e);
				__calculate_version(version, e);
			}
		} else {
			version_add(version, entry->str);
		}
	}
}

static void calculate_version(struct version *version,
			      struct type_expansion *type)
{
	version_init(version);
	__calculate_version(version, type);
	cache_free(&expansion_cache);
}

static void __type_expand(struct die *cache, struct type_expansion *type)
{
	struct die_fragment *df;
	struct die *child;
	char *name;

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

			name = get_type_name(child);
			if (name)
				type_expansion_append(type, name, name);
			else
				__type_expand(child, type);

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

static void type_expand(const char *name, struct die *cache,
			struct type_expansion *type)
{
	const char *override;

	type_expansion_init(type);

	if (stable && kabi_get_type_string(name, &override))
		type_parse(name, override, type);
	else
		__type_expand(cache, type);
}

static void type_parse(const char *name, const char *str,
		       struct type_expansion *type)
{
	char *fragment;
	size_t start = 0;
	size_t end;
	size_t pos;

	if (!*str)
		error("empty type string override for '%s'", name);

	for (pos = 0; str[pos]; ++pos) {
		bool empty;
		char marker = ' ';

		if (!is_type_prefix(&str[pos]))
			continue;

		end = pos + 2;

		/*
		 * Find the end of the type reference. If the type name contains
		 * spaces, it must be in single quotes.
		 */
		if (str[end] == '\'') {
			marker = '\'';
			++end;
		}
		while (str[end] && str[end] != marker)
			++end;

		/* Check that we have a non-empty type name */
		if (marker == '\'') {
			if (str[end] != marker)
				error("incomplete %c# type reference for '%s' (string : '%s')",
				      str[pos], name, str);
			empty = end == pos + 3;
			++end;
		} else {
			empty = end == pos + 2;
		}
		if (empty)
			error("empty %c# type name for '%s' (string: '%s')",
			      str[pos], name, str);

		/* Append the part of the string before the type reference */
		if (pos > start) {
			fragment = xstrndup(&str[start], pos - start);
			type_expansion_append(type, fragment, fragment);
		}

		/*
		 * Append the type reference -- note that if the reference
		 * is invalid, i.e. points to a non-existent type, we will
		 * print out an error when calculating versions.
		 */
		fragment = xstrndup(&str[pos], end - pos);
		type_expansion_append(type, fragment, fragment);

		start = end;
		pos = end - 1;
	}

	/* Append the rest of the type string, if there's any left */
	if (str[start])
		type_expansion_append(type, &str[start], NULL);
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

	type_expand(name, cache, &type);
	type_map_add(name, &type);
	type_expansion_free(&type);
	free(name);
}

static void expand_symbol(struct symbol *sym, void *arg)
{
	struct type_expansion type;
	struct version version;
	struct die *cache;

	/*
	 * No need to expand again unless we want a symtypes file entry
	 * for the symbol. Note that this means `sym` has the same address
	 * as another symbol that was already processed.
	 */
	if (!symtypes && sym->state == SYMBOL_PROCESSED)
		return;

	if (__die_map_get(sym->die_addr, DIE_SYMBOL, &cache))
		return; /* We'll warn about missing CRCs later. */

	type_expand(sym->name, cache, &type);

	/* If the symbol already has a version, don't calculate it again. */
	if (sym->state != SYMBOL_PROCESSED) {
		calculate_version(&version, &type);
		symbol_set_crc(sym, version.crc);
		debug("%s = %lx", sym->name, version.crc);

		if (dump_versions) {
			checkp(fputs(sym->name, stderr));
			checkp(fputs(" ", stderr));
			type_list_write(&version.type.expanded, stderr);
			checkp(fputs("\n", stderr));
		}

		version_free(&version);
	}

	/* These aren't needed in type_map unless we want a symtypes file. */
	if (symtypes)
		type_map_add(sym->name, &type);

	type_expansion_free(&type);
}

void generate_symtypes_and_versions(FILE *file)
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
	 *   2. For each exported symbol, expand the die_map type, and use
	 *      type_map expansions to calculate a symbol version from the
	 *      fully expanded type string.
	 */
	symbol_for_each(expand_symbol, NULL);

	/*
	 *   3. If a symtypes file is requested, write type_map contents to
	 *      the file.
	 */
	type_map_write(file);
	type_map_free();
}
