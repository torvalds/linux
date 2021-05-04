/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_RECORD_H
#define __PERF_RECORD_H
/*
 * The linux/stddef.h isn't need here, but is needed for __always_inline used
 * in files included from uapi/linux/perf_event.h such as
 * /usr/include/linux/swab.h and /usr/include/linux/byteorder/little_endian.h,
 * detected in at least musl libc, used in Alpine Linux. -acme
 */
#include <stdio.h>
#include <linux/stddef.h>
#include <perf/event.h>
#include <linux/types.h>

#include "perf_regs.h"

struct dso;
struct machine;
struct perf_event_attr;

#ifdef __LP64__
/*
 * /usr/include/inttypes.h uses just 'lu' for PRIu64, but we end up defining
 * __u64 as long long unsigned int, and then -Werror=format= kicks in and
 * complains of the mismatched types, so use these two special extra PRI
 * macros to overcome that.
 */
#define PRI_lu64 "l" PRIu64
#define PRI_lx64 "l" PRIx64
#define PRI_ld64 "l" PRId64
#else
#define PRI_lu64 PRIu64
#define PRI_lx64 PRIx64
#define PRI_ld64 PRId64
#endif

#define PERF_SAMPLE_MASK				\
	(PERF_SAMPLE_IP | PERF_SAMPLE_TID |		\
	 PERF_SAMPLE_TIME | PERF_SAMPLE_ADDR |		\
	PERF_SAMPLE_ID | PERF_SAMPLE_STREAM_ID |	\
	 PERF_SAMPLE_CPU | PERF_SAMPLE_PERIOD |		\
	 PERF_SAMPLE_IDENTIFIER)

/* perf sample has 16 bits size limit */
#define PERF_SAMPLE_MAX_SIZE (1 << 16)

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
	u64 ips[];
};

struct branch_stack;

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
	PERF_IP_FLAG_VMENTRY		= 1ULL << 11,
	PERF_IP_FLAG_VMEXIT		= 1ULL << 12,
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
	PERF_IP_FLAG_TRACE_END		|\
	PERF_IP_FLAG_VMENTRY		|\
	PERF_IP_FLAG_VMEXIT)

#define MAX_INSN 16

struct aux_sample {
	u64 size;
	void *data;
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
	u64 transaction;
	u64 insn_cnt;
	u64 cyc_cnt;
	u32 cpu;
	u32 raw_size;
	u64 data_src;
	u64 phys_addr;
	u64 data_page_size;
	u64 code_page_size;
	u64 cgroup;
	u32 flags;
	u16 insn_len;
	u8  cpumode;
	u16 misc;
	u16 ins_lat;
	bool no_hw_idx;		/* No hw_idx collected in branch_stack */
	char insn[MAX_INSN];
	void *raw_data;
	struct ip_callchain *callchain;
	struct branch_stack *branch_stack;
	struct regs_dump  user_regs;
	struct regs_dump  intr_regs;
	struct stack_dump user_stack;
	struct sample_read read;
	struct aux_sample aux_sample;
};

#define PERF_MEM_DATA_SRC_NONE \
	(PERF_MEM_S(OP, NA) |\
	 PERF_MEM_S(LVL, NA) |\
	 PERF_MEM_S(SNOOP, NA) |\
	 PERF_MEM_S(LOCK, NA) |\
	 PERF_MEM_S(TLB, NA))

/* Attribute type for custom synthesized events */
#define PERF_TYPE_SYNTH		(INT_MAX + 1U)

/* Attribute config for custom synthesized events */
enum perf_synth_id {
	PERF_SYNTH_INTEL_PTWRITE,
	PERF_SYNTH_INTEL_MWAIT,
	PERF_SYNTH_INTEL_PWRE,
	PERF_SYNTH_INTEL_EXSTOP,
	PERF_SYNTH_INTEL_PWRX,
	PERF_SYNTH_INTEL_CBR,
	PERF_SYNTH_INTEL_PSB,
};

/*
 * Raw data formats for synthesized events. Note that 4 bytes of padding are
 * present to match the 'size' member of PERF_SAMPLE_RAW data which is always
 * 8-byte aligned. That means we must dereference raw_data with an offset of 4.
 * Refer perf_sample__synth_ptr() and perf_synth__raw_data().  It also means the
 * structure sizes are 4 bytes bigger than the raw_size, refer
 * perf_synth__raw_size().
 */

