#ifndef _LINUX_FTRACE_EVENT_H
#define _LINUX_FTRACE_EVENT_H

#include <linux/ring_buffer.h>
#include <linux/trace_seq.h>
#include <linux/percpu.h>
#include <linux/hardirq.h>
#include <linux/perf_event.h>

struct trace_array;
struct trace_buffer;
struct tracer;
struct dentry;

struct trace_print_flags {
	unsigned long		mask;
	const char		*name;
};

struct trace_print_flags_u64 {
	unsigned long long	mask;
	const char		*name;
};

const char *ftrace_print_flags_seq(struct trace_seq *p, const char *delim,
				   unsigned long flags,
				   const struct trace_print_flags *flag_array);

const char *ftrace_print_symbols_seq(struct trace_seq *p, unsigned long val,
				     const struct trace_print_flags *symbol_array);

#if BITS_PER_LONG == 32
const char *ftrace_print_symbols_seq_u64(struct trace_seq *p,
					 unsigned long long val,
					 const struct trace_print_flags_u64
								 *symbol_array);
#endif

const char *ftrace_print_hex_seq(struct trace_seq *p,
				 const unsigned char *buf, int len);

struct trace_iterator;
struct trace_event;

int ftrace_raw_output_prep(struct trace_iterator *iter,
			   struct trace_event *event);

/*
 * The trace entry - the most basic unit of tracing. This is what
 * is printed in the end as a single line in the trace output, such as:
 *
 *     bash-15816 [01]   235.197585: idle_cpu <- irq_enter
 */
struct trace_entry {
	unsigned short		type;
	unsigned char		flags;
	unsigned char		preempt_count;
	int			pid;
};

#define FTRACE_MAX_EVENT						\
	((1 << (sizeof(((struct trace_entry *)0)->type) * 8)) - 1)

/*
 * Trace iterator - used by printout routines who present trace
 * results to users and which routines might sleep, etc:
 */
struct trace_iterator {
	struct trace_array	*tr;
	struct tracer		*trace;
	struct trace_buffer	*trace_buffer;
	void			*private;
	int			cpu_file;
	struct mutex		mutex;
	struct ring_buffer_iter	**buffer_iter;
	unsigned long		iter_flags;

	/* trace_seq for __print_flags() and __print_symbolic() etc. */
	struct trace_seq	tmp_seq;

	cpumask_var_t		started;

	/* it's true when current open file is snapshot */
	bool			snapshot;

	/* The below is zeroed out in pipe_read */
	struct trace_seq	seq;
	struct trace_entry	*ent;
	unsigned long		lost_events;
	int			leftover;
	int			ent_size;
	int			cpu;
	u64			ts;

	loff_t			pos;
	long			idx;

	/* All new field here will be zeroed out in pipe_read */
};

enum trace_iter_flags {
	TRACE_FILE_LAT_FMT	= 1,
	TRACE_FILE_ANNOTATE	= 2,
	TRACE_FILE_TIME_IN_NS	= 4,
};


typedef enum print_line_t (*trace_print_func)(struct trace_iterator *iter,
				      int flags, struct trace_event *event);

struct trace_event_functions {
	trace_print_func	trace;
	trace_print_func	raw;
	trace_print_func	hex;
	trace_print_func	binary;
};

struct trace_event {
	struct hlist_node		node;
	struct list_head		list;
	int				type;
	struct trace_event_functions	*funcs;
};

extern int register_ftrace_event(struct trace_event *event);
extern int unregister_ftrace_event(struct trace_event *event);

/* Return values for print_line callback */
enum print_line_t {
	TRACE_TYPE_PARTIAL_LINE	= 0,	/* Retry after flushing the seq */
	TRACE_TYPE_HANDLED	= 1,
	TRACE_TYPE_UNHANDLED	= 2,	/* Relay to other output functions */
	TRACE_TYPE_NO_CONSUME	= 3	/* Handled but ask to not consume */
};

void tracing_generic_entry_update(struct trace_entry *entry,
				  unsigned long flags,
				  int pc);
struct ftrace_event_file;

struct ring_buffer_event *
trace_event_buffer_lock_reserve(struct ring_buffer **current_buffer,
				struct ftrace_event_file *ftrace_file,
				int type, unsigned long len,
				unsigned long flags, int pc);
struct ring_buffer_event *
trace_current_buffer_lock_reserve(struct ring_buffer **current_buffer,
				  int type, unsigned long len,
				  unsigned long flags, int pc);
void trace_current_buffer_unlock_commit(struct ring_buffer *buffer,
					struct ring_buffer_event *event,
					unsigned long flags, int pc);
