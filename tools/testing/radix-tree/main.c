#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <assert.h>

#include <linux/slab.h>
#include <linux/radix-tree.h>

#include "test.h"
#include "regression.h"

void __gang_check(unsigned long middle, long down, long up, int chunk, int hop)
{
	long idx;
	RADIX_TREE(tree, GFP_KERNEL);

	middle = 1 << 30;

	for (idx = -down; idx < up; idx++)
		item_insert(&tree, middle + idx);

	item_check_absent(&tree, middle - down - 1);
	for (idx = -down; idx < up; idx++)
		item_check_present(&tree, middle + idx);
	item_check_absent(&tree, middle + up);

	item_gang_check_present(&tree, middle - down,
			up + down, chunk, hop);
	item_full_scan(&tree, middle - down, down + up, chunk);
	item_kill_tree(&tree);
}

void gang_check(void)
{
	__gang_check(1 << 30, 128, 128, 35, 2);
	__gang_check(1 << 31, 128, 128, 32, 32);
	__gang_check(1 << 31, 128, 128, 32, 100);
	__gang_check(1 << 31, 128, 128, 17, 7);
	__gang_check(0xffff0000, 0, 65536, 17, 7);
	__gang_check(0xfffffffe, 1, 1, 17, 7);
}

void __big_gang_check(void)
{
	unsigned long start;
	int wrapped = 0;

	start = 0;
	do {
		unsigned long old_start;

//		printf("0x%08lx\n", start);
		__gang_check(start, rand() % 113 + 1, rand() % 71,
				rand() % 157, rand() % 91 + 1);
		old_start = start;
		start += rand() % 1000000;
		start %= 1ULL << 33;
		if (start < old_start)
			wrapped = 1;
	} while (!wrapped);
}

void big_gang_check(bool long_run)
{
	int i;

	for (i = 0; i < (long_run ? 1000 : 3); i++) {
		__big_gang_check();
		printf("%d ", i);
		fflush(stdout);
	}
}

void add_and_check(void)
{
	RADIX_TREE(tree, GFP_KERNEL);

	item_insert(&tree, 44);
	item_check_present(&tree, 44);
	item_check_absent(&tree, 43);
	item_kill_tree(&tree);
}

void dynamic_height_check(void)
{
	int i;
	RADIX_TREE(tree, GFP_KERNEL);
	tree_verify_min_height(&tree, 0);

	item_insert(&tree, 42);
	tree_verify_min_height(&tree, 42);

	item_insert(&tree, 1000000);
	tree_verify_min_height(&tree, 1000000);

	assert(item_delete(&tree, 1000000));
	tree_verify_min_height(&tree, 42);

	assert(item_delete(&tree, 42));
	tree_verify_min_height(&tree, 0);

	for (i = 0; i < 1000; i++) {
		item_insert(&tree, i);
		tree_verify_min_height(&tree, i);
	}

	i--;
	for (;;) {
		assert(item_delete(&tree, i));
		if (i == 0) {
			tree_verify_min_height(&tree, 0);
			break;
		}
		i--;
		tree_verify_min_height(&tree, i);
	}

	item_kill_tree(&tree);
}

void check_copied_tags(struct radix_tree_root *tree, unsigned long start, unsigned long end, unsigned long *idx, int count, int fromtag, int totag)
{
	int i;

	for (i = 0; i < count; i++) {
/*		if (i % 1000 == 0)
			putchar('.'); */
		if (idx[i] < start || idx[i] > end) {
			if (item_tag_get(tree, idx[i], totag)) {
				printf("%lu-%lu: %lu, tags %d-%d\n", start, end, idx[i], item_tag_get(tree, idx[i], fromtag), item_tag_get(tree, idx[i], totag));
			}
			assert(!item_tag_get(tree, idx[i], totag));
			continue;
		}
		if (item_tag_get(tree, idx[i], fromtag) ^
			item_tag_get(tree, idx[i], totag)) {
			printf("%lu-%lu: %lu, tags %d-%d\n", start, end, idx[i], item_tag_get(tree, idx[i], fromtag), item_tag_get(tree, idx[i], totag));
		}
		assert(!(item_tag_get(tree, idx[i], fromtag) ^
			 item_tag_get(tree, idx[i], totag)));
	}
}

#define ITEMS 50000

