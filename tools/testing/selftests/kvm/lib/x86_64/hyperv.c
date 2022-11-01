// SPDX-License-Identifier: GPL-2.0-only
/*
 * Hyper-V specific functions.
 *
 * Copyright (C) 2021, Red Hat Inc.
 */
#include <stdint.h>
#include "processor.h"
#include "hyperv.h"

int enable_vp_assist(uint64_t vp_assist_pa, void *vp_assist)
{
	uint64_t val = (vp_assist_pa & HV_X64_MSR_VP_ASSIST_PAGE_ADDRESS_MASK) |
		HV_X64_MSR_VP_ASSIST_PAGE_ENABLE;

	wrmsr(HV_X64_MSR_VP_ASSIST_PAGE, val);

	current_vp_assist = vp_assist;

	return 0;
}
