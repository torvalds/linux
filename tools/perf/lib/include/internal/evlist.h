/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LIBPERF_INTERNAL_EVLIST_H
#define __LIBPERF_INTERNAL_EVLIST_H

struct perf_evlist {
	struct list_head	entries;
	int			nr_entries;
};

#endif /* __LIBPERF_INTERNAL_EVLIST_H */
