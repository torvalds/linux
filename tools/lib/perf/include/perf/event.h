/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LIBPERF_EVENT_H
#define __LIBPERF_EVENT_H

#include <linux/perf_event.h>
#include <linux/types.h>
#include <linux/limits.h>
#include <linux/bpf.h>
#include <linux/compiler.h>
#include <sys/types.h> /* pid_t */

#define event_contains(obj, mem) ((obj).header.size > offsetof(typeof(obj), mem))

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
	union {
		struct {
			__u32	 maj;
			__u32	 min;
			__u64	 ino;
			__u64	 ino_generation;
		};
		struct {
			__u8	 build_id_size;
			__u8	 __reserved_1;
			__u16	 __reserved_2;
			__u8	 build_id[20];
		};
	};
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
 * PERF_FORMAT_ENABLED | PERF_FORMAT_RUNNING | PERF_FORMAT_ID | PERF_FORMAT_LOST
 */
struct perf_record_read {
	struct perf_event_header header;
	__u32			 pid, tid;
	__u64			 value;
	__u64			 time_enabled;
	__u64			 time_running;
	__u64			 id;
	__u64			 lost;
};

struct perf_record_throttle {
	struct perf_event_header header;
	__u64			 time;
	__u64			 id;
	__u64			 stream_id;
};

#ifndef KSYM_NAME_LEN
#define KSYM_NAME_LEN 512
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

struct perf_record_text_poke_event {
	struct perf_event_header header;
	__u64			addr;
	__u16			old_len;
	__u16			new_len;
	__u8			bytes[];
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
	PERF_CPU_MAP__RANGE_CPUS = 2,
};

/*
 * Array encoding of a perf_cpu_map where nr is the number of entries in cpu[]
 * and each entry is a value for a CPU in the map.
 */
struct cpu_map_entries {
	__u16			 nr;
	__u16			 cpu[];
};

/* Bitmap encoding of a perf_cpu_map where bitmap entries are 32-bit. */
struct perf_record_mask_cpu_map32 {
	/* Number of mask values. */
	__u16			 nr;
	/* Constant 4. */
	__u16			 long_size;
	/* Bitmap data. */
	__u32			 mask[];
};

/* Bitmap encoding of a perf_cpu_map where bitmap entries are 64-bit. */
struct perf_record_mask_cpu_map64 {
	/* Number of mask values. */
	__u16			 nr;
	/* Constant 8. */
	__u16			 long_size;
	/* Legacy padding. */
	char                     __pad[4];
	/* Bitmap data. */
	__u64			 mask[];
};

/*
 * 'struct perf_record_cpu_map_data' is packed as unfortunately an earlier
 * version had unaligned data and we wish to retain file format compatibility.
 * -irogers
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpacked"
#pragma GCC diagnostic ignored "-Wattributes"

/*
 * An encoding of a CPU map for a range starting at start_cpu through to
 * end_cpu. If any_cpu is 1, an any CPU (-1) value (aka dummy value) is present.
 */
struct perf_record_range_cpu_map {
	__u8 any_cpu;
	__u8 __pad;
	__u16 start_cpu;
	__u16 end_cpu;
};

struct __packed perf_record_cpu_map_data {
	__u16			 type;
	union {
		/* Used when type == PERF_CPU_MAP__CPUS. */
		struct cpu_map_entries cpus_data;
		/* Used when type == PERF_CPU_MAP__MASK and long_size == 4. */
		struct perf_record_mask_cpu_map32 mask32_data;
		/* Used when type == PERF_CPU_MAP__MASK and long_size == 8. */
		struct perf_record_mask_cpu_map64 mask64_data;
		/* Used when type == PERF_CPU_MAP__RANGE_CPUS. */
		struct perf_record_range_cpu_map range_cpu_data;
	};
};

#pragma GCC diagnostic pop

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
	union {
		/* Used when type == PERF_EVENT_UPDATE__SCALE. */
		struct perf_record_event_update_scale scale;
		/* Used when type == PERF_EVENT_UPDATE__UNIT. */
		char unit[0];
		/* Used when type == PERF_EVENT_UPDATE__NAME. */
		char name[0];
		/* Used when type == PERF_EVENT_UPDATE__CPUS. */
		struct perf_record_event_update_cpus cpus;
	};
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

#define PERF_RECORD_MISC_BUILD_ID_SIZE (1 << 15)

struct perf_record_header_build_id {
	struct perf_event_header header;
	pid_t			 pid;
	union {
		__u8		 build_id[24];
		struct {
			__u8	 data[20];
			__u8	 size;
			__u8	 reserved1__;
			__u16	 reserved2__;
		};
	};
	char			 filename[];
};

struct id_index_entry {
	__u64			 id;
	__u64			 idx;
	__u64			 cpu;
	__u64			 tid;
};

struct id_index_entry_2 {
	__u64			 machine_pid;
	__u64			 vcpu;
};

struct perf_record_id_index {
	struct perf_event_header header;
	__u64			 nr;
	struct id_index_entry	 entries[];
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
	__u32			 machine_pid;
	__u32			 vcpu;
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

struct perf_record_aux_output_hw_id {
	struct perf_event_header header;
	__u64			hw_id;
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
	__u64			 time_cycles;
	__u64			 time_mask;
	__u8			 cap_user_time_zero;
	__u8			 cap_user_time_short;
	__u8			 reserved[6];	/* For alignment */
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
	PERF_RECORD_FINISHED_INIT		= 82,
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
	struct perf_record_text_poke_event	text_poke;
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
	struct perf_record_aux_output_hw_id	aux_output_hw_id;
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
