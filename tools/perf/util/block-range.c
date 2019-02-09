// SPDX-License-Identifier: GPL-2.0
#include "block-range.h"
#include "annotate.h"

struct {
	struct rb_root root;
	u64 blocks;
} block_ranges;

static void block_range__debug(void)
{
	/*
	 * XXX still paranoid for now; see if we can make this depend on
	 * DEBUG=1 builds.
	 */
#if 1
	struct rb_node *rb;
	u64 old = 0; /* NULL isn't executable */

	for (rb = rb_first(&block_ranges.root); rb; rb = rb_next(rb)) {
		struct block_range *entry = rb_entry(rb, struct block_range, node);

		assert(old < entry->start);
		assert(entry->start <= entry->end); /* single instruction block; jump to a jump */

		old = entry->end;
	}
#endif
}

struct block_range *block_range__find(u64 addr)
{
	struct rb_node **p = &block_ranges.root.rb_node;
	struct rb_node *parent = NULL;
	struct block_range *entry;

	while (*p != NULL) {
		parent = *p;
		entry = rb_entry(parent, struct block_range, node);

		if (addr < entry->start)
			p = &parent->rb_left;
		else if (addr > entry->end)
			p = &parent->rb_right;
		else
			return entry;
	}

	return NULL;
}

static inline void rb_link_left_of_node(struct rb_node *left, struct rb_node *node)
{
	struct rb_node **p = &node->rb_left;
	while (*p) {
		node = *p;
		p = &node->rb_right;
	}
	rb_link_node(left, node, p);
}

static inline void rb_link_right_of_node(struct rb_node *right, struct rb_node *node)
{
	struct rb_node **p = &node->rb_right;
	while (*p) {
		node = *p;
		p = &node->rb_left;
	}
	rb_link_node(right, node, p);
}

/**
 * block_range__create
 * @start: branch target starting this basic block
 * @end:   branch ending this basic block
 *
 * Create all the required block ranges to precisely span the given range.
 */
struct block_range_iter block_range__create(u64 start, u64 end)
{
	struct rb_node **p = &block_ranges.root.rb_node;
	struct rb_node *n, *parent = NULL;
	struct block_range *next, *entry = NULL;
	struct block_range_iter iter = { NULL, NULL };

	while (*p != NULL) {
		parent = *p;
		entry = rb_entry(parent, struct block_range, node);

		if (start < entry->start)
			p = &parent->rb_left;
		else if (start > entry->end)
			p = &parent->rb_right;
		else
			break;
	}

	/*
	 * Didn't find anything.. there's a hole at @start, however @end might
	 * be inside/behind the next range.
	 */
	if (!*p) {
		if (!entry) /* tree empty */
			goto do_whole;

		/*
		 * If the last node is before, advance one to find the next.
		 */
		n = parent;
		if (entry->end < start) {
			n = rb_next(n);
			if (!n)
				goto do_whole;
		}
		next = rb_entry(n, struct block_range, node);

		if (next->start <= end) { /* add head: [start...][n->start...] */
			struct block_range *head = malloc(sizeof(struct block_range));
			if (!head)
				return iter;

			*head = (struct block_range){
				.start		= start,
				.end		= next->start - 1,
				.is_target	= 1,
				.is_branch	= 0,
			};

			rb_link_left_of_node(&head->node, &next->node);
			rb_insert_color(&head->node, &block_ranges.root);
			block_range__debug();

			iter.start = head;
			goto do_tail;
		}

do_whole:
		/*
		 * The whole [start..end] range is non-overlapping.
		 */
		entry = malloc(sizeof(struct block_range));
		if (!entry)
			return iter;

		*entry = (struct block_range){
			.start		= start,
			.end		= end,
			.is_target	= 1,
			.is_branch	= 1,
		};

		rb_link_node(&entry->node, parent, p);
		rb_insert_color(&entry->node, &block_ranges.root);
		block_range__debug();

		iter.start = entry;
		iter.end   = entry;
		goto done;
	}

