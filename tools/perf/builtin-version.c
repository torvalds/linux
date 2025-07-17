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

static void library_status(void)
{
	for (int i = 0; supported_features[i].name; ++i)
		feature_status__printf(&supported_features[i]);
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
