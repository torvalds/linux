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

#ifndef __PERF_DRV_CONFIGS_H
#define __PERF_DRV_CONFIGS_H

#include "drv_configs.h"
#include "evlist.h"
#include "evsel.h"

int perf_evlist__apply_drv_configs(struct perf_evlist *evlist,
				   struct perf_evsel **err_evsel,
				   struct perf_evsel_config_term **term);
#endif
