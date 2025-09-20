// SPDX-License-Identifier: GPL-2.0
#include <stdbool.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <subcmd/exec-cmd.h>

#include "debug.h"
#include "util/build-id.h"
#include "util/symbol.h"
#include "util/dso.h"

#include "tests.h"

#ifdef HAVE_LIBBFD_SUPPORT

static int run_dir(const char *d)
{
	char filename[PATH_MAX];
	char debugfile[PATH_MAX];
	struct build_id bid = { .size = 0, };
	char debuglink[PATH_MAX];
	char expect_build_id[] = {
		0x5a, 0x0f, 0xd8, 0x82, 0xb5, 0x30, 0x84, 0x22,
		0x4b, 0xa4, 0x7b, 0x62, 0x4c, 0x55, 0xa4, 0x69,
	};
	char expect_debuglink[PATH_MAX] = "pe-file.exe.debug";
	struct dso *dso;
	struct symbol *sym;
	int ret;
	size_t idx;

	scnprintf(filename, PATH_MAX, "%s/pe-file.exe", d);
	ret = filename__read_build_id(filename, &bid, /*block=*/true);
	TEST_ASSERT_VAL("Failed to read build_id",
			ret == sizeof(expect_build_id));
	TEST_ASSERT_VAL("Wrong build_id", !memcmp(bid.data, expect_build_id,
						  sizeof(expect_build_id)));

	ret = filename__read_debuglink(filename, debuglink, PATH_MAX);
	TEST_ASSERT_VAL("Failed to read debuglink", ret == 0);
	TEST_ASSERT_VAL("Wrong debuglink",
			!strcmp(debuglink, expect_debuglink));

	scnprintf(debugfile, PATH_MAX, "%s/%s", d, debuglink);
	ret = filename__read_build_id(debugfile, &bid, /*block=*/true);
	TEST_ASSERT_VAL("Failed to read debug file build_id",
			ret == sizeof(expect_build_id));
	TEST_ASSERT_VAL("Wrong build_id", !memcmp(bid.data, expect_build_id,
						  sizeof(expect_build_id)));

	dso = dso__new(filename);
	TEST_ASSERT_VAL("Failed to get dso", dso);

	ret = dso__load_bfd_symbols(dso, debugfile);
	TEST_ASSERT_VAL("Failed to load symbols", ret == 0);

	dso__sort_by_name(dso);
	sym = dso__find_symbol_by_name(dso, "main", &idx);
	TEST_ASSERT_VAL("Failed to find main", sym);
	dso__delete(dso);

	return TEST_OK;
}

static int test__pe_file_parsing(struct test_suite *test __maybe_unused,
			  int subtest __maybe_unused)
{
	struct stat st;
	char path_dir[PATH_MAX];

	/* First try development tree tests. */
	if (!lstat("./tests", &st))
		return run_dir("./tests");

	/* Then installed path. */
	snprintf(path_dir, PATH_MAX, "%s/tests", get_argv_exec_path());

	if (!lstat(path_dir, &st))
		return run_dir(path_dir);

	return TEST_SKIP;
}

#else

static int test__pe_file_parsing(struct test_suite *test __maybe_unused,
			  int subtest __maybe_unused)
{
	return TEST_SKIP;
}

#endif

DEFINE_SUITE("PE file support", pe_file_parsing);
