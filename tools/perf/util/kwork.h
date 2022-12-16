#ifndef PERF_UTIL_KWORK_H
#define PERF_UTIL_KWORK_H

#include "util/tool.h"
#include "util/time-utils.h"

#include <linux/bitmap.h>
#include <linux/list.h>
#include <linux/rbtree.h>
#include <linux/types.h>

struct perf_sample;
struct perf_session;

enum kwork_class_type {
	KWORK_CLASS_IRQ,
	KWORK_CLASS_SOFTIRQ,
	KWORK_CLASS_WORKQUEUE,
	KWORK_CLASS_MAX,
};

enum kwork_report_type {
	KWORK_REPORT_RUNTIME,
	KWORK_REPORT_LATENCY,
	KWORK_REPORT_TIMEHIST,
};

enum kwork_trace_type {
	KWORK_TRACE_RAISE,
	KWORK_TRACE_ENTRY,
	KWORK_TRACE_EXIT,
	KWORK_TRACE_MAX,
};

/*
 * data structure:
 *
 *                 +==================+ +============+ +======================+
 *                 |      class       | |    work    | |         atom         |
 *                 +==================+ +============+ +======================+
 * +------------+  |  +-----+         | |  +------+  | |  +-------+   +-----+ |
 * | perf_kwork | +-> | irq | --------|+-> | eth0 | --+-> | raise | - | ... | --+   +-----------+
 * +-----+------+ ||  +-----+         |||  +------+  |||  +-------+   +-----+ | |   |           |
 *       |        ||                  |||            |||                      | +-> | atom_page |
 *       |        ||                  |||            |||  +-------+   +-----+ |     |           |
 *       |  class_list                |||            |+-> | entry | - | ... | ----> |           |
 *       |        ||                  |||            |||  +-------+   +-----+ |     |           |
 *       |        ||                  |||            |||                      | +-> |           |
 *       |        ||                  |||            |||  +-------+   +-----+ | |   |           |
 *       |        ||                  |||            |+-> | exit  | - | ... | --+   +-----+-----+
 *       |        ||                  |||            | |  +-------+   +-----+ |           |
 *       |        ||                  |||            | |                      |           |
 *       |        ||                  |||  +-----+   | |                      |           |
 *       |        ||                  |+-> | ... |   | |                      |           |
 *       |        ||                  | |  +-----+   | |                      |           |
 *       |        ||                  | |            | |                      |           |
 *       |        ||  +---------+     | |  +-----+   | |  +-------+   +-----+ |           |
 *       |        +-> | softirq | -------> | RCU | ---+-> | raise | - | ... | --+   +-----+-----+
 *       |        ||  +---------+     | |  +-----+   |||  +-------+   +-----+ | |   |           |
 *       |        ||                  | |            |||                      | +-> | atom_page |
 *       |        ||                  | |            |||  +-------+   +-----+ |     |           |
 *       |        ||                  | |            |+-> | entry | - | ... | ----> |           |
 *       |        ||                  | |            |||  +-------+   +-----+ |     |           |
 *       |        ||                  | |            |||                      | +-> |           |
 *       |        ||                  | |            |||  +-------+   +-----+ | |   |           |
 *       |        ||                  | |            |+-> | exit  | - | ... | --+   +-----+-----+
 *       |        ||                  | |            | |  +-------+   +-----+ |           |
 *       |        ||                  | |            | |                      |           |
 *       |        ||  +-----------+   | |  +-----+   | |                      |           |
 *       |        +-> | workqueue | -----> | ... |   | |                      |           |
 *       |         |  +-----------+   | |  +-----+   | |                      |           |
 *       |         +==================+ +============+ +======================+           |
 *       |                                                                                |
 *       +---->  atom_page_list  ---------------------------------------------------------+
 *
 */

struct kwork_atom {
	struct list_head list;
	u64 time;
	struct kwork_atom *prev;

	void *page_addr;
	unsigned long bit_inpage;
};

#define NR_ATOM_PER_PAGE 128
struct kwork_atom_page {
	struct list_head list;
	struct kwork_atom atoms[NR_ATOM_PER_PAGE];
	DECLARE_BITMAP(bitmap, NR_ATOM_PER_PAGE);
};

