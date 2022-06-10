// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2022, Athira Rajeev, IBM Corp.
 */

#include <stdio.h>
#include <sys/prctl.h>
#include <limits.h>
#include "../event.h"
#include "../sampling_tests/misc.h"

#define PM_DTLB_MISS_16G 0x1c058
#define PM_DERAT_MISS_2M 0x1c05a
#define PM_DTLB_MISS_2M 0x1c05c
#define PM_MRK_DTLB_MISS_1G 0x1d15c
#define PM_DTLB_MISS_4K 0x2c056
#define PM_DERAT_MISS_1G 0x2c05a
#define PM_MRK_DERAT_MISS_2M 0x2d152
#define PM_MRK_DTLB_MISS_4K  0x2d156
#define PM_MRK_DTLB_MISS_16G 0x2d15e
#define PM_DTLB_MISS_64K 0x3c056
#define PM_MRK_DERAT_MISS_1G 0x3d152
#define PM_MRK_DTLB_MISS_64K 0x3d156
#define PM_DISP_HELD_SYNC_HOLD 0x4003c
#define PM_DTLB_MISS_16M 0x4c056
#define PM_DTLB_MISS_1G 0x4c05a
#define PM_MRK_DTLB_MISS_16M 0x4c15e
#define PM_MRK_ST_DONE_L2 0x10134
#define PM_RADIX_PWC_L1_HIT 0x1f056
#define PM_FLOP_CMPL 0x100f4
#define PM_MRK_NTF_FIN 0x20112
#define PM_RADIX_PWC_L2_HIT 0x2d024
#define PM_IFETCH_THROTTLE 0x3405e
#define PM_MRK_L2_TM_ST_ABORT_SISTER 0x3e15c
#define PM_RADIX_PWC_L3_HIT 0x3f056
#define PM_RUN_CYC_SMT2_MODE 0x3006c
#define PM_TM_TX_PASS_RUN_INST 0x4e014

#define PVR_POWER9_CUMULUS 0x00002000

int blacklist_events_dd21[] = {
	PM_MRK_ST_DONE_L2,
	PM_RADIX_PWC_L1_HIT,
	PM_FLOP_CMPL,
	PM_MRK_NTF_FIN,
	PM_RADIX_PWC_L2_HIT,
	PM_IFETCH_THROTTLE,
	PM_MRK_L2_TM_ST_ABORT_SISTER,
	PM_RADIX_PWC_L3_HIT,
	PM_RUN_CYC_SMT2_MODE,
	PM_TM_TX_PASS_RUN_INST,
	PM_DISP_HELD_SYNC_HOLD,
};

int blacklist_events_dd22[] = {
	PM_DTLB_MISS_16G,
	PM_DERAT_MISS_2M,
	PM_DTLB_MISS_2M,
	PM_MRK_DTLB_MISS_1G,
	PM_DTLB_MISS_4K,
	PM_DERAT_MISS_1G,
	PM_MRK_DERAT_MISS_2M,
	PM_MRK_DTLB_MISS_4K,
	PM_MRK_DTLB_MISS_16G,
	PM_DTLB_MISS_64K,
	PM_MRK_DERAT_MISS_1G,
	PM_MRK_DTLB_MISS_64K,
	PM_DISP_HELD_SYNC_HOLD,
	PM_DTLB_MISS_16M,
	PM_DTLB_MISS_1G,
	PM_MRK_DTLB_MISS_16M,
};

int pvr_min;

/*
 * check for power9 support for 2.1 and
 * 2.2 model where blacklist is applicable.
 */
int check_for_power9_version(void)
{
	pvr_min = PVR_MIN(mfspr(SPRN_PVR));

	SKIP_IF(PVR_VER(pvr) != POWER9);
	SKIP_IF(!(pvr & PVR_POWER9_CUMULUS));

	SKIP_IF(!(3 - pvr_min));

	return 0;
}

/*
 * Testcase to ensure that using blacklisted bits in
 * event code should cause event_open to fail in power9
 */

static int blacklisted_events(void)
{
	struct event event;
	int i = 0;

	/* Check for platform support for the test */
	SKIP_IF(platform_check_for_tests());

	/*
	 * check for power9 support for 2.1 and
	 * 2.2 model where blacklist is applicable.
	 */
	SKIP_IF(check_for_power9_version());

	/* Skip for Generic compat mode */
	SKIP_IF(check_for_generic_compat_pmu());

	if (pvr_min == 1) {
		for (i = 0; i < ARRAY_SIZE(blacklist_events_dd21); i++) {
			event_init(&event, blacklist_events_dd21[i]);
			FAIL_IF(!event_open(&event));
		}
	} else if (pvr_min == 2) {
		for (i = 0; i < ARRAY_SIZE(blacklist_events_dd22); i++) {
			event_init(&event, blacklist_events_dd22[i]);
			FAIL_IF(!event_open(&event));
		}
	}

	return 0;
}

int main(void)
{
	return test_harness(blacklisted_events, "blacklisted_events");
}
