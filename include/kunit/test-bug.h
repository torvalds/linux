/* SPDX-License-Identifier: GPL-2.0 */
/*
 * KUnit API allowing dynamic analysis tools to interact with KUnit tests
 *
 * Copyright (C) 2020, Google LLC.
 * Author: Uriel Guajardo <urielguajardo@google.com>
 */

#ifndef _KUNIT_TEST_BUG_H
#define _KUNIT_TEST_BUG_H

#define kunit_fail_current_test(fmt, ...) \
	__kunit_fail_current_test(__FILE__, __LINE__, fmt, ##__VA_ARGS__)

#if IS_BUILTIN(CONFIG_KUNIT)

extern __printf(3, 4) void __kunit_fail_current_test(const char *file, int line,
						    const char *fmt, ...);

#else

static inline __printf(3, 4) void __kunit_fail_current_test(const char *file, int line,
							    const char *fmt, ...)
{
}

#endif

#endif /* _KUNIT_TEST_BUG_H */
