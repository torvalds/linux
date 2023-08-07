// SPDX-License-Identifier: GPL-2.0
#include "arch-tests.h"
#include "linux/perf_event.h"
#include "tests/tests.h"
#include "pmu.h"
#include "pmus.h"
#include "../perf-sys.h"
#include "debug.h"

#define NR_SUB_TESTS 5

static struct sub_tests {
	int type;
	unsigned long config;
	bool valid;
} sub_tests[NR_SUB_TESTS] = {
	{ PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES, true },
	{ PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS, false },
	{ PERF_TYPE_RAW, 0x076, true },
	{ PERF_TYPE_RAW, 0x0C1, true },
	{ PERF_TYPE_RAW, 0x012, false },
};

static int event_open(int type, unsigned long config)
{
	struct perf_event_attr attr;

	memset(&attr, 0, sizeof(struct perf_event_attr));
	attr.type = type;
	attr.size = sizeof(struct perf_event_attr);
	attr.config = config;
	attr.disabled = 1;
	attr.precise_ip = 1;
	attr.sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_TID;
	attr.sample_period = 100000;

	return sys_perf_event_open(&attr, -1, 0, -1, 0);
}

int test__amd_ibs_via_core_pmu(struct test_suite *test __maybe_unused,
			       int subtest __maybe_unused)
{
	struct perf_pmu *ibs_pmu;
	int ret = TEST_OK;
	int fd, i;

	ibs_pmu = perf_pmus__find("ibs_op");
	if (!ibs_pmu)
		return TEST_SKIP;

	for (i = 0; i < NR_SUB_TESTS; i++) {
		fd = event_open(sub_tests[i].type, sub_tests[i].config);
		pr_debug("type: 0x%x, config: 0x%lx, fd: %d  -  ", sub_tests[i].type,
			 sub_tests[i].config, fd);
		if ((sub_tests[i].valid && fd == -1) ||
		    (!sub_tests[i].valid && fd > 0)) {
			pr_debug("Fail\n");
			ret = TEST_FAIL;
		} else {
			pr_debug("Pass\n");
		}

		if (fd > 0)
			close(fd);
	}

	return ret;
}
