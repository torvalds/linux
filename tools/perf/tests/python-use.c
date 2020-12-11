// SPDX-License-Identifier: GPL-2.0
/*
 * Just test if we can load the python binding.
 */

#include <stdio.h>
#include <stdlib.h>
#include <linux/compiler.h>
#include "tests.h"
#include "util/debug.h"

int test__python_use(struct test *test __maybe_unused, int subtest __maybe_unused)
{
	char *cmd;
	int ret;

	if (asprintf(&cmd, "echo \"import sys ; sys.path.append('%s'); import perf\" | %s %s",
		     PYTHONPATH, PYTHON, verbose > 0 ? "" : "2> /dev/null") < 0)
		return -1;

	pr_debug("python usage test: \"%s\"\n", cmd);
	ret = system(cmd) ? -1 : 0;
	free(cmd);
	return ret;
}
