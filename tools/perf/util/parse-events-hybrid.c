// SPDX-License-Identifier: GPL-2.0
#include <linux/err.h>
#include <linux/zalloc.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/param.h>
#include "evlist.h"
#include "evsel.h"
#include "parse-events.h"
#include "parse-events-hybrid.h"
#include "debug.h"
#include "pmu.h"
#include "pmu-hybrid.h"
#include "perf.h"

static void config_hybrid_attr(struct perf_event_attr *attr,
			       int type, int pmu_type)
{
	/*
	 * attr.config layout for type PERF_TYPE_HARDWARE and
	 * PERF_TYPE_HW_CACHE
	 *
	 * PERF_TYPE_HARDWARE:                 0xEEEEEEEE000000AA
	 *                                     AA: hardware event ID
	 *                                     EEEEEEEE: PMU type ID
	 * PERF_TYPE_HW_CACHE:                 0xEEEEEEEE00DDCCBB
	 *                                     BB: hardware cache ID
	 *                                     CC: hardware cache op ID
	 *                                     DD: hardware cache op result ID
	 *                                     EEEEEEEE: PMU type ID
	 * If the PMU type ID is 0, the PERF_TYPE_RAW will be applied.
	 */
	attr->type = type;
	attr->config = attr->config | ((__u64)pmu_type << PERF_PMU_TYPE_SHIFT);
}

static int create_event_hybrid(__u32 config_type, int *idx,
			       struct list_head *list,
			       struct perf_event_attr *attr, const char *name,
			       const char *metric_id,
			       struct list_head *config_terms,
			       struct perf_pmu *pmu)
{
	struct evsel *evsel;
	__u32 type = attr->type;
	__u64 config = attr->config;

	config_hybrid_attr(attr, config_type, pmu->type);
	evsel = parse_events__add_event_hybrid(list, idx, attr, name, metric_id,
					       pmu, config_terms);
	if (evsel)
		evsel->pmu_name = strdup(pmu->name);
	else
		return -ENOMEM;

	attr->type = type;
	attr->config = config;
	return 0;
}

static int pmu_cmp(struct parse_events_state *parse_state,
		   struct perf_pmu *pmu)
{
	if (parse_state->evlist && parse_state->evlist->hybrid_pmu_name)
		return strcmp(parse_state->evlist->hybrid_pmu_name, pmu->name);

	if (parse_state->hybrid_pmu_name)
		return strcmp(parse_state->hybrid_pmu_name, pmu->name);

	return 0;
}

static int add_hw_hybrid(struct parse_events_state *parse_state,
			 struct list_head *list, struct perf_event_attr *attr,
			 const char *name, const char *metric_id,
			 struct list_head *config_terms)
{
	struct perf_pmu *pmu;
	int ret;

	perf_pmu__for_each_hybrid_pmu(pmu) {
		LIST_HEAD(terms);

		if (pmu_cmp(parse_state, pmu))
			continue;

		copy_config_terms(&terms, config_terms);
		ret = create_event_hybrid(PERF_TYPE_HARDWARE,
					  &parse_state->idx, list, attr, name,
					  metric_id, &terms, pmu);
		free_config_terms(&terms);
		if (ret)
			return ret;
	}

	return 0;
}

static int create_raw_event_hybrid(int *idx, struct list_head *list,
				   struct perf_event_attr *attr,
				   const char *name,
				   const char *metric_id,
				   struct list_head *config_terms,
				   struct perf_pmu *pmu)
{
	struct evsel *evsel;

	attr->type = pmu->type;
	evsel = parse_events__add_event_hybrid(list, idx, attr, name, metric_id,
					       pmu, config_terms);
	if (evsel)
		evsel->pmu_name = strdup(pmu->name);
	else
		return -ENOMEM;

	return 0;
}

static int add_raw_hybrid(struct parse_events_state *parse_state,
			  struct list_head *list, struct perf_event_attr *attr,
			  const char *name, const char *metric_id,
			  struct list_head *config_terms)
{
	struct perf_pmu *pmu;
	int ret;

	perf_pmu__for_each_hybrid_pmu(pmu) {
		LIST_HEAD(terms);

		if (pmu_cmp(parse_state, pmu))
			continue;

		copy_config_terms(&terms, config_terms);
		ret = create_raw_event_hybrid(&parse_state->idx, list, attr,
					      name, metric_id, &terms, pmu);
		free_config_terms(&terms);
		if (ret)
			return ret;
	}

	return 0;
}

int parse_events__add_numeric_hybrid(struct parse_events_state *parse_state,
				     struct list_head *list,
				     struct perf_event_attr *attr,
				     const char *name, const char *metric_id,
				     struct list_head *config_terms,
				     bool *hybrid)
{
	*hybrid = false;
	if (attr->type == PERF_TYPE_SOFTWARE)
		return 0;

	if (!perf_pmu__has_hybrid())
		return 0;

	*hybrid = true;
	if (attr->type != PERF_TYPE_RAW) {
		return add_hw_hybrid(parse_state, list, attr, name, metric_id,
				     config_terms);
	}

	return add_raw_hybrid(parse_state, list, attr, name, metric_id,
			      config_terms);
}

int parse_events__add_cache_hybrid(struct list_head *list, int *idx,
				   struct perf_event_attr *attr,
				   const char *name,
				   const char *metric_id,
				   struct list_head *config_terms,
				   bool *hybrid,
				   struct parse_events_state *parse_state)
{
	struct perf_pmu *pmu;
	int ret;

	*hybrid = false;
	if (!perf_pmu__has_hybrid())
		return 0;

	*hybrid = true;
	perf_pmu__for_each_hybrid_pmu(pmu) {
		LIST_HEAD(terms);

		if (pmu_cmp(parse_state, pmu))
			continue;

		copy_config_terms(&terms, config_terms);
		ret = create_event_hybrid(PERF_TYPE_HW_CACHE, idx, list,
					  attr, name, metric_id, &terms, pmu);
		free_config_terms(&terms);
		if (ret)
			return ret;
	}

	return 0;
}
