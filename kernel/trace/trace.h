#ifndef _LINUX_KERNEL_TRACE_H
#define _LINUX_KERNEL_TRACE_H

#include <linux/fs.h>
#include <asm/atomic.h>
#include <linux/sched.h>
#include <linux/clocksource.h>
#include <linux/ring_buffer.h>
#include <linux/mmiotrace.h>
#include <linux/ftrace.h>
#include <trace/boot.h>
#include <linux/kmemtrace.h>
#include <trace/power.h>

enum trace_type {
	__TRACE_FIRST_TYPE = 0,

	TRACE_FN,
	TRACE_CTX,
	TRACE_WAKE,
	TRACE_STACK,
	TRACE_PRINT,
	TRACE_BPRINT,
	TRACE_SPECIAL,
	TRACE_MMIO_RW,
	TRACE_MMIO_MAP,
	TRACE_BRANCH,
	TRACE_BOOT_CALL,
	TRACE_BOOT_RET,
	TRACE_GRAPH_RET,
	TRACE_GRAPH_ENT,
	TRACE_USER_STACK,
	TRACE_HW_BRANCHES,
	TRACE_SYSCALL_ENTER,
	TRACE_SYSCALL_EXIT,
	TRACE_KMEM_ALLOC,
	TRACE_KMEM_FREE,
	TRACE_POWER,
	TRACE_BLK,

	__TRACE_LAST_TYPE,
};

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
 * Function trace entry - function address and parent function addres:
 */
struct ftrace_entry {
	struct trace_entry	ent;
	unsigned long		ip;
	unsigned long		parent_ip;
};

/* Function call entry */
struct ftrace_graph_ent_entry {
	struct trace_entry		ent;
	struct ftrace_graph_ent		graph_ent;
};

/* Function return entry */
struct ftrace_graph_ret_entry {
	struct trace_entry		ent;
	struct ftrace_graph_ret		ret;
};
extern struct tracer boot_tracer;

/*
 * Context switch trace entry - which task (and prio) we switched from/to:
 */
struct ctx_switch_entry {
	struct trace_entry	ent;
	unsigned int		prev_pid;
	unsigned char		prev_prio;
	unsigned char		prev_state;
	unsigned int		next_pid;
	unsigned char		next_prio;
	unsigned char		next_state;
	unsigned int		next_cpu;
};

/*
 * Special (free-form) trace entry:
 */
struct special_entry {
	struct trace_entry	ent;
	unsigned long		arg1;
	unsigned long		arg2;
	unsigned long		arg3;
};

/*
 * Stack-trace entry:
 */

#define FTRACE_STACK_ENTRIES	8

struct stack_entry {
	struct trace_entry	ent;
	unsigned long		caller[FTRACE_STACK_ENTRIES];
};

struct userstack_entry {
	struct trace_entry	ent;
	unsigned long		caller[FTRACE_STACK_ENTRIES];
};

/*
 * trace_printk entry:
 */
struct bprint_entry {
	struct trace_entry	ent;
	unsigned long		ip;
	const char		*fmt;
	u32			buf[];
};

struct print_entry {
	struct trace_entry	ent;
	unsigned long		ip;
	char			buf[];
};

#define TRACE_OLD_SIZE		88

struct trace_field_cont {
	unsigned char		type;
	/* Temporary till we get rid of this completely */
	char			buf[TRACE_OLD_SIZE - 1];
};

struct trace_mmiotrace_rw {
	struct trace_entry	ent;
	struct mmiotrace_rw	rw;
};

struct trace_mmiotrace_map {
	struct trace_entry	ent;
	struct mmiotrace_map	map;
};

struct trace_boot_call {
	struct trace_entry	ent;
	struct boot_trace_call boot_call;
};

struct trace_boot_ret {
	struct trace_entry	ent;
	struct boot_trace_ret boot_ret;
};

#define TRACE_FUNC_SIZE 30
#define TRACE_FILE_SIZE 20
struct trace_branch {
	struct trace_entry	ent;
	unsigned	        line;
	char			func[TRACE_FUNC_SIZE+1];
	char			file[TRACE_FILE_SIZE+1];
	char			correct;
};

