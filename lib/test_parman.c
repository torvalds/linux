/*
 * lib/test_parman.c - Test module for parman
 * Copyright (c) 2017 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2017 Jiri Pirko <jiri@mellanox.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/random.h>
#include <linux/parman.h>

#define TEST_PARMAN_PRIO_SHIFT 7 /* defines number of prios for testing */
#define TEST_PARMAN_PRIO_COUNT BIT(TEST_PARMAN_PRIO_SHIFT)
#define TEST_PARMAN_PRIO_MASK (TEST_PARMAN_PRIO_COUNT - 1)

#define TEST_PARMAN_ITEM_SHIFT 13 /* defines a total number
				   * of items for testing
				   */
#define TEST_PARMAN_ITEM_COUNT BIT(TEST_PARMAN_ITEM_SHIFT)
#define TEST_PARMAN_ITEM_MASK (TEST_PARMAN_ITEM_COUNT - 1)

#define TEST_PARMAN_BASE_SHIFT 8
#define TEST_PARMAN_BASE_COUNT BIT(TEST_PARMAN_BASE_SHIFT)
#define TEST_PARMAN_RESIZE_STEP_SHIFT 7
#define TEST_PARMAN_RESIZE_STEP_COUNT BIT(TEST_PARMAN_RESIZE_STEP_SHIFT)

#define TEST_PARMAN_BULK_MAX_SHIFT (2 + TEST_PARMAN_RESIZE_STEP_SHIFT)
#define TEST_PARMAN_BULK_MAX_COUNT BIT(TEST_PARMAN_BULK_MAX_SHIFT)
#define TEST_PARMAN_BULK_MAX_MASK (TEST_PARMAN_BULK_MAX_COUNT - 1)

#define TEST_PARMAN_RUN_BUDGET (TEST_PARMAN_ITEM_COUNT * 256)

struct test_parman_prio {
	struct parman_prio parman_prio;
	unsigned long priority;
};

struct test_parman_item {
	struct parman_item parman_item;
	struct test_parman_prio *prio;
	bool used;
};

struct test_parman {
	struct parman *parman;
	struct test_parman_item **prio_array;
	unsigned long prio_array_limit;
	struct test_parman_prio prios[TEST_PARMAN_PRIO_COUNT];
	struct test_parman_item items[TEST_PARMAN_ITEM_COUNT];
	struct rnd_state rnd;
	unsigned long run_budget;
	unsigned long bulk_budget;
	bool bulk_noop;
	unsigned int used_items;
};

#define ITEM_PTRS_SIZE(count) (sizeof(struct test_parman_item *) * (count))

static int test_parman_resize(void *priv, unsigned long new_count)
{
	struct test_parman *test_parman = priv;
	struct test_parman_item **prio_array;
	unsigned long old_count;

	prio_array = krealloc(test_parman->prio_array,
			      ITEM_PTRS_SIZE(new_count), GFP_KERNEL);
	if (new_count == 0)
		return 0;
	if (!prio_array)
		return -ENOMEM;
	old_count = test_parman->prio_array_limit;
	if (new_count > old_count)
		memset(&prio_array[old_count], 0,
		       ITEM_PTRS_SIZE(new_count - old_count));
	test_parman->prio_array = prio_array;
	test_parman->prio_array_limit = new_count;
	return 0;
}

static void test_parman_move(void *priv, unsigned long from_index,
			     unsigned long to_index, unsigned long count)
{
	struct test_parman *test_parman = priv;
	struct test_parman_item **prio_array = test_parman->prio_array;

	memmove(&prio_array[to_index], &prio_array[from_index],
		ITEM_PTRS_SIZE(count));
	memset(&prio_array[from_index], 0, ITEM_PTRS_SIZE(count));
}

static const struct parman_ops test_parman_lsort_ops = {
	.base_count	= TEST_PARMAN_BASE_COUNT,
	.resize_step	= TEST_PARMAN_RESIZE_STEP_COUNT,
	.resize		= test_parman_resize,
	.move		= test_parman_move,
	.algo		= PARMAN_ALGO_TYPE_LSORT,
};

