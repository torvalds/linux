// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <subcmd/pager.h>
#include <string.h>
#include "config.h"
#include <stdlib.h>
#include <stdio.h>
#include "color.h"
#include <math.h>
#include <unistd.h>

int perf_config_colorbool(const char *var, const char *value, int stdout_is_tty)
{
	if (value) {
		if (!strcasecmp(value, "never"))
			return 0;
		if (!strcasecmp(value, "always"))
			return 1;
		if (!strcasecmp(value, "auto"))
			goto auto_color;
	}

	/* Missing or explicit false to turn off colorization */
	if (!perf_config_bool(var, value))
		return 0;

	/* any normal truth value defaults to 'auto' */
 auto_color:
	if (stdout_is_tty < 0)
		stdout_is_tty = isatty(1);
	if (stdout_is_tty || pager_in_use()) {
		char *term = getenv("TERM");
		if (term && strcmp(term, "dumb"))
			return 1;
	}
	return 0;
}

int perf_color_default_config(const char *var, const char *value,
			      void *cb __maybe_unused)
{
	if (!strcmp(var, "color.ui")) {
		perf_use_color_default = perf_config_colorbool(var, value, -1);
		return 0;
	}

	return 0;
}
