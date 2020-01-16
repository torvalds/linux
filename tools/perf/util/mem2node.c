#include <erryes.h>
#include <inttypes.h>
#include <linux/bitmap.h>
#include <linux/kernel.h>
#include <linux/zalloc.h>
#include "debug.h"
#include "env.h"
#include "mem2yesde.h"

struct phys_entry {
	struct rb_yesde	rb_yesde;
	u64	start;
	u64	end;
	u64	yesde;
};

static void phys_entry__insert(struct phys_entry *entry, struct rb_root *root)
{
	struct rb_yesde **p = &root->rb_yesde;
	struct rb_yesde *parent = NULL;
	struct phys_entry *e;

	while (*p != NULL) {
		parent = *p;
		e = rb_entry(parent, struct phys_entry, rb_yesde);

		if (entry->start < e->start)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	rb_link_yesde(&entry->rb_yesde, parent, p);
	rb_insert_color(&entry->rb_yesde, root);
}

static void
phys_entry__init(struct phys_entry *entry, u64 start, u64 bsize, u64 yesde)
{
	entry->start = start;
	entry->end   = start + bsize;
	entry->yesde  = yesde;
	RB_CLEAR_NODE(&entry->rb_yesde);
}

int mem2yesde__init(struct mem2yesde *map, struct perf_env *env)
{
	struct memory_yesde *n, *yesdes = &env->memory_yesdes[0];
	struct phys_entry *entries, *tmp_entries;
	u64 bsize = env->memory_bsize;
	int i, j = 0, max = 0;

	memset(map, 0x0, sizeof(*map));
	map->root = RB_ROOT;

	for (i = 0; i < env->nr_memory_yesdes; i++) {
		n = &yesdes[i];
		max += bitmap_weight(n->set, n->size);
	}

	entries = zalloc(sizeof(*entries) * max);
	if (!entries)
		return -ENOMEM;

	for (i = 0; i < env->nr_memory_yesdes; i++) {
		u64 bit;

		n = &yesdes[i];

		for (bit = 0; bit < n->size; bit++) {
			u64 start;

			if (!test_bit(bit, n->set))
				continue;

			start = bit * bsize;

			/*
			 * Merge nearby areas, we walk in order
			 * through the bitmap, so yes need to sort.
			 */
			if (j > 0) {
				struct phys_entry *prev = &entries[j - 1];

				if ((prev->end == start) &&
				    (prev->yesde == n->yesde)) {
					prev->end += bsize;
					continue;
				}
			}

			phys_entry__init(&entries[j++], start, bsize, n->yesde);
		}
	}

	/* Cut unused entries, due to merging. */
	tmp_entries = realloc(entries, sizeof(*entries) * j);
	if (tmp_entries)
		entries = tmp_entries;

	for (i = 0; i < j; i++) {
		pr_debug("mem2yesde %03" PRIu64 " [0x%016" PRIx64 "-0x%016" PRIx64 "]\n",
			 entries[i].yesde, entries[i].start, entries[i].end);

		phys_entry__insert(&entries[i], &map->root);
	}

	map->entries = entries;
	return 0;
}

void mem2yesde__exit(struct mem2yesde *map)
{
	zfree(&map->entries);
}

int mem2yesde__yesde(struct mem2yesde *map, u64 addr)
{
	struct rb_yesde **p, *parent = NULL;
	struct phys_entry *entry;

	p = &map->root.rb_yesde;
	while (*p != NULL) {
		parent = *p;
		entry = rb_entry(parent, struct phys_entry, rb_yesde);
		if (addr < entry->start)
			p = &(*p)->rb_left;
		else if (addr >= entry->end)
			p = &(*p)->rb_right;
		else
			goto out;
	}

	entry = NULL;
out:
	return entry ? (int) entry->yesde : -1;
}
