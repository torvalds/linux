// SPDX-License-Identifier: GPL-2.0
#include <string.h>
#include "tests/tests.h"
#include "arch-tests.h"

struct test arch_tests[] = {
	{
		.desc = "x86 rdpmc",
		.func = test__rdpmc,
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
	{
		.desc = "Intel PT packet decoder",
		.func = test__intel_pt_pkt_decoder,
	},
#endif
#if defined(__x86_64__)
	{
		.desc = "x86 bp modify",
		.func = test__bp_modify,
	},
#endif
	{
		.desc = "x86 Sample parsing",
		.func = test__x86_sample_parsing,
	},
	{
		.func = NULL,
	},

};
