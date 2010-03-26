#ifndef __PERF_RECORD_H
#define __PERF_RECORD_H

#include <limits.h>

#include "../perf.h"
#include "map.h"

/*
 * PERF_SAMPLE_IP | PERF_SAMPLE_TID | *
 */
struct ip_event {
	struct perf_event_header header;
	u64 ip;
	u32 pid, tid;
	unsigned char __more_data[];
};

struct mmap_event {
	struct perf_event_header header;
	u32 pid, tid;
	u64 start;
	u64 len;
	u64 pgoff;
	char filename[PATH_MAX];
};

struct comm_event {
	struct perf_event_header header;
	u32 pid, tid;
	char comm[16];
};

struct fork_event {
	struct perf_event_header header;
	u32 pid, ppid;
	u32 tid, ptid;
	u64 time;
};

struct lost_event {
	struct perf_event_header header;
	u64 id;
	u64 lost;
};

/*
 * PERF_FORMAT_ENABLED | PERF_FORMAT_RUNNING | PERF_FORMAT_ID
 */
struct read_event {
	struct perf_event_header header;
	u32 pid, tid;
	u64 value;
	u64 time_enabled;
	u64 time_running;
	u64 id;
};

struct sample_event {
	struct perf_event_header        header;
	u64 array[];
};

struct sample_data {
	u64 ip;
	u32 pid, tid;
	u64 time;
	u64 addr;
	u64 id;
	u64 stream_id;
	u32 cpu;
	u64 period;
	struct ip_callchain *callchain;
	u32 raw_size;
	void *raw_data;
};

#define BUILD_ID_SIZE 20

struct build_id_event {
	struct perf_event_header header;
	u8			 build_id[ALIGN(BUILD_ID_SIZE, sizeof(u64))];
	char			 filename[];
};

typedef union event_union {
	struct perf_event_header	header;
	struct ip_event			ip;
	struct mmap_event		mmap;
	struct comm_event		comm;
	struct fork_event		fork;
	struct lost_event		lost;
	struct read_event		read;
	struct sample_event		sample;
} event_t;

struct events_stats {
	u64 total;
	u64 lost;
};

struct event_stat_id {
	struct rb_node		rb_node;
	struct rb_root		hists;
	struct events_stats	stats;
	u64			config;
	u64			event_stream;
	u32			type;
};

void event__print_totals(void);

struct perf_session;

typedef int (*event__handler_t)(event_t *event, struct perf_session *session);

int event__synthesize_thread(pid_t pid, event__handler_t process,
			     struct perf_session *session);
void event__synthesize_threads(event__handler_t process,
			       struct perf_session *session);
int event__synthesize_kernel_mmap(event__handler_t process,
				  struct perf_session *session,
				  const char *symbol_name);
int event__synthesize_modules(event__handler_t process,
			      struct perf_session *session);

int event__process_comm(event_t *self, struct perf_session *session);
int event__process_lost(event_t *self, struct perf_session *session);
int event__process_mmap(event_t *self, struct perf_session *session);
int event__process_task(event_t *self, struct perf_session *session);

struct addr_location;
int event__preprocess_sample(const event_t *self, struct perf_session *session,
			     struct addr_location *al, symbol_filter_t filter);
int event__parse_sample(event_t *event, u64 type, struct sample_data *data);

#endif /* __PERF_RECORD_H */
