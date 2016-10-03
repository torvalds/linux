#include <string.h>
#include "tests/tests.h"
#include "arch-tests.h"

struct test arch_tests[] = {
#ifdef HAVE_DWARF_UNWIND_SUPPORT
	{
		.desc = "Test dwarf unwind",
		.func = test__dwarf_unwind,
	},
#endif
	{
		.func = NULL,
	},
};
