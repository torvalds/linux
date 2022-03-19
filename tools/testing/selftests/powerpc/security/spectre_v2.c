// SPDX-License-Identifier: GPL-2.0+

/*
 * Copyright 2018-2019 IBM Corporation.
 */

#define __SANE_USERSPACE_TYPES__

#include <sys/types.h>
#include <stdint.h>
#include <malloc.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/prctl.h>
#include "utils.h"

#include "../pmu/event.h"


extern void pattern_cache_loop(void);
extern void indirect_branch_loop(void);

static int do_count_loop(struct event *events, bool is_p9, s64 *miss_percent)
{
	u64 pred, mpred;

	prctl(PR_TASK_PERF_EVENTS_ENABLE);

	if (is_p9)
		pattern_cache_loop();
	else
		indirect_branch_loop();

	prctl(PR_TASK_PERF_EVENTS_DISABLE);

	event_read(&events[0]);
	event_read(&events[1]);

	// We could scale all the events by running/enabled but we're lazy
	// As long as the PMU is uncontended they should all run
	FAIL_IF(events[0].result.running != events[0].result.enabled);
	FAIL_IF(events[1].result.running != events[1].result.enabled);

	pred =  events[0].result.value;
	mpred = events[1].result.value;

	if (is_p9) {
		event_read(&events[2]);
		event_read(&events[3]);
		FAIL_IF(events[2].result.running != events[2].result.enabled);
		FAIL_IF(events[3].result.running != events[3].result.enabled);

		pred  += events[2].result.value;
		mpred += events[3].result.value;
	}

	*miss_percent = 100 * mpred / pred;

	return 0;
}

static void setup_event(struct event *e, u64 config, char *name)
{
	event_init_named(e, config, name);

	e->attr.disabled = 1;
	e->attr.exclude_kernel = 1;
	e->attr.exclude_hv = 1;
	e->attr.exclude_idle = 1;
}

enum spectre_v2_state {
	VULNERABLE = 0,
	UNKNOWN = 1,		// Works with FAIL_IF()
	NOT_AFFECTED,
	BRANCH_SERIALISATION,
	COUNT_CACHE_DISABLED,
	COUNT_CACHE_FLUSH_SW,
	COUNT_CACHE_FLUSH_HW,
	BTB_FLUSH,
};

static enum spectre_v2_state get_sysfs_state(void)
{
	enum spectre_v2_state state = UNKNOWN;
	char buf[256];
	int len;

	memset(buf, 0, sizeof(buf));
	FAIL_IF(read_sysfs_file("devices/system/cpu/vulnerabilities/spectre_v2", buf, sizeof(buf)));

	// Make sure it's NULL terminated
	buf[sizeof(buf) - 1] = '\0';

	// Trim the trailing newline
	len = strlen(buf);
	FAIL_IF(len < 1);
	buf[len - 1] = '\0';

	printf("sysfs reports: '%s'\n", buf);

	// Order matters
	if (strstr(buf, "Vulnerable"))
		state = VULNERABLE;
	else if (strstr(buf, "Not affected"))
		state = NOT_AFFECTED;
	else if (strstr(buf, "Indirect branch serialisation (kernel only)"))
		state = BRANCH_SERIALISATION;
	else if (strstr(buf, "Indirect branch cache disabled"))
		state = COUNT_CACHE_DISABLED;
	else if (strstr(buf, "Software count cache flush (hardware accelerated)"))
		state = COUNT_CACHE_FLUSH_HW;
	else if (strstr(buf, "Software count cache flush"))
		state = COUNT_CACHE_FLUSH_SW;
	else if (strstr(buf, "Branch predictor state flush"))
		state = BTB_FLUSH;

	return state;
}

#define PM_BR_PRED_CCACHE	0x040a4	// P8 + P9
#define PM_BR_MPRED_CCACHE	0x040ac	// P8 + P9
#define PM_BR_PRED_PCACHE	0x048a0	// P9 only
#define PM_BR_MPRED_PCACHE	0x048b0	// P9 only

