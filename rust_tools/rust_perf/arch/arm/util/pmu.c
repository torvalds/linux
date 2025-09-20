// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright(C) 2015 Linaro Limited. All rights reserved.
 * Author: Mathieu Poirier <mathieu.poirier@linaro.org>
 */

#include <string.h>
#include <linux/coresight-pmu.h>
#include <linux/perf_event.h>
#include <linux/string.h>

#include "arm-spe.h"
#include "hisi-ptt.h"
#include "../../../util/cpumap.h"
#include "../../../util/pmu.h"
#include "../../../util/cs-etm.h"
#include "../../arm64/util/mem-events.h"

void perf_pmu__arch_init(struct perf_pmu *pmu)
{
	struct perf_cpu_map *intersect, *online = cpu_map__online();

#ifdef HAVE_AUXTRACE_SUPPORT
	if (!strcmp(pmu->name, CORESIGHT_ETM_PMU_NAME)) {
		/* add ETM default config here */
		pmu->auxtrace = true;
		pmu->selectable = true;
		pmu->perf_event_attr_init_default = cs_etm_get_default_config;
#if defined(__aarch64__)
	} else if (strstarts(pmu->name, ARM_SPE_PMU_NAME)) {
		pmu->auxtrace = true;
		pmu->selectable = true;
		pmu->is_uncore = false;
		pmu->perf_event_attr_init_default = arm_spe_pmu_default_config;
		if (strstarts(pmu->name, "arm_spe_"))
			pmu->mem_events = perf_mem_events_arm;
	} else if (strstarts(pmu->name, HISI_PTT_PMU_NAME)) {
		pmu->auxtrace = true;
		pmu->selectable = true;
#endif
	}
#endif
	/* Workaround some ARM PMU's failing to correctly set CPU maps for online processors. */
	intersect = perf_cpu_map__intersect(online, pmu->cpus);
	perf_cpu_map__put(online);
	perf_cpu_map__put(pmu->cpus);
	pmu->cpus = intersect;
}
