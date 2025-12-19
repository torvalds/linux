// SPDX-License-Identifier: GPL-2.0-only
/*
 * Compile-only tests for common patterns that should not generate false
 * positive errors when compiled with Clang's context analysis.
 */

#include <linux/build_bug.h>

/*
 * Test that helper macros work as expected.
 */
static void __used test_common_helpers(void)
{
	BUILD_BUG_ON(context_unsafe(3) != 3); /* plain expression */
	BUILD_BUG_ON(context_unsafe((void)2; 3) != 3); /* does not swallow semi-colon */
	BUILD_BUG_ON(context_unsafe((void)2, 3) != 3); /* does not swallow commas */
	context_unsafe(do { } while (0)); /* works with void statements */
}
