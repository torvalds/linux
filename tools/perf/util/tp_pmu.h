/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */
#ifndef __TP_PMU_H
#define __TP_PMU_H

#include "pmu.h"

typedef int (*tp_sys_callback)(void *state, const char *sys_name);
typedef int (*tp_event_callback)(void *state, const char *sys_name, const char *evt_name);

int tp_pmu__id(const char *sys, const char *name);
int tp_pmu__for_each_tp_event(const char *sys, void *state, tp_event_callback cb);
int tp_pmu__for_each_tp_sys(void *state, tp_sys_callback cb);

bool perf_pmu__is_tracepoint(const struct perf_pmu *pmu);
int tp_pmu__for_each_event(struct perf_pmu *pmu, void *state, pmu_event_callback cb);
size_t tp_pmu__num_events(struct perf_pmu *pmu);
bool tp_pmu__have_event(struct perf_pmu *pmu, const char *name);

#endif /* __TP_PMU_H */
