/* SPDX-License-Identifier: GPL-2.0 */
#ifndef TOPDOWN_H
#define TOPDOWN_H 1
#include "evsel.h"
#include "evlist.h"

bool arch_topdown_check_group(bool *warn);
void arch_topdown_group_warn(void);
bool arch_topdown_sample_read(struct evsel *leader);
const char *arch_get_topdown_pmu_name(struct evlist *evlist, bool warn);
int topdown_filter_events(const char **attr, char **str, bool use_group,
			  const char *pmu_name);

#endif
