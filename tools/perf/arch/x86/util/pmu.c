// SPDX-License-Identifier: GPL-2.0
#include <string.h>

#include <linux/perf_event.h>

#include "../../util/intel-pt.h"
#include "../../util/intel-bts.h"
#include "../../util/pmu.h"

struct perf_event_attr *perf_pmu__get_default_config(struct perf_pmu *pmu __maybe_unused)
{
#ifdef HAVE_AUXTRACE_SUPPORT
	if (!strcmp(pmu->name, INTEL_PT_PMU_NAME))
		return intel_pt_pmu_default_config(pmu);
	if (!strcmp(pmu->name, INTEL_BTS_PMU_NAME))
		pmu->selectable = true;
#endif
	return NULL;
}