struct hw_branch_entry {
	struct trace_entry	ent;
	u64			from;
	u64			to;
};

struct trace_power {
	struct trace_entry	ent;
	struct power_trace	state_data;
};

enum kmemtrace_type_id {
	KMEMTRACE_TYPE_KMALLOC = 0,	/* kmalloc() or kfree(). */
	KMEMTRACE_TYPE_CACHE,		/* kmem_cache_*(). */
	KMEMTRACE_TYPE_PAGES,		/* __get_free_pages() and friends. */
};

struct kmemtrace_alloc_entry {
	struct trace_entry	ent;
	enum kmemtrace_type_id type_id;
	unsigned long call_site;
	const void *ptr;
	size_t bytes_req;
	size_t bytes_alloc;
	gfp_t gfp_flags;
	int node;
};

struct kmemtrace_free_entry {
	struct trace_entry	ent;
	enum kmemtrace_type_id type_id;
	unsigned long call_site;
	const void *ptr;
};

struct syscall_trace_enter {
	struct trace_entry	ent;
	int			nr;
	unsigned long		args[];
};

struct syscall_trace_exit {
	struct trace_entry	ent;
	int			nr;
	unsigned long		ret;
};


/*
 * trace_flag_type is an enumeration that holds different
 * states when a trace occurs. These are:
 *  IRQS_OFF		- interrupts were disabled
 *  IRQS_NOSUPPORT	- arch does not support irqs_disabled_flags
 *  NEED_RESCED		- reschedule is requested
 *  HARDIRQ		- inside an interrupt handler
 *  SOFTIRQ		- inside a softirq handler
 */
enum trace_flag_type {
	TRACE_FLAG_IRQS_OFF		= 0x01,
	TRACE_FLAG_IRQS_NOSUPPORT	= 0x02,
	TRACE_FLAG_NEED_RESCHED		= 0x04,
	TRACE_FLAG_HARDIRQ		= 0x08,
	TRACE_FLAG_SOFTIRQ		= 0x10,
};

#define TRACE_BUF_SIZE		1024

/*
 * The CPU trace array - it consists of thousands of trace entries
 * plus some other descriptor data: (for example which task started
 * the trace, etc.)
 */
struct trace_array_cpu {
	atomic_t		disabled;
	void			*buffer_page;	/* ring buffer spare */

	/* these fields get copied into max-trace: */
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
	struct ring_buffer	*buffer;
	unsigned long		entries;
	int			cpu;
	cycle_t			time_start;
	struct task_struct	*waiter;
	struct trace_array_cpu	*data[NR_CPUS];
};

#define FTRACE_CMP_TYPE(var, type) \
	__builtin_types_compatible_p(typeof(var), type *)

#undef IF_ASSIGN
#define IF_ASSIGN(var, entry, etype, id)		\
	if (FTRACE_CMP_TYPE(var, etype)) {		\
		var = (typeof(var))(entry);		\
		WARN_ON(id && (entry)->type != id);	\
		break;					\
	}

/* Will cause compile errors if type is not found. */
extern void __ftrace_bad_type(void);

/*
 * The trace_assign_type is a verifier that the entry type is
 * the same as the type being assigned. To add new types simply
 * add a line with the following format:
 *
 * IF_ASSIGN(var, ent, type, id);
 *
 *  Where "type" is the trace type that includes the trace_entry
 *  as the "ent" item. And "id" is the trace identifier that is
 *  used in the trace_type enum.
 *
 *  If the type can have more than one id, then use zero.
 */
