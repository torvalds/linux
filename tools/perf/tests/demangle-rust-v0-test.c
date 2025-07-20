// SPDX-License-Identifier: Apache-2.0 OR MIT
#include "tests.h"
#include "debug.h"
#include "symbol.h"
#include <linux/kernel.h>
#include <stdlib.h>
#include <string.h>

static int test__demangle_rust(struct test_suite *test __maybe_unused, int subtest __maybe_unused)
{
	int ret = TEST_OK;
	char *buf = NULL;
	size_t i;

	struct {
		const char *mangled, *demangled;
	} test_cases[] = {
		{ "_RNvMsr_NtCs3ssYzQotkvD_3std4pathNtB5_7PathBuf3newCs15kBYyAo9fc_7mycrate",
		  "<std::path::PathBuf>::new" },
		{ "_RNvCs15kBYyAo9fc_7mycrate7example",
		  "mycrate::example" },
		{ "_RNvMs_Cs4Cv8Wi1oAIB_7mycrateNtB4_7Example3foo",
		  "<mycrate::Example>::foo" },
		{ "_RNvXCs15kBYyAo9fc_7mycrateNtB2_7ExampleNtB2_5Trait3foo",
		  "<mycrate::Example as mycrate::Trait>::foo" },
		{ "_RNvMCs7qp2U7fqm6G_7mycrateNtB2_7Example3foo",
		  "<mycrate::Example>::foo" },
		{ "_RNvMs_Cs7qp2U7fqm6G_7mycrateNtB4_7Example3bar",
		  "<mycrate::Example>::bar" },
		{ "_RNvYNtCs15kBYyAo9fc_7mycrate7ExampleNtB4_5Trait7exampleB4_",
		  "<mycrate::Example as mycrate::Trait>::example" },
		{ "_RNCNvCsgStHSCytQ6I_7mycrate4main0B3_",
		  "mycrate::main::{closure#0}" },
		{ "_RNCNvCsgStHSCytQ6I_7mycrate4mains_0B3_",
		  "mycrate::main::{closure#1}" },
		{ "_RINvCsgStHSCytQ6I_7mycrate7examplelKj1_EB2_",
		  "mycrate::example::<i32, 1>" },
		{ "_RINvCs7qp2U7fqm6G_7mycrate7exampleFG0_RL1_hRL0_tEuEB2_",
		  "mycrate::example::<for<'a, 'b> fn(&'a u8, &'b u16)>",
		},
		{ "_RINvCs7qp2U7fqm6G_7mycrate7exampleKy12345678_EB2_",
		  "mycrate::example::<305419896>" },
		{ "_RNvNvMCsd9PVOYlP1UU_7mycrateINtB4_7ExamplepKpE3foo14EXAMPLE_STATIC",
		  "<mycrate::Example<_, _>>::foo::EXAMPLE_STATIC",
		},
		{ "_RINvCs7qp2U7fqm6G_7mycrate7exampleAtj8_EB2_",
		  "mycrate::example::<[u16; 8]>" },
		{ "_RINvCs7qp2U7fqm6G_7mycrate7exampleNtB2_7ExampleBw_EB2_",
		  "mycrate::example::<mycrate::Example, mycrate::Example>" },
		{ "_RINvMsY_NtCseXNvpPnDBDp_3std4pathNtB6_4Path3neweECs7qp2U7fqm6G_7mycrate",
		  "<std::path::Path>::new::<str>" },
		{ "_RNvNvNvCs7qp2U7fqm6G_7mycrate7EXAMPLE7___getit5___KEY",
		  "mycrate::EXAMPLE::__getit::__KEY" },
	};

	for (i = 0; i < ARRAY_SIZE(test_cases); i++) {
		buf = dso__demangle_sym(/*dso=*/NULL, /*kmodule=*/0, test_cases[i].mangled);
		if (!buf) {
			pr_debug("FAILED to demangle: \"%s\"\n \"%s\"\n", test_cases[i].mangled,
				 test_cases[i].demangled);
			continue;
		}
		if (strcmp(buf, test_cases[i].demangled)) {
			pr_debug("FAILED: %s: %s != %s\n", test_cases[i].mangled,
				 buf, test_cases[i].demangled);
			ret = TEST_FAIL;
		}
		free(buf);
	}

	return ret;
}

DEFINE_SUITE("Demangle Rust", demangle_rust);
