/*
 * drv_configs.h: Interface to apply PMU specific configuration
 * Copyright (c) 2016-2018, Linaro Ltd.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#include "drv_configs.h"
#include "evlist.h"
#include "evsel.h"
#include "pmu.h"
#include <errno.h>

static int
perf_evsel__apply_drv_configs(struct perf_evsel *evsel,
			      struct perf_evsel_config_term **err_term)
{
	bool found = false;
	int err = 0;
	struct perf_evsel_config_term *term;
	struct perf_pmu *pmu = NULL;

	while ((pmu = perf_pmu__scan(pmu)) != NULL)
		if (pmu->type == evsel->attr.type) {
			found = true;
			break;
		}

	list_for_each_entry(term, &evsel->config_terms, list) {
		if (term->type != PERF_EVSEL__CONFIG_TERM_DRV_CFG)
			continue;

		/*
		 * We have a configuration term, report an error if we
		 * can't find the PMU or if the PMU driver doesn't support
		 * cmd line driver configuration.
		 */
		if (!found || !pmu->set_drv_config) {
			err = -EINVAL;
			*err_term = term;
			break;
		}

		err = pmu->set_drv_config(term);
		if (err) {
			*err_term = term;
			break;
		}
	}

	return err;
}

int perf_evlist__apply_drv_configs(struct perf_evlist *evlist,
				   struct perf_evsel **err_evsel,
				   struct perf_evsel_config_term **err_term)
{
	struct perf_evsel *evsel;
	int err = 0;

	evlist__for_each_entry(evlist, evsel) {
		err = perf_evsel__apply_drv_configs(evsel, err_term);
		if (err) {
			*err_evsel = evsel;
			break;
		}
	}

	return err;
}
