/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PMUS_H
#define __PMUS_H

struct perf_pmu;
struct print_callbacks;

int pmu_name_len_no_suffix(const char *str, unsigned long *num);

void perf_pmus__destroy(void);

struct perf_pmu *perf_pmus__find(const char *name);
struct perf_pmu *perf_pmus__find_by_type(unsigned int type);

struct perf_pmu *perf_pmus__scan(struct perf_pmu *pmu);
struct perf_pmu *perf_pmus__scan_core(struct perf_pmu *pmu);

const struct perf_pmu *perf_pmus__pmu_for_pmu_filter(const char *str);

void perf_pmus__print_pmu_events(const struct print_callbacks *print_cb, void *print_state);
bool perf_pmus__have_event(const char *pname, const char *name);
int perf_pmus__num_core_pmus(void);
bool perf_pmus__supports_extended_type(void);
char *perf_pmus__default_pmu_name(void);

#endif /* __PMUS_H */
