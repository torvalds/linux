/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PMU_HYBRID_H
#define __PMU_HYBRID_H

#include <linux/perf_event.h>
#include <linux/compiler.h>
#include <linux/list.h>
#include <stdbool.h>
#include "pmu.h"

extern struct list_head perf_pmu__hybrid_pmus;

#define perf_pmu__for_each_hybrid_pmu(pmu)	\
	list_for_each_entry(pmu, &perf_pmu__hybrid_pmus, hybrid_list)

bool perf_pmu__hybrid_mounted(const char *name);

struct perf_pmu *perf_pmu__find_hybrid_pmu(const char *name);
bool perf_pmu__is_hybrid(const char *name);
char *perf_pmu__hybrid_type_to_pmu(const char *type);

#endif /* __PMU_HYBRID_H */
