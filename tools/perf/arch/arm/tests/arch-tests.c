// SPDX-License-Identifier: GPL-2.0
#include <string.h>
#include "tests/tests.h"
#include "arch-tests.h"

struct test *arch_tests[] = {
#ifdef HAVE_DWARF_UNWIND_SUPPORT
	&dwarf_unwind,
#endif
	&vectors_page,
	NULL,
};