int spectre_v2_test(void)
{
	enum spectre_v2_state state;
	struct event events[4];
	s64 miss_percent;
	bool is_p9;

	// The PMU events we use only work on Power8 or later
	SKIP_IF(!have_hwcap2(PPC_FEATURE2_ARCH_2_07));

	state = get_sysfs_state();
	if (state == UNKNOWN) {
		printf("Error: couldn't determine spectre_v2 mitigation state?\n");
		return -1;
	}

	memset(events, 0, sizeof(events));

	setup_event(&events[0], PM_BR_PRED_CCACHE,  "PM_BR_PRED_CCACHE");
	setup_event(&events[1], PM_BR_MPRED_CCACHE, "PM_BR_MPRED_CCACHE");
	FAIL_IF(event_open(&events[0]));
	FAIL_IF(event_open_with_group(&events[1], events[0].fd) == -1);

	is_p9 = ((mfspr(SPRN_PVR) >>  16) & 0xFFFF) == 0x4e;

	if (is_p9) {
		// Count pattern cache too
		setup_event(&events[2], PM_BR_PRED_PCACHE,  "PM_BR_PRED_PCACHE");
		setup_event(&events[3], PM_BR_MPRED_PCACHE, "PM_BR_MPRED_PCACHE");

		FAIL_IF(event_open_with_group(&events[2], events[0].fd) == -1);
		FAIL_IF(event_open_with_group(&events[3], events[0].fd) == -1);
	}

	FAIL_IF(do_count_loop(events, is_p9, &miss_percent));

	event_report_justified(&events[0], 18, 10);
	event_report_justified(&events[1], 18, 10);
	event_close(&events[0]);
	event_close(&events[1]);

	if (is_p9) {
		event_report_justified(&events[2], 18, 10);
		event_report_justified(&events[3], 18, 10);
		event_close(&events[2]);
		event_close(&events[3]);
	}

	printf("Miss percent %lld %%\n", miss_percent);

	switch (state) {
	case VULNERABLE:
	case NOT_AFFECTED:
	case COUNT_CACHE_FLUSH_SW:
	case COUNT_CACHE_FLUSH_HW:
		// These should all not affect userspace branch prediction
		if (miss_percent > 15) {
			if (miss_percent > 95) {
				/*
				 * Such a mismatch may be caused by a system being unaware
				 * the count cache is disabled. This may be to enable
				 * guest migration between hosts with different settings.
				 * Return skip code to avoid detecting this as an error.
				 * We are not vulnerable and reporting otherwise, so
				 * missing such a mismatch is safe.
				 */
				printf("Branch misses > 95%% unexpected in this configuration.\n");
				printf("Count cache likely disabled without Linux knowing.\n");
				if (state == COUNT_CACHE_FLUSH_SW)
					printf("WARNING: Kernel performing unnecessary flushes.\n");
				return 4;
			}
			printf("Branch misses > 15%% unexpected in this configuration!\n");
			printf("Possible mismatch between reported & actual mitigation\n");

			return 1;
		}
		break;
	case BRANCH_SERIALISATION:
		// This seems to affect userspace branch prediction a bit?
		if (miss_percent > 25) {
			printf("Branch misses > 25%% unexpected in this configuration!\n");
			printf("Possible mismatch between reported & actual mitigation\n");
			return 1;
		}
		break;
	case COUNT_CACHE_DISABLED:
		if (miss_percent < 95) {
			printf("Branch misses < 95%% unexpected in this configuration!\n");
			printf("Possible mismatch between reported & actual mitigation\n");
			return 1;
		}
		break;
	case UNKNOWN:
	case BTB_FLUSH:
		printf("Not sure!\n");
		return 1;
	}

	printf("OK - Measured branch prediction rates match reported spectre v2 mitigation.\n");

	return 0;
}

int main(int argc, char *argv[])
{
	return test_harness(spectre_v2_test, "spectre_v2");
}