#define trace_assign_type(var, ent)					\
	do {								\
		IF_ASSIGN(var, ent, struct ftrace_entry, TRACE_FN);	\
		IF_ASSIGN(var, ent, struct ctx_switch_entry, 0);	\
		IF_ASSIGN(var, ent, struct stack_entry, TRACE_STACK);	\
		IF_ASSIGN(var, ent, struct userstack_entry, TRACE_USER_STACK);\
		IF_ASSIGN(var, ent, struct print_entry, TRACE_PRINT);	\
		IF_ASSIGN(var, ent, struct bprint_entry, TRACE_BPRINT);	\
		IF_ASSIGN(var, ent, struct special_entry, 0);		\
		IF_ASSIGN(var, ent, struct trace_mmiotrace_rw,		\
			  TRACE_MMIO_RW);				\
		IF_ASSIGN(var, ent, struct trace_mmiotrace_map,		\
			  TRACE_MMIO_MAP);				\
		IF_ASSIGN(var, ent, struct trace_boot_call, TRACE_BOOT_CALL);\
		IF_ASSIGN(var, ent, struct trace_boot_ret, TRACE_BOOT_RET);\
		IF_ASSIGN(var, ent, struct trace_branch, TRACE_BRANCH); \
		IF_ASSIGN(var, ent, struct ftrace_graph_ent_entry,	\
			  TRACE_GRAPH_ENT);		\
		IF_ASSIGN(var, ent, struct ftrace_graph_ret_entry,	\
			  TRACE_GRAPH_RET);		\
		IF_ASSIGN(var, ent, struct hw_branch_entry, TRACE_HW_BRANCHES);\
		IF_ASSIGN(var, ent, struct trace_power, TRACE_POWER); \
		IF_ASSIGN(var, ent, struct kmemtrace_alloc_entry,	\
			  TRACE_KMEM_ALLOC);	\
		IF_ASSIGN(var, ent, struct kmemtrace_free_entry,	\
			  TRACE_KMEM_FREE);	\
		IF_ASSIGN(var, ent, struct syscall_trace_enter,		\
			  TRACE_SYSCALL_ENTER);				\
		IF_ASSIGN(var, ent, struct syscall_trace_exit,		\
			  TRACE_SYSCALL_EXIT);				\
		__ftrace_bad_type();					\
	} while (0)

/* Return values for print_line callback */
enum print_line_t {
	TRACE_TYPE_PARTIAL_LINE	= 0,	/* Retry after flushing the seq */
	TRACE_TYPE_HANDLED	= 1,
	TRACE_TYPE_UNHANDLED	= 2,	/* Relay to other output functions */
	TRACE_TYPE_NO_CONSUME	= 3	/* Handled but ask to not consume */
};


/*
 * An option specific to a tracer. This is a boolean value.
 * The bit is the bit index that sets its value on the
 * flags value in struct tracer_flags.
 */
struct tracer_opt {
	const char	*name; /* Will appear on the trace_options file */
	u32		bit; /* Mask assigned in val field in tracer_flags */
};

/*
 * The set of specific options for a tracer. Your tracer
 * have to set the initial value of the flags val.
 */
struct tracer_flags {
	u32			val;
	struct tracer_opt	*opts;
};

/* Makes more easy to define a tracer opt */
#define TRACER_OPT(s, b)	.name = #s, .bit = b


/**
 * struct tracer - a specific tracer and its callbacks to interact with debugfs
 * @name: the name chosen to select it on the available_tracers file
 * @init: called when one switches to this tracer (echo name > current_tracer)
 * @reset: called when one switches to another tracer
 * @start: called when tracing is unpaused (echo 1 > tracing_enabled)
 * @stop: called when tracing is paused (echo 0 > tracing_enabled)
 * @open: called when the trace file is opened
 * @pipe_open: called when the trace_pipe file is opened
 * @wait_pipe: override how the user waits for traces on trace_pipe
 * @close: called when the trace file is released
 * @read: override the default read callback on trace_pipe
 * @splice_read: override the default splice_read callback on trace_pipe
 * @selftest: selftest to run on boot (see trace_selftest.c)
 * @print_headers: override the first lines that describe your columns
 * @print_line: callback that prints a trace
 * @set_flag: signals one of your private flags changed (trace_options file)
 * @flags: your private flags
 */