	/*
	 * We found a range that overlapped with ours, split if needed.
	 */
	if (entry->start < start) { /* split: [e->start...][start...] */
		struct block_range *head = malloc(sizeof(struct block_range));
		if (!head)
			return iter;

		*head = (struct block_range){
			.start		= entry->start,
			.end		= start - 1,
			.is_target	= entry->is_target,
			.is_branch	= 0,

			.coverage	= entry->coverage,
			.entry		= entry->entry,
		};

		entry->start		= start;
		entry->is_target	= 1;
		entry->entry		= 0;

		rb_link_left_of_node(&head->node, &entry->node);
		rb_insert_color(&head->node, &block_ranges.root);
		block_range__debug();

	} else if (entry->start == start)
		entry->is_target = 1;

	iter.start = entry;

do_tail:
	/*
	 * At this point we've got: @iter.start = [@start...] but @end can still be
	 * inside or beyond it.
	 */
	entry = iter.start;
	for (;;) {
		/*
		 * If @end is inside @entry, split.
		 */
		if (end < entry->end) { /* split: [...end][...e->end] */
			struct block_range *tail = malloc(sizeof(struct block_range));
			if (!tail)
				return iter;

			*tail = (struct block_range){
				.start		= end + 1,
				.end		= entry->end,
				.is_target	= 0,
				.is_branch	= entry->is_branch,

				.coverage	= entry->coverage,
				.taken		= entry->taken,
				.pred		= entry->pred,
			};

			entry->end		= end;
			entry->is_branch	= 1;
			entry->taken		= 0;
			entry->pred		= 0;

			rb_link_right_of_node(&tail->node, &entry->node);
			rb_insert_color(&tail->node, &block_ranges.root);
			block_range__debug();

			iter.end = entry;
			goto done;
		}

		/*
		 * If @end matches @entry, done
		 */
		if (end == entry->end) {
			entry->is_branch = 1;
			iter.end = entry;
			goto done;
		}

		next = block_range__next(entry);
		if (!next)
			goto add_tail;

		/*
		 * If @end is in beyond @entry but not inside @next, add tail.
		 */
		if (end < next->start) { /* add tail: [...e->end][...end] */
			struct block_range *tail;
add_tail:
			tail = malloc(sizeof(struct block_range));
			if (!tail)
				return iter;

			*tail = (struct block_range){
				.start		= entry->end + 1,
				.end		= end,
				.is_target	= 0,
				.is_branch	= 1,
			};

			rb_link_right_of_node(&tail->node, &entry->node);
			rb_insert_color(&tail->node, &block_ranges.root);
			block_range__debug();

			iter.end = tail;
			goto done;
		}

		/*
		 * If there is a hole between @entry and @next, fill it.
		 */
		if (entry->end + 1 != next->start) {
			struct block_range *hole = malloc(sizeof(struct block_range));
			if (!hole)
				return iter;

			*hole = (struct block_range){
				.start		= entry->end + 1,
				.end		= next->start - 1,
				.is_target	= 0,
				.is_branch	= 0,
			};

			rb_link_left_of_node(&hole->node, &next->node);
			rb_insert_color(&hole->node, &block_ranges.root);
			block_range__debug();
		}

		entry = next;
	}

done:
	assert(iter.start->start == start && iter.start->is_target);
	assert(iter.end->end == end && iter.end->is_branch);

	block_ranges.blocks++;

	return iter;
}


/*
 * Compute coverage as:
 *
 *    br->coverage / br->sym->max_coverage
 *
 * This ensures each symbol has a 100% spot, to reflect that each symbol has a
 * most covered section.
 *
 * Returns [0-1] for coverage and -1 if we had no data what so ever or the
 * symbol does not exist.
 */
double block_range__coverage(struct block_range *br)
{
	struct symbol *sym;

	if (!br) {
		if (block_ranges.blocks)
			return 0;

		return -1;
	}

	sym = br->sym;
	if (!sym)
		return -1;

	return (double)br->coverage / symbol__annotation(sym)->max_coverage;
}