static void test_parman_rnd_init(struct test_parman *test_parman)
{
	prandom_seed_state(&test_parman->rnd, 3141592653589793238ULL);
}

static u32 test_parman_rnd_get(struct test_parman *test_parman)
{
	return prandom_u32_state(&test_parman->rnd);
}

static unsigned long test_parman_priority_gen(struct test_parman *test_parman)
{
	unsigned long priority;
	int i;

again:
	priority = test_parman_rnd_get(test_parman);
	if (priority == 0)
		goto again;

	for (i = 0; i < TEST_PARMAN_PRIO_COUNT; i++) {
		struct test_parman_prio *prio = &test_parman->prios[i];

		if (prio->priority == 0)
			break;
		if (prio->priority == priority)
			goto again;
	}
	return priority;
}

static void test_parman_prios_init(struct test_parman *test_parman)
{
	int i;

	for (i = 0; i < TEST_PARMAN_PRIO_COUNT; i++) {
		struct test_parman_prio *prio = &test_parman->prios[i];

		/* Assign random uniqueue priority to each prio structure */
		prio->priority = test_parman_priority_gen(test_parman);
		parman_prio_init(test_parman->parman, &prio->parman_prio,
				 prio->priority);
	}
}

static void test_parman_prios_fini(struct test_parman *test_parman)
{
	int i;

	for (i = 0; i < TEST_PARMAN_PRIO_COUNT; i++) {
		struct test_parman_prio *prio = &test_parman->prios[i];

		parman_prio_fini(&prio->parman_prio);
	}
}

static void test_parman_items_init(struct test_parman *test_parman)
{
	int i;

	for (i = 0; i < TEST_PARMAN_ITEM_COUNT; i++) {
		struct test_parman_item *item = &test_parman->items[i];
		unsigned int prio_index = test_parman_rnd_get(test_parman) &
					  TEST_PARMAN_PRIO_MASK;

		/* Assign random prio to each item structure */
		item->prio = &test_parman->prios[prio_index];
	}
}

static void test_parman_items_fini(struct test_parman *test_parman)
{
	int i;

	for (i = 0; i < TEST_PARMAN_ITEM_COUNT; i++) {
		struct test_parman_item *item = &test_parman->items[i];

		if (!item->used)
			continue;
		parman_item_remove(test_parman->parman,
				   &item->prio->parman_prio,
				   &item->parman_item);
	}
}

static struct test_parman *test_parman_create(const struct parman_ops *ops)
{
	struct test_parman *test_parman;
	int err;

	test_parman = kzalloc(sizeof(*test_parman), GFP_KERNEL);
	if (!test_parman)
		return ERR_PTR(-ENOMEM);
	err = test_parman_resize(test_parman, TEST_PARMAN_BASE_COUNT);
	if (err)
		goto err_resize;
	test_parman->parman = parman_create(ops, test_parman);
	if (!test_parman->parman) {
		err = -ENOMEM;
		goto err_parman_create;
	}
	test_parman_rnd_init(test_parman);
	test_parman_prios_init(test_parman);
	test_parman_items_init(test_parman);
	test_parman->run_budget = TEST_PARMAN_RUN_BUDGET;
	return test_parman;

err_parman_create:
	test_parman_resize(test_parman, 0);
err_resize:
	kfree(test_parman);
	return ERR_PTR(err);
}

static void test_parman_destroy(struct test_parman *test_parman)
{
	test_parman_items_fini(test_parman);
	test_parman_prios_fini(test_parman);
	parman_destroy(test_parman->parman);
	test_parman_resize(test_parman, 0);
	kfree(test_parman);
}

