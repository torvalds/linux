// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit test for the Kernel Linked-list structures.
 *
 * Copyright (C) 2019, Google LLC.
 * Author: David Gow <davidgow@google.com>
 */
#include <kunit/test.h>

#include <linux/list.h>

struct list_test_struct {
	int data;
	struct list_head list;
};

static void list_test_list_init(struct kunit *test)
{
	/* Test the different ways of initialising a list. */
	struct list_head list1 = LIST_HEAD_INIT(list1);
	struct list_head list2;
	LIST_HEAD(list3);
	struct list_head *list4;
	struct list_head *list5;

	INIT_LIST_HEAD(&list2);

	list4 = kzalloc(sizeof(*list4), GFP_KERNEL | __GFP_NOFAIL);
	INIT_LIST_HEAD(list4);

	list5 = kmalloc(sizeof(*list5), GFP_KERNEL | __GFP_NOFAIL);
	memset(list5, 0xFF, sizeof(*list5));
	INIT_LIST_HEAD(list5);

	/* list_empty_careful() checks both next and prev. */
	KUNIT_EXPECT_TRUE(test, list_empty_careful(&list1));
	KUNIT_EXPECT_TRUE(test, list_empty_careful(&list2));
	KUNIT_EXPECT_TRUE(test, list_empty_careful(&list3));
	KUNIT_EXPECT_TRUE(test, list_empty_careful(list4));
	KUNIT_EXPECT_TRUE(test, list_empty_careful(list5));

	kfree(list4);
	kfree(list5);
}

static void list_test_list_add(struct kunit *test)
{
	struct list_head a, b;
	LIST_HEAD(list);

	list_add(&a, &list);
	list_add(&b, &list);

	/* should be [list] -> b -> a */
	KUNIT_EXPECT_PTR_EQ(test, list.next, &b);
	KUNIT_EXPECT_PTR_EQ(test, b.prev, &list);
	KUNIT_EXPECT_PTR_EQ(test, b.next, &a);
}

static void list_test_list_add_tail(struct kunit *test)
{
	struct list_head a, b;
	LIST_HEAD(list);

	list_add_tail(&a, &list);
	list_add_tail(&b, &list);

	/* should be [list] -> a -> b */
	KUNIT_EXPECT_PTR_EQ(test, list.next, &a);
	KUNIT_EXPECT_PTR_EQ(test, a.prev, &list);
	KUNIT_EXPECT_PTR_EQ(test, a.next, &b);
}

static void list_test_list_del(struct kunit *test)
{
	struct list_head a, b;
	LIST_HEAD(list);

	list_add_tail(&a, &list);
	list_add_tail(&b, &list);

	/* before: [list] -> a -> b */
	list_del(&a);

	/* now: [list] -> b */
	KUNIT_EXPECT_PTR_EQ(test, list.next, &b);
	KUNIT_EXPECT_PTR_EQ(test, b.prev, &list);
}

static void list_test_list_replace(struct kunit *test)
{
	struct list_head a_old, a_new, b;
	LIST_HEAD(list);

	list_add_tail(&a_old, &list);
	list_add_tail(&b, &list);

	/* before: [list] -> a_old -> b */
	list_replace(&a_old, &a_new);

	/* now: [list] -> a_new -> b */
	KUNIT_EXPECT_PTR_EQ(test, list.next, &a_new);
	KUNIT_EXPECT_PTR_EQ(test, b.prev, &a_new);
}

static void list_test_list_replace_init(struct kunit *test)
{
	struct list_head a_old, a_new, b;
	LIST_HEAD(list);

	list_add_tail(&a_old, &list);
	list_add_tail(&b, &list);

	/* before: [list] -> a_old -> b */
	list_replace_init(&a_old, &a_new);

	/* now: [list] -> a_new -> b */
	KUNIT_EXPECT_PTR_EQ(test, list.next, &a_new);
	KUNIT_EXPECT_PTR_EQ(test, b.prev, &a_new);

	/* check a_old is empty (initialized) */
	KUNIT_EXPECT_TRUE(test, list_empty_careful(&a_old));
}

