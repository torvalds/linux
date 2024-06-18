/* SPDX-License-Identifier: GPL-2.0 */
/*
 * KUnit API to save and access test attributes
 *
 * Copyright (C) 2023, Google LLC.
 * Author: Rae Moar <rmoar@google.com>
 */

#ifndef _KUNIT_ATTRIBUTES_H
#define _KUNIT_ATTRIBUTES_H

/*
 * struct kunit_attr_filter - representation of attributes filter with the
 * attribute object and string input
 */
struct kunit_attr_filter {
	struct kunit_attr *attr;
	char *input;
};

/*
 * Returns the name of the filter's attribute.
 */
const char *kunit_attr_filter_name(struct kunit_attr_filter filter);

/*
 * Print all test attributes for a test case or suite.
 * Output format for test cases: "# <test_name>.<attribute>: <value>"
 * Output format for test suites: "# <attribute>: <value>"
 */
void kunit_print_attr(void *test_or_suite, bool is_test, unsigned int test_level);

/*
 * Returns the number of fitlers in input.
 */
int kunit_get_filter_count(char *input);

/*
 * Parse attributes filter input and return an objects containing the
 * attribute object and the string input of the next filter.
 */
struct kunit_attr_filter kunit_next_attr_filter(char **filters, int *err);

/*
 * Returns a copy of the suite containing only tests that pass the filter.
 */
struct kunit_suite *kunit_filter_attr_tests(const struct kunit_suite *const suite,
		struct kunit_attr_filter filter, char *action, int *err);

#endif /* _KUNIT_ATTRIBUTES_H */
