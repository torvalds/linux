/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright(C) 2015 Linaro Limited. All rights reserved.
 * Author: Mathieu Poirier <mathieu.poirier@linaro.org>
 */

#ifndef INCLUDE__PERF_CS_ETM_H__
#define INCLUDE__PERF_CS_ETM_H__

#include "../../util/evsel.h"

struct auxtrace_record *cs_etm_record_init(int *err);
int cs_etm_set_drv_config(struct perf_evsel_config_term *term);

#endif