struct kwork_class;
struct kwork_work {
	/*
	 * class field
	 */
	struct rb_node node;
	struct kwork_class *class;

	/*
	 * work field
	 */
	u64 id;
	int cpu;
	char *name;

	/*
	 * atom field
	 */
	u64 nr_atoms;
	struct list_head atom_list[KWORK_TRACE_MAX];

	/*
	 * runtime report
	 */
	u64 max_runtime;
	u64 max_runtime_start;
	u64 max_runtime_end;
	u64 total_runtime;

	/*
	 * latency report
	 */
	u64 max_latency;
	u64 max_latency_start;
	u64 max_latency_end;
	u64 total_latency;
};

struct kwork_class {
	struct list_head list;
	const char *name;
	enum kwork_class_type type;

	unsigned int nr_tracepoints;
	const struct evsel_str_handler *tp_handlers;

	struct rb_root_cached work_root;

	int (*class_init)(struct kwork_class *class,
			  struct perf_session *session);

	void (*work_init)(struct kwork_class *class,
			  struct kwork_work *work,
			  struct evsel *evsel,
			  struct perf_sample *sample,
			  struct machine *machine);

	void (*work_name)(struct kwork_work *work,
			  char *buf, int len);
};

struct perf_kwork;
struct trace_kwork_handler {
	int (*raise_event)(struct perf_kwork *kwork,
			   struct kwork_class *class, struct evsel *evsel,
			   struct perf_sample *sample, struct machine *machine);

	int (*entry_event)(struct perf_kwork *kwork,
			   struct kwork_class *class, struct evsel *evsel,
			   struct perf_sample *sample, struct machine *machine);

	int (*exit_event)(struct perf_kwork *kwork,
			  struct kwork_class *class, struct evsel *evsel,
			  struct perf_sample *sample, struct machine *machine);
};

struct perf_kwork {
	/*
	 * metadata
	 */
	struct perf_tool tool;
	struct list_head class_list;
	struct list_head atom_page_list;
	struct list_head sort_list, cmp_id;
	struct rb_root_cached sorted_work_root;
	const struct trace_kwork_handler *tp_handler;

	/*
	 * profile filters
	 */
	const char *profile_name;

	const char *cpu_list;
	DECLARE_BITMAP(cpu_bitmap, MAX_NR_CPUS);

	const char *time_str;
	struct perf_time_interval ptime;

	/*
	 * options for command
	 */
	bool force;
	const char *event_list_str;
	enum kwork_report_type report;

	/*
	 * options for subcommand
	 */
	bool summary;
	const char *sort_order;
	bool show_callchain;
	unsigned int max_stack;
	bool use_bpf;

	/*
	 * statistics
	 */
	u64 timestart;
	u64 timeend;

	unsigned long nr_events;
	unsigned long nr_lost_chunks;
	unsigned long nr_lost_events;

	u64 all_runtime;
	u64 all_count;
	u64 nr_skipped_events[KWORK_TRACE_MAX + 1];
};

struct kwork_work *perf_kwork_add_work(struct perf_kwork *kwork,
				       struct kwork_class *class,
				       struct kwork_work *key);

#ifdef HAVE_BPF_SKEL

int perf_kwork__trace_prepare_bpf(struct perf_kwork *kwork);
int perf_kwork__report_read_bpf(struct perf_kwork *kwork);
void perf_kwork__report_cleanup_bpf(void);

void perf_kwork__trace_start(void);
void perf_kwork__trace_finish(void);

#else  /* !HAVE_BPF_SKEL */

static inline int
perf_kwork__trace_prepare_bpf(struct perf_kwork *kwork __maybe_unused)
{
	return -1;
}

static inline int
perf_kwork__report_read_bpf(struct perf_kwork *kwork __maybe_unused)
{
	return -1;
}

static inline void perf_kwork__report_cleanup_bpf(void) {}

static inline void perf_kwork__trace_start(void) {}
static inline void perf_kwork__trace_finish(void) {}

#endif  /* HAVE_BPF_SKEL */

#endif  /* PERF_UTIL_KWORK_H */
