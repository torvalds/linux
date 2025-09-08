/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _TOPDOWN_H
#define _TOPDOWN_H 1

#include <stdbool.h>

struct evsel;
struct list_head;

bool topdown_sys_has_perf_metrics(void);
bool arch_is_topdown_slots(const struct evsel *evsel);
bool arch_is_topdown_metrics(const struct evsel *evsel);
int topdown_insert_slots_event(struct list_head *list, int idx, struct evsel *metric_event);

#endif
