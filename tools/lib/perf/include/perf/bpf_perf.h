/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */
#ifndef __LIBPERF_BPF_PERF_H
#define __LIBPERF_BPF_PERF_H

#include <linux/types.h>  /* for __u32 */

/*
 * bpf_perf uses a hashmap, the attr_map, to track all the leader programs.
 * The hashmap is pinned in bpffs. flock() on this file is used to ensure
 * no concurrent access to the attr_map.  The key of attr_map is struct
 * perf_event_attr, and the value is struct perf_event_attr_map_entry.
 *
 * struct perf_event_attr_map_entry contains two __u32 IDs, bpf_link of the
 * leader prog, and the diff_map. Each perf-stat session holds a reference
 * to the bpf_link to make sure the leader prog is attached to sched_switch
 * tracepoint.
 *
 * Since the hashmap only contains IDs of the bpf_link and diff_map, it
 * does not hold any references to the leader program. Once all perf-stat
 * sessions of these events exit, the leader prog, its maps, and the
 * perf_events will be freed.
 */
struct perf_event_attr_map_entry {
	__u32 link_id;
	__u32 diff_map_id;
};

/* default attr_map name */
#define BPF_PERF_DEFAULT_ATTR_MAP_PATH "perf_attr_map"

#endif /* __LIBPERF_BPF_PERF_H */
