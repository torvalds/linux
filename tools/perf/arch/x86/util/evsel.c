// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include "util/evlist.h"
#include "util/evsel.h"
#include "util/evsel_config.h"
#include "util/env.h"
#include "util/pmu.h"
#include "util/pmus.h"
#include "util/stat.h"
#include "util/strbuf.h"
#include "linux/string.h"
#include "topdown.h"
#include "evsel.h"
#include "util/debug.h"
#include "env.h"

#define IBS_FETCH_L3MISSONLY   (1ULL << 59)
#define IBS_OP_L3MISSONLY      (1ULL << 16)

void arch_evsel__set_sample_weight(struct evsel *evsel)
{
	evsel__set_sample_bit(evsel, WEIGHT_STRUCT);
}

/* Check whether the evsel's PMU supports the perf metrics */
bool evsel__sys_has_perf_metrics(const struct evsel *evsel)
{
	struct perf_pmu *pmu;

	if (!topdown_sys_has_perf_metrics())
		return false;

	/*
	 * The PERF_TYPE_RAW type is the core PMU type, e.g., "cpu" PMU on a
	 * non-hybrid machine, "cpu_core" PMU on a hybrid machine.  The
	 * topdown_sys_has_perf_metrics checks the slots event is only available
	 * for the core PMU, which supports the perf metrics feature. Checking
	 * both the PERF_TYPE_RAW type and the slots event should be good enough
	 * to detect the perf metrics feature.
	 */
	pmu = evsel__find_pmu(evsel);
	return pmu && pmu->type == PERF_TYPE_RAW;
}

bool arch_evsel__must_be_in_group(const struct evsel *evsel)
{
	if (!evsel__sys_has_perf_metrics(evsel))
		return false;

	return arch_is_topdown_metrics(evsel) || arch_is_topdown_slots(evsel);
}

int arch_evsel__hw_name(struct evsel *evsel, char *bf, size_t size)
{
	u64 event = evsel->core.attr.config & PERF_HW_EVENT_MASK;
	u64 pmu = evsel->core.attr.config >> PERF_PMU_TYPE_SHIFT;
	const char *event_name;

	if (event < PERF_COUNT_HW_MAX && evsel__hw_names[event])
		event_name = evsel__hw_names[event];
	else
		event_name = "unknown-hardware";

	/* The PMU type is not required for the non-hybrid platform. */
	if (!pmu)
		return  scnprintf(bf, size, "%s", event_name);

	return scnprintf(bf, size, "%s/%s/",
			 evsel->pmu ? evsel->pmu->name : "cpu",
			 event_name);
}

void arch_evsel__apply_ratio_to_prev(struct evsel *evsel,
				struct perf_event_attr *attr)
{
	struct perf_event_attr *prev_attr = NULL;
	struct evsel *evsel_prev = NULL;
	const char *name = "acr_mask";
	int evsel_idx = 0;
	__u64 ev_mask, pr_ev_mask;

	if (!perf_pmu__has_format(evsel->pmu, name)) {
		pr_err("'%s' does not have acr_mask format support\n", evsel->pmu->name);
		return;
	}
	if (perf_pmu__format_type(evsel->pmu, name) !=
			PERF_PMU_FORMAT_VALUE_CONFIG2) {
		pr_err("'%s' does not have config2 format support\n", evsel->pmu->name);
		return;
	}

	evsel_prev = evsel__prev(evsel);
	if (!evsel_prev) {
		pr_err("Previous event does not exist.\n");
		return;
	}

	prev_attr = &evsel_prev->core.attr;

	if (prev_attr->config2) {
		pr_err("'%s' has set config2 (acr_mask?) already, configuration not supported\n", evsel_prev->name);
		return;
	}

	/*
	 * acr_mask (config2) is calculated using the event's index in
	 * the event group. The first event will use the index of the
	 * second event as its mask (e.g., 0x2), indicating that the
	 * second event counter will be reset and a sample taken for
	 * the first event if its counter overflows. The second event
	 * will use the mask consisting of the first and second bits
	 * (e.g., 0x3), meaning both counters will be reset if the
	 * second event counter overflows.
	 */

