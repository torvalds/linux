/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _LINUX_TRACE_EVENT_H
#define _LINUX_TRACE_EVENT_H

#include <linux/ring_buffer.h>
#include <linux/trace_seq.h>
#include <linux/percpu.h>
#include <linux/hardirq.h>
#include <linux/perf_event.h>
#include <linux/tracepoint.h>

struct trace_array;
struct trace_buffer;
struct tracer;
struct dentry;
struct bpf_prog;

const char *trace_print_flags_seq(struct trace_seq *p, const char *delim,
				  unsigned long flags,
				  const struct trace_print_flags *flag_array);

const char *trace_print_symbols_seq(struct trace_seq *p, unsigned long val,
				    const struct trace_print_flags *symbol_array);

#if BITS_PER_LONG == 32
const char *trace_print_flags_seq_u64(struct trace_seq *p, const char *delim,
		      unsigned long long flags,
		      const struct trace_print_flags_u64 *flag_array);

const char *trace_print_symbols_seq_u64(struct trace_seq *p,
					unsigned long long val,
					const struct trace_print_flags_u64
								 *symbol_array);
#endif

const char *trace_print_bitmask_seq(struct trace_seq *p, void *bitmask_ptr,
				    unsigned int bitmask_size);

const char *trace_print_hex_seq(struct trace_seq *p,
				const unsigned char *buf, int len,
				bool concatenate);

const char *trace_print_array_seq(struct trace_seq *p,
				   const void *buf, int count,
				   size_t el_size);

struct trace_iterator;
struct trace_event;

