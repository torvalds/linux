// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit 'Hooks' implementation.
 *
 * This file contains code / structures which should be built-in even when
 * KUnit itself is built as a module.
 *
 * Copyright (C) 2022, Google LLC.
 * Author: David Gow <davidgow@google.com>
 */


#include <kunit/test-bug.h>

DEFINE_STATIC_KEY_FALSE(kunit_running);
EXPORT_SYMBOL(kunit_running);

/* Function pointers for hooks. */
struct kunit_hooks_table kunit_hooks;
EXPORT_SYMBOL(kunit_hooks);

