#ifndef __PERF_RECORD_H
#define __PERF_RECORD_H

#include <limits.h>
#include <stdio.h>
#include <linux/kernel.h>

#include "../perf.h"
#include "build-id.h"
#include "perf_regs.h"

struct mmap_event {
	struct perf_event_header header;
	u32 pid, tid;
	u64 start;
	u64 len;
	u64 pgoff;
	char filename[PATH_MAX];
};

struct mmap2_event {
	struct perf_event_header header;
	u32 pid, tid;
	u64 start;
	u64 len;
	u64 pgoff;
	u32 maj;
	u32 min;
	u64 ino;
	u64 ino_generation;
	u32 prot;
	u32 flags;
	char filename[PATH_MAX];
};

struct comm_event {
	struct perf_event_header header;
	u32 pid, tid;
	char comm[16];
};

struct namespaces_event {
	struct perf_event_header header;
	u32 pid, tid;
	u64 nr_namespaces;
	struct perf_ns_link_info link_info[];
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

struct lost_samples_event {
	struct perf_event_header header;
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

struct throttle_event {
	struct perf_event_header header;
	u64 time;
	u64 id;
	u64 stream_id;
};

#define PERF_SAMPLE_MASK				\
	(PERF_SAMPLE_IP | PERF_SAMPLE_TID |		\
	 PERF_SAMPLE_TIME | PERF_SAMPLE_ADDR |		\
	PERF_SAMPLE_ID | PERF_SAMPLE_STREAM_ID |	\
	 PERF_SAMPLE_CPU | PERF_SAMPLE_PERIOD |		\
	 PERF_SAMPLE_IDENTIFIER)

/* perf sample has 16 bits size limit */
#define PERF_SAMPLE_MAX_SIZE (1 << 16)

struct sample_event {
	struct perf_event_header        header;
	u64 array[];
};

struct regs_dump {
	u64 abi;
	u64 mask;
	u64 *regs;