static void list_test_list_swap(struct kunit *test)
{
	struct list_head a, b;
	LIST_HEAD(list);

	list_add_tail(&a, &list);
	list_add_tail(&b, &list);

	/* before: [list] -> a -> b */
	list_swap(&a, &b);

	/* after: [list] -> b -> a */
	KUNIT_EXPECT_PTR_EQ(test, &b, list.next);
	KUNIT_EXPECT_PTR_EQ(test, &a, list.prev);

	KUNIT_EXPECT_PTR_EQ(test, &a, b.next);
	KUNIT_EXPECT_PTR_EQ(test, &list, b.prev);

	KUNIT_EXPECT_PTR_EQ(test, &list, a.next);
	KUNIT_EXPECT_PTR_EQ(test, &b, a.prev);
}

static void list_test_list_del_init(struct kunit *test)
{
	struct list_head a, b;
	LIST_HEAD(list);

	list_add_tail(&a, &list);
	list_add_tail(&b, &list);

	/* before: [list] -> a -> b */
	list_del_init(&a);
	/* after: [list] -> b, a initialised */

	KUNIT_EXPECT_PTR_EQ(test, list.next, &b);
	KUNIT_EXPECT_PTR_EQ(test, b.prev, &list);
	KUNIT_EXPECT_TRUE(test, list_empty_careful(&a));
}

static void list_test_list_move(struct kunit *test)
{
	struct list_head a, b;
	LIST_HEAD(list1);
	LIST_HEAD(list2);

	list_add_tail(&a, &list1);
	list_add_tail(&b, &list2);

	/* before: [list1] -> a, [list2] -> b */
	list_move(&a, &list2);
	/* after: [list1] empty, [list2] -> a -> b */

	KUNIT_EXPECT_TRUE(test, list_empty(&list1));

	KUNIT_EXPECT_PTR_EQ(test, &a, list2.next);
	KUNIT_EXPECT_PTR_EQ(test, &b, a.next);
}

static void list_test_list_move_tail(struct kunit *test)
{
	struct list_head a, b;
	LIST_HEAD(list1);
	LIST_HEAD(list2);

	list_add_tail(&a, &list1);
	list_add_tail(&b, &list2);

	/* before: [list1] -> a, [list2] -> b */
	list_move_tail(&a, &list2);
	/* after: [list1] empty, [list2] -> b -> a */

	KUNIT_EXPECT_TRUE(test, list_empty(&list1));

	KUNIT_EXPECT_PTR_EQ(test, &b, list2.next);
	KUNIT_EXPECT_PTR_EQ(test, &a, b.next);
}

static void list_test_list_bulk_move_tail(struct kunit *test)
{
	struct list_head a, b, c, d, x, y;
	struct list_head *list1_values[] = { &x, &b, &c, &y };
	struct list_head *list2_values[] = { &a, &d };
	struct list_head *ptr;
	LIST_HEAD(list1);
	LIST_HEAD(list2);
	int i = 0;

	list_add_tail(&x, &list1);
	list_add_tail(&y, &list1);

	list_add_tail(&a, &list2);
	list_add_tail(&b, &list2);
	list_add_tail(&c, &list2);
	list_add_tail(&d, &list2);

	/* before: [list1] -> x -> y, [list2] -> a -> b -> c -> d */
	list_bulk_move_tail(&y, &b, &c);
	/* after: [list1] -> x -> b -> c -> y, [list2] -> a -> d */

	list_for_each(ptr, &list1) {
		KUNIT_EXPECT_PTR_EQ(test, ptr, list1_values[i]);
		i++;
	}
	KUNIT_EXPECT_EQ(test, i, 4);
	i = 0;
	list_for_each(ptr, &list2) {
		KUNIT_EXPECT_PTR_EQ(test, ptr, list2_values[i]);
		i++;
	}
	KUNIT_EXPECT_EQ(test, i, 2);
}

static void list_test_list_is_first(struct kunit *test)
{
	struct list_head a, b;
	LIST_HEAD(list);

	list_add_tail(&a, &list);
	list_add_tail(&b, &list);

	KUNIT_EXPECT_TRUE(test, list_is_first(&a, &list));
	KUNIT_EXPECT_FALSE(test, list_is_first(&b, &list));
}

