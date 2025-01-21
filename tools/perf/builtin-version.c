// SPDX-License-Identifier: GPL-2.0
#include "builtin.h"
#include "color.h"
#include "util/debug.h"
#include "util/header.h"
#include <tools/config.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <subcmd/parse-options.h>

struct version {
	bool	build_options;
};

static struct version version;

static struct option version_options[] = {
	OPT_BOOLEAN(0, "build-options", &version.build_options,
		    "display the build options"),
	OPT_END(),
};

static const char * const version_usage[] = {
	"perf version [<options>]",
	NULL
};

static void on_off_print(const char *status)
{
	printf("[ ");

	if (!strcmp(status, "OFF"))
		color_fprintf(stdout, PERF_COLOR_RED, "%-3s", status);
	else
		color_fprintf(stdout, PERF_COLOR_GREEN, "%-3s", status);

	printf(" ]");
}

static void status_print(const char *name, const char *macro,
			 const char *status)
{
	printf("%22s: ", name);
	on_off_print(status);
	printf("  # %s\n", macro);
}

#define STATUS(feature)                                   \
do {                                                      \
	if (feature.is_builtin)                               \
		status_print(feature.name, feature.macro, "on");  \
	else                                                  \
		status_print(feature.name, feature.macro, "OFF"); \
} while (0)

static void library_status(void)
{
	for (int i = 0; supported_features[i].name; ++i)
		STATUS(supported_features[i]);
}

int cmd_version(int argc, const char **argv)
{
	argc = parse_options(argc, argv, version_options, version_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);

	printf("perf version %s\n", perf_version_string);

	if (version.build_options || verbose > 0)
		library_status();

	return 0;
}