	/* Cached values/mask filled by first register access. */
	u64 cache_regs[PERF_REGS_MAX];
	u64 cache_mask;
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

struct ip_callchain {
	u64 nr;
	u64 ips[0];
};

struct branch_flags {
	u64 mispred:1;
	u64 predicted:1;
	u64 in_tx:1;
	u64 abort:1;
	u64 cycles:16;
	u64 reserved:44;
};

struct branch_entry {
	u64			from;
	u64			to;
	struct branch_flags	flags;
};

struct branch_stack {
	u64			nr;
	struct branch_entry	entries[0];
};

enum {
	PERF_IP_FLAG_BRANCH		= 1ULL << 0,
	PERF_IP_FLAG_CALL		= 1ULL << 1,
	PERF_IP_FLAG_RETURN		= 1ULL << 2,
	PERF_IP_FLAG_CONDITIONAL	= 1ULL << 3,
	PERF_IP_FLAG_SYSCALLRET		= 1ULL << 4,
	PERF_IP_FLAG_ASYNC		= 1ULL << 5,
	PERF_IP_FLAG_INTERRUPT		= 1ULL << 6,
	PERF_IP_FLAG_TX_ABORT		= 1ULL << 7,
	PERF_IP_FLAG_TRACE_BEGIN	= 1ULL << 8,
	PERF_IP_FLAG_TRACE_END		= 1ULL << 9,
	PERF_IP_FLAG_IN_TX		= 1ULL << 10,
};

#define PERF_IP_FLAG_CHARS "bcrosyiABEx"

#define PERF_BRANCH_MASK		(\
	PERF_IP_FLAG_BRANCH		|\
	PERF_IP_FLAG_CALL		|\
	PERF_IP_FLAG_RETURN		|\
	PERF_IP_FLAG_CONDITIONAL	|\
	PERF_IP_FLAG_SYSCALLRET		|\
	PERF_IP_FLAG_ASYNC		|\
	PERF_IP_FLAG_INTERRUPT		|\
	PERF_IP_FLAG_TX_ABORT		|\
	PERF_IP_FLAG_TRACE_BEGIN	|\
	PERF_IP_FLAG_TRACE_END)

#define MAX_INSN 16

struct perf_sample {
	u64 ip;
	u32 pid, tid;
	u64 time;
	u64 addr;
	u64 id;
	u64 stream_id;
	u64 period;
	u64 weight;
	u64 transaction;
	u32 cpu;
	u32 raw_size;
	u64 data_src;
	u32 flags;
	u16 insn_len;
	u8  cpumode;
	char insn[MAX_INSN];
	void *raw_data;
	struct ip_callchain *callchain;
	struct branch_stack *branch_stack;
	struct regs_dump  user_regs;
	struct regs_dump  intr_regs;
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
	PERF_RECORD_HEADER_MAX
};

enum auxtrace_error_type {
	PERF_AUXTRACE_ERROR_ITRACE  = 1,
	PERF_AUXTRACE_ERROR_MAX
};

/*
 * The kernel collects the number of events it couldn't send in a stretch and
 * when possible sends this number in a PERF_RECORD_LOST event. The number of
 * such "chunks" of lost events is stored in .nr_events[PERF_EVENT_LOST] while
 * total_lost tells exactly how many events the kernel in fact lost, i.e. it is
 * the sum of all struct lost_event.lost fields reported.
 *
 * The kernel discards mixed up samples and sends the number in a
 * PERF_RECORD_LOST_SAMPLES event. The number of lost-samples events is stored
 * in .nr_events[PERF_RECORD_LOST_SAMPLES] while total_lost_samples tells
 * exactly how many samples the kernel in fact dropped, i.e. it is the sum of
 * all struct lost_samples_event.lost fields reported.
 *
 * The total_period is needed because by default auto-freq is used, so
 * multipling nr_events[PERF_EVENT_SAMPLE] by a frequency isn't possible to get
 * the total number of low level events, it is necessary to to sum all struct
 * sample_event.period and stash the result in total_period.
 */
struct events_stats {
	u64 total_period;
	u64 total_non_filtered_period;
	u64 total_lost;
	u64 total_lost_samples;
	u64 total_aux_lost;
	u64 total_aux_partial;
	u64 total_invalid_chains;
	u32 nr_events[PERF_RECORD_HEADER_MAX];
	u32 nr_non_filtered_samples;
	u32 nr_lost_warned;
	u32 nr_unknown_events;
	u32 nr_invalid_chains;
	u32 nr_unknown_id;
	u32 nr_unprocessable_samples;
	u32 nr_auxtrace_errors[PERF_AUXTRACE_ERROR_MAX];
	u32 nr_proc_map_timeout;
};

enum {
	PERF_CPU_MAP__CPUS = 0,
	PERF_CPU_MAP__MASK = 1,
};

struct cpu_map_entries {
	u16	nr;
	u16	cpu[];
};

struct cpu_map_mask {
	u16	nr;
	u16	long_size;
	unsigned long mask[];
};

struct cpu_map_data {
	u16	type;
	char	data[];
};

struct cpu_map_event {
	struct perf_event_header	header;
	struct cpu_map_data		data;
};

struct attr_event {
	struct perf_event_header header;
	struct perf_event_attr attr;
	u64 id[];
};

enum {
	PERF_EVENT_UPDATE__UNIT  = 0,
	PERF_EVENT_UPDATE__SCALE = 1,
	PERF_EVENT_UPDATE__NAME  = 2,
	PERF_EVENT_UPDATE__CPUS  = 3,
};

struct event_update_event_cpus {
	struct cpu_map_data cpus;
};

struct event_update_event_scale {
	double scale;
};

struct event_update_event {
	struct perf_event_header header;
	u64 type;
	u64 id;