static void list_test_list_is_last(struct kunit *test)
{
	struct list_head a, b;
	LIST_HEAD(list);

	list_add_tail(&a, &list);
	list_add_tail(&b, &list);

	KUNIT_EXPECT_FALSE(test, list_is_last(&a, &list));
	KUNIT_EXPECT_TRUE(test, list_is_last(&b, &list));
}

static void list_test_list_empty(struct kunit *test)
{
	struct list_head a;
	LIST_HEAD(list1);
	LIST_HEAD(list2);

	list_add_tail(&a, &list1);

	KUNIT_EXPECT_FALSE(test, list_empty(&list1));
	KUNIT_EXPECT_TRUE(test, list_empty(&list2));
}

static void list_test_list_empty_careful(struct kunit *test)
{
	/* This test doesn't check correctness under concurrent access */
	struct list_head a;
	LIST_HEAD(list1);
	LIST_HEAD(list2);

	list_add_tail(&a, &list1);

	KUNIT_EXPECT_FALSE(test, list_empty_careful(&list1));
	KUNIT_EXPECT_TRUE(test, list_empty_careful(&list2));
}

static void list_test_list_rotate_left(struct kunit *test)
{
	struct list_head a, b;
	LIST_HEAD(list);

	list_add_tail(&a, &list);
	list_add_tail(&b, &list);

	/* before: [list] -> a -> b */
	list_rotate_left(&list);
	/* after: [list] -> b -> a */

	KUNIT_EXPECT_PTR_EQ(test, list.next, &b);
	KUNIT_EXPECT_PTR_EQ(test, b.prev, &list);
	KUNIT_EXPECT_PTR_EQ(test, b.next, &a);
}

static void list_test_list_rotate_to_front(struct kunit *test)
{
	struct list_head a, b, c, d;
	struct list_head *list_values[] = { &c, &d, &a, &b };
	struct list_head *ptr;
	LIST_HEAD(list);
	int i = 0;

	list_add_tail(&a, &list);
	list_add_tail(&b, &list);
	list_add_tail(&c, &list);
	list_add_tail(&d, &list);

	/* before: [list] -> a -> b -> c -> d */
	list_rotate_to_front(&c, &list);
	/* after: [list] -> c -> d -> a -> b */

	list_for_each(ptr, &list) {
		KUNIT_EXPECT_PTR_EQ(test, ptr, list_values[i]);
		i++;
	}
	KUNIT_EXPECT_EQ(test, i, 4);
}

static void list_test_list_is_singular(struct kunit *test)
{
	struct list_head a, b;
	LIST_HEAD(list);

	/* [list] empty */
	KUNIT_EXPECT_FALSE(test, list_is_singular(&list));

	list_add_tail(&a, &list);

	/* [list] -> a */
	KUNIT_EXPECT_TRUE(test, list_is_singular(&list));

	list_add_tail(&b, &list);

	/* [list] -> a -> b */
	KUNIT_EXPECT_FALSE(test, list_is_singular(&list));
}

static void list_test_list_cut_position(struct kunit *test)
{
	struct list_head entries[3], *cur;
	LIST_HEAD(list1);
	LIST_HEAD(list2);
	int i = 0;

	list_add_tail(&entries[0], &list1);
	list_add_tail(&entries[1], &list1);
	list_add_tail(&entries[2], &list1);

	/* before: [list1] -> entries[0] -> entries[1] -> entries[2] */
	list_cut_position(&list2, &list1, &entries[1]);
	/* after: [list2] -> entries[0] -> entries[1], [list1] -> entries[2] */

	list_for_each(cur, &list2) {
		KUNIT_EXPECT_PTR_EQ(test, cur, &entries[i]);
		i++;
	}

	KUNIT_EXPECT_EQ(test, i, 2);

	list_for_each(cur, &list1) {
		KUNIT_EXPECT_PTR_EQ(test, cur, &entries[i]);
		i++;
	}
}