struct tracer {
	const char		*name;
	int			(*init)(struct trace_array *tr);
	void			(*reset)(struct trace_array *tr);
	void			(*start)(struct trace_array *tr);
	void			(*stop)(struct trace_array *tr);
	void			(*open)(struct trace_iterator *iter);
	void			(*pipe_open)(struct trace_iterator *iter);
	void			(*wait_pipe)(struct trace_iterator *iter);
	void			(*close)(struct trace_iterator *iter);
	ssize_t			(*read)(struct trace_iterator *iter,
					struct file *filp, char __user *ubuf,
					size_t cnt, loff_t *ppos);
	ssize_t			(*splice_read)(struct trace_iterator *iter,
					       struct file *filp,
					       loff_t *ppos,
					       struct pipe_inode_info *pipe,
					       size_t len,
					       unsigned int flags);
#ifdef CONFIG_FTRACE_STARTUP_TEST
	int			(*selftest)(struct tracer *trace,
					    struct trace_array *tr);
#endif
	void			(*print_header)(struct seq_file *m);
	enum print_line_t	(*print_line)(struct trace_iterator *iter);
	/* If you handled the flag setting, return 0 */
	int			(*set_flag)(u32 old_flags, u32 bit, int set);
	struct tracer		*next;
	int			print_max;
	struct tracer_flags	*flags;
	struct tracer_stat	*stats;
};

struct trace_seq {
	unsigned char		buffer[PAGE_SIZE];
	unsigned int		len;
	unsigned int		readpos;
};

static inline void
trace_seq_init(struct trace_seq *s)
{
	s->len = 0;
	s->readpos = 0;
}


#define TRACE_PIPE_ALL_CPU	-1

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

int tracer_init(struct tracer *t, struct trace_array *tr);
int tracing_is_enabled(void);
void trace_wake_up(void);
void tracing_reset(struct trace_array *tr, int cpu);
void tracing_reset_online_cpus(struct trace_array *tr);
int tracing_open_generic(struct inode *inode, struct file *filp);
struct dentry *trace_create_file(const char *name,
				 mode_t mode,
				 struct dentry *parent,
				 void *data,
				 const struct file_operations *fops);

struct dentry *tracing_init_dentry(void);
void init_tracer_sysprof_debugfs(struct dentry *d_tracer);

struct ring_buffer_event;

struct ring_buffer_event *trace_buffer_lock_reserve(struct trace_array *tr,
						    unsigned char type,
						    unsigned long len,
						    unsigned long flags,
						    int pc);
void trace_buffer_unlock_commit(struct trace_array *tr,
				struct ring_buffer_event *event,
				unsigned long flags, int pc);

struct ring_buffer_event *
trace_current_buffer_lock_reserve(unsigned char type, unsigned long len,
				  unsigned long flags, int pc);
void trace_current_buffer_unlock_commit(struct ring_buffer_event *event,
					unsigned long flags, int pc);
void trace_nowake_buffer_unlock_commit(struct ring_buffer_event *event,
					unsigned long flags, int pc);
void trace_current_buffer_discard_commit(struct ring_buffer_event *event);

struct trace_entry *tracing_get_trace_entry(struct trace_array *tr,
						struct trace_array_cpu *data);

struct trace_entry *trace_find_next_entry(struct trace_iterator *iter,
					  int *ent_cpu, u64 *ent_ts);

void tracing_generic_entry_update(struct trace_entry *entry,
				  unsigned long flags,
				  int pc);

void default_wait_pipe(struct trace_iterator *iter);
void poll_wait_pipe(struct trace_iterator *iter);

void ftrace(struct trace_array *tr,
			    struct trace_array_cpu *data,
			    unsigned long ip,
			    unsigned long parent_ip,
			    unsigned long flags, int pc);
void tracing_sched_switch_trace(struct trace_array *tr,
				struct task_struct *prev,
				struct task_struct *next,
				unsigned long flags, int pc);
void tracing_record_cmdline(struct task_struct *tsk);

void tracing_sched_wakeup_trace(struct trace_array *tr,
				struct task_struct *wakee,
				struct task_struct *cur,
				unsigned long flags, int pc);