void trace_buffer_unlock_commit(struct ring_buffer *buffer,
				struct ring_buffer_event *event,
				unsigned long flags, int pc);
void trace_buffer_unlock_commit_regs(struct ring_buffer *buffer,
				     struct ring_buffer_event *event,
				     unsigned long flags, int pc,
				     struct pt_regs *regs);
void trace_current_buffer_discard_commit(struct ring_buffer *buffer,
					 struct ring_buffer_event *event);

void tracing_record_cmdline(struct task_struct *tsk);

struct event_filter;

enum trace_reg {
	TRACE_REG_REGISTER,
	TRACE_REG_UNREGISTER,
#ifdef CONFIG_PERF_EVENTS
	TRACE_REG_PERF_REGISTER,
	TRACE_REG_PERF_UNREGISTER,
	TRACE_REG_PERF_OPEN,
	TRACE_REG_PERF_CLOSE,
	TRACE_REG_PERF_ADD,
	TRACE_REG_PERF_DEL,
#endif
};

struct ftrace_event_call;

struct ftrace_event_class {
	char			*system;
	void			*probe;
#ifdef CONFIG_PERF_EVENTS
	void			*perf_probe;
#endif
	int			(*reg)(struct ftrace_event_call *event,
				       enum trace_reg type, void *data);
	int			(*define_fields)(struct ftrace_event_call *);
	struct list_head	*(*get_fields)(struct ftrace_event_call *);
	struct list_head	fields;
	int			(*raw_init)(struct ftrace_event_call *);
};

extern int ftrace_event_reg(struct ftrace_event_call *event,
			    enum trace_reg type, void *data);

enum {
	TRACE_EVENT_FL_FILTERED_BIT,
	TRACE_EVENT_FL_CAP_ANY_BIT,
	TRACE_EVENT_FL_NO_SET_FILTER_BIT,
	TRACE_EVENT_FL_IGNORE_ENABLE_BIT,
	TRACE_EVENT_FL_WAS_ENABLED_BIT,
};

/*
 * Event flags:
 *  FILTERED	  - The event has a filter attached
 *  CAP_ANY	  - Any user can enable for perf
 *  NO_SET_FILTER - Set when filter has error and is to be ignored
 *  IGNORE_ENABLE - For ftrace internal events, do not enable with debugfs file
 *  WAS_ENABLED   - Set and stays set when an event was ever enabled
 *                    (used for module unloading, if a module event is enabled,
 *                     it is best to clear the buffers that used it).
 */
enum {
	TRACE_EVENT_FL_FILTERED		= (1 << TRACE_EVENT_FL_FILTERED_BIT),
	TRACE_EVENT_FL_CAP_ANY		= (1 << TRACE_EVENT_FL_CAP_ANY_BIT),
	TRACE_EVENT_FL_NO_SET_FILTER	= (1 << TRACE_EVENT_FL_NO_SET_FILTER_BIT),
	TRACE_EVENT_FL_IGNORE_ENABLE	= (1 << TRACE_EVENT_FL_IGNORE_ENABLE_BIT),
	TRACE_EVENT_FL_WAS_ENABLED	= (1 << TRACE_EVENT_FL_WAS_ENABLED_BIT),
};

struct ftrace_event_call {
	struct list_head	list;
	struct ftrace_event_class *class;
	char			*name;
	struct trace_event	event;
	const char		*print_fmt;
	struct event_filter	*filter;
	struct list_head	*files;
	void			*mod;
	void			*data;
	/*
	 *   bit 0:		filter_active
	 *   bit 1:		allow trace by non root (cap any)
	 *   bit 2:		failed to apply filter
	 *   bit 3:		ftrace internal event (do not enable)
	 *   bit 4:		Event was enabled by module
	 */
	int			flags; /* static flags of different events */

#ifdef CONFIG_PERF_EVENTS
	int				perf_refcount;
	struct hlist_head __percpu	*perf_events;
#endif
};

struct trace_array;
struct ftrace_subsystem_dir;

enum {
	FTRACE_EVENT_FL_ENABLED_BIT,
	FTRACE_EVENT_FL_RECORDED_CMD_BIT,
	FTRACE_EVENT_FL_SOFT_MODE_BIT,
	FTRACE_EVENT_FL_SOFT_DISABLED_BIT,
};

/*
 * Ftrace event file flags:
 *  ENABLED	  - The event is enabled
 *  RECORDED_CMD  - The comms should be recorded at sched_switch
 *  SOFT_MODE     - The event is enabled/disabled by SOFT_DISABLED
 *  SOFT_DISABLED - When set, do not trace the event (even though its
 *                   tracepoint may be enabled)
 */
