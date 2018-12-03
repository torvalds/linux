// SPDX-License-Identifier: GPL-2.0
/*
 * Regression3
 * Description:
 * Helper radix_tree_iter_retry resets next_index to the current index.
 * In following radix_tree_next_slot current chunk size becomes zero.
 * This isn't checked and it tries to dereference null pointer in slot.
 *
 * Helper radix_tree_iter_resume reset slot to NULL and next_index to index + 1,
 * for tagger iteraction it also must reset cached tags in iterator to abort
 * next radix_tree_next_slot and go to slow-path into radix_tree_next_chunk.
 *
 * Running:
 * This test should run to completion immediately. The above bug would
 * cause it to segfault.
 *
 * Upstream commit:
 * Not yet
 */
#include <linux/kernel.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include <linux/radix-tree.h>
#include <stdlib.h>
#include <stdio.h>

#include "regression.h"

void regression3_test(void)
{
	RADIX_TREE(root, GFP_KERNEL);
	void *ptr0 = (void *)4ul;
	void *ptr = (void *)8ul;
	struct radix_tree_iter iter;
	void **slot;
	bool first;

	printv(1, "running regression test 3 (should take milliseconds)\n");

	radix_tree_insert(&root, 0, ptr0);
	radix_tree_tag_set(&root, 0, 0);

	first = true;
	radix_tree_for_each_tagged(slot, &root, &iter, 0, 0) {
		printv(2, "tagged %ld %p\n", iter.index, *slot);
		if (first) {
			radix_tree_insert(&root, 1, ptr);
			radix_tree_tag_set(&root, 1, 0);
			first = false;
		}
		if (radix_tree_deref_retry(*slot)) {
			printv(2, "retry at %ld\n", iter.index);
			slot = radix_tree_iter_retry(&iter);
			continue;
		}
	}
	radix_tree_delete(&root, 1);

	first = true;
	radix_tree_for_each_slot(slot, &root, &iter, 0) {
		printv(2, "slot %ld %p\n", iter.index, *slot);
		if (first) {
			radix_tree_insert(&root, 1, ptr);
			first = false;
		}
		if (radix_tree_deref_retry(*slot)) {
			printv(2, "retry at %ld\n", iter.index);
			slot = radix_tree_iter_retry(&iter);
			continue;
		}
	}

	radix_tree_for_each_slot(slot, &root, &iter, 0) {
		printv(2, "slot %ld %p\n", iter.index, *slot);
		if (!iter.index) {
			printv(2, "next at %ld\n", iter.index);
			slot = radix_tree_iter_resume(slot, &iter);
		}
	}

	radix_tree_tag_set(&root, 0, 0);
	radix_tree_tag_set(&root, 1, 0);
	radix_tree_for_each_tagged(slot, &root, &iter, 0, 0) {
		printv(2, "tagged %ld %p\n", iter.index, *slot);
		if (!iter.index) {
			printv(2, "next at %ld\n", iter.index);
			slot = radix_tree_iter_resume(slot, &iter);
		}
	}

	radix_tree_delete(&root, 0);
	radix_tree_delete(&root, 1);

	printv(1, "regression test 3 passed\n");
}
