#ifndef _LINUX_FTRACE_EVENT_H
#define _LINUX_FTRACE_EVENT_H

#include <linux/ring_buffer.h>
#include <linux/trace_seq.h>
#include <linux/percpu.h>
#include <linux/hardirq.h>

struct trace_array;
struct tracer;
struct dentry;

DECLARE_PER_CPU(struct trace_seq, ftrace_event_seq);

struct trace_print_flags {
	unsigned long		mask;
	const char		*name;
};

const char *ftrace_print_flags_seq(struct trace_seq *p, const char *delim,
				   unsigned long flags,
				   const struct trace_print_flags *flag_array);

const char *ftrace_print_symbols_seq(struct trace_seq *p, unsigned long val,
				     const struct trace_print_flags *symbol_array);

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
	int			lock_depth;
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
	void			*private;
	int			cpu_file;
	struct mutex		mutex;
	struct ring_buffer_iter	*buffer_iter[NR_CPUS];
	unsigned long		iter_flags;

	/* The below is zeroed out in pipe_read */
	struct trace_seq	seq;
	struct trace_entry	*ent;
	int			cpu;
	u64			ts;

	loff_t			pos;
	long			idx;

	cpumask_var_t		started;
};


typedef enum print_line_t (*trace_print_func)(struct trace_iterator *iter,
					      int flags);
struct trace_event {
	struct hlist_node	node;
	struct list_head	list;
	int			type;
	trace_print_func	trace;
	trace_print_func	raw;
	trace_print_func	hex;
	trace_print_func	binary;
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
struct ring_buffer_event *
trace_current_buffer_lock_reserve(struct ring_buffer **current_buffer,
				  int type, unsigned long len,
				  unsigned long flags, int pc);
void trace_current_buffer_unlock_commit(struct ring_buffer *buffer,
					struct ring_buffer_event *event,
					unsigned long flags, int pc);
void trace_nowake_buffer_unlock_commit(struct ring_buffer *buffer,
				       struct ring_buffer_event *event,
					unsigned long flags, int pc);
void trace_current_buffer_discard_commit(struct ring_buffer *buffer,
					 struct ring_buffer_event *event);

void tracing_record_cmdline(struct task_struct *tsk);

struct event_filter;

struct ftrace_event_call {
	struct list_head	list;
	char			*name;
	char			*system;
	struct dentry		*dir;
	struct trace_event	*event;
	int			enabled;
	int			(*regfunc)(void *);
	void			(*unregfunc)(void *);
	int			id;
	int			(*raw_init)(void);
	int			(*show_format)(struct ftrace_event_call *call,
					       struct trace_seq *s);
	int			(*define_fields)(struct ftrace_event_call *);
	struct list_head	fields;
	int			filter_active;
	struct event_filter	*filter;
	void			*mod;
	void			*data;

	atomic_t		profile_count;
	int			(*profile_enable)(void);
	void			(*profile_disable)(void);
};

#define FTRACE_MAX_PROFILE_SIZE	2048

extern char			*trace_profile_buf;
extern char			*trace_profile_buf_nmi;

#define MAX_FILTER_PRED		32
#define MAX_FILTER_STR_VAL	256	/* Should handle KSYM_SYMBOL_LEN */

extern void destroy_preds(struct ftrace_event_call *call);
extern int filter_match_preds(struct ftrace_event_call *call, void *rec);
extern int filter_current_check_discard(struct ring_buffer *buffer,
					struct ftrace_event_call *call,
					void *rec,
					struct ring_buffer_event *event);

enum {
	FILTER_OTHER = 0,
	FILTER_STATIC_STRING,
	FILTER_DYN_STRING,
	FILTER_PTR_STRING,
};

extern int trace_define_field(struct ftrace_event_call *call,
			      const char *type, const char *name,
			      int offset, int size, int is_signed,
			      int filter_type);
extern int trace_define_common_fields(struct ftrace_event_call *call);

#define is_signed_type(type)	(((type)(-1)) < 0)

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

#endif /* _LINUX_FTRACE_EVENT_H */
