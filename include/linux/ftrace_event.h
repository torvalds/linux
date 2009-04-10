#ifndef _LINUX_FTRACE_EVENT_H
#define _LINUX_FTRACE_EVENT_H

#include <linux/trace_seq.h>
#include <linux/ring_buffer.h>


struct trace_array;
struct tracer;
struct dentry;

/*
 * The trace entry - the most basic unit of tracing. This is what
 * is printed in the end as a single line in the trace output, such as:
 *
 *     bash-15816 [01]   235.197585: idle_cpu <- irq_enter
 */
struct trace_entry {
	unsigned char		type;
	unsigned char		flags;
	unsigned char		preempt_count;
	int			pid;
	int			tgid;
};

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

	/* The below is zeroed out in pipe_read */
	struct trace_seq	seq;
	struct trace_entry	*ent;
	int			cpu;
	u64			ts;

	unsigned long		iter_flags;
	loff_t			pos;
	long			idx;

	cpumask_var_t		started;
};


typedef enum print_line_t (*trace_print_func)(struct trace_iterator *iter,
					      int flags);
struct trace_event {
	struct hlist_node	node;
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


struct ring_buffer_event *
trace_current_buffer_lock_reserve(unsigned char type, unsigned long len,
				  unsigned long flags, int pc);
void trace_current_buffer_unlock_commit(struct ring_buffer_event *event,
					unsigned long flags, int pc);
void trace_nowake_buffer_unlock_commit(struct ring_buffer_event *event,
					unsigned long flags, int pc);
void trace_current_buffer_discard_commit(struct ring_buffer_event *event);

void tracing_record_cmdline(struct task_struct *tsk);

struct ftrace_event_call {
	struct list_head	list;
	char			*name;
	char			*system;
	struct dentry		*dir;
	struct trace_event	*event;
	int			enabled;
	int			(*regfunc)(void);
	void			(*unregfunc)(void);
	int			id;
	int			(*raw_init)(void);
	int			(*show_format)(struct trace_seq *s);
	int			(*define_fields)(void);
	struct list_head	fields;
	int			n_preds;
	struct filter_pred	**preds;
	void			*mod;

#ifdef CONFIG_EVENT_PROFILE
	atomic_t	profile_count;
	int		(*profile_enable)(struct ftrace_event_call *);
	void		(*profile_disable)(struct ftrace_event_call *);
#endif
};

#define MAX_FILTER_PRED		8
#define MAX_FILTER_STR_VAL	128

extern int init_preds(struct ftrace_event_call *call);
extern int filter_match_preds(struct ftrace_event_call *call, void *rec);
extern int filter_current_check_discard(struct ftrace_event_call *call,
					void *rec,
					struct ring_buffer_event *event);

extern int trace_define_field(struct ftrace_event_call *call, char *type,
			      char *name, int offset, int size);


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

#define __common_field(type, item)					\
	ret = trace_define_field(event_call, #type, "common_" #item,	\
				 offsetof(typeof(field.ent), item),	\
				 sizeof(field.ent.item));		\
	if (ret)							\
		return ret;

#endif /* _LINUX_FTRACE_EVENT_H */
