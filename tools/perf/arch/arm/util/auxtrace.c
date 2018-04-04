/*
 * Copyright(C) 2015 Linaro Limited. All rights reserved.
 * Author: Mathieu Poirier <mathieu.poirier@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdbool.h>
#include <linux/coresight-pmu.h>

#include "../../util/auxtrace.h"
#include "../../util/evlist.h"
#include "../../util/pmu.h"
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
*auxtrace_record__init(struct perf_evlist *evlist, int *err)
{
	struct perf_pmu	*cs_etm_pmu;
	struct perf_evsel *evsel;
	bool found_etm = false;
	bool found_spe = false;
	static struct perf_pmu **arm_spe_pmus = NULL;
	static int nr_spes = 0;
	int i;

	if (!evlist)
		return NULL;

	cs_etm_pmu = perf_pmu__find(CORESIGHT_ETM_PMU_NAME);

	if (!arm_spe_pmus)
		arm_spe_pmus = find_all_arm_spe_pmus(&nr_spes, err);

	evlist__for_each_entry(evlist, evsel) {
		if (cs_etm_pmu &&
		    evsel->attr.type == cs_etm_pmu->type)
			found_etm = true;

		if (!nr_spes)
			continue;

		for (i = 0; i < nr_spes; i++) {
			if (evsel->attr.type == arm_spe_pmus[i]->type) {
				found_spe = true;
				break;
			}
		}
	}

	if (found_etm && found_spe) {
		pr_err("Concurrent ARM Coresight ETM and SPE operation not currently supported\n");
		*err = -EOPNOTSUPP;
		return NULL;
	}

	if (found_etm)
		return cs_etm_record_init(err);

#if defined(__aarch64__)
	if (found_spe)
		return arm_spe_recording_init(err, arm_spe_pmus[i]);
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
