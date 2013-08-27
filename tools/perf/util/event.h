#ifndef __PERF_RECORD_H
#define __PERF_RECORD_H

#include <limits.h>
#include <stdio.h>

#include "../perf.h"
#include "map.h"
#include "build-id.h"

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


#define PERF_SAMPLE_MASK				\
	(PERF_SAMPLE_IP | PERF_SAMPLE_TID |		\
	 PERF_SAMPLE_TIME | PERF_SAMPLE_ADDR |		\
	PERF_SAMPLE_ID | PERF_SAMPLE_STREAM_ID |	\
	 PERF_SAMPLE_CPU | PERF_SAMPLE_PERIOD |		\
	 PERF_SAMPLE_IDENTIFIER)

struct sample_event {
	struct perf_event_header        header;
	u64 array[];
};

struct regs_dump {
	u64 abi;
	u64 *regs;
};

struct stack_dump {
	u16 offset;
	u64 size;
	char *data;
};

struct sample_read_value {
	u64 value;
	u64 id;
};

struct sample_read {
	u64 time_enabled;
	u64 time_running;
	union {
		struct {
			u64 nr;
			struct sample_read_value *values;
		} group;
		struct sample_read_value one;
	};
};

struct perf_sample {
	u64 ip;
	u32 pid, tid;
	u64 time;
	u64 addr;
	u64 id;
	u64 stream_id;
	u64 period;
	u64 weight;
	u32 cpu;
	u32 raw_size;
	u64 data_src;
	void *raw_data;
	struct ip_callchain *callchain;
	struct branch_stack *branch_stack;
	struct regs_dump  user_regs;
	struct stack_dump user_stack;
	struct sample_read read;
};

#define PERF_MEM_DATA_SRC_NONE \
	(PERF_MEM_S(OP, NA) |\
	 PERF_MEM_S(LVL, NA) |\
	 PERF_MEM_S(SNOOP, NA) |\
	 PERF_MEM_S(LOCK, NA) |\
	 PERF_MEM_S(TLB, NA))

struct build_id_event {
	struct perf_event_header header;
	pid_t			 pid;
	u8			 build_id[PERF_ALIGN(BUILD_ID_SIZE, sizeof(u64))];
	char			 filename[];
};

enum perf_user_event_type { /* above any possible kernel type */
	PERF_RECORD_USER_TYPE_START		= 64,
	PERF_RECORD_HEADER_ATTR			= 64,
	PERF_RECORD_HEADER_EVENT_TYPE		= 65, /* depreceated */
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

union perf_event {
	struct perf_event_header	header;
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
};

void perf_event__print_totals(void);

struct perf_tool;
struct thread_map;

typedef int (*perf_event__handler_t)(struct perf_tool *tool,
				     union perf_event *event,
				     struct perf_sample *sample,
				     struct machine *machine);

int perf_event__synthesize_thread_map(struct perf_tool *tool,
				      struct thread_map *threads,
				      perf_event__handler_t process,
				      struct machine *machine);
int perf_event__synthesize_threads(struct perf_tool *tool,
				   perf_event__handler_t process,
				   struct machine *machine);
int perf_event__synthesize_kernel_mmap(struct perf_tool *tool,
				       perf_event__handler_t process,
				       struct machine *machine,
				       const char *symbol_name);

int perf_event__synthesize_modules(struct perf_tool *tool,
				   perf_event__handler_t process,
				   struct machine *machine);

int perf_event__process_comm(struct perf_tool *tool,
			     union perf_event *event,
			     struct perf_sample *sample,
			     struct machine *machine);
int perf_event__process_lost(struct perf_tool *tool,
			     union perf_event *event,
			     struct perf_sample *sample,
			     struct machine *machine);
int perf_event__process_mmap(struct perf_tool *tool,
			     union perf_event *event,
			     struct perf_sample *sample,
			     struct machine *machine);
int perf_event__process_fork(struct perf_tool *tool,
			     union perf_event *event,
			     struct perf_sample *sample,
			     struct machine *machine);
int perf_event__process_exit(struct perf_tool *tool,
			     union perf_event *event,
			     struct perf_sample *sample,
			     struct machine *machine);
int perf_event__process(struct perf_tool *tool,
			union perf_event *event,
			struct perf_sample *sample,
			struct machine *machine);

struct addr_location;
int perf_event__preprocess_sample(const union perf_event *self,
				  struct machine *machine,
				  struct addr_location *al,
				  struct perf_sample *sample);

const char *perf_event__name(unsigned int id);

int perf_event__synthesize_sample(union perf_event *event, u64 type,
				  u64 sample_regs_user, u64 read_format,
				  const struct perf_sample *sample,
				  bool swapped);

size_t perf_event__fprintf_comm(union perf_event *event, FILE *fp);
size_t perf_event__fprintf_mmap(union perf_event *event, FILE *fp);
size_t perf_event__fprintf_task(union perf_event *event, FILE *fp);
size_t perf_event__fprintf(union perf_event *event, FILE *fp);

#endif /* __PERF_RECORD_H */
