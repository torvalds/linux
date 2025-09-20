// SPDX-License-Identifier: GPL-2.0
#include <string.h>
#include "tests/tests.h"
#include "arch-tests.h"

struct test_suite *arch_tests[] = {
#ifdef HAVE_DWARF_UNWIND_SUPPORT
	&suite__dwarf_unwind,
#endif
	&suite__vectors_page,
	NULL,
};
