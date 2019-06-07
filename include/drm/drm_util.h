/*
 * Internal Header for the Direct Rendering Manager
 *
 * Copyright 2018 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef _DRM_UTIL_H_
#define _DRM_UTIL_H_

/**
 * DOC: drm utils
 *
 * Macros and inline functions that does not naturally belong in other places
 */

#include <linux/interrupt.h>
#include <linux/kgdb.h>
#include <linux/preempt.h>
#include <linux/smp.h>

/*
 * Use EXPORT_SYMBOL_FOR_TESTS_ONLY() for functions that shall
 * only be visible for drmselftests.
 */
#if defined(CONFIG_DRM_DEBUG_SELFTEST_MODULE)
#define EXPORT_SYMBOL_FOR_TESTS_ONLY(x) EXPORT_SYMBOL(x)
#else
#define EXPORT_SYMBOL_FOR_TESTS_ONLY(x)
#endif

/**
 * for_each_if - helper for handling conditionals in various for_each macros
 * @condition: The condition to check
 *
 * Typical use::
 *
 *	#define for_each_foo_bar(x, y) \'
 *		list_for_each_entry(x, y->list, head) \'
 *			for_each_if(x->something == SOMETHING)
 *
 * The for_each_if() macro makes the use of for_each_foo_bar() less error
 * prone.
 */
#define for_each_if(condition) if (!(condition)) {} else

/**
 * drm_can_sleep - returns true if currently okay to sleep
 *
 * This function shall not be used in new code.
 * The check for running in atomic context may not work - see linux/preempt.h.
 *
 * FIXME: All users of drm_can_sleep should be removed (see todo.rst)
 *
 * Returns:
 * False if kgdb is active, we are in atomic context or irqs are disabled.
 */
static inline bool drm_can_sleep(void)
{
	if (in_atomic() || in_dbg_master() || irqs_disabled())
		return false;
	return true;
}

#endif
