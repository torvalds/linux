// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit compilation/smoke test for Private list primitives.
 *
 * Copyright (c) 2025, Google LLC.
 * Pasha Tatashin <pasha.tatashin@soleen.com>
 */
#include <linux/list_private.h>
#include <kunit/test.h>

/*
 * This forces compiler to warn if you access it directly, because list
 * primitives expect (struct list_head *), not (volatile struct list_head *).
 */
#undef __private
#define __private volatile

/* Redefine ACCESS_PRIVATE for this test. */
#undef ACCESS_PRIVATE
#define ACCESS_PRIVATE(p, member) \
	(*((struct list_head *)((unsigned long)&((p)->member))))

struct list_test_struct {
	int data;
	struct list_head __private list;
};

static void list_private_compile_test(struct kunit *test)
{
	struct list_test_struct entry;
	struct list_test_struct *pos, *n;
	LIST_HEAD(head);

	INIT_LIST_HEAD(&ACCESS_PRIVATE(&entry, list));
	list_add(&ACCESS_PRIVATE(&entry, list), &head);
	pos = &entry;

	pos = list_private_entry(&ACCESS_PRIVATE(&entry, list), struct list_test_struct, list);
	pos = list_private_first_entry(&head, struct list_test_struct, list);
	pos = list_private_last_entry(&head, struct list_test_struct, list);
	pos = list_private_next_entry(pos, list);
	pos = list_private_prev_entry(pos, list);
	pos = list_private_next_entry_circular(pos, &head, list);
	pos = list_private_prev_entry_circular(pos, &head, list);

	if (list_private_entry_is_head(pos, &head, list))
		return;

	list_private_for_each_entry(pos, &head, list) { }
	list_private_for_each_entry_reverse(pos, &head, list) { }
	list_private_for_each_entry_continue(pos, &head, list) { }
	list_private_for_each_entry_continue_reverse(pos, &head, list) { }
	list_private_for_each_entry_from(pos, &head, list) { }
	list_private_for_each_entry_from_reverse(pos, &head, list) { }

	list_private_for_each_entry_safe(pos, n, &head, list)
		list_private_safe_reset_next(pos, n, list);
	list_private_for_each_entry_safe_continue(pos, n, &head, list) { }
	list_private_for_each_entry_safe_from(pos, n, &head, list) { }
	list_private_for_each_entry_safe_reverse(pos, n, &head, list) { }
}

static struct kunit_case list_private_test_cases[] = {
	KUNIT_CASE(list_private_compile_test),
	{},
};

static struct kunit_suite list_private_test_module = {
	.name = "list-private-kunit-test",
	.test_cases = list_private_test_cases,
};

kunit_test_suite(list_private_test_module);

MODULE_DESCRIPTION("KUnit compilation test for private list primitives");
MODULE_LICENSE("GPL");
