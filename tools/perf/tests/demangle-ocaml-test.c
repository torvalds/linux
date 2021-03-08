// SPDX-License-Identifier: GPL-2.0
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "tests.h"
#include "session.h"
#include "debug.h"
#include "demangle-ocaml.h"

int test__demangle_ocaml(struct test *test __maybe_unused, int subtest __maybe_unused)
{
	int ret = TEST_OK;
	char *buf = NULL;
	size_t i;

	struct {
		const char *mangled, *demangled;
	} test_cases[] = {
		{ "main",
		  NULL },
		{ "camlStdlib__array__map_154",
		  "Stdlib.array.map" },
		{ "camlStdlib__anon_fn$5bstdlib$2eml$3a334$2c0$2d$2d54$5d_1453",
		  "Stdlib.anon_fn[stdlib.ml:334,0--54]" },
		{ "camlStdlib__bytes__$2b$2b_2205",
		  "Stdlib.bytes.++" },
	};

	for (i = 0; i < sizeof(test_cases) / sizeof(test_cases[0]); i++) {
		buf = ocaml_demangle_sym(test_cases[i].mangled);
		if ((buf == NULL && test_cases[i].demangled != NULL)
				|| (buf != NULL && test_cases[i].demangled == NULL)
				|| (buf != NULL && strcmp(buf, test_cases[i].demangled))) {
			pr_debug("FAILED: %s: %s != %s\n", test_cases[i].mangled,
				 buf == NULL ? "(null)" : buf,
				 test_cases[i].demangled == NULL ? "(null)" : test_cases[i].demangled);
			ret = TEST_FAIL;
		}
		free(buf);
	}

	return ret;
}
