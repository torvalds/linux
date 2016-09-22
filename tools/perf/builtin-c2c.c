#include <linux/compiler.h>
#include <linux/kernel.h>
#include "util.h"
#include "debug.h"
#include "builtin.h"
#include <subcmd/parse-options.h>

static const char * const c2c_usage[] = {
	"perf c2c",
	NULL
};

int cmd_c2c(int argc, const char **argv, const char *prefix __maybe_unused)
{
	const struct option c2c_options[] = {
	OPT_INCR('v', "verbose", &verbose, "be more verbose"),
	OPT_END()
	};

	argc = parse_options(argc, argv, c2c_options, c2c_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION);
	return 0;
}
