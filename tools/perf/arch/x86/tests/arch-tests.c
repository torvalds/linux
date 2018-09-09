// SPDX-License-Identifier: GPL-2.0
#include <string.h>
#include "tests/tests.h"
#include "arch-tests.h"

struct test arch_tests[] = {
	{
		.desc = "x86 rdpmc",
		.func = test__rdpmc,
	},
	{
		.desc = "Convert perf time to TSC",
		.func = test__perf_time_to_tsc,
	},
#ifdef HAVE_DWARF_UNWIND_SUPPORT
	{
		.desc = "DWARF unwind",
		.func = test__dwarf_unwind,
	},
#endif
#ifdef HAVE_AUXTRACE_SUPPORT
	{
		.desc = "x86 instruction decoder - new instructions",
		.func = test__insn_x86,
	},
#endif
#if defined(__x86_64__)
	{
		.desc = "x86 bp modify",
		.func = test__bp_modify,
	},
#endif
	{
		.func = NULL,
	},

};