enum {
	FTRACE_EVENT_FL_ENABLED		= (1 << FTRACE_EVENT_FL_ENABLED_BIT),
	FTRACE_EVENT_FL_RECORDED_CMD	= (1 << FTRACE_EVENT_FL_RECORDED_CMD_BIT),
	FTRACE_EVENT_FL_SOFT_MODE	= (1 << FTRACE_EVENT_FL_SOFT_MODE_BIT),
	FTRACE_EVENT_FL_SOFT_DISABLED	= (1 << FTRACE_EVENT_FL_SOFT_DISABLED_BIT),
};

struct ftrace_event_file {
	struct list_head		list;
	struct ftrace_event_call	*event_call;
	struct dentry			*dir;
	struct trace_array		*tr;
	struct ftrace_subsystem_dir	*system;

	/*
	 * 32 bit flags:
	 *   bit 0:		enabled
	 *   bit 1:		enabled cmd record
	 *   bit 2:		enable/disable with the soft disable bit
	 *   bit 3:		soft disabled
	 *
	 * Note: The bits must be set atomically to prevent races
	 * from other writers. Reads of flags do not need to be in
	 * sync as they occur in critical sections. But the way flags
	 * is currently used, these changes do not affect the code
	 * except that when a change is made, it may have a slight
	 * delay in propagating the changes to other CPUs due to
	 * caching and such. Which is mostly OK ;-)
	 */
	unsigned long		flags;
	atomic_t		sm_ref;	/* soft-mode reference counter */
};

#define __TRACE_EVENT_FLAGS(name, value)				\
	static int __init trace_init_flags_##name(void)			\
	{								\
		event_##name.flags = value;				\
		return 0;						\
	}								\
	early_initcall(trace_init_flags_##name);

#define PERF_MAX_TRACE_SIZE	2048

#define MAX_FILTER_STR_VAL	256	/* Should handle KSYM_SYMBOL_LEN */

extern void destroy_preds(struct ftrace_event_call *call);
extern int filter_match_preds(struct event_filter *filter, void *rec);
extern int filter_current_check_discard(struct ring_buffer *buffer,
					struct ftrace_event_call *call,
					void *rec,
					struct ring_buffer_event *event);

enum {
	FILTER_OTHER = 0,
	FILTER_STATIC_STRING,
	FILTER_DYN_STRING,
	FILTER_PTR_STRING,
	FILTER_TRACE_FN,
};

extern int trace_event_raw_init(struct ftrace_event_call *call);
extern int trace_define_field(struct ftrace_event_call *call, const char *type,
			      const char *name, int offset, int size,
			      int is_signed, int filter_type);
extern int trace_add_event_call(struct ftrace_event_call *call);
extern int trace_remove_event_call(struct ftrace_event_call *call);

#define is_signed_type(type)	(((type)(-1)) < (type)1)

int trace_set_clr_event(const char *system, const char *event, int set);

/*
 * The double __builtin_constant_p is because gcc will give us an error
 * if we try to allocate the static variable to fmt if it is not a
 * constant. Even with the outer if statement optimizing out.
 */
#define event_trace_printk(ip, fmt, args...)				\
do {									\
	__trace_printk_check_format(fmt, ##args);			\
	tracing_record_cmdline(current);				\
	if (__builtin_constant_p(fmt)) {				\
		static const char *trace_printk_fmt			\
		  __attribute__((section("__trace_printk_fmt"))) =	\
			__builtin_constant_p(fmt) ? fmt : NULL;		\
									\
		__trace_bprintk(ip, trace_printk_fmt, ##args);		\
	} else								\
		__trace_printk(ip, fmt, ##args);			\
} while (0)

#ifdef CONFIG_PERF_EVENTS
struct perf_event;

DECLARE_PER_CPU(struct pt_regs, perf_trace_regs);

extern int  perf_trace_init(struct perf_event *event);
extern void perf_trace_destroy(struct perf_event *event);
extern int  perf_trace_add(struct perf_event *event, int flags);
extern void perf_trace_del(struct perf_event *event, int flags);
extern int  ftrace_profile_set_filter(struct perf_event *event, int event_id,
				     char *filter_str);
extern void ftrace_profile_free_filter(struct perf_event *event);
extern void *perf_trace_buf_prepare(int size, unsigned short type,
				    struct pt_regs *regs, int *rctxp);

static inline void
perf_trace_buf_submit(void *raw_data, int size, int rctx, u64 addr,
		       u64 count, struct pt_regs *regs, void *head,
		       struct task_struct *task)
{
	perf_tp_event(addr, count, raw_data, size, regs, head, rctx, task);
}
#endif

#endif /* _LINUX_FTRACE_EVENT_H */