static void list_test_list_cut_before(struct kunit *test)
{
	struct list_head entries[3], *cur;
	LIST_HEAD(list1);
	LIST_HEAD(list2);
	int i = 0;

	list_add_tail(&entries[0], &list1);
	list_add_tail(&entries[1], &list1);
	list_add_tail(&entries[2], &list1);

	/* before: [list1] -> entries[0] -> entries[1] -> entries[2] */
	list_cut_before(&list2, &list1, &entries[1]);
	/* after: [list2] -> entries[0], [list1] -> entries[1] -> entries[2] */

	list_for_each(cur, &list2) {
		KUNIT_EXPECT_PTR_EQ(test, cur, &entries[i]);
		i++;
	}

	KUNIT_EXPECT_EQ(test, i, 1);

	list_for_each(cur, &list1) {
		KUNIT_EXPECT_PTR_EQ(test, cur, &entries[i]);
		i++;
	}
}

static void list_test_list_splice(struct kunit *test)
{
	struct list_head entries[5], *cur;
	LIST_HEAD(list1);
	LIST_HEAD(list2);
	int i = 0;

	list_add_tail(&entries[0], &list1);
	list_add_tail(&entries[1], &list1);
	list_add_tail(&entries[2], &list2);
	list_add_tail(&entries[3], &list2);
	list_add_tail(&entries[4], &list1);

	/* before: [list1]->e[0]->e[1]->e[4], [list2]->e[2]->e[3] */
	list_splice(&list2, &entries[1]);
	/* after: [list1]->e[0]->e[1]->e[2]->e[3]->e[4], [list2] uninit */

	list_for_each(cur, &list1) {
		KUNIT_EXPECT_PTR_EQ(test, cur, &entries[i]);
		i++;
	}

	KUNIT_EXPECT_EQ(test, i, 5);
}

static void list_test_list_splice_tail(struct kunit *test)
{
	struct list_head entries[5], *cur;
	LIST_HEAD(list1);
	LIST_HEAD(list2);
	int i = 0;

	list_add_tail(&entries[0], &list1);
	list_add_tail(&entries[1], &list1);
	list_add_tail(&entries[2], &list2);
	list_add_tail(&entries[3], &list2);
	list_add_tail(&entries[4], &list1);

	/* before: [list1]->e[0]->e[1]->e[4], [list2]->e[2]->e[3] */
	list_splice_tail(&list2, &entries[4]);
	/* after: [list1]->e[0]->e[1]->e[2]->e[3]->e[4], [list2] uninit */

	list_for_each(cur, &list1) {
		KUNIT_EXPECT_PTR_EQ(test, cur, &entries[i]);
		i++;
	}

	KUNIT_EXPECT_EQ(test, i, 5);
}

static void list_test_list_splice_init(struct kunit *test)
{
	struct list_head entries[5], *cur;
	LIST_HEAD(list1);
	LIST_HEAD(list2);
	int i = 0;

	list_add_tail(&entries[0], &list1);
	list_add_tail(&entries[1], &list1);
	list_add_tail(&entries[2], &list2);
	list_add_tail(&entries[3], &list2);
	list_add_tail(&entries[4], &list1);

	/* before: [list1]->e[0]->e[1]->e[4], [list2]->e[2]->e[3] */
	list_splice_init(&list2, &entries[1]);
	/* after: [list1]->e[0]->e[1]->e[2]->e[3]->e[4], [list2] empty */

	list_for_each(cur, &list1) {
		KUNIT_EXPECT_PTR_EQ(test, cur, &entries[i]);
		i++;
	}

	KUNIT_EXPECT_EQ(test, i, 5);

	KUNIT_EXPECT_TRUE(test, list_empty_careful(&list2));
}

static void list_test_list_splice_tail_init(struct kunit *test)
{
	struct list_head entries[5], *cur;
	LIST_HEAD(list1);
	LIST_HEAD(list2);
	int i = 0;

	list_add_tail(&entries[0], &list1);
	list_add_tail(&entries[1], &list1);
	list_add_tail(&entries[2], &list2);
	list_add_tail(&entries[3], &list2);
	list_add_tail(&entries[4], &list1);

	/* before: [list1]->e[0]->e[1]->e[4], [list2]->e[2]->e[3] */
	list_splice_tail_init(&list2, &entries[4]);
	/* after: [list1]->e[0]->e[1]->e[2]->e[3]->e[4], [list2] empty */

	list_for_each(cur, &list1) {
		KUNIT_EXPECT_PTR_EQ(test, cur, &entries[i]);
		i++;
	}

	KUNIT_EXPECT_EQ(test, i, 5);

	KUNIT_EXPECT_TRUE(test, list_empty_careful(&list2));
}