	char data[];
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

struct id_index_entry {
	u64 id;
	u64 idx;
	u64 cpu;
	u64 tid;
};

struct id_index_event {
	struct perf_event_header header;
	u64 nr;
	struct id_index_entry entries[0];
};

struct auxtrace_info_event {
	struct perf_event_header header;
	u32 type;
	u32 reserved__; /* For alignment */
	u64 priv[];
};

struct auxtrace_event {
	struct perf_event_header header;
	u64 size;
	u64 offset;
	u64 reference;
	u32 idx;
	u32 tid;
	u32 cpu;
	u32 reserved__; /* For alignment */
};

#define MAX_AUXTRACE_ERROR_MSG 64

struct auxtrace_error_event {
	struct perf_event_header header;
	u32 type;
	u32 code;
	u32 cpu;
	u32 pid;
	u32 tid;
	u32 reserved__; /* For alignment */
	u64 ip;
	char msg[MAX_AUXTRACE_ERROR_MSG];
};

struct aux_event {
	struct perf_event_header header;
	u64	aux_offset;
	u64	aux_size;
	u64	flags;
};

struct itrace_start_event {
	struct perf_event_header header;
	u32 pid, tid;
};

struct context_switch_event {
	struct perf_event_header header;
	u32 next_prev_pid;
	u32 next_prev_tid;
};

struct thread_map_event_entry {
	u64	pid;
	char	comm[16];
};

struct thread_map_event {
	struct perf_event_header	header;
	u64				nr;
	struct thread_map_event_entry	entries[];
};

enum {
	PERF_STAT_CONFIG_TERM__AGGR_MODE	= 0,
	PERF_STAT_CONFIG_TERM__INTERVAL		= 1,
	PERF_STAT_CONFIG_TERM__SCALE		= 2,
	PERF_STAT_CONFIG_TERM__MAX		= 3,
};

struct stat_config_event_entry {
	u64	tag;
	u64	val;
};

struct stat_config_event {
	struct perf_event_header	header;
	u64				nr;
	struct stat_config_event_entry	data[];
};

struct stat_event {
	struct perf_event_header	header;

	u64	id;
	u32	cpu;
	u32	thread;

