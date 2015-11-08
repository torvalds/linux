#include <string.h>
#include "tests/tests.h"
#include "arch-tests.h"

struct test arch_tests[] = {
	{
		.desc = "x86 rdpmc test",
		.func = test__rdpmc,
	},
	{
		.desc = "Test converting perf time to TSC",
		.func = test__perf_time_to_tsc,
	},
#ifdef HAVE_DWARF_UNWIND_SUPPORT
	{
		.desc = "Test dwarf unwind",
		.func = test__dwarf_unwind,
	},
#endif
#ifdef HAVE_AUXTRACE_SUPPORT
	{
		.desc = "Test x86 instruction decoder - new instructions",
		.func = test__insn_x86,
	},
#endif
	{
		.desc = "Test intel cqm nmi context read",
		.func = test__intel_cqm_count_nmi_context,
	},
	{
		.func = NULL,
	},

};
