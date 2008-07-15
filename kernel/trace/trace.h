#ifndef _LINUX_KERNEL_TRACE_H
#define _LINUX_KERNEL_TRACE_H

#include <linux/fs.h>
#include <asm/atomic.h>
#include <linux/sched.h>
#include <linux/clocksource.h>
#include <linux/mmiotrace.h>

enum trace_type {
	__TRACE_FIRST_TYPE = 0,

	TRACE_FN,
	TRACE_CTX,
	TRACE_WAKE,
	TRACE_STACK,
	TRACE_SPECIAL,
	TRACE_MMIO_RW,
	TRACE_MMIO_MAP,

	__TRACE_LAST_TYPE
};

/*
 * Function trace entry - function address and parent function addres:
 */
struct ftrace_entry {
	unsigned long		ip;
	unsigned long		parent_ip;
};

/*
 * Context switch trace entry - which task (and prio) we switched from/to:
 */
struct ctx_switch_entry {
	unsigned int		prev_pid;
	unsigned char		prev_prio;
	unsigned char		prev_state;
	unsigned int		next_pid;
	unsigned char		next_prio;
	unsigned char		next_state;
};

/*
 * Special (free-form) trace entry:
 */
struct special_entry {
	unsigned long		arg1;
	unsigned long		arg2;
	unsigned long		arg3;
};

/*
 * Stack-trace entry:
 */

#define FTRACE_STACK_ENTRIES	8

struct stack_entry {
	unsigned long		caller[FTRACE_STACK_ENTRIES];
};

/*
 * The trace entry - the most basic unit of tracing. This is what
 * is printed in the end as a single line in the trace output, such as:
 *
 *     bash-15816 [01]   235.197585: idle_cpu <- irq_enter
 */
struct trace_entry {
	char			type;
	char			cpu;
	char			flags;
	char			preempt_count;
	int			pid;
	cycle_t			t;
	union {
		struct ftrace_entry		fn;
		struct ctx_switch_entry		ctx;
		struct special_entry		special;
		struct stack_entry		stack;
		struct mmiotrace_rw		mmiorw;
		struct mmiotrace_map		mmiomap;
	};
};

#define TRACE_ENTRY_SIZE	sizeof(struct trace_entry)

/*
 * The CPU trace array - it consists of thousands of trace entries
 * plus some other descriptor data: (for example which task started
 * the trace, etc.)
 */
struct trace_array_cpu {
	struct list_head	trace_pages;
	atomic_t		disabled;
	raw_spinlock_t		lock;
	struct lock_class_key	lock_key;

	/* these fields get copied into max-trace: */
	unsigned		trace_head_idx;
	unsigned		trace_tail_idx;
	void			*trace_head; /* producer */
	void			*trace_tail; /* consumer */
	unsigned long		trace_idx;
	unsigned long		overrun;
	unsigned long		saved_latency;
	unsigned long		critical_start;
	unsigned long		critical_end;
	unsigned long		critical_sequence;
	unsigned long		nice;
	unsigned long		policy;
	unsigned long		rt_priority;
	cycle_t			preempt_timestamp;
	pid_t			pid;
	uid_t			uid;
	char			comm[TASK_COMM_LEN];
};

struct trace_iterator;

/*
 * The trace array - an array of per-CPU trace arrays. This is the
 * highest level data structure that individual tracers deal with.
 * They have on/off state as well:
 */
struct trace_array {
	unsigned long		entries;
	long			ctrl;
	int			cpu;
	cycle_t			time_start;
	struct task_struct	*waiter;
	struct trace_array_cpu	*data[NR_CPUS];
};

/*
 * A specific tracer, represented by methods that operate on a trace array:
 */
struct tracer {
	const char		*name;
	void			(*init)(struct trace_array *tr);
	void			(*reset)(struct trace_array *tr);
	void			(*open)(struct trace_iterator *iter);
	void			(*pipe_open)(struct trace_iterator *iter);
	void			(*close)(struct trace_iterator *iter);
	void			(*start)(struct trace_iterator *iter);
	void			(*stop)(struct trace_iterator *iter);
	ssize_t			(*read)(struct trace_iterator *iter,
					struct file *filp, char __user *ubuf,
					size_t cnt, loff_t *ppos);
	void			(*ctrl_update)(struct trace_array *tr);
#ifdef CONFIG_FTRACE_STARTUP_TEST
	int			(*selftest)(struct tracer *trace,
					    struct trace_array *tr);
#endif
	int			(*print_line)(struct trace_iterator *iter);
	struct tracer		*next;
	int			print_max;
};

struct trace_seq {
	unsigned char		buffer[PAGE_SIZE];
	unsigned int		len;
	unsigned int		readpos;
};

/*
 * Trace iterator - used by printout routines who present trace
 * results to users and which routines might sleep, etc:
 */
struct trace_iterator {
	struct trace_array	*tr;
	struct tracer		*trace;
	void			*private;
	long			last_overrun[NR_CPUS];
	long			overrun[NR_CPUS];

	/* The below is zeroed out in pipe_read */
	struct trace_seq	seq;
	struct trace_entry	*ent;
	int			cpu;

	struct trace_entry	*prev_ent;
	int			prev_cpu;

	unsigned long		iter_flags;
	loff_t			pos;
	unsigned long		next_idx[NR_CPUS];
	struct list_head	*next_page[NR_CPUS];
	unsigned		next_page_idx[NR_CPUS];
	long			idx;
};

void tracing_reset(struct trace_array_cpu *data);
int tracing_open_generic(struct inode *inode, struct file *filp);
struct dentry *tracing_init_dentry(void);
void init_tracer_sysprof_debugfs(struct dentry *d_tracer);