	union {
		struct {
			u64 val;
			u64 ena;
			u64 run;
		};
		u64 values[3];
	};
};

enum {
	PERF_STAT_ROUND_TYPE__INTERVAL	= 0,
	PERF_STAT_ROUND_TYPE__FINAL	= 1,
};

struct stat_round_event {
	struct perf_event_header	header;
	u64				type;
	u64				time;
};

struct time_conv_event {
	struct perf_event_header header;
	u64 time_shift;
	u64 time_mult;
	u64 time_zero;
};

union perf_event {
	struct perf_event_header	header;
	struct mmap_event		mmap;
	struct mmap2_event		mmap2;
	struct comm_event		comm;
	struct namespaces_event		namespaces;
	struct fork_event		fork;
	struct lost_event		lost;
	struct lost_samples_event	lost_samples;
	struct read_event		read;
	struct throttle_event		throttle;
	struct sample_event		sample;
	struct attr_event		attr;
	struct event_update_event	event_update;
	struct event_type_event		event_type;
	struct tracing_data_event	tracing_data;
	struct build_id_event		build_id;
	struct id_index_event		id_index;
	struct auxtrace_info_event	auxtrace_info;
	struct auxtrace_event		auxtrace;
	struct auxtrace_error_event	auxtrace_error;
	struct aux_event		aux;
	struct itrace_start_event	itrace_start;
	struct context_switch_event	context_switch;
	struct thread_map_event		thread_map;
	struct cpu_map_event		cpu_map;
	struct stat_config_event	stat_config;
	struct stat_event		stat;
	struct stat_round_event		stat_round;
	struct time_conv_event		time_conv;
};

void perf_event__print_totals(void);

struct perf_tool;
struct thread_map;
struct cpu_map;
struct perf_stat_config;
struct perf_counts_values;

typedef int (*perf_event__handler_t)(struct perf_tool *tool,
				     union perf_event *event,
				     struct perf_sample *sample,
				     struct machine *machine);

int perf_event__synthesize_thread_map(struct perf_tool *tool,
				      struct thread_map *threads,
				      perf_event__handler_t process,
				      struct machine *machine, bool mmap_data,
				      unsigned int proc_map_timeout);
int perf_event__synthesize_thread_map2(struct perf_tool *tool,
				      struct thread_map *threads,
				      perf_event__handler_t process,
				      struct machine *machine);
int perf_event__synthesize_cpu_map(struct perf_tool *tool,
				   struct cpu_map *cpus,
				   perf_event__handler_t process,
				   struct machine *machine);
int perf_event__synthesize_threads(struct perf_tool *tool,
				   perf_event__handler_t process,
				   struct machine *machine, bool mmap_data,
				   unsigned int proc_map_timeout);
int perf_event__synthesize_kernel_mmap(struct perf_tool *tool,
				       perf_event__handler_t process,
				       struct machine *machine);
int perf_event__synthesize_stat_config(struct perf_tool *tool,
				       struct perf_stat_config *config,
				       perf_event__handler_t process,
				       struct machine *machine);
void perf_event__read_stat_config(struct perf_stat_config *config,
				  struct stat_config_event *event);
int perf_event__synthesize_stat(struct perf_tool *tool,
				u32 cpu, u32 thread, u64 id,
				struct perf_counts_values *count,
				perf_event__handler_t process,
				struct machine *machine);
int perf_event__synthesize_stat_round(struct perf_tool *tool,
				      u64 time, u64 type,
				      perf_event__handler_t process,
				      struct machine *machine);
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
int perf_event__process_lost_samples(struct perf_tool *tool,
				     union perf_event *event,
				     struct perf_sample *sample,
				     struct machine *machine);
int perf_event__process_aux(struct perf_tool *tool,
			    union perf_event *event,
			    struct perf_sample *sample,
			    struct machine *machine);
int perf_event__process_itrace_start(struct perf_tool *tool,
				     union perf_event *event,
				     struct perf_sample *sample,
				     struct machine *machine);
int perf_event__process_switch(struct perf_tool *tool,
			       union perf_event *event,
			       struct perf_sample *sample,
			       struct machine *machine);
int perf_event__process_namespaces(struct perf_tool *tool,
				   union perf_event *event,
				   struct perf_sample *sample,
				   struct machine *machine);
int perf_event__process_mmap(struct perf_tool *tool,
			     union perf_event *event,
			     struct perf_sample *sample,
			     struct machine *machine);
int perf_event__process_mmap2(struct perf_tool *tool,
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

int machine__resolve(struct machine *machine, struct addr_location *al,
		     struct perf_sample *sample);

void addr_location__put(struct addr_location *al);

struct thread;

bool is_bts_event(struct perf_event_attr *attr);
bool sample_addr_correlates_sym(struct perf_event_attr *attr);
void thread__resolve(struct thread *thread, struct addr_location *al,
		     struct perf_sample *sample);

const char *perf_event__name(unsigned int id);

size_t perf_event__sample_event_size(const struct perf_sample *sample, u64 type,
				     u64 read_format);
int perf_event__synthesize_sample(union perf_event *event, u64 type,
				  u64 read_format,
				  const struct perf_sample *sample,
				  bool swapped);

pid_t perf_event__synthesize_comm(struct perf_tool *tool,
				  union perf_event *event, pid_t pid,
				  perf_event__handler_t process,
				  struct machine *machine);

int perf_event__synthesize_namespaces(struct perf_tool *tool,
				      union perf_event *event,
				      pid_t pid, pid_t tgid,
				      perf_event__handler_t process,
				      struct machine *machine);

int perf_event__synthesize_mmap_events(struct perf_tool *tool,
				       union perf_event *event,
				       pid_t pid, pid_t tgid,
				       perf_event__handler_t process,
				       struct machine *machine,
				       bool mmap_data,
				       unsigned int proc_map_timeout);

size_t perf_event__fprintf_comm(union perf_event *event, FILE *fp);
size_t perf_event__fprintf_mmap(union perf_event *event, FILE *fp);
size_t perf_event__fprintf_mmap2(union perf_event *event, FILE *fp);
size_t perf_event__fprintf_task(union perf_event *event, FILE *fp);
size_t perf_event__fprintf_aux(union perf_event *event, FILE *fp);
size_t perf_event__fprintf_itrace_start(union perf_event *event, FILE *fp);
size_t perf_event__fprintf_switch(union perf_event *event, FILE *fp);
size_t perf_event__fprintf_thread_map(union perf_event *event, FILE *fp);
size_t perf_event__fprintf_cpu_map(union perf_event *event, FILE *fp);
size_t perf_event__fprintf_namespaces(union perf_event *event, FILE *fp);
size_t perf_event__fprintf(union perf_event *event, FILE *fp);

int kallsyms__get_function_start(const char *kallsyms_filename,
				 const char *symbol_name, u64 *addr);

void *cpu_map_data__alloc(struct cpu_map *map, size_t *size, u16 *type, int *max);
void  cpu_map_data__synthesize(struct cpu_map_data *data, struct cpu_map *map,
			       u16 type, int max);

void event_attr_init(struct perf_event_attr *attr);

int perf_event_paranoid(void);

extern int sysctl_perf_event_max_stack;
extern int sysctl_perf_event_max_contexts_per_stack;

#endif /* __PERF_RECORD_H */
