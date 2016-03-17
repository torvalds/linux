/*
 * Regression3
 * Description:
 * Helper radix_tree_iter_retry resets next_index to the current index.
 * In following radix_tree_next_slot current chunk size becomes zero.
 * This isn't checked and it tries to dereference null pointer in slot.
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
	void *ptr = (void *)4ul;
	struct radix_tree_iter iter;
	void **slot;
	bool first;

	printf("running regression test 3 (should take milliseconds)\n");

	radix_tree_insert(&root, 0, ptr);
	radix_tree_tag_set(&root, 0, 0);

	first = true;
	radix_tree_for_each_tagged(slot, &root, &iter, 0, 0) {
//		printk("tagged %ld %p\n", iter.index, *slot);
		if (first) {
			radix_tree_insert(&root, 1, ptr);
			radix_tree_tag_set(&root, 1, 0);
			first = false;
		}
		if (radix_tree_deref_retry(*slot)) {
//			printk("retry %ld\n", iter.index);
			slot = radix_tree_iter_retry(&iter);
			continue;
		}
	}
	radix_tree_delete(&root, 1);

	first = true;
	radix_tree_for_each_slot(slot, &root, &iter, 0) {
//		printk("slot %ld %p\n", iter.index, *slot);
		if (first) {
			radix_tree_insert(&root, 1, ptr);
			first = false;
		}
		if (radix_tree_deref_retry(*slot)) {
//			printk("retry %ld\n", iter.index);
			slot = radix_tree_iter_retry(&iter);
			continue;
		}
	}
	radix_tree_delete(&root, 1);

	first = true;
	radix_tree_for_each_contig(slot, &root, &iter, 0) {
//		printk("contig %ld %p\n", iter.index, *slot);
		if (first) {
			radix_tree_insert(&root, 1, ptr);
			first = false;
		}
		if (radix_tree_deref_retry(*slot)) {
//			printk("retry %ld\n", iter.index);
			slot = radix_tree_iter_retry(&iter);
			continue;
		}
	}

	radix_tree_delete(&root, 0);
	radix_tree_delete(&root, 1);

	printf("regression test 3 passed\n");
}
