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

#include <string.h>
#include <linux/coresight-pmu.h>
#include <linux/perf_event.h>

#include "../../util/pmu.h"

struct perf_event_attr
*perf_pmu__get_default_config(struct perf_pmu *pmu __maybe_unused)
{
#ifdef HAVE_AUXTRACE_SUPPORT
	if (!strcmp(pmu->name, CORESIGHT_ETM_PMU_NAME)) {
		/* add ETM default config here */
		pmu->selectable = true;
	}
#endif
	return NULL;
}
