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
	u64 period;
	u32 cpu;
	u32 raw_size;
	void *raw_data;
	struct ip_callchain *callchain;
};

#define BUILD_ID_SIZE 20

struct build_id_event {
	struct perf_event_header header;
	pid_t			 pid;
	u8			 build_id[ALIGN(BUILD_ID_SIZE, sizeof(u64))];
	char			 filename[];
};

enum perf_user_event_type { /* above any possible kernel type */
	PERF_RECORD_USER_TYPE_START		= 64,
	PERF_RECORD_HEADER_ATTR			= 64,
	PERF_RECORD_HEADER_EVENT_TYPE		= 65,
	PERF_RECORD_HEADER_TRACING_DATA		= 66,
	PERF_RECORD_HEADER_BUILD_ID		= 67,
	PERF_RECORD_FINISHED_ROUND		= 68,
	PERF_RECORD_HEADER_MAX
};

struct attr_event {
	struct perf_event_header header;
	struct perf_event_attr attr;
	u64 id[];
};

#define MAX_EVENT_NAME 64

struct perf_trace_event_type {
	u64	event_id;
	char	name[MAX_EVENT_NAME];
};

struct event_type_event {
	struct perf_event_header header;
	struct perf_trace_event_type event_type;
};

struct tracing_data_event {
	struct perf_event_header header;
	u32 size;
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
	struct attr_event		attr;
	struct event_type_event		event_type;
	struct tracing_data_event	tracing_data;
	struct build_id_event		build_id;
} event_t;

void event__print_totals(void);

struct perf_session;
struct thread_map;

typedef int (*event__handler_synth_t)(event_t *event, 
				      struct perf_session *session);
typedef int (*event__handler_t)(event_t *event, struct sample_data *sample,
				struct perf_session *session);

int event__synthesize_thread_map(struct thread_map *threads,
				 event__handler_t process,
				 struct perf_session *session);
int event__synthesize_threads(event__handler_t process,
			      struct perf_session *session);
int event__synthesize_kernel_mmap(event__handler_t process,
				struct perf_session *session,
				struct machine *machine,
				const char *symbol_name);

int event__synthesize_modules(event__handler_t process,
			      struct perf_session *session,
			      struct machine *machine);

int event__process_comm(event_t *self, struct sample_data *sample,
			struct perf_session *session);
int event__process_lost(event_t *self, struct sample_data *sample,
			struct perf_session *session);
int event__process_mmap(event_t *self, struct sample_data *sample,
			struct perf_session *session);
int event__process_task(event_t *self, struct sample_data *sample,
			struct perf_session *session);
int event__process(event_t *event, struct sample_data *sample,
		   struct perf_session *session);

struct addr_location;
int event__preprocess_sample(const event_t *self, struct perf_session *session,
			     struct addr_location *al, struct sample_data *data,
			     symbol_filter_t filter);
int event__parse_sample(const event_t *event, struct perf_session *session,
			struct sample_data *sample);

const char *event__get_event_name(unsigned int id);

#endif /* __PERF_RECORD_H */
