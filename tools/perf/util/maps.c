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

static void __maps__insert(struct maps *maps, struct map *map);

static void maps__init(struct maps *maps, struct machine *machine)
{
	maps->entries = RB_ROOT;
	init_rwsem(&maps->lock);
	maps->machine = machine;
	maps->last_search_by_name = NULL;
	maps->nr_maps = 0;
	maps->maps_by_name = NULL;
	refcount_set(&maps->refcnt, 1);
}

static void __maps__free_maps_by_name(struct maps *maps)
{
	/*
	 * Free everything to try to do it from the rbtree in the next search
	 */
	zfree(&maps->maps_by_name);
	maps->nr_maps_allocated = 0;
}

void maps__insert(struct maps *maps, struct map *map)
{
	down_write(&maps->lock);
	__maps__insert(maps, map);
	++maps->nr_maps;

	if (map->dso && map->dso->kernel) {
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
	if (maps->maps_by_name) {
		if (maps->nr_maps > maps->nr_maps_allocated) {
			int nr_allocate = maps->nr_maps * 2;
			struct map **maps_by_name = realloc(maps->maps_by_name, nr_allocate * sizeof(map));

			if (maps_by_name == NULL) {
				__maps__free_maps_by_name(maps);
				up_write(&maps->lock);
				return;
			}

			maps->maps_by_name = maps_by_name;
			maps->nr_maps_allocated = nr_allocate;
		}
		maps->maps_by_name[maps->nr_maps - 1] = map;
		__maps__sort_by_name(maps);
	}
	up_write(&maps->lock);
}

static void __maps__remove(struct maps *maps, struct map *map)
{
	rb_erase_init(&map->rb_node, &maps->entries);
	map__put(map);
}

void maps__remove(struct maps *maps, struct map *map)
{
	down_write(&maps->lock);
	if (maps->last_search_by_name == map)
		maps->last_search_by_name = NULL;

	__maps__remove(maps, map);
	--maps->nr_maps;
	if (maps->maps_by_name)
		__maps__free_maps_by_name(maps);
	up_write(&maps->lock);
}

static void __maps__purge(struct maps *maps)
{
	struct map *pos, *next;

	maps__for_each_entry_safe(maps, pos, next) {
		rb_erase_init(&pos->rb_node,  &maps->entries);
		map__put(pos);
	}
}

static void maps__exit(struct maps *maps)
{
	down_write(&maps->lock);
	__maps__purge(maps);
	up_write(&maps->lock);
}

bool maps__empty(struct maps *maps)
{
	return !maps__first(maps);
}

struct maps *maps__new(struct machine *machine)
{
	struct maps *maps = zalloc(sizeof(*maps));

	if (maps != NULL)
		maps__init(maps, machine);

	return maps;
}

void maps__delete(struct maps *maps)
{
	maps__exit(maps);
	unwind__finish_access(maps);
	free(maps);
}

void maps__put(struct maps *maps)
{
	if (maps && refcount_dec_and_test(&maps->refcnt))
		maps__delete(maps);
}

struct symbol *maps__find_symbol(struct maps *maps, u64 addr, struct map **mapp)
{
	struct map *map = maps__find(maps, addr);

	/* Ensure map is loaded before using map->map_ip */
	if (map != NULL && map__load(map) >= 0) {
		if (mapp != NULL)
			*mapp = map;
		return map__find_symbol(map, map->map_ip(map, addr));
	}

	return NULL;
}

struct symbol *maps__find_symbol_by_name(struct maps *maps, const char *name, struct map **mapp)
{
	struct symbol *sym;
	struct map *pos;

	down_read(&maps->lock);

	maps__for_each_entry(maps, pos) {
		sym = map__find_symbol_by_name(pos, name);

		if (sym == NULL)
			continue;
		if (!map__contains_symbol(pos, sym)) {
			sym = NULL;
			continue;
		}
		if (mapp != NULL)
			*mapp = pos;
		goto out;
	}

	sym = NULL;
out:
	up_read(&maps->lock);
	return sym;
}

int maps__find_ams(struct maps *maps, struct addr_map_symbol *ams)
{
	if (ams->addr < ams->ms.map->start || ams->addr >= ams->ms.map->end) {
		if (maps == NULL)
			return -1;
		ams->ms.map = maps__find(maps, ams->addr);
		if (ams->ms.map == NULL)
			return -1;
	}

	ams->al_addr = ams->ms.map->map_ip(ams->ms.map, ams->addr);
	ams->ms.sym = map__find_symbol(ams->ms.map, ams->al_addr);

	return ams->ms.sym ? 0 : -1;
}

size_t maps__fprintf(struct maps *maps, FILE *fp)
{
	size_t printed = 0;
	struct map *pos;

	down_read(&maps->lock);

	maps__for_each_entry(maps, pos) {
		printed += fprintf(fp, "Map:");
		printed += map__fprintf(pos, fp);
		if (verbose > 2) {
			printed += dso__fprintf(pos->dso, fp);
			printed += fprintf(fp, "--\n");
		}
	}

	up_read(&maps->lock);

	return printed;
}

int maps__fixup_overlappings(struct maps *maps, struct map *map, FILE *fp)
{
	struct rb_root *root;
	struct rb_node *next, *first;
	int err = 0;

	down_write(&maps->lock);

	root = &maps->entries;

	/*
	 * Find first map where end > map->start.
	 * Same as find_vma() in kernel.
	 */
	next = root->rb_node;
	first = NULL;
	while (next) {
		struct map *pos = rb_entry(next, struct map, rb_node);

		if (pos->end > map->start) {
			first = next;
			if (pos->start <= map->start)
				break;
			next = next->rb_left;
		} else
			next = next->rb_right;
	}

	next = first;
	while (next) {
		struct map *pos = rb_entry(next, struct map, rb_node);
		next = rb_next(&pos->rb_node);

		/*
		 * Stop if current map starts after map->end.
		 * Maps are ordered by start: next will not overlap for sure.
		 */
		if (pos->start >= map->end)
			break;

		if (verbose >= 2) {

			if (use_browser) {
				pr_debug("overlapping maps in %s (disable tui for more info)\n",
					   map->dso->name);
			} else {
				fputs("overlapping maps:\n", fp);
				map__fprintf(map, fp);
				map__fprintf(pos, fp);
			}
		}

		rb_erase_init(&pos->rb_node, root);
		/*
		 * Now check if we need to create new maps for areas not
		 * overlapped by the new map:
		 */
		if (map->start > pos->start) {
			struct map *before = map__clone(pos);

			if (before == NULL) {
				err = -ENOMEM;
				goto put_map;
			}

			before->end = map->start;
			__maps__insert(maps, before);
			if (verbose >= 2 && !use_browser)
				map__fprintf(before, fp);
			map__put(before);
		}

		if (map->end < pos->end) {
			struct map *after = map__clone(pos);

			if (after == NULL) {
				err = -ENOMEM;
				goto put_map;
			}

			after->start = map->end;
			after->pgoff += map->end - pos->start;
			assert(pos->map_ip(pos, map->end) == after->map_ip(after, map->end));
			__maps__insert(maps, after);
			if (verbose >= 2 && !use_browser)
				map__fprintf(after, fp);
			map__put(after);
		}
put_map:
		map__put(pos);

		if (err)
			goto out;
	}

	err = 0;
out:
	up_write(&maps->lock);
	return err;
}

/*
 * XXX This should not really _copy_ te maps, but refcount them.
 */
int maps__clone(struct thread *thread, struct maps *parent)
{
	struct maps *maps = thread->maps;
	int err;
	struct map *map;

	down_read(&parent->lock);

	maps__for_each_entry(parent, map) {
		struct map *new = map__clone(map);

		if (new == NULL) {
			err = -ENOMEM;
			goto out_unlock;
		}

		err = unwind__prepare_access(maps, new, NULL);
		if (err)
			goto out_unlock;

		maps__insert(maps, new);
		map__put(new);
	}

	err = 0;
out_unlock:
	up_read(&parent->lock);
	return err;
}

static void __maps__insert(struct maps *maps, struct map *map)
{
	struct rb_node **p = &maps->entries.rb_node;
	struct rb_node *parent = NULL;
	const u64 ip = map->start;
	struct map *m;

	while (*p != NULL) {
		parent = *p;
		m = rb_entry(parent, struct map, rb_node);
		if (ip < m->start)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	rb_link_node(&map->rb_node, parent, p);
	rb_insert_color(&map->rb_node, &maps->entries);
	map__get(map);
}

struct map *maps__find(struct maps *maps, u64 ip)
{
	struct rb_node *p;
	struct map *m;

	down_read(&maps->lock);

	p = maps->entries.rb_node;
	while (p != NULL) {
		m = rb_entry(p, struct map, rb_node);
		if (ip < m->start)
			p = p->rb_left;
		else if (ip >= m->end)
			p = p->rb_right;
		else
			goto out;
	}

	m = NULL;
out:
	up_read(&maps->lock);
	return m;
}

struct map *maps__first(struct maps *maps)
{
	struct rb_node *first = rb_first(&maps->entries);

	if (first)
		return rb_entry(first, struct map, rb_node);
	return NULL;
}