static void list_test_list_entry(struct kunit *test)
{
	struct list_test_struct test_struct;

	KUNIT_EXPECT_PTR_EQ(test, &test_struct, list_entry(&(test_struct.list),
				struct list_test_struct, list));
}

static void list_test_list_first_entry(struct kunit *test)
{
	struct list_test_struct test_struct1, test_struct2;
	LIST_HEAD(list);

	list_add_tail(&test_struct1.list, &list);
	list_add_tail(&test_struct2.list, &list);


	KUNIT_EXPECT_PTR_EQ(test, &test_struct1, list_first_entry(&list,
				struct list_test_struct, list));
}

static void list_test_list_last_entry(struct kunit *test)
{
	struct list_test_struct test_struct1, test_struct2;
	LIST_HEAD(list);

	list_add_tail(&test_struct1.list, &list);
	list_add_tail(&test_struct2.list, &list);


	KUNIT_EXPECT_PTR_EQ(test, &test_struct2, list_last_entry(&list,
				struct list_test_struct, list));
}

static void list_test_list_first_entry_or_null(struct kunit *test)
{
	struct list_test_struct test_struct1, test_struct2;
	LIST_HEAD(list);

	KUNIT_EXPECT_FALSE(test, list_first_entry_or_null(&list,
				struct list_test_struct, list));

	list_add_tail(&test_struct1.list, &list);
	list_add_tail(&test_struct2.list, &list);

	KUNIT_EXPECT_PTR_EQ(test, &test_struct1,
			list_first_entry_or_null(&list,
				struct list_test_struct, list));
}

static void list_test_list_next_entry(struct kunit *test)
{
	struct list_test_struct test_struct1, test_struct2;
	LIST_HEAD(list);

	list_add_tail(&test_struct1.list, &list);
	list_add_tail(&test_struct2.list, &list);


	KUNIT_EXPECT_PTR_EQ(test, &test_struct2, list_next_entry(&test_struct1,
				list));
}

static void list_test_list_prev_entry(struct kunit *test)
{
	struct list_test_struct test_struct1, test_struct2;
	LIST_HEAD(list);

	list_add_tail(&test_struct1.list, &list);
	list_add_tail(&test_struct2.list, &list);


	KUNIT_EXPECT_PTR_EQ(test, &test_struct1, list_prev_entry(&test_struct2,
				list));
}

static void list_test_list_for_each(struct kunit *test)
{
	struct list_head entries[3], *cur;
	LIST_HEAD(list);
	int i = 0;

	list_add_tail(&entries[0], &list);
	list_add_tail(&entries[1], &list);
	list_add_tail(&entries[2], &list);

	list_for_each(cur, &list) {
		KUNIT_EXPECT_PTR_EQ(test, cur, &entries[i]);
		i++;
	}

	KUNIT_EXPECT_EQ(test, i, 3);
}

static void list_test_list_for_each_prev(struct kunit *test)
{
	struct list_head entries[3], *cur;
	LIST_HEAD(list);
	int i = 2;

	list_add_tail(&entries[0], &list);
	list_add_tail(&entries[1], &list);
	list_add_tail(&entries[2], &list);

	list_for_each_prev(cur, &list) {
		KUNIT_EXPECT_PTR_EQ(test, cur, &entries[i]);
		i--;
	}

	KUNIT_EXPECT_EQ(test, i, -1);
}

static void list_test_list_for_each_safe(struct kunit *test)
{
	struct list_head entries[3], *cur, *n;
	LIST_HEAD(list);
	int i = 0;


	list_add_tail(&entries[0], &list);
	list_add_tail(&entries[1], &list);
	list_add_tail(&entries[2], &list);

	list_for_each_safe(cur, n, &list) {
		KUNIT_EXPECT_PTR_EQ(test, cur, &entries[i]);
		list_del(&entries[i]);
		i++;
	}

	KUNIT_EXPECT_EQ(test, i, 3);
	KUNIT_EXPECT_TRUE(test, list_empty(&list));
}

