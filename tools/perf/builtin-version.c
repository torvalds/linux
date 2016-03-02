#include "util/util.h"
#include "builtin.h"
#include "perf.h"

int cmd_version(int argc __maybe_unused, const char **argv __maybe_unused,
		const char *prefix __maybe_unused)
{
	printf("perf version %s\n", perf_version_string);
	return 0;
}
