/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Declarations for hook implementations.
 *
 * These will be set as the function pointers in struct kunit_hook_table,
 * found in include/kunit/test-bug.h.
 *
 * Copyright (C) 2023, Google LLC.
 * Author: David Gow <davidgow@google.com>
 */

#ifndef _KUNIT_HOOKS_IMPL_H
#define _KUNIT_HOOKS_IMPL_H

#include <kunit/test-bug.h>

/* List of declarations. */
void __printf(3, 4) __kunit_fail_current_test_impl(const char *file,
						   int line,
						   const char *fmt, ...);
void *__kunit_get_static_stub_address_impl(struct kunit *test, void *real_fn_addr);

/* Code to set all of the function pointers. */
static inline void kunit_install_hooks(void)
{
	/* Install the KUnit hook functions. */
	kunit_hooks.fail_current_test = __kunit_fail_current_test_impl;
	kunit_hooks.get_static_stub_address = __kunit_get_static_stub_address_impl;
}

#endif /* _KUNIT_HOOKS_IMPL_H */
