// SPDX-License-Identifier: GPL-2.0
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include "linux/perf_event.h"
#include "tests.h"
#include "debug.h"
#include "pmu.h"
#include "pmus.h"
#include "header.h"
#include "../perf-sys.h"

/* hw: cycles, sw: context-switch, uncore: [arch dependent] */
static int types[] = {0, 1, -1};
static unsigned long configs[] = {0, 3, 0};

#define NR_UNCORE_PMUS 5

/* Uncore pmus that support more than 3 counters */
static struct uncore_pmus {
	const char *name;
	__u64 config;
} uncore_pmus[NR_UNCORE_PMUS] = {
	{ "amd_l3", 0x0 },
	{ "amd_df", 0x0 },
	{ "uncore_imc_0", 0x1 },         /* Intel */
	{ "core_imc", 0x318 },           /* PowerPC: core_imc/CPM_STCX_FIN/ */
	{ "hv_24x7", 0x22000000003 },    /* PowerPC: hv_24x7/CPM_STCX_FIN/ */
};

static int event_open(int type, unsigned long config, int group_fd)
{
	struct perf_event_attr attr;

	memset(&attr, 0, sizeof(struct perf_event_attr));
	attr.type = type;
	attr.size = sizeof(struct perf_event_attr);
	attr.config = config;
	/*
	 * When creating an event group, typically the group leader is
	 * initialized with disabled set to 1 and any child events are
	 * initialized with disabled set to 0. Despite disabled being 0,
	 * the child events will not start until the group leader is
	 * enabled.
	 */
	attr.disabled = group_fd == -1 ? 1 : 0;

	return sys_perf_event_open(&attr, -1, 0, group_fd, 0);
}

static int setup_uncore_event(void)
{
	struct perf_pmu *pmu = NULL;
	int i, fd;

	while ((pmu = perf_pmus__scan(pmu)) != NULL) {
		for (i = 0; i < NR_UNCORE_PMUS; i++) {
			if (!strcmp(uncore_pmus[i].name, pmu->name)) {
				pr_debug("Using %s for uncore pmu event\n", pmu->name);
				types[2] = pmu->type;
				configs[2] = uncore_pmus[i].config;
				/*
				 * Check if the chosen uncore pmu event can be
				 * used in the test. For example, incase of accessing
				 * hv_24x7 pmu counters, partition should have
				 * additional permissions. If not, event open will
				 * fail. So check if the event open succeeds
				 * before proceeding.
				 */
				fd = event_open(types[2], configs[2], -1);
				if (fd < 0)
					return -1;
				close(fd);
				return 0;
			}
		}
	}
	return -1;
}

static int run_test(int i, int j, int k)
{
	int erroneous = ((((1 << i) | (1 << j) | (1 << k)) & 5) == 5);
	int group_fd, sibling_fd1, sibling_fd2;

	group_fd = event_open(types[i], configs[i], -1);
	if (group_fd == -1)
		return -1;

	sibling_fd1 = event_open(types[j], configs[j], group_fd);
	if (sibling_fd1 == -1) {
		close(group_fd);
		return erroneous ? 0 : -1;
	}

	sibling_fd2 = event_open(types[k], configs[k], group_fd);
	if (sibling_fd2 == -1) {
		close(sibling_fd1);
		close(group_fd);
		return erroneous ? 0 : -1;
	}

	close(sibling_fd2);
	close(sibling_fd1);
	close(group_fd);
	return erroneous ? -1 : 0;
}

static int test__event_groups(struct test_suite *text __maybe_unused, int subtest __maybe_unused)
{
	int i, j, k;
	int ret;
	int r;

	ret = setup_uncore_event();
	if (ret || types[2] == -1)
		return TEST_SKIP;

	ret = TEST_OK;
	for (i = 0; i < 3; i++) {
		for (j = 0; j < 3; j++) {
			for (k = 0; k < 3; k++) {
				r = run_test(i, j, k);
				if (r)
					ret = TEST_FAIL;

				pr_debug("0x%x 0x%lx, 0x%x 0x%lx, 0x%x 0x%lx: %s\n",
					 types[i], configs[i], types[j], configs[j],
					 types[k], configs[k], r ? "Fail" : "Pass");
			}
		}
	}
	return ret;
}

DEFINE_SUITE("Event groups", event_groups);
