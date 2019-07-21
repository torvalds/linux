// SPDX-License-Identifier: GPL-2.0
#include <perf/evlist.h>
#include <linux/list.h>
#include <internal/evlist.h>

void perf_evlist__init(struct perf_evlist *evlist)
{
	INIT_LIST_HEAD(&evlist->entries);
}
