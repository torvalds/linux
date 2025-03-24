/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _TOPDOWN_H
#define _TOPDOWN_H 1

bool topdown_sys_has_perf_metrics(void);
bool arch_is_topdown_slots(const struct evsel *evsel);
bool arch_is_topdown_metrics(const struct evsel *evsel);

#endif
