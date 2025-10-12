// SPDX-License-Identifier: GPL-2.0
#include "builtin.h"
#include "color.h"
#include "util/bpf-utils.h"
#include "util/debug.h"
#include "util/header.h"
#include <tools/config.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <subcmd/parse-options.h>

static const char * const check_subcommands[] = { "feature", NULL };
static struct option check_options[] = {
	OPT_BOOLEAN('q', "quiet", &quiet, "do not show any warnings or messages"),
	OPT_END()
};
static struct option check_feature_options[] = { OPT_PARENT(check_options) };

static const char *check_usage[] = { NULL, NULL };
static const char *check_feature_usage[] = {
	"perf check feature <feature_list>",
	NULL
};

#define FEATURE_STATUS(name_, macro_) {    \
	.name = name_,                     \
	.macro = #macro_,                  \
	.is_builtin = IS_BUILTIN(macro_) }

#define FEATURE_STATUS_TIP(name_, macro_, tip_) { \
	.name = name_,				  \
	.macro = #macro_,			  \
	.tip = tip_,				  \
	.is_builtin = IS_BUILTIN(macro_) }

struct feature_status supported_features[] = {
	FEATURE_STATUS("aio", HAVE_AIO_SUPPORT),
	FEATURE_STATUS("bpf", HAVE_LIBBPF_SUPPORT),
	FEATURE_STATUS("bpf_skeletons", HAVE_BPF_SKEL),
	FEATURE_STATUS("debuginfod", HAVE_DEBUGINFOD_SUPPORT),
	FEATURE_STATUS("dwarf", HAVE_LIBDW_SUPPORT),
	FEATURE_STATUS("dwarf_getlocations", HAVE_LIBDW_SUPPORT),
	FEATURE_STATUS("dwarf-unwind", HAVE_DWARF_UNWIND_SUPPORT),
	FEATURE_STATUS("auxtrace", HAVE_AUXTRACE_SUPPORT),
	FEATURE_STATUS_TIP("libbfd", HAVE_LIBBFD_SUPPORT, "Deprecated, license incompatibility, use BUILD_NONDISTRO=1 and install binutils-dev[el]"),
	FEATURE_STATUS("libbpf-strings", HAVE_LIBBPF_STRINGS_SUPPORT),
	FEATURE_STATUS("libcapstone", HAVE_LIBCAPSTONE_SUPPORT),
	FEATURE_STATUS("libdw-dwarf-unwind", HAVE_LIBDW_SUPPORT),
	FEATURE_STATUS("libelf", HAVE_LIBELF_SUPPORT),
	FEATURE_STATUS("libLLVM", HAVE_LIBLLVM_SUPPORT),
	FEATURE_STATUS("libnuma", HAVE_LIBNUMA_SUPPORT),
	FEATURE_STATUS("libopencsd", HAVE_CSTRACE_SUPPORT),
	FEATURE_STATUS_TIP("libperl", HAVE_LIBPERL_SUPPORT, "Deprecated, use LIBPERL=1 and install perl-ExtUtils-Embed/libperl-dev to build with it"),
	FEATURE_STATUS("libpfm4", HAVE_LIBPFM),
	FEATURE_STATUS("libpython", HAVE_LIBPYTHON_SUPPORT),
	FEATURE_STATUS("libslang", HAVE_SLANG_SUPPORT),
	FEATURE_STATUS("libtraceevent", HAVE_LIBTRACEEVENT),
	FEATURE_STATUS_TIP("libunwind", HAVE_LIBUNWIND_SUPPORT, "Deprecated, use LIBUNWIND=1 and install libunwind-dev[el] to build with it"),
	FEATURE_STATUS("lzma", HAVE_LZMA_SUPPORT),
	FEATURE_STATUS("numa_num_possible_cpus", HAVE_LIBNUMA_SUPPORT),
	FEATURE_STATUS("zlib", HAVE_ZLIB_SUPPORT),
	FEATURE_STATUS("zstd", HAVE_ZSTD_SUPPORT),

	/* this should remain at end, to know the array end */
	FEATURE_STATUS(NULL, _)
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

/* Helper function to print status of a feature along with name/macro */
void feature_status__printf(const struct feature_status *feature)
{
	const char *name = feature->name, *macro = feature->macro,
		   *status = feature->is_builtin ? "on" : "OFF";

	printf("%22s: ", name);
	on_off_print(status);
	printf("  # %s", macro);

	if (!feature->is_builtin && feature->tip)
		printf(" ( tip: %s )", feature->tip);

	putchar('\n');
}

/**
 * check whether "feature" is built-in with perf
 *
 * returns:
 *    0: NOT built-in or Feature not known
 *    1: Built-in
 */
static int has_support(const char *feature)
{
	for (int i = 0; supported_features[i].name; ++i) {
		if ((strcasecmp(feature, supported_features[i].name) == 0) ||
		    (strcasecmp(feature, supported_features[i].macro) == 0)) {
			if (!quiet)
				feature_status__printf(&supported_features[i]);
			return supported_features[i].is_builtin;
		}
	}

	if (!quiet)
		pr_err("Unknown feature '%s', please use 'perf version --build-options' to see which ones are available.\n", feature);

	return 0;
}


/**
 * Usage: 'perf check feature <feature_list>'
 *
 * <feature_list> can be a single feature name/macro, or a comma-separated list
 * of feature names/macros
 * eg. argument can be "libtraceevent" or "libtraceevent,bpf" etc
 *
 * In case of a comma-separated list, feature_enabled will be 1, only if
 * all features passed in the string are supported
 *
 * Note that argv will get modified
 */
static int subcommand_feature(int argc, const char **argv)
{
	char *feature_list;
	char *feature_name;
	int feature_enabled;

	argc = parse_options(argc, argv, check_feature_options,
			check_feature_usage, 0);

	if (!argc)
		usage_with_options(check_feature_usage, check_feature_options);

	if (argc > 1) {
		pr_err("Too many arguments passed to 'perf check feature'\n");
		return -1;
	}

	feature_enabled = 1;
	/* feature_list is a non-const copy of 'argv[0]' */
	feature_list = strdup(argv[0]);
	if (!feature_list) {
		pr_err("ERROR: failed to allocate memory for feature list\n");
		return -1;
	}

	feature_name = strtok(feature_list, ",");

	while (feature_name) {
		feature_enabled &= has_support(feature_name);
		feature_name = strtok(NULL, ",");
	}

	free(feature_list);

	return !feature_enabled;
}

int cmd_check(int argc, const char **argv)
{
	argc = parse_options_subcommand(argc, argv, check_options,
			check_subcommands, check_usage, 0);

	if (!argc)
		usage_with_options(check_usage, check_options);

	if (strcmp(argv[0], "feature") == 0)
		return subcommand_feature(argc, argv);

	/* If no subcommand matched above, print usage help */
	pr_err("Unknown subcommand: %s\n", argv[0]);
	usage_with_options(check_usage, check_options);

	/* free usage string allocated by parse_options_subcommand */
	free((void *)check_usage[0]);

	return 0;
}
