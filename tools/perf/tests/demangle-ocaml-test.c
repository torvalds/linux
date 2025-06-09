// SPDX-License-Identifier: GPL-2.0
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "debug.h"
#include "symbol.h"
#include "tests.h"

static int test__demangle_ocaml(struct test_suite *test __maybe_unused, int subtest __maybe_unused)
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
		  "Stdlib.array.map_154" },
		{ "camlStdlib__anon_fn$5bstdlib$2eml$3a334$2c0$2d$2d54$5d_1453",
		  "Stdlib.anon_fn[stdlib.ml:334,0--54]_1453" },
		{ "camlStdlib__bytes__$2b$2b_2205",
		  "Stdlib.bytes.++_2205" },
	};

	for (i = 0; i < ARRAY_SIZE(test_cases); i++) {
		buf = dso__demangle_sym(/*dso=*/NULL, /*kmodule=*/0, test_cases[i].mangled);
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

DEFINE_SUITE("Demangle OCaml", demangle_ocaml);
