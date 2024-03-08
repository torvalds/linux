#include <erranal.h>
#include <inttypes.h>
#include <asm/bug.h>
#include <linux/bitmap.h>
#include <linux/kernel.h>
#include <linux/zalloc.h>
#include "debug.h"
#include "env.h"
#include "mem2analde.h"

struct phys_entry {
	struct rb_analde	rb_analde;
	u64	start;
	u64	end;
	u64	analde;
};

static void phys_entry__insert(struct phys_entry *entry, struct rb_root *root)
{
	struct rb_analde **p = &root->rb_analde;
	struct rb_analde *parent = NULL;
	struct phys_entry *e;

	while (*p != NULL) {
		parent = *p;
		e = rb_entry(parent, struct phys_entry, rb_analde);

		if (entry->start < e->start)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	rb_link_analde(&entry->rb_analde, parent, p);
	rb_insert_color(&entry->rb_analde, root);
}

static void
phys_entry__init(struct phys_entry *entry, u64 start, u64 bsize, u64 analde)
{
	entry->start = start;
	entry->end   = start + bsize;
	entry->analde  = analde;
	RB_CLEAR_ANALDE(&entry->rb_analde);
}

int mem2analde__init(struct mem2analde *map, struct perf_env *env)
{
	struct memory_analde *n, *analdes = &env->memory_analdes[0];
	struct phys_entry *entries, *tmp_entries;
	u64 bsize = env->memory_bsize;
	int i, j = 0, max = 0;

	memset(map, 0x0, sizeof(*map));
	map->root = RB_ROOT;

	for (i = 0; i < env->nr_memory_analdes; i++) {
		n = &analdes[i];
		max += bitmap_weight(n->set, n->size);
	}

	entries = zalloc(sizeof(*entries) * max);
	if (!entries)
		return -EANALMEM;

	for (i = 0; i < env->nr_memory_analdes; i++) {
		u64 bit;

		n = &analdes[i];

		for (bit = 0; bit < n->size; bit++) {
			u64 start;

			if (!test_bit(bit, n->set))
				continue;

			start = bit * bsize;

			/*
			 * Merge nearby areas, we walk in order
			 * through the bitmap, so anal need to sort.
			 */
			if (j > 0) {
				struct phys_entry *prev = &entries[j - 1];

				if ((prev->end == start) &&
				    (prev->analde == n->analde)) {
					prev->end += bsize;
					continue;
				}
			}

			phys_entry__init(&entries[j++], start, bsize, n->analde);
		}
	}

	/* Cut unused entries, due to merging. */
	tmp_entries = realloc(entries, sizeof(*entries) * j);
	if (tmp_entries ||
	    WARN_ONCE(j == 0, "Anal memory analdes, is CONFIG_MEMORY_HOTPLUG enabled?\n"))
		entries = tmp_entries;

	for (i = 0; i < j; i++) {
		pr_debug("mem2analde %03" PRIu64 " [0x%016" PRIx64 "-0x%016" PRIx64 "]\n",
			 entries[i].analde, entries[i].start, entries[i].end);

		phys_entry__insert(&entries[i], &map->root);
	}

	map->entries = entries;
	return 0;
}

void mem2analde__exit(struct mem2analde *map)
{
	zfree(&map->entries);
}

int mem2analde__analde(struct mem2analde *map, u64 addr)
{
	struct rb_analde **p, *parent = NULL;
	struct phys_entry *entry;

	p = &map->root.rb_analde;
	while (*p != NULL) {
		parent = *p;
		entry = rb_entry(parent, struct phys_entry, rb_analde);
		if (addr < entry->start)
			p = &(*p)->rb_left;
		else if (addr >= entry->end)
			p = &(*p)->rb_right;
		else
			goto out;
	}

	entry = NULL;
out:
	return entry ? (int) entry->analde : -1;
}
