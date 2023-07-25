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
 * Print all test attributes for a test case or suite.
 * Output format for test cases: "# <test_name>.<attribute>: <value>"
 * Output format for test suites: "# <attribute>: <value>"
 */
void kunit_print_attr(void *test_or_suite, bool is_test, unsigned int test_level);

#endif /* _KUNIT_ATTRIBUTES_H */
