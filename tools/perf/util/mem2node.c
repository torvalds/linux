#include <errno.h>
#include <inttypes.h>
#include <asm/bug.h>
#include <linux/bitmap.h>
#include "mem2node.h"
#include "util.h"

struct phys_entry {
	struct rb_node	rb_node;
	u64	start;
	u64	end;
	u64	node;
};

static void phys_entry__insert(struct phys_entry *entry, struct rb_root *root)
{
	struct rb_node **p = &root->rb_node;
	struct rb_node *parent = NULL;
	struct phys_entry *e;

	while (*p != NULL) {
		parent = *p;
		e = rb_entry(parent, struct phys_entry, rb_node);

		if (entry->start < e->start)
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}

	rb_link_node(&entry->rb_node, parent, p);
	rb_insert_color(&entry->rb_node, root);
}

static void
phys_entry__init(struct phys_entry *entry, u64 start, u64 bsize, u64 node)
{
	entry->start = start;
	entry->end   = start + bsize;
	entry->node  = node;
	RB_CLEAR_NODE(&entry->rb_node);
}

int mem2node__init(struct mem2node *map, struct perf_env *env)
{
	struct memory_node *n, *nodes = &env->memory_nodes[0];
	struct phys_entry *entries, *tmp_entries;
	u64 bsize = env->memory_bsize;
	int i, j = 0, max = 0;

	memset(map, 0x0, sizeof(*map));
	map->root = RB_ROOT;

	for (i = 0; i < env->nr_memory_nodes; i++) {
		n = &nodes[i];
		max += bitmap_weight(n->set, n->size);
	}

	entries = zalloc(sizeof(*entries) * max);
	if (!entries)
		return -ENOMEM;

	for (i = 0; i < env->nr_memory_nodes; i++) {
		u64 bit;

		n = &nodes[i];

		for (bit = 0; bit < n->size; bit++) {
			u64 start;

			if (!test_bit(bit, n->set))
				continue;

			start = bit * bsize;

			/*
			 * Merge nearby areas, we walk in order
			 * through the bitmap, so no need to sort.
			 */
			if (j > 0) {
				struct phys_entry *prev = &entries[j - 1];

				if ((prev->end == start) &&
				    (prev->node == n->node)) {
					prev->end += bsize;
					continue;
				}
			}

			phys_entry__init(&entries[j++], start, bsize, n->node);
		}
	}

	/* Cut unused entries, due to merging. */
	tmp_entries = realloc(entries, sizeof(*entries) * j);
	if (tmp_entries || WARN_ON_ONCE(j == 0))
		entries = tmp_entries;

	for (i = 0; i < j; i++) {
		pr_debug("mem2node %03" PRIu64 " [0x%016" PRIx64 "-0x%016" PRIx64 "]\n",
			 entries[i].node, entries[i].start, entries[i].end);

		phys_entry__insert(&entries[i], &map->root);
	}

	map->entries = entries;
	return 0;
}

void mem2node__exit(struct mem2node *map)
{
	zfree(&map->entries);
}

int mem2node__node(struct mem2node *map, u64 addr)
{
	struct rb_node **p, *parent = NULL;
	struct phys_entry *entry;

	p = &map->root.rb_node;
	while (*p != NULL) {
		parent = *p;
		entry = rb_entry(parent, struct phys_entry, rb_node);
		if (addr < entry->start)
			p = &(*p)->rb_left;
		else if (addr >= entry->end)
			p = &(*p)->rb_right;
		else
			goto out;
	}

	entry = NULL;
out:
	return entry ? (int) entry->node : -1;
}