	evsel_idx = evsel__group_idx(evsel);
	ev_mask = 1ull << evsel_idx;
	pr_ev_mask = 1ull << (evsel_idx - 1);

	prev_attr->config2 = ev_mask;
	attr->config2 = ev_mask | pr_ev_mask;
}

static void ibs_l3miss_warn(void)
{
	pr_warning(
"WARNING: Hw internally resets sampling period when L3 Miss Filtering is enabled\n"
"and tagged operation does not cause L3 Miss. This causes sampling period skew.\n");
}

void arch__post_evsel_config(struct evsel *evsel, struct perf_event_attr *attr)
{
	struct perf_pmu *evsel_pmu, *ibs_fetch_pmu, *ibs_op_pmu;
	static int warned_once;

	if (warned_once || !x86__is_amd_cpu())
		return;

	evsel_pmu = evsel__find_pmu(evsel);
	if (!evsel_pmu)
		return;

	ibs_fetch_pmu = perf_pmus__find("ibs_fetch");
	ibs_op_pmu = perf_pmus__find("ibs_op");

	if (ibs_fetch_pmu && ibs_fetch_pmu->type == evsel_pmu->type) {
		if (attr->config & IBS_FETCH_L3MISSONLY) {
			ibs_l3miss_warn();
			warned_once = 1;
		}
	} else if (ibs_op_pmu && ibs_op_pmu->type == evsel_pmu->type) {
		if (attr->config & IBS_OP_L3MISSONLY) {
			ibs_l3miss_warn();
			warned_once = 1;
		}
	}
}

static int amd_evsel__open_strerror(struct evsel *evsel, char *msg, size_t size)
{
	struct perf_pmu *pmu;

	if (evsel->core.attr.precise_ip == 0)
		return 0;

	pmu = evsel__find_pmu(evsel);
	if (!pmu || strncmp(pmu->name, "ibs", 3))
		return 0;

	/* More verbose IBS errors. */
	if (evsel->core.attr.exclude_kernel || evsel->core.attr.exclude_user ||
	    evsel->core.attr.exclude_hv || evsel->core.attr.exclude_idle ||
	    evsel->core.attr.exclude_host || evsel->core.attr.exclude_guest) {
		return scnprintf(msg, size, "AMD IBS doesn't support privilege filtering. Try "
				 "again without the privilege modifiers (like 'k') at the end.");
	}
	return 0;
}

static int intel_evsel__open_strerror(struct evsel *evsel, int err, char *msg, size_t size)
{
	struct strbuf sb = STRBUF_INIT;
	int ret;

	if (err != EINVAL)
		return 0;

	if (!topdown_sys_has_perf_metrics())
		return 0;

	if (arch_is_topdown_slots(evsel)) {
		if (!evsel__is_group_leader(evsel)) {
			evlist__uniquify_evsel_names(evsel->evlist, &stat_config);
			evlist__format_evsels(evsel->evlist, &sb, 2048);
			ret = scnprintf(msg, size, "Topdown slots event can only be group leader "
					"in '%s'.", sb.buf);
			strbuf_release(&sb);
			return ret;
		}
	} else if (arch_is_topdown_metrics(evsel)) {
		struct evsel *pos;

		evlist__for_each_entry(evsel->evlist, pos) {
			if (pos == evsel || !arch_is_topdown_metrics(pos))
				continue;

			if (pos->core.attr.config != evsel->core.attr.config)
				continue;

			evlist__uniquify_evsel_names(evsel->evlist, &stat_config);
			evlist__format_evsels(evsel->evlist, &sb, 2048);
			ret = scnprintf(msg, size, "Perf metric event '%s' is duplicated "
					"in the same group (only one event is allowed) in '%s'.",
					evsel__name(evsel), sb.buf);
			strbuf_release(&sb);
			return ret;
		}
	}
	return 0;
}

int arch_evsel__open_strerror(struct evsel *evsel, int err, char *msg, size_t size)
{
	return x86__is_amd_cpu()
		? amd_evsel__open_strerror(evsel, msg, size)
		: intel_evsel__open_strerror(evsel, err, msg, size);
}
