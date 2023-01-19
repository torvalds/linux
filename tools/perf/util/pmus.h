/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PMUS_H
#define __PMUS_H

extern struct list_head pmus;

#define perf_pmus__for_each_pmu(pmu) list_for_each_entry(pmu, &pmus, list)

#endif /* __PMUS_H */
