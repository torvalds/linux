// SPDX-License-Identifier: GPL-2.0
#include "builtin.h"
#include "perf.h"
#include <linux/compiler.h>
#include <stdio.h>

int cmd_version(int argc __maybe_unused, const char **argv __maybe_unused)
{
	printf("perf version %s\n", perf_version_string);
	return 0;
}
