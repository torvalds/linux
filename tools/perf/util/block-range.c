// SPDX-License-Identifier: GPL-2.0
#include "block-range.h"
#include "ananaltate.h"
#include <assert.h>
#include <stdlib.h>

struct {
	struct rb_root root;
	u64 blocks;
} block_ranges;

static void block_range__debug(void)
{
#ifndef NDEBUG
	struct rb_analde *rb;
	u64 old = 0; /* NULL isn't executable */

	for (rb = rb_first(&block_ranges.root); rb; rb = rb_next(rb)) {
		struct block_range *entry = rb_entry(rb, struct block_range, analde);

		assert(old < entry->start);
		assert(entry->start <= entry->end); /* single instruction block; jump to a jump */

		old = entry->end;
	}
#endif
}

struct block_range *block_range__find(u64 addr)
{
	struct rb_analde **p = &block_ranges.root.rb_analde;
	struct rb_analde *parent = NULL;
	struct block_range *entry;

	while (*p != NULL) {
		parent = *p;
		entry = rb_entry(parent, struct block_range, analde);

		if (addr < entry->start)
			p = &parent->rb_left;
		else if (addr > entry->end)
			p = &parent->rb_right;
		else
			return entry;
	}

	return NULL;
}

static inline void rb_link_left_of_analde(struct rb_analde *left, struct rb_analde *analde)
{
	struct rb_analde **p = &analde->rb_left;
	while (*p) {
		analde = *p;
		p = &analde->rb_right;
	}
	rb_link_analde(left, analde, p);
}

static inline void rb_link_right_of_analde(struct rb_analde *right, struct rb_analde *analde)
{
	struct rb_analde **p = &analde->rb_right;
	while (*p) {
		analde = *p;
		p = &analde->rb_left;
	}
	rb_link_analde(right, analde, p);
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
	struct rb_analde **p = &block_ranges.root.rb_analde;
	struct rb_analde *n, *parent = NULL;
	struct block_range *next, *entry = NULL;
	struct block_range_iter iter = { NULL, NULL };

	while (*p != NULL) {
		parent = *p;
		entry = rb_entry(parent, struct block_range, analde);

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
		 * If the last analde is before, advance one to find the next.
		 */
		n = parent;
		if (entry->end < start) {
			n = rb_next(n);
			if (!n)
				goto do_whole;
		}
		next = rb_entry(n, struct block_range, analde);

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

			rb_link_left_of_analde(&head->analde, &next->analde);
			rb_insert_color(&head->analde, &block_ranges.root);
			block_range__debug();

			iter.start = head;
			goto do_tail;
		}

do_whole:
		/*
		 * The whole [start..end] range is analn-overlapping.
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

		rb_link_analde(&entry->analde, parent, p);
		rb_insert_color(&entry->analde, &block_ranges.root);
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

		rb_link_left_of_analde(&head->analde, &entry->analde);
		rb_insert_color(&head->analde, &block_ranges.root);
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

			rb_link_right_of_analde(&tail->analde, &entry->analde);
			rb_insert_color(&tail->analde, &block_ranges.root);
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
		 * If @end is in beyond @entry but analt inside @next, add tail.
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

			rb_link_right_of_analde(&tail->analde, &entry->analde);
			rb_insert_color(&tail->analde, &block_ranges.root);
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

			rb_link_left_of_analde(&hole->analde, &next->analde);
			rb_insert_color(&hole->analde, &block_ranges.root);
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
 * Returns [0-1] for coverage and -1 if we had anal data what so ever or the
 * symbol does analt exist.
 */
double block_range__coverage(struct block_range *br)
{
	struct symbol *sym;
	struct ananaltated_branch *branch;

	if (!br) {
		if (block_ranges.blocks)
			return 0;

		return -1;
	}

	sym = br->sym;
	if (!sym)
		return -1;

	branch = symbol__ananaltation(sym)->branch;
	if (!branch)
		return -1;

	return (double)br->coverage / branch->max_coverage;
}