void ftrace(struct trace_array *tr,
			    struct trace_array_cpu *data,
			    unsigned long ip,
			    unsigned long parent_ip,
			    unsigned long flags);
void tracing_sched_switch_trace(struct trace_array *tr,
				struct trace_array_cpu *data,
				struct task_struct *prev,
				struct task_struct *next,
				unsigned long flags);
void tracing_record_cmdline(struct task_struct *tsk);

void tracing_sched_wakeup_trace(struct trace_array *tr,
				struct trace_array_cpu *data,
				struct task_struct *wakee,
				struct task_struct *cur,
				unsigned long flags);
void trace_special(struct trace_array *tr,
		   struct trace_array_cpu *data,
		   unsigned long arg1,
		   unsigned long arg2,
		   unsigned long arg3);
void trace_function(struct trace_array *tr,
		    struct trace_array_cpu *data,
		    unsigned long ip,
		    unsigned long parent_ip,
		    unsigned long flags);

void tracing_start_cmdline_record(void);
void tracing_stop_cmdline_record(void);
int register_tracer(struct tracer *type);
void unregister_tracer(struct tracer *type);

extern unsigned long nsecs_to_usecs(unsigned long nsecs);

extern unsigned long tracing_max_latency;
extern unsigned long tracing_thresh;

void update_max_tr(struct trace_array *tr, struct task_struct *tsk, int cpu);
void update_max_tr_single(struct trace_array *tr,
			  struct task_struct *tsk, int cpu);

extern cycle_t ftrace_now(int cpu);

#ifdef CONFIG_FTRACE
void tracing_start_function_trace(void);
void tracing_stop_function_trace(void);
#else
# define tracing_start_function_trace()		do { } while (0)
# define tracing_stop_function_trace()		do { } while (0)
#endif

#ifdef CONFIG_CONTEXT_SWITCH_TRACER
typedef void
(*tracer_switch_func_t)(void *private,
			void *__rq,
			struct task_struct *prev,
			struct task_struct *next);

struct tracer_switch_ops {
	tracer_switch_func_t		func;
	void				*private;
	struct tracer_switch_ops	*next;
};

#endif /* CONFIG_CONTEXT_SWITCH_TRACER */

#ifdef CONFIG_DYNAMIC_FTRACE
extern unsigned long ftrace_update_tot_cnt;
#define DYN_FTRACE_TEST_NAME trace_selftest_dynamic_test_func
extern int DYN_FTRACE_TEST_NAME(void);
#endif

#ifdef CONFIG_MMIOTRACE
extern void __trace_mmiotrace_rw(struct trace_array *tr,
				struct trace_array_cpu *data,
				struct mmiotrace_rw *rw);
extern void __trace_mmiotrace_map(struct trace_array *tr,
				struct trace_array_cpu *data,
				struct mmiotrace_map *map);
#endif

#ifdef CONFIG_FTRACE_STARTUP_TEST
#ifdef CONFIG_FTRACE
extern int trace_selftest_startup_function(struct tracer *trace,
					   struct trace_array *tr);
#endif
#ifdef CONFIG_IRQSOFF_TRACER
extern int trace_selftest_startup_irqsoff(struct tracer *trace,
					  struct trace_array *tr);
#endif
#ifdef CONFIG_PREEMPT_TRACER
extern int trace_selftest_startup_preemptoff(struct tracer *trace,
					     struct trace_array *tr);
#endif
#if defined(CONFIG_IRQSOFF_TRACER) && defined(CONFIG_PREEMPT_TRACER)
extern int trace_selftest_startup_preemptirqsoff(struct tracer *trace,
						 struct trace_array *tr);
#endif
#ifdef CONFIG_SCHED_TRACER
extern int trace_selftest_startup_wakeup(struct tracer *trace,
					 struct trace_array *tr);
#endif
#ifdef CONFIG_CONTEXT_SWITCH_TRACER
extern int trace_selftest_startup_sched_switch(struct tracer *trace,
					       struct trace_array *tr);
#endif
#ifdef CONFIG_SYSPROF_TRACER
extern int trace_selftest_startup_sysprof(struct tracer *trace,
					       struct trace_array *tr);
#endif
#endif /* CONFIG_FTRACE_STARTUP_TEST */

extern void *head_page(struct trace_array_cpu *data);
extern int trace_seq_printf(struct trace_seq *s, const char *fmt, ...);
extern ssize_t trace_seq_to_user(struct trace_seq *s, char __user *ubuf,
				 size_t cnt);
extern long ns2usecs(cycle_t nsec);

extern unsigned long trace_flags;

/*
 * trace_iterator_flags is an enumeration that defines bit
 * positions into trace_flags that controls the output.
 *
 * NOTE: These bits must match the trace_options array in
 *       trace.c.
 */
enum trace_iterator_flags {
	TRACE_ITER_PRINT_PARENT		= 0x01,
	TRACE_ITER_SYM_OFFSET		= 0x02,
	TRACE_ITER_SYM_ADDR		= 0x04,
	TRACE_ITER_VERBOSE		= 0x08,
	TRACE_ITER_RAW			= 0x10,
	TRACE_ITER_HEX			= 0x20,
	TRACE_ITER_BIN			= 0x40,
	TRACE_ITER_BLOCK		= 0x80,
	TRACE_ITER_STACKTRACE		= 0x100,
	TRACE_ITER_SCHED_TREE		= 0x200,
};

#endif /* _LINUX_KERNEL_TRACE_H */