static void list_test_list_for_each_prev_safe(struct kunit *test)
{
	struct list_head entries[3], *cur, *n;
	LIST_HEAD(list);
	int i = 2;

	list_add_tail(&entries[0], &list);
	list_add_tail(&entries[1], &list);
	list_add_tail(&entries[2], &list);

	list_for_each_prev_safe(cur, n, &list) {
		KUNIT_EXPECT_PTR_EQ(test, cur, &entries[i]);
		list_del(&entries[i]);
		i--;
	}

	KUNIT_EXPECT_EQ(test, i, -1);
	KUNIT_EXPECT_TRUE(test, list_empty(&list));
}

static void list_test_list_for_each_entry(struct kunit *test)
{
	struct list_test_struct entries[5], *cur;
	LIST_HEAD(list);
	int i = 0;

	for (i = 0; i < 5; ++i) {
		entries[i].data = i;
		list_add_tail(&entries[i].list, &list);
	}

	i = 0;

	list_for_each_entry(cur, &list, list) {
		KUNIT_EXPECT_EQ(test, cur->data, i);
		i++;
	}

	KUNIT_EXPECT_EQ(test, i, 5);
}

static void list_test_list_for_each_entry_reverse(struct kunit *test)
{
	struct list_test_struct entries[5], *cur;
	LIST_HEAD(list);
	int i = 0;

	for (i = 0; i < 5; ++i) {
		entries[i].data = i;
		list_add_tail(&entries[i].list, &list);
	}

	i = 4;

	list_for_each_entry_reverse(cur, &list, list) {
		KUNIT_EXPECT_EQ(test, cur->data, i);
		i--;
	}

	KUNIT_EXPECT_EQ(test, i, -1);
}

static struct kunit_case list_test_cases[] = {
	KUNIT_CASE(list_test_list_init),
	KUNIT_CASE(list_test_list_add),
	KUNIT_CASE(list_test_list_add_tail),
	KUNIT_CASE(list_test_list_del),
	KUNIT_CASE(list_test_list_replace),
	KUNIT_CASE(list_test_list_replace_init),
	KUNIT_CASE(list_test_list_swap),
	KUNIT_CASE(list_test_list_del_init),
	KUNIT_CASE(list_test_list_move),
	KUNIT_CASE(list_test_list_move_tail),
	KUNIT_CASE(list_test_list_bulk_move_tail),
	KUNIT_CASE(list_test_list_is_first),
	KUNIT_CASE(list_test_list_is_last),
	KUNIT_CASE(list_test_list_empty),
	KUNIT_CASE(list_test_list_empty_careful),
	KUNIT_CASE(list_test_list_rotate_left),
	KUNIT_CASE(list_test_list_rotate_to_front),
	KUNIT_CASE(list_test_list_is_singular),
	KUNIT_CASE(list_test_list_cut_position),
	KUNIT_CASE(list_test_list_cut_before),
	KUNIT_CASE(list_test_list_splice),
	KUNIT_CASE(list_test_list_splice_tail),
	KUNIT_CASE(list_test_list_splice_init),
	KUNIT_CASE(list_test_list_splice_tail_init),
	KUNIT_CASE(list_test_list_entry),
	KUNIT_CASE(list_test_list_first_entry),
	KUNIT_CASE(list_test_list_last_entry),
	KUNIT_CASE(list_test_list_first_entry_or_null),
	KUNIT_CASE(list_test_list_next_entry),
	KUNIT_CASE(list_test_list_prev_entry),
	KUNIT_CASE(list_test_list_for_each),
	KUNIT_CASE(list_test_list_for_each_prev),
	KUNIT_CASE(list_test_list_for_each_safe),
	KUNIT_CASE(list_test_list_for_each_prev_safe),
	KUNIT_CASE(list_test_list_for_each_entry),
	KUNIT_CASE(list_test_list_for_each_entry_reverse),
	{},
};

static struct kunit_suite list_test_module = {
	.name = "list-kunit-test",
	.test_cases = list_test_cases,
};

kunit_test_suites(&list_test_module);

MODULE_LICENSE("GPL v2");
