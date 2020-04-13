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

struct perf_record_cgroup {
	struct perf_event_header header;
	__u64			 id;
	char			 path[PATH_MAX];
};

struct perf_record_sample {
	struct perf_event_header header;
	__u64			 array[];
};

struct perf_record_switch {
	struct perf_event_header header;
	__u32			 next_prev_pid;
	__u32			 next_prev_tid;
};

struct perf_record_header_attr {
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

struct perf_record_record_cpu_map {
	__u16			 nr;
	__u16			 long_size;
	unsigned long		 mask[];
};

struct perf_record_cpu_map_data {
	__u16			 type;
	char			 data[];
};

struct perf_record_cpu_map {
	struct perf_event_header	 header;
	struct perf_record_cpu_map_data	 data;
};

enum {
	PERF_EVENT_UPDATE__UNIT  = 0,
	PERF_EVENT_UPDATE__SCALE = 1,
	PERF_EVENT_UPDATE__NAME  = 2,
	PERF_EVENT_UPDATE__CPUS  = 3,
};

struct perf_record_event_update_cpus {
	struct perf_record_cpu_map_data	 cpus;
};

struct perf_record_event_update_scale {
	double			 scale;
};

struct perf_record_event_update {
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

struct perf_record_header_event_type {
	struct perf_event_header	 header;
	struct perf_trace_event_type	 event_type;
};

struct perf_record_header_tracing_data {
	struct perf_event_header header;
	__u32			 size;
};

struct perf_record_header_build_id {
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

struct perf_record_id_index {
	struct perf_event_header header;
	__u64			 nr;
	struct id_index_entry	 entries[0];
};

struct perf_record_auxtrace_info {
	struct perf_event_header header;
	__u32			 type;
	__u32			 reserved__; /* For alignment */
	__u64			 priv[];
};

struct perf_record_auxtrace {
	struct perf_event_header header;
	__u64			 size;
	__u64			 offset;
	__u64			 reference;
	__u32			 idx;
	__u32			 tid;
	__u32			 cpu;
	__u32			 reserved__; /* For alignment */
};

#define MAX_AUXTRACE_ERROR_MSG 64

struct perf_record_auxtrace_error {
	struct perf_event_header header;
	__u32			 type;
	__u32			 code;
	__u32			 cpu;
	__u32			 pid;
	__u32			 tid;
	__u32			 fmt;
	__u64			 ip;
	__u64			 time;
	char			 msg[MAX_AUXTRACE_ERROR_MSG];
};

struct perf_record_aux {
	struct perf_event_header header;
	__u64			 aux_offset;
	__u64			 aux_size;
	__u64			 flags;
};

struct perf_record_itrace_start {
	struct perf_event_header header;
	__u32			 pid;
	__u32			 tid;
};

struct perf_record_thread_map_entry {
	__u64			 pid;
	char			 comm[16];
};

struct perf_record_thread_map {
	struct perf_event_header		 header;
	__u64					 nr;
	struct perf_record_thread_map_entry	 entries[];
};

enum {
	PERF_STAT_CONFIG_TERM__AGGR_MODE	= 0,
	PERF_STAT_CONFIG_TERM__INTERVAL		= 1,
	PERF_STAT_CONFIG_TERM__SCALE		= 2,
	PERF_STAT_CONFIG_TERM__MAX		= 3,
};

struct perf_record_stat_config_entry {
	__u64			 tag;
	__u64			 val;
};

struct perf_record_stat_config {
	struct perf_event_header		 header;
	__u64					 nr;
	struct perf_record_stat_config_entry	 data[];
};

struct perf_record_stat {
	struct perf_event_header header;

	__u64			 id;
	__u32			 cpu;
	__u32			 thread;

	union {
		struct {
			__u64	 val;
			__u64	 ena;
			__u64	 run;
		};
		__u64		 values[3];
	};
};

struct perf_record_stat_round {
	struct perf_event_header header;
	__u64			 type;
	__u64			 time;
};

struct perf_record_time_conv {
	struct perf_event_header header;
	__u64			 time_shift;
	__u64			 time_mult;
	__u64			 time_zero;
};

struct perf_record_header_feature {
	struct perf_event_header header;
	__u64			 feat_id;
	char			 data[];
};

struct perf_record_compressed {
	struct perf_event_header header;
	char			 data[];
};

enum perf_user_event_type { /* above any possible kernel type */
	PERF_RECORD_USER_TYPE_START		= 64,
	PERF_RECORD_HEADER_ATTR			= 64,
	PERF_RECORD_HEADER_EVENT_TYPE		= 65, /* deprecated */
	PERF_RECORD_HEADER_TRACING_DATA		= 66,
	PERF_RECORD_HEADER_BUILD_ID		= 67,
	PERF_RECORD_FINISHED_ROUND		= 68,
	PERF_RECORD_ID_INDEX			= 69,
	PERF_RECORD_AUXTRACE_INFO		= 70,
	PERF_RECORD_AUXTRACE			= 71,
	PERF_RECORD_AUXTRACE_ERROR		= 72,
	PERF_RECORD_THREAD_MAP			= 73,
	PERF_RECORD_CPU_MAP			= 74,
	PERF_RECORD_STAT_CONFIG			= 75,
	PERF_RECORD_STAT			= 76,
	PERF_RECORD_STAT_ROUND			= 77,
	PERF_RECORD_EVENT_UPDATE		= 78,
	PERF_RECORD_TIME_CONV			= 79,
	PERF_RECORD_HEADER_FEATURE		= 80,
	PERF_RECORD_COMPRESSED			= 81,
	PERF_RECORD_HEADER_MAX
};

union perf_event {
	struct perf_event_header		header;
	struct perf_record_mmap			mmap;
	struct perf_record_mmap2		mmap2;
	struct perf_record_comm			comm;
	struct perf_record_namespaces		namespaces;
	struct perf_record_cgroup		cgroup;
	struct perf_record_fork			fork;
	struct perf_record_lost			lost;
	struct perf_record_lost_samples		lost_samples;
	struct perf_record_read			read;
	struct perf_record_throttle		throttle;
	struct perf_record_sample		sample;
	struct perf_record_bpf_event		bpf;
	struct perf_record_ksymbol		ksymbol;
	struct perf_record_header_attr		attr;
	struct perf_record_event_update		event_update;
	struct perf_record_header_event_type	event_type;
	struct perf_record_header_tracing_data	tracing_data;
	struct perf_record_header_build_id	build_id;
	struct perf_record_id_index		id_index;
	struct perf_record_auxtrace_info	auxtrace_info;
	struct perf_record_auxtrace		auxtrace;
	struct perf_record_auxtrace_error	auxtrace_error;
	struct perf_record_aux			aux;
	struct perf_record_itrace_start		itrace_start;
	struct perf_record_switch		context_switch;
	struct perf_record_thread_map		thread_map;
	struct perf_record_cpu_map		cpu_map;
	struct perf_record_stat_config		stat_config;
	struct perf_record_stat			stat;
	struct perf_record_stat_round		stat_round;
	struct perf_record_time_conv		time_conv;
	struct perf_record_header_feature	feat;
	struct perf_record_compressed		pack;
};

#endif /* __LIBPERF_EVENT_H */
