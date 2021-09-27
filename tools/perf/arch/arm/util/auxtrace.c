// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright(C) 2015 Linaro Limited. All rights reserved.
 * Author: Mathieu Poirier <mathieu.poirier@linaro.org>
 */

#include <stdbool.h>
#include <linux/coresight-pmu.h>
#include <linux/zalloc.h>

#include "../../../util/auxtrace.h"
#include "../../../util/debug.h"
#include "../../../util/evlist.h"
#include "../../../util/pmu.h"
#include "cs-etm.h"
#include "arm-spe.h"

static struct perf_pmu **find_all_arm_spe_pmus(int *nr_spes, int *err)
{
	struct perf_pmu **arm_spe_pmus = NULL;
	int ret, i, nr_cpus = sysconf(_SC_NPROCESSORS_CONF);
	/* arm_spe_xxxxxxxxx\0 */
	char arm_spe_pmu_name[sizeof(ARM_SPE_PMU_NAME) + 10];

	arm_spe_pmus = zalloc(sizeof(struct perf_pmu *) * nr_cpus);
	if (!arm_spe_pmus) {
		pr_err("spes alloc failed\n");
		*err = -ENOMEM;
		return NULL;
	}

	for (i = 0; i < nr_cpus; i++) {
		ret = sprintf(arm_spe_pmu_name, "%s%d", ARM_SPE_PMU_NAME, i);
		if (ret < 0) {
			pr_err("sprintf failed\n");
			*err = -ENOMEM;
			return NULL;
		}

		arm_spe_pmus[*nr_spes] = perf_pmu__find(arm_spe_pmu_name);
		if (arm_spe_pmus[*nr_spes]) {
			pr_debug2("%s %d: arm_spe_pmu %d type %d name %s\n",
				 __func__, __LINE__, *nr_spes,
				 arm_spe_pmus[*nr_spes]->type,
				 arm_spe_pmus[*nr_spes]->name);
			(*nr_spes)++;
		}
	}

	return arm_spe_pmus;
}

struct auxtrace_record
*auxtrace_record__init(struct evlist *evlist, int *err)
{
	struct perf_pmu	*cs_etm_pmu;
	struct evsel *evsel;
	bool found_etm = false;
	struct perf_pmu *found_spe = NULL;
	struct perf_pmu **arm_spe_pmus = NULL;
	int nr_spes = 0;
	int i = 0;

	if (!evlist)
		return NULL;

	cs_etm_pmu = perf_pmu__find(CORESIGHT_ETM_PMU_NAME);
	arm_spe_pmus = find_all_arm_spe_pmus(&nr_spes, err);

	evlist__for_each_entry(evlist, evsel) {
		if (cs_etm_pmu &&
		    evsel->core.attr.type == cs_etm_pmu->type)
			found_etm = true;

		if (!nr_spes || found_spe)
			continue;

		for (i = 0; i < nr_spes; i++) {
			if (evsel->core.attr.type == arm_spe_pmus[i]->type) {
				found_spe = arm_spe_pmus[i];
				break;
			}
		}
	}
	free(arm_spe_pmus);

	if (found_etm && found_spe) {
		pr_err("Concurrent ARM Coresight ETM and SPE operation not currently supported\n");
		*err = -EOPNOTSUPP;
		return NULL;
	}

	if (found_etm)
		return cs_etm_record_init(err);

#if defined(__aarch64__)
	if (found_spe)
		return arm_spe_recording_init(err, found_spe);
#endif

	/*
	 * Clear 'err' even if we haven't found an event - that way perf
	 * record can still be used even if tracers aren't present.  The NULL
	 * return value will take care of telling the infrastructure HW tracing
	 * isn't available.
	 */
	*err = 0;
	return NULL;
}

#if defined(__arm__)
u64 compat_auxtrace_mmap__read_head(struct auxtrace_mmap *mm)
{
	struct perf_event_mmap_page *pc = mm->userpg;
	u64 result;

	__asm__ __volatile__(
"	ldrd    %0, %H0, [%1]"
	: "=&r" (result)
	: "r" (&pc->aux_head), "Qo" (pc->aux_head)
	);

	return result;
}

int compat_auxtrace_mmap__write_tail(struct auxtrace_mmap *mm, u64 tail)
{
	struct perf_event_mmap_page *pc = mm->userpg;

	/* Ensure all reads are done before we write the tail out */
	smp_mb();

	__asm__ __volatile__(
"	strd    %2, %H2, [%1]"
	: "=Qo" (pc->aux_tail)
	: "r" (&pc->aux_tail), "r" (tail)
	);

	return 0;
}
#endif
