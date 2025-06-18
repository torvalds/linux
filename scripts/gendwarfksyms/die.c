// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 Google LLC
 */

#include <string.h>
#include "gendwarfksyms.h"

#define DIE_HASH_BITS 16

/* {die->addr, state} -> struct die * */
static HASHTABLE_DEFINE(die_map, 1 << DIE_HASH_BITS);

static unsigned int map_hits;
static unsigned int map_misses;

static inline unsigned int die_hash(uintptr_t addr, enum die_state state)
{
	return hash_32(addr_hash(addr) ^ (unsigned int)state);
}

static void init_die(struct die *cd)
{
	cd->state = DIE_INCOMPLETE;
	cd->mapped = false;
	cd->fqn = NULL;
	cd->tag = -1;
	cd->addr = 0;
	INIT_LIST_HEAD(&cd->fragments);
}

static struct die *create_die(Dwarf_Die *die, enum die_state state)
{
	struct die *cd;

	cd = xmalloc(sizeof(struct die));
	init_die(cd);
	cd->addr = (uintptr_t)die->addr;

	hash_add(die_map, &cd->hash, die_hash(cd->addr, state));
	return cd;
}

int __die_map_get(uintptr_t addr, enum die_state state, struct die **res)
{
	struct die *cd;

	hash_for_each_possible(die_map, cd, hash, die_hash(addr, state)) {
		if (cd->addr == addr && cd->state == state) {
			*res = cd;
			return 0;
		}
	}

	return -1;
}

struct die *die_map_get(Dwarf_Die *die, enum die_state state)
{
	struct die *cd;

	if (__die_map_get((uintptr_t)die->addr, state, &cd) == 0) {
		map_hits++;
		return cd;
	}

	map_misses++;
	return create_die(die, state);
}

static void reset_die(struct die *cd)
{
	struct die_fragment *tmp;
	struct die_fragment *df;

	list_for_each_entry_safe(df, tmp, &cd->fragments, list) {
		if (df->type == FRAGMENT_STRING)
			free(df->data.str);
		free(df);
	}

	if (cd->fqn && *cd->fqn)
		free(cd->fqn);
	init_die(cd);
}

void die_map_for_each(die_map_callback_t func, void *arg)
{
	struct hlist_node *tmp;
	struct die *cd;

	hash_for_each_safe(die_map, cd, tmp, hash) {
		func(cd, arg);
	}
}

void die_map_free(void)
{
	struct hlist_node *tmp;
	unsigned int stats[DIE_LAST + 1];
	struct die *cd;
	int i;

	memset(stats, 0, sizeof(stats));

	hash_for_each_safe(die_map, cd, tmp, hash) {
		stats[cd->state]++;
		reset_die(cd);
		free(cd);
	}
	hash_init(die_map);

	if (map_hits + map_misses > 0)
		debug("hits %u, misses %u (hit rate %.02f%%)", map_hits,
		      map_misses,
		      (100.0f * map_hits) / (map_hits + map_misses));

	for (i = 0; i <= DIE_LAST; i++)
		debug("%s: %u entries", die_state_name(i), stats[i]);
}

static struct die_fragment *append_item(struct die *cd)
{
	struct die_fragment *df;

	df = xmalloc(sizeof(struct die_fragment));
	df->type = FRAGMENT_EMPTY;
	list_add_tail(&df->list, &cd->fragments);
	return df;
}

void die_map_add_string(struct die *cd, const char *str)
{
	struct die_fragment *df;

	if (!cd)
		return;

	df = append_item(cd);
	df->data.str = xstrdup(str);
	df->type = FRAGMENT_STRING;
}

void die_map_add_linebreak(struct die *cd, int linebreak)
{
	struct die_fragment *df;

	if (!cd)
		return;

	df = append_item(cd);
	df->data.linebreak = linebreak;
	df->type = FRAGMENT_LINEBREAK;
}

void die_map_add_die(struct die *cd, struct die *child)
{
	struct die_fragment *df;

	if (!cd)
		return;

	df = append_item(cd);
	df->data.addr = child->addr;
	df->type = FRAGMENT_DIE;
}
