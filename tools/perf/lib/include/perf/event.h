/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LIBPERF_EVENT_H
#define __LIBPERF_EVENT_H

#include <linux/perf_event.h>
#include <linux/types.h>
#include <linux/limits.h>

struct mmap_event {
	struct perf_event_header header;
	__u32			 pid, tid;
	__u64			 start;
	__u64			 len;
	__u64			 pgoff;
	char			 filename[PATH_MAX];
};

struct mmap2_event {
	struct perf_event_header header;
	__u32			 pid, tid;
	__u64			 start;
	__u64			 len;
	__u64			 pgoff;
	__u32			 maj;
	__u32			 min;
	__u64			 ino;
	__u64			 ino_generation;
	__u32			 prot;
	__u32			 flags;
	char			 filename[PATH_MAX];
};

struct comm_event {
	struct perf_event_header header;
	__u32			 pid, tid;
	char			 comm[16];
};

struct namespaces_event {
	struct perf_event_header header;
	__u32			 pid, tid;
	__u64			 nr_namespaces;
	struct perf_ns_link_info link_info[];
};

struct fork_event {
	struct perf_event_header header;
	__u32			 pid, ppid;
	__u32			 tid, ptid;
	__u64			 time;
};

struct lost_event {
	struct perf_event_header header;
	__u64			 id;
	__u64			 lost;
};

struct lost_samples_event {
	struct perf_event_header header;
	__u64			 lost;
};

/*
 * PERF_FORMAT_ENABLED | PERF_FORMAT_RUNNING | PERF_FORMAT_ID
 */
struct read_event {
	struct perf_event_header header;
	__u32			 pid, tid;
	__u64			 value;
	__u64			 time_enabled;
	__u64			 time_running;
	__u64			 id;
};

struct throttle_event {
	struct perf_event_header header;
	__u64			 time;
	__u64			 id;
	__u64			 stream_id;
};

#endif /* __LIBPERF_EVENT_H */
