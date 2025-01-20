// SPDX-License-Identifier: GPL-2.0

#include <kunit/test-bug.h>

struct kunit *rust_helper_kunit_get_current_test(void)
{
	return kunit_get_current_test();
}
