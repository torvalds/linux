// SPDX-License-Identifier: GPL-2.0
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>
#include <fcntl.h>
#include <linux/stddef.h>
#include <linux/perf_event.h>
#include <linux/zalloc.h>
#include <api/fs/fs.h>
#include <errno.h>

#include "../../../util/intel-pt.h"
#include "../../../util/intel-bts.h"
#include "../../../util/pmu.h"
#include "../../../util/fncache.h"
#include "../../../util/pmus.h"
#include "env.h"

void perf_pmu__arch_init(struct perf_pmu *pmu __maybe_unused)
{
#ifdef HAVE_AUXTRACE_SUPPORT
	if (!strcmp(pmu->name, INTEL_PT_PMU_NAME)) {
		pmu->auxtrace = true;
		pmu->selectable = true;
		pmu->perf_event_attr_init_default = intel_pt_pmu_default_config;
	}
	if (!strcmp(pmu->name, INTEL_BTS_PMU_NAME)) {
		pmu->auxtrace = true;
		pmu->selectable = true;
	}
#endif
}

int perf_pmus__num_mem_pmus(void)
{
	/* AMD uses IBS OP pmu and not a core PMU for perf mem/c2c */
	if (x86__is_amd_cpu())
		return 1;

	/* Intel uses core pmus for perf mem/c2c */
	return perf_pmus__num_core_pmus();
}
