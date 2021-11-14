// SPDX-License-Identifier: GPL-2.0
#include "util/map_symbol.h"
#include "util/mem-events.h"
#include "util/symbol.h"
#include "linux/perf_event.h"
#include "util/debug.h"
#include "tests.h"
#include <string.h>

static int check(union perf_mem_data_src data_src,
		  const char *string)
{
	char out[100];
	char failure[100];
	struct mem_info mi = { .data_src = data_src };

	int n;

	n = perf_mem__snp_scnprintf(out, sizeof out, &mi);
	n += perf_mem__lvl_scnprintf(out + n, sizeof out - n, &mi);
	scnprintf(failure, sizeof failure, "unexpected %s", out);
	TEST_ASSERT_VAL(failure, !strcmp(string, out));
	return 0;
}

static int test__mem(struct test_suite *text __maybe_unused, int subtest __maybe_unused)
{
	int ret = 0;
	union perf_mem_data_src src;

	memset(&src, 0, sizeof(src));

	src.mem_lvl = PERF_MEM_LVL_HIT;
	src.mem_lvl_num = 4;

	ret |= check(src, "N/AL4 hit");

	src.mem_remote = 1;

	ret |= check(src, "N/ARemote L4 hit");

	src.mem_lvl = PERF_MEM_LVL_MISS;
	src.mem_lvl_num = PERF_MEM_LVLNUM_PMEM;
	src.mem_remote = 0;

	ret |= check(src, "N/APMEM miss");

	src.mem_remote = 1;

	ret |= check(src, "N/ARemote PMEM miss");

	src.mem_snoopx = PERF_MEM_SNOOPX_FWD;
	src.mem_lvl_num = PERF_MEM_LVLNUM_RAM;

	ret |= check(src , "FwdRemote RAM miss");

	return ret;
}

DEFINE_SUITE("Test data source output", mem);
