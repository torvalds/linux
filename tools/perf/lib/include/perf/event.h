/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LIBPERF_EVENT_H
#define __LIBPERF_EVENT_H

#include <linux/perf_event.h>
#include <linux/types.h>
#include <linux/limits.h>
#include <linux/bpf.h>
#include <sys/types.h> /* pid_t */

struct perf_record_mmap {
	struct perf_event_header header;
	__u32			 pid, tid;
	__u64			 start;
	__u64			 len;
	__u64			 pgoff;
	char			 filename[PATH_MAX];
};

struct perf_record_mmap2 {
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

struct perf_record_comm {
	struct perf_event_header header;
	__u32			 pid, tid;
	char			 comm[16];
};

struct perf_record_namespaces {
	struct perf_event_header header;
	__u32			 pid, tid;
	__u64			 nr_namespaces;
	struct perf_ns_link_info link_info[];
};

struct perf_record_fork {
	struct perf_event_header header;
	__u32			 pid, ppid;
	__u32			 tid, ptid;
	__u64			 time;
};

struct perf_record_lost {
	struct perf_event_header header;
	__u64			 id;
	__u64			 lost;
};

struct perf_record_lost_samples {
	struct perf_event_header header;
	__u64			 lost;
};

/*
 * PERF_FORMAT_ENABLED | PERF_FORMAT_RUNNING | PERF_FORMAT_ID
 */
struct perf_record_read {
	struct perf_event_header header;
	__u32			 pid, tid;
	__u64			 value;
	__u64			 time_enabled;
	__u64			 time_running;
	__u64			 id;
};

struct perf_record_throttle {
	struct perf_event_header header;
	__u64			 time;
	__u64			 id;
	__u64			 stream_id;
};

#ifndef KSYM_NAME_LEN
#define KSYM_NAME_LEN 256
#endif

struct perf_record_ksymbol {
	struct perf_event_header header;
	__u64			 addr;
	__u32			 len;
	__u16			 ksym_type;
	__u16			 flags;
	char			 name[KSYM_NAME_LEN];
};

struct perf_record_bpf_event {
	struct perf_event_header header;
	__u16			 type;
	__u16			 flags;
	__u32			 id;

	/* for bpf_prog types */
	__u8			 tag[BPF_TAG_SIZE];  // prog tag
};

struct perf_record_sample {
	struct perf_event_header header;
	__u64			 array[];
};

struct attr_event {
	struct perf_event_header header;
	struct perf_event_attr	 attr;
	__u64			 id[];
};

enum {
	PERF_CPU_MAP__CPUS = 0,
	PERF_CPU_MAP__MASK = 1,
};

struct cpu_map_entries {
	__u16			 nr;
	__u16			 cpu[];
};

struct cpu_map_mask {
	__u16			 nr;
	__u16			 long_size;
	unsigned long		 mask[];
};

struct cpu_map_data {
	__u16			 type;
	char			 data[];
};

struct cpu_map_event {
	struct perf_event_header header;
	struct cpu_map_data	 data;
};

enum {
	PERF_EVENT_UPDATE__UNIT  = 0,
	PERF_EVENT_UPDATE__SCALE = 1,
	PERF_EVENT_UPDATE__NAME  = 2,
	PERF_EVENT_UPDATE__CPUS  = 3,
};

struct event_update_event_cpus {
	struct cpu_map_data	 cpus;
};

struct event_update_event_scale {
	double			 scale;
};

struct event_update_event {
	struct perf_event_header header;
	__u64			 type;
	__u64			 id;
	char			 data[];
};

#define MAX_EVENT_NAME 64

struct perf_trace_event_type {
	__u64			 event_id;
	char			 name[MAX_EVENT_NAME];
};

struct event_type_event {
	struct perf_event_header	 header;
	struct perf_trace_event_type	 event_type;
};

struct tracing_data_event {
	struct perf_event_header header;
	__u32			 size;
};

struct build_id_event {
	struct perf_event_header header;
	pid_t			 pid;
	__u8			 build_id[24];
	char			 filename[];
};

struct id_index_entry {
	__u64			 id;
	__u64			 idx;
	__u64			 cpu;
	__u64			 tid;
};

struct id_index_event {
	struct perf_event_header header;
	__u64			 nr;
	struct id_index_entry	 entries[0];
};

#endif /* __LIBPERF_EVENT_H */
