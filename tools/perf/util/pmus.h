/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PMUS_H
#define __PMUS_H

extern struct list_head pmus;
struct perf_pmu;

#define perf_pmus__for_each_pmu(pmu) list_for_each_entry(pmu, &pmus, list)

const struct perf_pmu *perf_pmus__pmu_for_pmu_filter(const char *str);

#endif /* __PMUS_H */