struct perf_synth_intel_ptwrite {
	u32 padding;
	union {
		struct {
			u32	ip		:  1,
				reserved	: 31;
		};
		u32	flags;
	};
	u64	payload;
};

struct perf_synth_intel_mwait {
	u32 padding;
	u32 reserved;
	union {
		struct {
			u64	hints		:  8,
				reserved1	: 24,
				extensions	:  2,
				reserved2	: 30;
		};
		u64	payload;
	};
};

struct perf_synth_intel_pwre {
	u32 padding;
	u32 reserved;
	union {
		struct {
			u64	reserved1	:  7,
				hw		:  1,
				subcstate	:  4,
				cstate		:  4,
				reserved2	: 48;
		};
		u64	payload;
	};
};

struct perf_synth_intel_exstop {
	u32 padding;
	union {
		struct {
			u32	ip		:  1,
				reserved	: 31;
		};
		u32	flags;
	};
};

struct perf_synth_intel_pwrx {
	u32 padding;
	u32 reserved;
	union {
		struct {
			u64	deepest_cstate	:  4,
				last_cstate	:  4,
				wake_reason	:  4,
				reserved1	: 52;
		};
		u64	payload;
	};
};

struct perf_synth_intel_cbr {
	u32 padding;
	union {
		struct {
			u32	cbr		:  8,
				reserved1	:  8,
				max_nonturbo	:  8,
				reserved2	:  8;
		};
		u32	flags;
	};
	u32 freq;
	u32 reserved3;
};

struct perf_synth_intel_psb {
	u32 padding;
	u32 reserved;
	u64 offset;
};

/*
 * raw_data is always 4 bytes from an 8-byte boundary, so subtract 4 to get
 * 8-byte alignment.
 */
static inline void *perf_sample__synth_ptr(struct perf_sample *sample)
{
	return sample->raw_data - 4;
}

static inline void *perf_synth__raw_data(void *p)
{
	return p + 4;
}

#define perf_synth__raw_size(d) (sizeof(d) - 4)

#define perf_sample__bad_synth_size(s, d) ((s)->raw_size < sizeof(d) - 4)

enum {
	PERF_STAT_ROUND_TYPE__INTERVAL	= 0,
	PERF_STAT_ROUND_TYPE__FINAL	= 1,
};

void perf_event__print_totals(void);

struct perf_cpu_map;
struct perf_record_stat_config;
struct perf_stat_config;
struct perf_tool;

void perf_event__read_stat_config(struct perf_stat_config *config,
				  struct perf_record_stat_config *event);

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
int perf_event__process_cgroup(struct perf_tool *tool,
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
int perf_event__process_ksymbol(struct perf_tool *tool,
				union perf_event *event,
				struct perf_sample *sample,
				struct machine *machine);
int perf_event__process_bpf(struct perf_tool *tool,
			    union perf_event *event,
			    struct perf_sample *sample,
			    struct machine *machine);
int perf_event__process_text_poke(struct perf_tool *tool,
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
size_t perf_event__fprintf_cgroup(union perf_event *event, FILE *fp);
size_t perf_event__fprintf_ksymbol(union perf_event *event, FILE *fp);
size_t perf_event__fprintf_bpf(union perf_event *event, FILE *fp);
size_t perf_event__fprintf_text_poke(union perf_event *event, struct machine *machine,FILE *fp);
size_t perf_event__fprintf(union perf_event *event, struct machine *machine, FILE *fp);

int kallsyms__get_function_start(const char *kallsyms_filename,
				 const char *symbol_name, u64 *addr);

void *cpu_map_data__alloc(struct perf_cpu_map *map, size_t *size, u16 *type, int *max);
void  cpu_map_data__synthesize(struct perf_record_cpu_map_data *data, struct perf_cpu_map *map,
			       u16 type, int max);

void event_attr_init(struct perf_event_attr *attr);

int perf_event_paranoid(void);
bool perf_event_paranoid_check(int max_level);

extern int sysctl_perf_event_max_stack;
extern int sysctl_perf_event_max_contexts_per_stack;
extern unsigned int proc_map_timeout;

#define PAGE_SIZE_NAME_LEN	32
char *get_page_size_name(u64 size, char *str);

void arch_perf_parse_sample_weight(struct perf_sample *data, const __u64 *array, u64 type);
void arch_perf_synthesize_sample_weight(const struct perf_sample *data, __u64 *array, u64 type);

#endif /* __PERF_RECORD_H */