void copy_tag_check(void)
{
	RADIX_TREE(tree, GFP_KERNEL);
	unsigned long idx[ITEMS];
	unsigned long start, end, count = 0, tagged, cur, tmp;
	int i;

//	printf("generating radix tree indices...\n");
	start = rand();
	end = rand();
	if (start > end && (rand() % 10)) {
		cur = start;
		start = end;
		end = cur;
	}
	/* Specifically create items around the start and the end of the range
	 * with high probability to check for off by one errors */
	cur = rand();
	if (cur & 1) {
		item_insert(&tree, start);
		if (cur & 2) {
			if (start <= end)
				count++;
			item_tag_set(&tree, start, 0);
		}
	}
	if (cur & 4) {
		item_insert(&tree, start-1);
		if (cur & 8)
			item_tag_set(&tree, start-1, 0);
	}
	if (cur & 16) {
		item_insert(&tree, end);
		if (cur & 32) {
			if (start <= end)
				count++;
			item_tag_set(&tree, end, 0);
		}
	}
	if (cur & 64) {
		item_insert(&tree, end+1);
		if (cur & 128)
			item_tag_set(&tree, end+1, 0);
	}

	for (i = 0; i < ITEMS; i++) {
		do {
			idx[i] = rand();
		} while (item_lookup(&tree, idx[i]));

		item_insert(&tree, idx[i]);
		if (rand() & 1) {
			item_tag_set(&tree, idx[i], 0);
			if (idx[i] >= start && idx[i] <= end)
				count++;
		}
/*		if (i % 1000 == 0)
			putchar('.'); */
	}

//	printf("\ncopying tags...\n");
	tagged = tag_tagged_items(&tree, NULL, start, end, ITEMS, 0, 1);

//	printf("checking copied tags\n");
	assert(tagged == count);
	check_copied_tags(&tree, start, end, idx, ITEMS, 0, 1);

	/* Copy tags in several rounds */
//	printf("\ncopying tags...\n");
	tmp = rand() % (count / 10 + 2);
	tagged = tag_tagged_items(&tree, NULL, start, end, tmp, 0, 2);
	assert(tagged == count);

//	printf("%lu %lu %lu\n", tagged, tmp, count);
//	printf("checking copied tags\n");
	check_copied_tags(&tree, start, end, idx, ITEMS, 0, 2);
	verify_tag_consistency(&tree, 0);
	verify_tag_consistency(&tree, 1);
	verify_tag_consistency(&tree, 2);
//	printf("\n");
	item_kill_tree(&tree);
}

static void __locate_check(struct radix_tree_root *tree, unsigned long index,
			unsigned order)
{
	struct item *item;
	unsigned long index2;

	item_insert_order(tree, index, order);
	item = item_lookup(tree, index);
	index2 = find_item(tree, item);
	if (index != index2) {
		printf("index %ld order %d inserted; found %ld\n",
			index, order, index2);
		abort();
	}
}

static void __order_0_locate_check(void)
{
	RADIX_TREE(tree, GFP_KERNEL);
	int i;

	for (i = 0; i < 50; i++)
		__locate_check(&tree, rand() % INT_MAX, 0);

	item_kill_tree(&tree);
}

static void locate_check(void)
{
	RADIX_TREE(tree, GFP_KERNEL);
	unsigned order;
	unsigned long offset, index;

	__order_0_locate_check();

	for (order = 0; order < 20; order++) {
		for (offset = 0; offset < (1 << (order + 3));
		     offset += (1UL << order)) {
			for (index = 0; index < (1UL << (order + 5));
			     index += (1UL << order)) {
				__locate_check(&tree, index + offset, order);
			}
			if (find_item(&tree, &tree) != -1)
				abort();

			item_kill_tree(&tree);
		}
	}

	if (find_item(&tree, &tree) != -1)
		abort();
	__locate_check(&tree, -1, 0);
	if (find_item(&tree, &tree) != -1)
		abort();
	item_kill_tree(&tree);
}

static void single_thread_tests(bool long_run)
{
	int i;

	printf("starting single_thread_tests: %d allocated, preempt %d\n",
		nr_allocated, preempt_count);
	multiorder_checks();
	rcu_barrier();
	printf("after multiorder_check: %d allocated, preempt %d\n",
		nr_allocated, preempt_count);
	locate_check();
	rcu_barrier();
	printf("after locate_check: %d allocated, preempt %d\n",
		nr_allocated, preempt_count);
	tag_check();
	rcu_barrier();
	printf("after tag_check: %d allocated, preempt %d\n",
		nr_allocated, preempt_count);
	gang_check();
	rcu_barrier();
	printf("after gang_check: %d allocated, preempt %d\n",
		nr_allocated, preempt_count);
	add_and_check();
	rcu_barrier();
	printf("after add_and_check: %d allocated, preempt %d\n",
		nr_allocated, preempt_count);
	dynamic_height_check();
	rcu_barrier();
	printf("after dynamic_height_check: %d allocated, preempt %d\n",
		nr_allocated, preempt_count);
	big_gang_check(long_run);
	rcu_barrier();
	printf("after big_gang_check: %d allocated, preempt %d\n",
		nr_allocated, preempt_count);
	for (i = 0; i < (long_run ? 2000 : 3); i++) {
		copy_tag_check();
		printf("%d ", i);
		fflush(stdout);
	}
	rcu_barrier();
	printf("after copy_tag_check: %d allocated, preempt %d\n",
		nr_allocated, preempt_count);
}

int main(int argc, char **argv)
{
	bool long_run = false;
	int opt;
	unsigned int seed = time(NULL);

	while ((opt = getopt(argc, argv, "ls:")) != -1) {
		if (opt == 'l')
			long_run = true;
		else if (opt == 's')
			seed = strtoul(optarg, NULL, 0);
	}

	printf("random seed %u\n", seed);
	srand(seed);

	rcu_register_thread();
	radix_tree_init();

	regression1_test();
	regression2_test();
	regression3_test();
	iteration_test(0, 10);
	iteration_test(7, 20);
	single_thread_tests(long_run);

	/* Free any remaining preallocated nodes */
	radix_tree_cpu_dead(0);

	benchmark();

	rcu_barrier();
	printf("after rcu_barrier: %d allocated, preempt %d\n",
		nr_allocated, preempt_count);
	rcu_unregister_thread();

	exit(0);
}