void trace_special(struct trace_array *tr,
		   struct trace_array_cpu *data,
		   unsigned long arg1,
		   unsigned long arg2,
		   unsigned long arg3, int pc);
void trace_function(struct trace_array *tr,
		    unsigned long ip,
		    unsigned long parent_ip,
		    unsigned long flags, int pc);

void trace_graph_return(struct ftrace_graph_ret *trace);
int trace_graph_entry(struct ftrace_graph_ent *trace);

void tracing_start_cmdline_record(void);
void tracing_stop_cmdline_record(void);
void tracing_sched_switch_assign_trace(struct trace_array *tr);
void tracing_stop_sched_switch_record(void);
void tracing_start_sched_switch_record(void);
int register_tracer(struct tracer *type);
void unregister_tracer(struct tracer *type);

extern unsigned long nsecs_to_usecs(unsigned long nsecs);

extern unsigned long tracing_max_latency;
extern unsigned long tracing_thresh;

void update_max_tr(struct trace_array *tr, struct task_struct *tsk, int cpu);
void update_max_tr_single(struct trace_array *tr,
			  struct task_struct *tsk, int cpu);

void __trace_stack(struct trace_array *tr,
		   unsigned long flags,
		   int skip, int pc);

extern cycle_t ftrace_now(int cpu);

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

extern void trace_find_cmdline(int pid, char comm[]);

#ifdef CONFIG_DYNAMIC_FTRACE
extern unsigned long ftrace_update_tot_cnt;
#define DYN_FTRACE_TEST_NAME trace_selftest_dynamic_test_func
extern int DYN_FTRACE_TEST_NAME(void);
#endif

#ifdef CONFIG_FTRACE_STARTUP_TEST
extern int trace_selftest_startup_function(struct tracer *trace,
					   struct trace_array *tr);
extern int trace_selftest_startup_function_graph(struct tracer *trace,
						 struct trace_array *tr);
extern int trace_selftest_startup_irqsoff(struct tracer *trace,
					  struct trace_array *tr);
extern int trace_selftest_startup_preemptoff(struct tracer *trace,
					     struct trace_array *tr);
extern int trace_selftest_startup_preemptirqsoff(struct tracer *trace,
						 struct trace_array *tr);
extern int trace_selftest_startup_wakeup(struct tracer *trace,
					 struct trace_array *tr);
extern int trace_selftest_startup_nop(struct tracer *trace,
					 struct trace_array *tr);
extern int trace_selftest_startup_sched_switch(struct tracer *trace,
					       struct trace_array *tr);
extern int trace_selftest_startup_sysprof(struct tracer *trace,
					       struct trace_array *tr);
extern int trace_selftest_startup_branch(struct tracer *trace,
					 struct trace_array *tr);
#endif /* CONFIG_FTRACE_STARTUP_TEST */

extern void *head_page(struct trace_array_cpu *data);
extern unsigned long long ns2usecs(cycle_t nsec);
extern int
trace_vbprintk(unsigned long ip, const char *fmt, va_list args);
extern int
trace_vprintk(unsigned long ip, const char *fmt, va_list args);

extern unsigned long trace_flags;

/* Standard output formatting function used for function return traces */
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
extern enum print_line_t print_graph_function(struct trace_iterator *iter);
extern enum print_line_t
trace_print_graph_duration(unsigned long long duration, struct trace_seq *s);

#ifdef CONFIG_DYNAMIC_FTRACE
/* TODO: make this variable */
#define FTRACE_GRAPH_MAX_FUNCS		32
extern int ftrace_graph_count;
extern unsigned long ftrace_graph_funcs[FTRACE_GRAPH_MAX_FUNCS];

