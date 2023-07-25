// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit API to save and access test attributes
 *
 * Copyright (C) 2023, Google LLC.
 * Author: Rae Moar <rmoar@google.com>
 */

#include <kunit/test.h>
#include <kunit/attributes.h>

/* Options for printing attributes:
 * PRINT_ALWAYS - attribute is printed for every test case and suite if set
 * PRINT_SUITE - attribute is printed for every suite if set but not for test cases
 * PRINT_NEVER - attribute is never printed
 */
enum print_ops {
	PRINT_ALWAYS,
	PRINT_SUITE,
	PRINT_NEVER,
};

/**
 * struct kunit_attr - represents a test attribute and holds flexible
 * helper functions to interact with attribute.
 *
 * @name: name of test attribute, eg. speed
 * @get_attr: function to return attribute value given a test
 * @to_string: function to return string representation of given
 * attribute value
 * @filter: function to indicate whether a given attribute value passes a
 * filter
 */
struct kunit_attr {
	const char *name;
	void *(*get_attr)(void *test_or_suite, bool is_test);
	const char *(*to_string)(void *attr, bool *to_free);
	int (*filter)(void *attr, const char *input, int *err);
	void *attr_default;
	enum print_ops print;
};

/* List of all Test Attributes */

static struct kunit_attr kunit_attr_list[] = {};

/* Helper Functions to Access Attributes */

void kunit_print_attr(void *test_or_suite, bool is_test, unsigned int test_level)
{
	int i;
	bool to_free;
	void *attr;
	const char *attr_name, *attr_str;
	struct kunit_suite *suite = is_test ? NULL : test_or_suite;
	struct kunit_case *test = is_test ? test_or_suite : NULL;

	for (i = 0; i < ARRAY_SIZE(kunit_attr_list); i++) {
		if (kunit_attr_list[i].print == PRINT_NEVER ||
				(test && kunit_attr_list[i].print == PRINT_SUITE))
			continue;
		attr = kunit_attr_list[i].get_attr(test_or_suite, is_test);
		if (attr) {
			attr_name = kunit_attr_list[i].name;
			attr_str = kunit_attr_list[i].to_string(attr, &to_free);
			if (test) {
				kunit_log(KERN_INFO, test, "%*s# %s.%s: %s",
					KUNIT_INDENT_LEN * test_level, "", test->name,
					attr_name, attr_str);
			} else {
				kunit_log(KERN_INFO, suite, "%*s# %s: %s",
					KUNIT_INDENT_LEN * test_level, "", attr_name, attr_str);
			}

			/* Free to_string of attribute if needed */
			if (to_free)
				kfree(attr_str);
		}
	}
}
