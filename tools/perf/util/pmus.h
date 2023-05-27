/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PMUS_H
#define __PMUS_H

struct perf_pmu;
struct print_callbacks;

void perf_pmus__destroy(void);

struct perf_pmu *perf_pmus__find(const char *name);
struct perf_pmu *perf_pmus__find_by_type(unsigned int type);

struct perf_pmu *perf_pmus__scan(struct perf_pmu *pmu);
struct perf_pmu *perf_pmus__scan_core(struct perf_pmu *pmu);

const struct perf_pmu *perf_pmus__pmu_for_pmu_filter(const char *str);

int perf_pmus__num_mem_pmus(void);
void perf_pmus__print_pmu_events(const struct print_callbacks *print_cb, void *print_state);
bool perf_pmus__have_event(const char *pname, const char *name);
bool perf_pmus__has_hybrid(void);

#endif /* __PMUS_H */