static bool test_parman_run_check_budgets(struct test_parman *test_parman)
{
	if (test_parman->run_budget-- == 0)
		return false;
	if (test_parman->bulk_budget-- != 0)
		return true;

	test_parman->bulk_budget = test_parman_rnd_get(test_parman) &
				   TEST_PARMAN_BULK_MAX_MASK;
	test_parman->bulk_noop = test_parman_rnd_get(test_parman) & 1;
	return true;
}

static int test_parman_run(struct test_parman *test_parman)
{
	unsigned int i = test_parman_rnd_get(test_parman);
	int err;

	while (test_parman_run_check_budgets(test_parman)) {
		unsigned int item_index = i++ & TEST_PARMAN_ITEM_MASK;
		struct test_parman_item *item = &test_parman->items[item_index];

		if (test_parman->bulk_noop)
			continue;

		if (!item->used) {
			err = parman_item_add(test_parman->parman,
					      &item->prio->parman_prio,
					      &item->parman_item);
			if (err)
				return err;
			test_parman->prio_array[item->parman_item.index] = item;
			test_parman->used_items++;
		} else {
			test_parman->prio_array[item->parman_item.index] = NULL;
			parman_item_remove(test_parman->parman,
					   &item->prio->parman_prio,
					   &item->parman_item);
			test_parman->used_items--;
		}
		item->used = !item->used;
	}
	return 0;
}

static int test_parman_check_array(struct test_parman *test_parman,
				   bool gaps_allowed)
{
	unsigned int last_unused_items = 0;
	unsigned long last_priority = 0;
	unsigned int used_items = 0;
	int i;

	if (test_parman->prio_array_limit < TEST_PARMAN_BASE_COUNT) {
		pr_err("Array limit is lower than the base count (%lu < %lu)\n",
		       test_parman->prio_array_limit, TEST_PARMAN_BASE_COUNT);
		return -EINVAL;
	}

	for (i = 0; i < test_parman->prio_array_limit; i++) {
		struct test_parman_item *item = test_parman->prio_array[i];

		if (!item) {
			last_unused_items++;
			continue;
		}
		if (last_unused_items && !gaps_allowed) {
			pr_err("Gap found in array even though they are forbidden\n");
			return -EINVAL;
		}

		last_unused_items = 0;
		used_items++;

		if (item->prio->priority < last_priority) {
			pr_err("Item belongs under higher priority then the last one (current: %lu, previous: %lu)\n",
			       item->prio->priority, last_priority);
			return -EINVAL;
		}
		last_priority = item->prio->priority;

		if (item->parman_item.index != i) {
			pr_err("Item has different index in compare to where it actualy is (%lu != %d)\n",
			       item->parman_item.index, i);
			return -EINVAL;
		}
	}

	if (used_items != test_parman->used_items) {
		pr_err("Number of used items in array does not match (%u != %u)\n",
		       used_items, test_parman->used_items);
		return -EINVAL;
	}

	if (last_unused_items >= TEST_PARMAN_RESIZE_STEP_COUNT) {
		pr_err("Number of unused item at the end of array is bigger than resize step (%u >= %lu)\n",
		       last_unused_items, TEST_PARMAN_RESIZE_STEP_COUNT);
		return -EINVAL;
	}

	pr_info("Priority array check successful\n");

	return 0;
}

static int test_parman_lsort(void)
{
	struct test_parman *test_parman;
	int err;

	test_parman = test_parman_create(&test_parman_lsort_ops);
	if (IS_ERR(test_parman))
		return PTR_ERR(test_parman);

	err = test_parman_run(test_parman);
	if (err)
		goto out;

	err = test_parman_check_array(test_parman, false);
	if (err)
		goto out;
out:
	test_parman_destroy(test_parman);
	return err;
}

static int __init test_parman_init(void)
{
	return test_parman_lsort();
}

static void __exit test_parman_exit(void)
{
}

module_init(test_parman_init);
module_exit(test_parman_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Jiri Pirko <jiri@mellanox.com>");
MODULE_DESCRIPTION("Test module for parman");