static inline int ftrace_graph_addr(unsigned long addr)
{
	int i;

	if (!ftrace_graph_count || test_tsk_trace_graph(current))
		return 1;

	for (i = 0; i < ftrace_graph_count; i++) {
		if (addr == ftrace_graph_funcs[i])
			return 1;
	}

	return 0;
}
#else
static inline int ftrace_trace_addr(unsigned long addr)
{
	return 1;
}
static inline int ftrace_graph_addr(unsigned long addr)
{
	return 1;
}
#endif /* CONFIG_DYNAMIC_FTRACE */
#else /* CONFIG_FUNCTION_GRAPH_TRACER */
static inline enum print_line_t
print_graph_function(struct trace_iterator *iter)
{
	return TRACE_TYPE_UNHANDLED;
}
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */

extern struct pid *ftrace_pid_trace;

static inline int ftrace_trace_task(struct task_struct *task)
{
	if (!ftrace_pid_trace)
		return 1;

	return test_tsk_trace_trace(task);
}

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
	TRACE_ITER_PRINTK		= 0x400,
	TRACE_ITER_PREEMPTONLY		= 0x800,
	TRACE_ITER_BRANCH		= 0x1000,
	TRACE_ITER_ANNOTATE		= 0x2000,
	TRACE_ITER_USERSTACKTRACE       = 0x4000,
	TRACE_ITER_SYM_USEROBJ          = 0x8000,
	TRACE_ITER_PRINTK_MSGONLY	= 0x10000,
	TRACE_ITER_CONTEXT_INFO		= 0x20000, /* Print pid/cpu/time */
	TRACE_ITER_LATENCY_FMT		= 0x40000,
	TRACE_ITER_GLOBAL_CLK		= 0x80000,
	TRACE_ITER_SLEEP_TIME		= 0x100000,
	TRACE_ITER_GRAPH_TIME		= 0x200000,
};

/*
 * TRACE_ITER_SYM_MASK masks the options in trace_flags that
 * control the output of kernel symbols.
 */
#define TRACE_ITER_SYM_MASK \
	(TRACE_ITER_PRINT_PARENT|TRACE_ITER_SYM_OFFSET|TRACE_ITER_SYM_ADDR)

extern struct tracer nop_trace;

/**
 * ftrace_preempt_disable - disable preemption scheduler safe
 *
 * When tracing can happen inside the scheduler, there exists
 * cases that the tracing might happen before the need_resched
 * flag is checked. If this happens and the tracer calls
 * preempt_enable (after a disable), a schedule might take place
 * causing an infinite recursion.
 *
 * To prevent this, we read the need_resched flag before
 * disabling preemption. When we want to enable preemption we
 * check the flag, if it is set, then we call preempt_enable_no_resched.
 * Otherwise, we call preempt_enable.
 *
 * The rational for doing the above is that if need_resched is set
 * and we have yet to reschedule, we are either in an atomic location
 * (where we do not need to check for scheduling) or we are inside
 * the scheduler and do not want to resched.
 */
static inline int ftrace_preempt_disable(void)
{
	int resched;

	resched = need_resched();
	preempt_disable_notrace();

	return resched;
}

/**
 * ftrace_preempt_enable - enable preemption scheduler safe
 * @resched: the return value from ftrace_preempt_disable
 *
 * This is a scheduler safe way to enable preemption and not miss
 * any preemption checks. The disabled saved the state of preemption.
 * If resched is set, then we are either inside an atomic or
 * are inside the scheduler (we would have already scheduled
 * otherwise). In this case, we do not want to call normal
 * preempt_enable, but preempt_enable_no_resched instead.
 */
static inline void ftrace_preempt_enable(int resched)
{
	if (resched)
		preempt_enable_no_resched_notrace();
	else
		preempt_enable_notrace();
}

#ifdef CONFIG_BRANCH_TRACER
extern int enable_branch_tracing(struct trace_array *tr);
extern void disable_branch_tracing(void);
static inline int trace_branch_enable(struct trace_array *tr)
{
	if (trace_flags & TRACE_ITER_BRANCH)
		return enable_branch_tracing(tr);
	return 0;
}
static inline void trace_branch_disable(void)
{
	/* due to races, always disable */
	disable_branch_tracing();
}
#else
static inline int trace_branch_enable(struct trace_array *tr)
{
	return 0;
}
static inline void trace_branch_disable(void)
{
}
#endif /* CONFIG_BRANCH_TRACER */

