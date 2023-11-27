// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <stdlib.h>
#include <linux/zalloc.h>
#include "debug.h"
#include "dso.h"
#include "map.h"
#include "maps.h"
#include "thread.h"
#include "ui/ui.h"
#include "unwind.h"

static void maps__init(struct maps *maps, struct machine *machine)
{
	refcount_set(maps__refcnt(maps), 1);
	init_rwsem(maps__lock(maps));
	RC_CHK_ACCESS(maps)->entries = RB_ROOT;
	RC_CHK_ACCESS(maps)->machine = machine;
	RC_CHK_ACCESS(maps)->last_search_by_name = NULL;
	RC_CHK_ACCESS(maps)->nr_maps = 0;
	RC_CHK_ACCESS(maps)->maps_by_name = NULL;
}

static void __maps__free_maps_by_name(struct maps *maps)
{
	/*
	 * Free everything to try to do it from the rbtree in the next search
	 */
	for (unsigned int i = 0; i < maps__nr_maps(maps); i++)
		map__put(maps__maps_by_name(maps)[i]);

	zfree(&RC_CHK_ACCESS(maps)->maps_by_name);
	RC_CHK_ACCESS(maps)->nr_maps_allocated = 0;
}

static int __maps__insert(struct maps *maps, struct map *map)
{
	struct rb_node **p = &maps__entries(maps)->rb_node;
	struct rb_node *parent = NULL;
	const u64 ip = map__start(map);
	struct map_rb_node *m, *new_rb_node;

	new_rb_node = malloc(sizeof(*new_rb_node));
	if (!new_rb_node)
		return -ENOMEM;

	RB_CLEAR_NODE(&new_rb_node->rb_node);
	new_rb_node->map = map__get(map);

	while (*p != NULL) {
		parent = *p;
		m = rb_entry(parent, struct map_rb_node, rb_node);
		if (ip < map__start(m->map))
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	rb_link_node(&new_rb_node->rb_node, parent, p);
	rb_insert_color(&new_rb_node->rb_node, maps__entries(maps));
	return 0;
}

int maps__insert(struct maps *maps, struct map *map)
{
	int err;
	const struct dso *dso = map__dso(map);

	down_write(maps__lock(maps));
	err = __maps__insert(maps, map);
	if (err)
		goto out;

	++RC_CHK_ACCESS(maps)->nr_maps;

	if (dso && dso->kernel) {
		struct kmap *kmap = map__kmap(map);

		if (kmap)
			kmap->kmaps = maps;
		else
			pr_err("Internal error: kernel dso with non kernel map\n");
	}


	/*
	 * If we already performed some search by name, then we need to add the just
	 * inserted map and resort.
	 */
	if (maps__maps_by_name(maps)) {
		if (maps__nr_maps(maps) > RC_CHK_ACCESS(maps)->nr_maps_allocated) {
			int nr_allocate = maps__nr_maps(maps) * 2;
			struct map **maps_by_name = realloc(maps__maps_by_name(maps),
							    nr_allocate * sizeof(map));

			if (maps_by_name == NULL) {
				__maps__free_maps_by_name(maps);
				err = -ENOMEM;
				goto out;
			}

			RC_CHK_ACCESS(maps)->maps_by_name = maps_by_name;
			RC_CHK_ACCESS(maps)->nr_maps_allocated = nr_allocate;
		}
		maps__maps_by_name(maps)[maps__nr_maps(maps) - 1] = map__get(map);
		__maps__sort_by_name(maps);
	}
 out:
	up_write(maps__lock(maps));
	return err;
}

static void __maps__remove(struct maps *maps, struct map_rb_node *rb_node)
{
	rb_erase_init(&rb_node->rb_node, maps__entries(maps));
	map__put(rb_node->map);
	free(rb_node);
}

void maps__remove(struct maps *maps, struct map *map)
{
	struct map_rb_node *rb_node;

	down_write(maps__lock(maps));
	if (RC_CHK_ACCESS(maps)->last_search_by_name == map)
		RC_CHK_ACCESS(maps)->last_search_by_name = NULL;

	rb_node = maps__find_node(maps, map);
	assert(rb_node->RC_CHK_ACCESS(map) == RC_CHK_ACCESS(map));
	__maps__remove(maps, rb_node);
	if (maps__maps_by_name(maps))
		__maps__free_maps_by_name(maps);
	--RC_CHK_ACCESS(maps)->nr_maps;
	up_write(maps__lock(maps));
}

static void __maps__purge(struct maps *maps)
{
	struct map_rb_node *pos, *next;

	if (maps__maps_by_name(maps))
		__maps__free_maps_by_name(maps);

	maps__for_each_entry_safe(maps, pos, next) {
		rb_erase_init(&pos->rb_node,  maps__entries(maps));
		map__put(pos->map);
		free(pos);
	}
}

static void maps__exit(struct maps *maps)
{
	down_write(maps__lock(maps));
	__maps__purge(maps);
	up_write(maps__lock(maps));
}

bool maps__empty(struct maps *maps)
{
	return !maps__first(maps);
}

struct maps *maps__new(struct machine *machine)
{
	struct maps *result;
	RC_STRUCT(maps) *maps = zalloc(sizeof(*maps));

	if (ADD_RC_CHK(result, maps))
		maps__init(result, machine);

	return result;
}

static void maps__delete(struct maps *maps)
{
	maps__exit(maps);
	unwind__finish_access(maps);
	RC_CHK_FREE(maps);
}

struct maps *maps__get(struct maps *maps)
{
	struct maps *result;

	if (RC_CHK_GET(result, maps))
		refcount_inc(maps__refcnt(maps));

	return result;
}

void maps__put(struct maps *maps)
{
	if (maps && refcount_dec_and_test(maps__refcnt(maps)))
		maps__delete(maps);
	else
		RC_CHK_PUT(maps);
}

struct symbol *maps__find_symbol(struct maps *maps, u64 addr, struct map **mapp)
{
	struct map *map = maps__find(maps, addr);

	/* Ensure map is loaded before using map->map_ip */
	if (map != NULL && map__load(map) >= 0) {
		if (mapp != NULL)
			*mapp = map;
		return map__find_symbol(map, map__map_ip(map, addr));
	}

	return NULL;
}

struct symbol *maps__find_symbol_by_name(struct maps *maps, const char *name, struct map **mapp)
{
	struct symbol *sym;
	struct map_rb_node *pos;

	down_read(maps__lock(maps));

	maps__for_each_entry(maps, pos) {
		sym = map__find_symbol_by_name(pos->map, name);

		if (sym == NULL)
			continue;
		if (!map__contains_symbol(pos->map, sym)) {
			sym = NULL;
			continue;
		}
		if (mapp != NULL)
			*mapp = pos->map;
		goto out;
	}

	sym = NULL;
out:
	up_read(maps__lock(maps));
	return sym;
}

int maps__find_ams(struct maps *maps, struct addr_map_symbol *ams)
{
	if (ams->addr < map__start(ams->ms.map) || ams->addr >= map__end(ams->ms.map)) {
		if (maps == NULL)
			return -1;
		ams->ms.map = maps__find(maps, ams->addr);
		if (ams->ms.map == NULL)
			return -1;
	}

	ams->al_addr = map__map_ip(ams->ms.map, ams->addr);
	ams->ms.sym = map__find_symbol(ams->ms.map, ams->al_addr);

	return ams->ms.sym ? 0 : -1;
}

size_t maps__fprintf(struct maps *maps, FILE *fp)
{
	size_t printed = 0;
	struct map_rb_node *pos;

	down_read(maps__lock(maps));

	maps__for_each_entry(maps, pos) {
		printed += fprintf(fp, "Map:");
		printed += map__fprintf(pos->map, fp);
		if (verbose > 2) {
			printed += dso__fprintf(map__dso(pos->map), fp);
			printed += fprintf(fp, "--\n");
		}
	}

	up_read(maps__lock(maps));

	return printed;
}

int maps__fixup_overlappings(struct maps *maps, struct map *map, FILE *fp)
{
	struct rb_root *root;
	struct rb_node *next, *first;
	int err = 0;

	down_write(maps__lock(maps));

	root = maps__entries(maps);

	/*
	 * Find first map where end > map->start.
	 * Same as find_vma() in kernel.
	 */
	next = root->rb_node;
	first = NULL;
	while (next) {
		struct map_rb_node *pos = rb_entry(next, struct map_rb_node, rb_node);

		if (map__end(pos->map) > map__start(map)) {
			first = next;
			if (map__start(pos->map) <= map__start(map))
				break;
			next = next->rb_left;
		} else
			next = next->rb_right;
	}

	next = first;
	while (next && !err) {
		struct map_rb_node *pos = rb_entry(next, struct map_rb_node, rb_node);
		next = rb_next(&pos->rb_node);

		/*
		 * Stop if current map starts after map->end.
		 * Maps are ordered by start: next will not overlap for sure.
		 */
		if (map__start(pos->map) >= map__end(map))
			break;

		if (verbose >= 2) {

			if (use_browser) {
				pr_debug("overlapping maps in %s (disable tui for more info)\n",
					 map__dso(map)->name);
			} else {
				fputs("overlapping maps:\n", fp);
				map__fprintf(map, fp);
				map__fprintf(pos->map, fp);
			}
		}

		rb_erase_init(&pos->rb_node, root);
		/*
		 * Now check if we need to create new maps for areas not
		 * overlapped by the new map:
		 */
		if (map__start(map) > map__start(pos->map)) {
			struct map *before = map__clone(pos->map);

			if (before == NULL) {
				err = -ENOMEM;
				goto put_map;
			}

			map__set_end(before, map__start(map));
			err = __maps__insert(maps, before);
			if (err) {
				map__put(before);
				goto put_map;
			}

			if (verbose >= 2 && !use_browser)
				map__fprintf(before, fp);
			map__put(before);
		}

		if (map__end(map) < map__end(pos->map)) {
			struct map *after = map__clone(pos->map);

			if (after == NULL) {
				err = -ENOMEM;
				goto put_map;
			}

			map__set_start(after, map__end(map));
			map__add_pgoff(after, map__end(map) - map__start(pos->map));
			assert(map__map_ip(pos->map, map__end(map)) ==
				map__map_ip(after, map__end(map)));
			err = __maps__insert(maps, after);
			if (err) {
				map__put(after);
				goto put_map;
			}
			if (verbose >= 2 && !use_browser)
				map__fprintf(after, fp);
			map__put(after);
		}
put_map:
		map__put(pos->map);
		free(pos);
	}
	up_write(maps__lock(maps));
	return err;
}

/*
 * XXX This should not really _copy_ te maps, but refcount them.
 */
int maps__clone(struct thread *thread, struct maps *parent)
{
	struct maps *maps = thread__maps(thread);
	int err;
	struct map_rb_node *rb_node;

	down_read(maps__lock(parent));

	maps__for_each_entry(parent, rb_node) {
		struct map *new = map__clone(rb_node->map);

		if (new == NULL) {
			err = -ENOMEM;
			goto out_unlock;
		}

		err = unwind__prepare_access(maps, new, NULL);
		if (err)
			goto out_unlock;

		err = maps__insert(maps, new);
		if (err)
			goto out_unlock;

		map__put(new);
	}

	err = 0;
out_unlock:
	up_read(maps__lock(parent));
	return err;
}

struct map_rb_node *maps__find_node(struct maps *maps, struct map *map)
{
	struct map_rb_node *rb_node;

	maps__for_each_entry(maps, rb_node) {
		if (rb_node->RC_CHK_ACCESS(map) == RC_CHK_ACCESS(map))
			return rb_node;
	}
	return NULL;
}

struct map *maps__find(struct maps *maps, u64 ip)
{
	struct rb_node *p;
	struct map_rb_node *m;


	down_read(maps__lock(maps));

	p = maps__entries(maps)->rb_node;
	while (p != NULL) {
		m = rb_entry(p, struct map_rb_node, rb_node);
		if (ip < map__start(m->map))
			p = p->rb_left;
		else if (ip >= map__end(m->map))
			p = p->rb_right;
		else
			goto out;
	}

	m = NULL;
out:
	up_read(maps__lock(maps));
	return m ? m->map : NULL;
}

struct map_rb_node *maps__first(struct maps *maps)
{
	struct rb_node *first = rb_first(maps__entries(maps));

	if (first)
		return rb_entry(first, struct map_rb_node, rb_node);
	return NULL;
}

struct map_rb_node *map_rb_node__next(struct map_rb_node *node)
{
	struct rb_node *next;

	if (!node)
		return NULL;

	next = rb_next(&node->rb_node);

	if (!next)
		return NULL;

	return rb_entry(next, struct map_rb_node, rb_node);
}

static int map__strcmp(const void *a, const void *b)
{
	const struct map *map_a = *(const struct map **)a;
	const struct map *map_b = *(const struct map **)b;
	const struct dso *dso_a = map__dso(map_a);
	const struct dso *dso_b = map__dso(map_b);
	int ret = strcmp(dso_a->short_name, dso_b->short_name);

	if (ret == 0 && map_a != map_b) {
		/*
		 * Ensure distinct but name equal maps have an order in part to
		 * aid reference counting.
		 */
		ret = (int)map__start(map_a) - (int)map__start(map_b);
		if (ret == 0)
			ret = (int)((intptr_t)map_a - (intptr_t)map_b);
	}

	return ret;
}

static int map__strcmp_name(const void *name, const void *b)
{
	const struct dso *dso = map__dso(*(const struct map **)b);

	return strcmp(name, dso->short_name);
}

void __maps__sort_by_name(struct maps *maps)
{
	qsort(maps__maps_by_name(maps), maps__nr_maps(maps), sizeof(struct map *), map__strcmp);
}

static int map__groups__sort_by_name_from_rbtree(struct maps *maps)
{
	struct map_rb_node *rb_node;
	struct map **maps_by_name = realloc(maps__maps_by_name(maps),
					    maps__nr_maps(maps) * sizeof(struct map *));
	int i = 0;

	if (maps_by_name == NULL)
		return -1;

	up_read(maps__lock(maps));
	down_write(maps__lock(maps));

	RC_CHK_ACCESS(maps)->maps_by_name = maps_by_name;
	RC_CHK_ACCESS(maps)->nr_maps_allocated = maps__nr_maps(maps);

	maps__for_each_entry(maps, rb_node)
		maps_by_name[i++] = map__get(rb_node->map);

	__maps__sort_by_name(maps);

	up_write(maps__lock(maps));
	down_read(maps__lock(maps));

	return 0;
}

static struct map *__maps__find_by_name(struct maps *maps, const char *name)
{
	struct map **mapp;

	if (maps__maps_by_name(maps) == NULL &&
	    map__groups__sort_by_name_from_rbtree(maps))
		return NULL;

	mapp = bsearch(name, maps__maps_by_name(maps), maps__nr_maps(maps),
		       sizeof(*mapp), map__strcmp_name);
	if (mapp)
		return *mapp;
	return NULL;
}

struct map *maps__find_by_name(struct maps *maps, const char *name)
{
	struct map_rb_node *rb_node;
	struct map *map;

	down_read(maps__lock(maps));


	if (RC_CHK_ACCESS(maps)->last_search_by_name) {
		const struct dso *dso = map__dso(RC_CHK_ACCESS(maps)->last_search_by_name);

		if (strcmp(dso->short_name, name) == 0) {
			map = RC_CHK_ACCESS(maps)->last_search_by_name;
			goto out_unlock;
		}
	}
	/*
	 * If we have maps->maps_by_name, then the name isn't in the rbtree,
	 * as maps->maps_by_name mirrors the rbtree when lookups by name are
	 * made.
	 */
	map = __maps__find_by_name(maps, name);
	if (map || maps__maps_by_name(maps) != NULL)
		goto out_unlock;

	/* Fallback to traversing the rbtree... */
	maps__for_each_entry(maps, rb_node) {
		struct dso *dso;

		map = rb_node->map;
		dso = map__dso(map);
		if (strcmp(dso->short_name, name) == 0) {
			RC_CHK_ACCESS(maps)->last_search_by_name = map;
			goto out_unlock;
		}
	}
	map = NULL;

out_unlock:
	up_read(maps__lock(maps));
	return map;
}

void maps__fixup_end(struct maps *maps)
{
	struct map_rb_node *prev = NULL, *curr;

	down_write(maps__lock(maps));

	maps__for_each_entry(maps, curr) {
		if (prev != NULL && !map__end(prev->map))
			map__set_end(prev->map, map__start(curr->map));

		prev = curr;
	}

	/*
	 * We still haven't the actual symbols, so guess the
	 * last map final address.
	 */
	if (curr && !map__end(curr->map))
		map__set_end(curr->map, ~0ULL);

	up_write(maps__lock(maps));
}

/*
 * Merges map into maps by splitting the new map within the existing map
 * regions.
 */
int maps__merge_in(struct maps *kmaps, struct map *new_map)
{
	struct map_rb_node *rb_node;
	LIST_HEAD(merged);
	int err = 0;

	maps__for_each_entry(kmaps, rb_node) {
		struct map *old_map = rb_node->map;

		/* no overload with this one */
		if (map__end(new_map) < map__start(old_map) ||
		    map__start(new_map) >= map__end(old_map))
			continue;

		if (map__start(new_map) < map__start(old_map)) {
			/*
			 * |new......
			 *       |old....
			 */
			if (map__end(new_map) < map__end(old_map)) {
				/*
				 * |new......|     -> |new..|
				 *       |old....| ->       |old....|
				 */
				map__set_end(new_map, map__start(old_map));
			} else {
				/*
				 * |new.............| -> |new..|       |new..|
				 *       |old....|    ->       |old....|
				 */
				struct map_list_node *m = map_list_node__new();

				if (!m) {
					err = -ENOMEM;
					goto out;
				}

				m->map = map__clone(new_map);
				if (!m->map) {
					free(m);
					err = -ENOMEM;
					goto out;
				}

				map__set_end(m->map, map__start(old_map));
				list_add_tail(&m->node, &merged);
				map__add_pgoff(new_map, map__end(old_map) - map__start(new_map));
				map__set_start(new_map, map__end(old_map));
			}
		} else {
			/*
			 *      |new......
			 * |old....
			 */
			if (map__end(new_map) < map__end(old_map)) {
				/*
				 *      |new..|   -> x
				 * |old.........| -> |old.........|
				 */
				map__put(new_map);
				new_map = NULL;
				break;
			} else {
				/*
				 *      |new......| ->         |new...|
				 * |old....|        -> |old....|
				 */
				map__add_pgoff(new_map, map__end(old_map) - map__start(new_map));
				map__set_start(new_map, map__end(old_map));
			}
		}
	}

out:
	while (!list_empty(&merged)) {
		struct map_list_node *old_node;

		old_node = list_entry(merged.next, struct map_list_node, node);
		list_del_init(&old_node->node);
		if (!err)
			err = maps__insert(kmaps, old_node->map);
		map__put(old_node->map);
		free(old_node);
	}

	if (new_map) {
		if (!err)
			err = maps__insert(kmaps, new_map);
		map__put(new_map);
	}
	return err;
}