int trace_raw_output_prep(struct trace_iterator *iter,
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

#define TRACE_EVENT_TYPE_MAX						\
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

extern int register_trace_event(struct trace_event *event);
extern int unregister_trace_event(struct trace_event *event);

/* Return values for print_line callback */
enum print_line_t {
	TRACE_TYPE_PARTIAL_LINE	= 0,	/* Retry after flushing the seq */
	TRACE_TYPE_HANDLED	= 1,
	TRACE_TYPE_UNHANDLED	= 2,	/* Relay to other output functions */
	TRACE_TYPE_NO_CONSUME	= 3	/* Handled but ask to not consume */
};

enum print_line_t trace_handle_return(struct trace_seq *s);

void tracing_generic_entry_update(struct trace_entry *entry,
				  unsigned long flags,
				  int pc);
struct trace_event_file;

struct ring_buffer_event *
trace_event_buffer_lock_reserve(struct ring_buffer **current_buffer,
				struct trace_event_file *trace_file,
				int type, unsigned long len,
				unsigned long flags, int pc);

#define TRACE_RECORD_CMDLINE	BIT(0)
#define TRACE_RECORD_TGID	BIT(1)

void tracing_record_taskinfo(struct task_struct *task, int flags);
void tracing_record_taskinfo_sched_switch(struct task_struct *prev,
					  struct task_struct *next, int flags);

void tracing_record_cmdline(struct task_struct *task);
void tracing_record_tgid(struct task_struct *task);

int trace_output_call(struct trace_iterator *iter, char *name, char *fmt, ...);

struct event_filter;

enum trace_reg {
	TRACE_REG_REGISTER,
	TRACE_REG_UNREGISTER,
#ifdef CONFIG_PERF_EVENTS
	TRACE_REG_PERF_REGISTER,
	TRACE_REG_PERF_UNREGISTER,
	TRACE_REG_PERF_OPEN,
	TRACE_REG_PERF_CLOSE,
	/*
	 * These (ADD/DEL) use a 'boolean' return value, where 1 (true) means a
	 * custom action was taken and the default action is not to be
	 * performed.
	 */
	TRACE_REG_PERF_ADD,
	TRACE_REG_PERF_DEL,
#endif
};

struct trace_event_call;

struct trace_event_class {
	const char		*system;
	void			*probe;
#ifdef CONFIG_PERF_EVENTS
	void			*perf_probe;
#endif
	int			(*reg)(struct trace_event_call *event,
				       enum trace_reg type, void *data);
	int			(*define_fields)(struct trace_event_call *);
	struct list_head	*(*get_fields)(struct trace_event_call *);
	struct list_head	fields;
	int			(*raw_init)(struct trace_event_call *);
};

extern int trace_event_reg(struct trace_event_call *event,
			    enum trace_reg type, void *data);

struct trace_event_buffer {
	struct ring_buffer		*buffer;
	struct ring_buffer_event	*event;
	struct trace_event_file		*trace_file;
	void				*entry;
	unsigned long			flags;
	int				pc;
};

void *trace_event_buffer_reserve(struct trace_event_buffer *fbuffer,
				  struct trace_event_file *trace_file,
				  unsigned long len);

void trace_event_buffer_commit(struct trace_event_buffer *fbuffer);

enum {
	TRACE_EVENT_FL_FILTERED_BIT,
	TRACE_EVENT_FL_CAP_ANY_BIT,
	TRACE_EVENT_FL_NO_SET_FILTER_BIT,
	TRACE_EVENT_FL_IGNORE_ENABLE_BIT,
	TRACE_EVENT_FL_TRACEPOINT_BIT,
	TRACE_EVENT_FL_KPROBE_BIT,
	TRACE_EVENT_FL_UPROBE_BIT,
};

/*
 * Event flags:
 *  FILTERED	  - The event has a filter attached
 *  CAP_ANY	  - Any user can enable for perf
 *  NO_SET_FILTER - Set when filter has error and is to be ignored
 *  IGNORE_ENABLE - For trace internal events, do not enable with debugfs file
 *  TRACEPOINT    - Event is a tracepoint
 *  KPROBE        - Event is a kprobe
 *  UPROBE        - Event is a uprobe
 */
enum {
	TRACE_EVENT_FL_FILTERED		= (1 << TRACE_EVENT_FL_FILTERED_BIT),
	TRACE_EVENT_FL_CAP_ANY		= (1 << TRACE_EVENT_FL_CAP_ANY_BIT),
	TRACE_EVENT_FL_NO_SET_FILTER	= (1 << TRACE_EVENT_FL_NO_SET_FILTER_BIT),
	TRACE_EVENT_FL_IGNORE_ENABLE	= (1 << TRACE_EVENT_FL_IGNORE_ENABLE_BIT),
	TRACE_EVENT_FL_TRACEPOINT	= (1 << TRACE_EVENT_FL_TRACEPOINT_BIT),
	TRACE_EVENT_FL_KPROBE		= (1 << TRACE_EVENT_FL_KPROBE_BIT),
	TRACE_EVENT_FL_UPROBE		= (1 << TRACE_EVENT_FL_UPROBE_BIT),
};

#define TRACE_EVENT_FL_UKPROBE (TRACE_EVENT_FL_KPROBE | TRACE_EVENT_FL_UPROBE)

struct trace_event_call {
	struct list_head	list;
	struct trace_event_class *class;
	union {
		char			*name;
		/* Set TRACE_EVENT_FL_TRACEPOINT flag when using "tp" */
		struct tracepoint	*tp;
	};
	struct trace_event	event;
	char			*print_fmt;
	struct event_filter	*filter;
	void			*mod;
	void			*data;
	/*
	 *   bit 0:		filter_active
	 *   bit 1:		allow trace by non root (cap any)
	 *   bit 2:		failed to apply filter
	 *   bit 3:		trace internal event (do not enable)
	 *   bit 4:		Event was enabled by module
	 *   bit 5:		use call filter rather than file filter
	 *   bit 6:		Event is a tracepoint
	 */
	int			flags; /* static flags of different events */

#ifdef CONFIG_PERF_EVENTS
	int				perf_refcount;
	struct hlist_head __percpu	*perf_events;
	struct bpf_prog_array __rcu	*prog_array;

	int	(*perf_perm)(struct trace_event_call *,
			     struct perf_event *);
#endif
};

#ifdef CONFIG_PERF_EVENTS
static inline bool bpf_prog_array_valid(struct trace_event_call *call)
{
	/*
	 * This inline function checks whether call->prog_array
	 * is valid or not. The function is called in various places,
	 * outside rcu_read_lock/unlock, as a heuristic to speed up execution.
	 *
	 * If this function returns true, and later call->prog_array
	 * becomes false inside rcu_read_lock/unlock region,
	 * we bail out then. If this function return false,
	 * there is a risk that we might miss a few events if the checking
	 * were delayed until inside rcu_read_lock/unlock region and
	 * call->prog_array happened to become non-NULL then.
	 *
	 * Here, READ_ONCE() is used instead of rcu_access_pointer().
	 * rcu_access_pointer() requires the actual definition of
	 * "struct bpf_prog_array" while READ_ONCE() only needs
	 * a declaration of the same type.
	 */
	return !!READ_ONCE(call->prog_array);
}
#endif

static inline const char *
trace_event_name(struct trace_event_call *call)
{
	if (call->flags & TRACE_EVENT_FL_TRACEPOINT)
		return call->tp ? call->tp->name : NULL;
	else
		return call->name;
}

struct trace_array;
struct trace_subsystem_dir;

enum {
	EVENT_FILE_FL_ENABLED_BIT,
	EVENT_FILE_FL_RECORDED_CMD_BIT,
	EVENT_FILE_FL_RECORDED_TGID_BIT,
	EVENT_FILE_FL_FILTERED_BIT,
	EVENT_FILE_FL_NO_SET_FILTER_BIT,
	EVENT_FILE_FL_SOFT_MODE_BIT,
	EVENT_FILE_FL_SOFT_DISABLED_BIT,
	EVENT_FILE_FL_TRIGGER_MODE_BIT,
	EVENT_FILE_FL_TRIGGER_COND_BIT,
	EVENT_FILE_FL_PID_FILTER_BIT,
	EVENT_FILE_FL_WAS_ENABLED_BIT,
};

/*
 * Event file flags:
 *  ENABLED	  - The event is enabled
 *  RECORDED_CMD  - The comms should be recorded at sched_switch
 *  RECORDED_TGID - The tgids should be recorded at sched_switch
 *  FILTERED	  - The event has a filter attached
 *  NO_SET_FILTER - Set when filter has error and is to be ignored
 *  SOFT_MODE     - The event is enabled/disabled by SOFT_DISABLED
 *  SOFT_DISABLED - When set, do not trace the event (even though its
 *                   tracepoint may be enabled)
 *  TRIGGER_MODE  - When set, invoke the triggers associated with the event
 *  TRIGGER_COND  - When set, one or more triggers has an associated filter
 *  PID_FILTER    - When set, the event is filtered based on pid
 *  WAS_ENABLED   - Set when enabled to know to clear trace on module removal
 */
enum {
	EVENT_FILE_FL_ENABLED		= (1 << EVENT_FILE_FL_ENABLED_BIT),
	EVENT_FILE_FL_RECORDED_CMD	= (1 << EVENT_FILE_FL_RECORDED_CMD_BIT),
	EVENT_FILE_FL_RECORDED_TGID	= (1 << EVENT_FILE_FL_RECORDED_TGID_BIT),
	EVENT_FILE_FL_FILTERED		= (1 << EVENT_FILE_FL_FILTERED_BIT),
	EVENT_FILE_FL_NO_SET_FILTER	= (1 << EVENT_FILE_FL_NO_SET_FILTER_BIT),
	EVENT_FILE_FL_SOFT_MODE		= (1 << EVENT_FILE_FL_SOFT_MODE_BIT),
	EVENT_FILE_FL_SOFT_DISABLED	= (1 << EVENT_FILE_FL_SOFT_DISABLED_BIT),
	EVENT_FILE_FL_TRIGGER_MODE	= (1 << EVENT_FILE_FL_TRIGGER_MODE_BIT),
	EVENT_FILE_FL_TRIGGER_COND	= (1 << EVENT_FILE_FL_TRIGGER_COND_BIT),
	EVENT_FILE_FL_PID_FILTER	= (1 << EVENT_FILE_FL_PID_FILTER_BIT),
	EVENT_FILE_FL_WAS_ENABLED	= (1 << EVENT_FILE_FL_WAS_ENABLED_BIT),
};

struct trace_event_file {
	struct list_head		list;
	struct trace_event_call		*event_call;
	struct event_filter __rcu	*filter;
	struct dentry			*dir;
	struct trace_array		*tr;
	struct trace_subsystem_dir	*system;
	struct list_head		triggers;

	/*
	 * 32 bit flags:
	 *   bit 0:		enabled
	 *   bit 1:		enabled cmd record
	 *   bit 2:		enable/disable with the soft disable bit
	 *   bit 3:		soft disabled
	 *   bit 4:		trigger enabled
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
	atomic_t		tm_ref;	/* trigger-mode reference counter */
};

#define __TRACE_EVENT_FLAGS(name, value)				\
	static int __init trace_init_flags_##name(void)			\
	{								\
		event_##name.flags |= value;				\
		return 0;						\
	}								\
	early_initcall(trace_init_flags_##name);

#define __TRACE_EVENT_PERF_PERM(name, expr...)				\
	static int perf_perm_##name(struct trace_event_call *tp_event, \
				    struct perf_event *p_event)		\
	{								\
		return ({ expr; });					\
	}								\
	static int __init trace_init_perf_perm_##name(void)		\
	{								\
		event_##name.perf_perm = &perf_perm_##name;		\
		return 0;						\
	}								\
	early_initcall(trace_init_perf_perm_##name);

#define PERF_MAX_TRACE_SIZE	2048

#define MAX_FILTER_STR_VAL	256	/* Should handle KSYM_SYMBOL_LEN */

enum event_trigger_type {
	ETT_NONE		= (0),
	ETT_TRACE_ONOFF		= (1 << 0),
	ETT_SNAPSHOT		= (1 << 1),
	ETT_STACKTRACE		= (1 << 2),
	ETT_EVENT_ENABLE	= (1 << 3),
	ETT_EVENT_HIST		= (1 << 4),
	ETT_HIST_ENABLE		= (1 << 5),
};

extern int filter_match_preds(struct event_filter *filter, void *rec);

extern enum event_trigger_type event_triggers_call(struct trace_event_file *file,
						   void *rec);
extern void event_triggers_post_call(struct trace_event_file *file,
				     enum event_trigger_type tt,
				     void *rec);

bool trace_event_ignore_this_pid(struct trace_event_file *trace_file);

/**
 * trace_trigger_soft_disabled - do triggers and test if soft disabled
 * @file: The file pointer of the event to test
 *
 * If any triggers without filters are attached to this event, they
 * will be called here. If the event is soft disabled and has no
 * triggers that require testing the fields, it will return true,
 * otherwise false.
 */
static inline bool
trace_trigger_soft_disabled(struct trace_event_file *file)
{
	unsigned long eflags = file->flags;

	if (!(eflags & EVENT_FILE_FL_TRIGGER_COND)) {
		if (eflags & EVENT_FILE_FL_TRIGGER_MODE)
			event_triggers_call(file, NULL);
		if (eflags & EVENT_FILE_FL_SOFT_DISABLED)
			return true;
		if (eflags & EVENT_FILE_FL_PID_FILTER)
			return trace_event_ignore_this_pid(file);
	}
	return false;
}

#ifdef CONFIG_BPF_EVENTS
unsigned int trace_call_bpf(struct trace_event_call *call, void *ctx);
int perf_event_attach_bpf_prog(struct perf_event *event, struct bpf_prog *prog);
void perf_event_detach_bpf_prog(struct perf_event *event);
int perf_event_query_prog_array(struct perf_event *event, void __user *info);
#else
static inline unsigned int trace_call_bpf(struct trace_event_call *call, void *ctx)
{
	return 1;
}

static inline int
perf_event_attach_bpf_prog(struct perf_event *event, struct bpf_prog *prog)
{
	return -EOPNOTSUPP;
}

static inline void perf_event_detach_bpf_prog(struct perf_event *event) { }

static inline int
perf_event_query_prog_array(struct perf_event *event, void __user *info)
{
	return -EOPNOTSUPP;
}
#endif

enum {
	FILTER_OTHER = 0,
	FILTER_STATIC_STRING,
	FILTER_DYN_STRING,
	FILTER_PTR_STRING,
	FILTER_TRACE_FN,
	FILTER_COMM,
	FILTER_CPU,
};

extern int trace_event_raw_init(struct trace_event_call *call);
extern int trace_define_field(struct trace_event_call *call, const char *type,
			      const char *name, int offset, int size,
			      int is_signed, int filter_type);
extern int trace_add_event_call(struct trace_event_call *call);
extern int trace_remove_event_call(struct trace_event_call *call);
extern int trace_event_get_offsets(struct trace_event_call *call);

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
DECLARE_PER_CPU(int, bpf_kprobe_override);

extern int  perf_trace_init(struct perf_event *event);
extern void perf_trace_destroy(struct perf_event *event);
extern int  perf_trace_add(struct perf_event *event, int flags);
extern void perf_trace_del(struct perf_event *event, int flags);
#ifdef CONFIG_KPROBE_EVENTS
extern int  perf_kprobe_init(struct perf_event *event, bool is_retprobe);
extern void perf_kprobe_destroy(struct perf_event *event);
#endif
#ifdef CONFIG_UPROBE_EVENTS
extern int  perf_uprobe_init(struct perf_event *event, bool is_retprobe);
extern void perf_uprobe_destroy(struct perf_event *event);
#endif
extern int  ftrace_profile_set_filter(struct perf_event *event, int event_id,
				     char *filter_str);
extern void ftrace_profile_free_filter(struct perf_event *event);
void perf_trace_buf_update(void *record, u16 type);
void *perf_trace_buf_alloc(int size, struct pt_regs **regs, int *rctxp);

void perf_trace_run_bpf_submit(void *raw_data, int size, int rctx,
			       struct trace_event_call *call, u64 count,
			       struct pt_regs *regs, struct hlist_head *head,
			       struct task_struct *task);

static inline void
perf_trace_buf_submit(void *raw_data, int size, int rctx, u16 type,
		       u64 count, struct pt_regs *regs, void *head,
		       struct task_struct *task)
{
	perf_tp_event(type, count, raw_data, size, regs, head, rctx, task);
}

#endif

#endif /* _LINUX_TRACE_EVENT_H */