/* set ring buffers to default size if not already done so */
int tracing_update_buffers(void);

/* trace event type bit fields, not numeric */
enum {
	TRACE_EVENT_TYPE_PRINTF		= 1,
	TRACE_EVENT_TYPE_RAW		= 2,
};

struct ftrace_event_field {
	struct list_head	link;
	char			*name;
	char			*type;
	int			offset;
	int			size;
};

struct ftrace_event_call {
	char			*name;
	char			*system;
	struct dentry		*dir;
	int			enabled;
	int			(*regfunc)(void);
	void			(*unregfunc)(void);
	int			id;
	int			(*raw_init)(void);
	int			(*show_format)(struct trace_seq *s);
	int			(*define_fields)(void);
	struct list_head	fields;
	struct filter_pred	**preds;

#ifdef CONFIG_EVENT_PROFILE
	atomic_t	profile_count;
	int		(*profile_enable)(struct ftrace_event_call *);
	void		(*profile_disable)(struct ftrace_event_call *);
#endif
};

struct event_subsystem {
	struct list_head	list;
	const char		*name;
	struct dentry		*entry;
	struct filter_pred	**preds;
};

#define events_for_each(event)						\
	for (event = __start_ftrace_events;				\
	     (unsigned long)event < (unsigned long)__stop_ftrace_events; \
	     event++)

#define MAX_FILTER_PRED 8

struct filter_pred;

typedef int (*filter_pred_fn_t) (struct filter_pred *pred, void *event);

struct filter_pred {
	filter_pred_fn_t fn;
	u64 val;
	char *str_val;
	int str_len;
	char *field_name;
	int offset;
	int not;
	int or;
	int compound;
	int clear;
};

int trace_define_field(struct ftrace_event_call *call, char *type,
		       char *name, int offset, int size);
extern void filter_free_pred(struct filter_pred *pred);
extern void filter_print_preds(struct filter_pred **preds,
			       struct trace_seq *s);
extern int filter_parse(char **pbuf, struct filter_pred *pred);
extern int filter_add_pred(struct ftrace_event_call *call,
			   struct filter_pred *pred);
extern void filter_free_preds(struct ftrace_event_call *call);
extern int filter_match_preds(struct ftrace_event_call *call, void *rec);
extern void filter_free_subsystem_preds(struct event_subsystem *system);
extern int filter_add_subsystem_pred(struct event_subsystem *system,
				     struct filter_pred *pred);

static inline void
filter_check_discard(struct ftrace_event_call *call, void *rec,
		     struct ring_buffer_event *event)
{
	if (unlikely(call->preds) && !filter_match_preds(call, rec))
		ring_buffer_event_discard(event);
}

#define __common_field(type, item)					\
	ret = trace_define_field(event_call, #type, "common_" #item,	\
				 offsetof(typeof(field.ent), item),	\
				 sizeof(field.ent.item));		\
	if (ret)							\
		return ret;

void event_trace_printk(unsigned long ip, const char *fmt, ...);
extern struct ftrace_event_call __start_ftrace_events[];
extern struct ftrace_event_call __stop_ftrace_events[];

#define for_each_event(event)						\
	for (event = __start_ftrace_events;				\
	     (unsigned long)event < (unsigned long)__stop_ftrace_events; \
	     event++)

extern const char *__start___trace_bprintk_fmt[];
extern const char *__stop___trace_bprintk_fmt[];

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

#undef TRACE_EVENT_FORMAT
#define TRACE_EVENT_FORMAT(call, proto, args, fmt, tstruct, tpfmt)	\
	extern struct ftrace_event_call event_##call;
#undef TRACE_EVENT_FORMAT_NOFILTER
#define TRACE_EVENT_FORMAT_NOFILTER(call, proto, args, fmt, tstruct, tpfmt)
#include "trace_event_types.h"

#endif /* _LINUX_KERNEL_TRACE_H */
