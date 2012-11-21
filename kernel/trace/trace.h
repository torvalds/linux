
#ifndef _LINUX_KERNEL_TRACE_H
#define _LINUX_KERNEL_TRACE_H

#include <linux/fs.h>
#include <linux/atomic.h>
#include <linux/sched.h>
#include <linux/clocksource.h>
#include <linux/ring_buffer.h>
#include <linux/mmiotrace.h>
#include <linux/tracepoint.h>
#include <linux/ftrace.h>
#include <linux/hw_breakpoint.h>
#include <linux/trace_seq.h>
#include <linux/trace_events.h>
#include <linux/compiler.h>
#include <linux/trace_seq.h>

#ifdef CONFIG_FTRACE_SYSCALLS
#include <asm/unistd.h>		/* For NR_SYSCALLS	     */
#include <asm/syscall.h>	/* some archs define it here */
#endif

enum trace_type {
	__TRACE_FIRST_TYPE = 0,

	TRACE_FN,
	TRACE_CTX,
	TRACE_WAKE,
	TRACE_STACK,
	TRACE_PRINT,
	TRACE_BPRINT,
	TRACE_MMIO_RW,
	TRACE_MMIO_MAP,
	TRACE_BRANCH,
	TRACE_GRAPH_RET,
	TRACE_GRAPH_ENT,
	TRACE_USER_STACK,
	TRACE_BLK,
	TRACE_BPUTS,
	TRACE_HWLAT,

	__TRACE_LAST_TYPE,
};


#undef __field
#define __field(type, item)		type	item;

#undef __field_struct
#define __field_struct(type, item)	__field(type, item)

#undef __field_desc
#define __field_desc(type, container, item)

#undef __array
#define __array(type, item, size)	type	item[size];

#undef __array_desc
#define __array_desc(type, container, item, size)

#undef __dynamic_array
#define __dynamic_array(type, item)	type	item[];

#undef F_STRUCT
#define F_STRUCT(args...)		args

#undef FTRACE_ENTRY
#define FTRACE_ENTRY(name, struct_name, id, tstruct, print, filter)	\
	struct struct_name {						\
		struct trace_entry	ent;				\
		tstruct							\
	}

#undef FTRACE_ENTRY_DUP
#define FTRACE_ENTRY_DUP(name, name_struct, id, tstruct, printk, filter)

#undef FTRACE_ENTRY_REG
#define FTRACE_ENTRY_REG(name, struct_name, id, tstruct, print,	\
			 filter, regfn) \
	FTRACE_ENTRY(name, struct_name, id, PARAMS(tstruct), PARAMS(print), \
		     filter)

#undef FTRACE_ENTRY_PACKED
#define FTRACE_ENTRY_PACKED(name, struct_name, id, tstruct, print,	\
			    filter)					\
	FTRACE_ENTRY(name, struct_name, id, PARAMS(tstruct), PARAMS(print), \
		     filter) __packed

#include "trace_entries.h"

/*
 * syscalls are special, and need special handling, this is why
 * they are not included in trace_entries.h
 */
struct syscall_trace_enter {
	struct trace_entry	ent;
	int			nr;
	unsigned long		args[];
};

struct syscall_trace_exit {
	struct trace_entry	ent;
	int			nr;
	long			ret;
};

struct kprobe_trace_entry_head {
	struct trace_entry	ent;
	unsigned long		ip;
};

struct kretprobe_trace_entry_head {
	struct trace_entry	ent;
	unsigned long		func;
	unsigned long		ret_ip;
};

/*
 * trace_flag_type is an enumeration that holds different
 * states when a trace occurs. These are:
 *  IRQS_OFF		- interrupts were disabled
 *  IRQS_NOSUPPORT	- arch does not support irqs_disabled_flags
 *  NEED_RESCHED	- reschedule is requested
 *  HARDIRQ		- inside an interrupt handler
 *  SOFTIRQ		- inside a softirq handler
 */
enum trace_flag_type {
	TRACE_FLAG_IRQS_OFF		= 0x01,
	TRACE_FLAG_IRQS_NOSUPPORT	= 0x02,
	TRACE_FLAG_NEED_RESCHED		= 0x04,
	TRACE_FLAG_HARDIRQ		= 0x08,
	TRACE_FLAG_SOFTIRQ		= 0x10,
	TRACE_FLAG_PREEMPT_RESCHED	= 0x20,
	TRACE_FLAG_NMI			= 0x40,
};

#define TRACE_BUF_SIZE		1024

struct trace_array;

/*
 * The CPU trace array - it consists of thousands of trace entries
 * plus some other descriptor data: (for example which task started
 * the trace, etc.)
 */
struct trace_array_cpu {
	atomic_t		disabled;
	void			*buffer_page;	/* ring buffer spare */

	unsigned long		entries;
	unsigned long		saved_latency;
	unsigned long		critical_start;
	unsigned long		critical_end;
	unsigned long		critical_sequence;
	unsigned long		nice;
	unsigned long		policy;
	unsigned long		rt_priority;
	unsigned long		skipped_entries;
	cycle_t			preempt_timestamp;
	pid_t			pid;
	kuid_t			uid;
	char			comm[TASK_COMM_LEN];

	bool			ignore_pid;
#ifdef CONFIG_FUNCTION_TRACER
	bool			ftrace_ignore_pid;
#endif
};

struct tracer;
struct trace_option_dentry;

struct trace_buffer {
	struct trace_array		*tr;
	struct ring_buffer		*buffer;
	struct trace_array_cpu __percpu	*data;
	cycle_t				time_start;
	int				cpu;
};

#define TRACE_FLAGS_MAX_SIZE		32

struct trace_options {
	struct tracer			*tracer;
	struct trace_option_dentry	*topts;
};

struct trace_pid_list {
	int				pid_max;
	unsigned long			*pids;
};

/*
 * The trace array - an array of per-CPU trace arrays. This is the
 * highest level data structure that individual tracers deal with.
 * They have on/off state as well:
 */
struct trace_array {
	struct list_head	list;
	char			*name;
	struct trace_buffer	trace_buffer;
#ifdef CONFIG_TRACER_MAX_TRACE
	/*
	 * The max_buffer is used to snapshot the trace when a maximum
	 * latency is reached, or when the user initiates a snapshot.
	 * Some tracers will use this to store a maximum trace while
	 * it continues examining live traces.
	 *
	 * The buffers for the max_buffer are set up the same as the trace_buffer
	 * When a snapshot is taken, the buffer of the max_buffer is swapped
	 * with the buffer of the trace_buffer and the buffers are reset for
	 * the trace_buffer so the tracing can continue.
	 */
	struct trace_buffer	max_buffer;
	bool			allocated_snapshot;
#endif
#if defined(CONFIG_TRACER_MAX_TRACE) || defined(CONFIG_HWLAT_TRACER)
	unsigned long		max_latency;
#endif
	struct trace_pid_list	__rcu *filtered_pids;
	/*
	 * max_lock is used to protect the swapping of buffers
	 * when taking a max snapshot. The buffers themselves are
	 * protected by per_cpu spinlocks. But the action of the swap
	 * needs its own lock.
	 *
	 * This is defined as a arch_spinlock_t in order to help
	 * with performance when lockdep debugging is enabled.
	 *
	 * It is also used in other places outside the update_max_tr
	 * so it needs to be defined outside of the
	 * CONFIG_TRACER_MAX_TRACE.
	 */
	arch_spinlock_t		max_lock;
	int			buffer_disabled;
#ifdef CONFIG_FTRACE_SYSCALLS
	int			sys_refcount_enter;
	int			sys_refcount_exit;
	struct trace_event_file __rcu *enter_syscall_files[NR_syscalls];
	struct trace_event_file __rcu *exit_syscall_files[NR_syscalls];
#endif
	int			stop_count;
	int			clock_id;
	int			nr_topts;
	struct tracer		*current_trace;
	unsigned int		trace_flags;
	unsigned char		trace_flags_index[TRACE_FLAGS_MAX_SIZE];
	unsigned int		flags;
	raw_spinlock_t		start_lock;
	struct dentry		*dir;
	struct dentry		*options;
	struct dentry		*percpu_dir;
	struct dentry		*event_dir;
	struct trace_options	*topts;
	struct list_head	systems;
	struct list_head	events;
	cpumask_var_t		tracing_cpumask; /* only trace on set CPUs */
	int			ref;
#ifdef CONFIG_FUNCTION_TRACER
	struct ftrace_ops	*ops;
	struct trace_pid_list	__rcu *function_pids;
	/* function tracing enabled */
	int			function_enabled;
#endif
};

enum {
	TRACE_ARRAY_FL_GLOBAL	= (1 << 0)
};

extern struct list_head ftrace_trace_arrays;

extern struct mutex trace_types_lock;

extern int trace_array_get(struct trace_array *tr);
extern void trace_array_put(struct trace_array *tr);

/*
 * The global tracer (top) should be the first trace array added,
 * but we check the flag anyway.
 */
static inline struct trace_array *top_trace_array(void)
{
	struct trace_array *tr;

	if (list_empty(&ftrace_trace_arrays))
		return NULL;

	tr = list_entry(ftrace_trace_arrays.prev,
			typeof(*tr), list);
	WARN_ON(!(tr->flags & TRACE_ARRAY_FL_GLOBAL));
	return tr;
}

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
		IF_ASSIGN(var, ent, struct bputs_entry, TRACE_BPUTS);	\
		IF_ASSIGN(var, ent, struct hwlat_entry, TRACE_HWLAT);	\
		IF_ASSIGN(var, ent, struct trace_mmiotrace_rw,		\
			  TRACE_MMIO_RW);				\
		IF_ASSIGN(var, ent, struct trace_mmiotrace_map,		\
			  TRACE_MMIO_MAP);				\
		IF_ASSIGN(var, ent, struct trace_branch, TRACE_BRANCH); \
		IF_ASSIGN(var, ent, struct ftrace_graph_ent_entry,	\
			  TRACE_GRAPH_ENT);		\
		IF_ASSIGN(var, ent, struct ftrace_graph_ret_entry,	\
			  TRACE_GRAPH_RET);		\
		__ftrace_bad_type();					\
	} while (0)

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
	struct tracer		*trace;
};

/* Makes more easy to define a tracer opt */
#define TRACER_OPT(s, b)	.name = #s, .bit = b


struct trace_option_dentry {
	struct tracer_opt		*opt;
	struct tracer_flags		*flags;
	struct trace_array		*tr;
	struct dentry			*entry;
};

/**
 * struct tracer - a specific tracer and its callbacks to interact with tracefs
 * @name: the name chosen to select it on the available_tracers file
 * @init: called when one switches to this tracer (echo name > current_tracer)
 * @reset: called when one switches to another tracer
 * @start: called when tracing is unpaused (echo 1 > tracing_on)
 * @stop: called when tracing is paused (echo 0 > tracing_on)
 * @update_thresh: called when tracing_thresh is updated
 * @open: called when the trace file is opened
 * @pipe_open: called when the trace_pipe file is opened
 * @close: called when the trace file is released
 * @pipe_close: called when the trace_pipe file is released
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
	int			(*update_thresh)(struct trace_array *tr);
	void			(*open)(struct trace_iterator *iter);
	void			(*pipe_open)(struct trace_iterator *iter);
	void			(*close)(struct trace_iterator *iter);
	void			(*pipe_close)(struct trace_iterator *iter);
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
	int			(*set_flag)(struct trace_array *tr,
					    u32 old_flags, u32 bit, int set);
	/* Return 0 if OK with change, else return non-zero */
	int			(*flag_changed)(struct trace_array *tr,
						u32 mask, int set);
	struct tracer		*next;
	struct tracer_flags	*flags;
	int			enabled;
	int			ref;
	bool			print_max;
	bool			allow_instances;
#ifdef CONFIG_TRACER_MAX_TRACE
	bool			use_max_tr;
#endif
};


/* Only current can touch trace_recursion */

/*
 * For function tracing recursion:
 *  The order of these bits are important.
 *
 *  When function tracing occurs, the following steps are made:
 *   If arch does not support a ftrace feature:
 *    call internal function (uses INTERNAL bits) which calls...
 *   If callback is registered to the "global" list, the list
 *    function is called and recursion checks the GLOBAL bits.
 *    then this function calls...
 *   The function callback, which can use the FTRACE bits to
 *    check for recursion.
 *
 * Now if the arch does not suppport a feature, and it calls
 * the global list function which calls the ftrace callback
 * all three of these steps will do a recursion protection.
 * There's no reason to do one if the previous caller already
 * did. The recursion that we are protecting against will
 * go through the same steps again.
 *
 * To prevent the multiple recursion checks, if a recursion
 * bit is set that is higher than the MAX bit of the current
 * check, then we know that the check was made by the previous
 * caller, and we can skip the current check.
 */
enum {
	TRACE_BUFFER_BIT,
	TRACE_BUFFER_NMI_BIT,
	TRACE_BUFFER_IRQ_BIT,
	TRACE_BUFFER_SIRQ_BIT,

	/* Start of function recursion bits */
	TRACE_FTRACE_BIT,
	TRACE_FTRACE_NMI_BIT,
	TRACE_FTRACE_IRQ_BIT,
	TRACE_FTRACE_SIRQ_BIT,

	/* INTERNAL_BITs must be greater than FTRACE_BITs */
	TRACE_INTERNAL_BIT,
	TRACE_INTERNAL_NMI_BIT,
	TRACE_INTERNAL_IRQ_BIT,
	TRACE_INTERNAL_SIRQ_BIT,

	TRACE_BRANCH_BIT,
/*
 * Abuse of the trace_recursion.
 * As we need a way to maintain state if we are tracing the function
 * graph in irq because we want to trace a particular function that
 * was called in irq context but we have irq tracing off. Since this
 * can only be modified by current, we can reuse trace_recursion.
 */
	TRACE_IRQ_BIT,
};

#define trace_recursion_set(bit)	do { (current)->trace_recursion |= (1<<(bit)); } while (0)
#define trace_recursion_clear(bit)	do { (current)->trace_recursion &= ~(1<<(bit)); } while (0)
#define trace_recursion_test(bit)	((current)->trace_recursion & (1<<(bit)))

#define TRACE_CONTEXT_BITS	4

#define TRACE_FTRACE_START	TRACE_FTRACE_BIT
#define TRACE_FTRACE_MAX	((1 << (TRACE_FTRACE_START + TRACE_CONTEXT_BITS)) - 1)

#define TRACE_LIST_START	TRACE_INTERNAL_BIT
#define TRACE_LIST_MAX		((1 << (TRACE_LIST_START + TRACE_CONTEXT_BITS)) - 1)

#define TRACE_CONTEXT_MASK	TRACE_LIST_MAX

static __always_inline int trace_get_context_bit(void)
{
	int bit;

	if (in_interrupt()) {
		if (in_nmi())
			bit = 0;

		else if (in_irq())
			bit = 1;
		else
			bit = 2;
	} else
		bit = 3;

	return bit;
}

static __always_inline int trace_test_and_set_recursion(int start, int max)
{
	unsigned int val = current->trace_recursion;
	int bit;

	/* A previous recursion check was made */
	if ((val & TRACE_CONTEXT_MASK) > max)
		return 0;

	bit = trace_get_context_bit() + start;
	if (unlikely(val & (1 << bit)))
		return -1;

	val |= 1 << bit;
	current->trace_recursion = val;
	barrier();

	return bit;
}

static __always_inline void trace_clear_recursion(int bit)
{
	unsigned int val = current->trace_recursion;

	if (!bit)
		return;

	bit = 1 << bit;
	val &= ~bit;

	barrier();
	current->trace_recursion = val;
}

static inline struct ring_buffer_iter *
trace_buffer_iter(struct trace_iterator *iter, int cpu)
{
	if (iter->buffer_iter && iter->buffer_iter[cpu])
		return iter->buffer_iter[cpu];
	return NULL;
}

int tracer_init(struct tracer *t, struct trace_array *tr);
int tracing_is_enabled(void);
void tracing_reset(struct trace_buffer *buf, int cpu);
void tracing_reset_online_cpus(struct trace_buffer *buf);
void tracing_reset_current(int cpu);
void tracing_reset_all_online_cpus(void);
int tracing_open_generic(struct inode *inode, struct file *filp);
bool tracing_is_disabled(void);
int tracer_tracing_is_on(struct trace_array *tr);
struct dentry *trace_create_file(const char *name,
				 umode_t mode,
				 struct dentry *parent,
				 void *data,
				 const struct file_operations *fops);

struct dentry *tracing_init_dentry(void);

struct ring_buffer_event;

struct ring_buffer_event *
trace_buffer_lock_reserve(struct ring_buffer *buffer,
			  int type,
			  unsigned long len,
			  unsigned long flags,
			  int pc);

struct trace_entry *tracing_get_trace_entry(struct trace_array *tr,
						struct trace_array_cpu *data);

struct trace_entry *trace_find_next_entry(struct trace_iterator *iter,
					  int *ent_cpu, u64 *ent_ts);

void __buffer_unlock_commit(struct ring_buffer *buffer,
			    struct ring_buffer_event *event);

int trace_empty(struct trace_iterator *iter);

void *trace_find_next_entry_inc(struct trace_iterator *iter);

void trace_init_global_iter(struct trace_iterator *iter);

void tracing_iter_reset(struct trace_iterator *iter, int cpu);

void trace_function(struct trace_array *tr,
		    unsigned long ip,
		    unsigned long parent_ip,
		    unsigned long flags, int pc);
void trace_graph_function(struct trace_array *tr,
		    unsigned long ip,
		    unsigned long parent_ip,
		    unsigned long flags, int pc);
void trace_latency_header(struct seq_file *m);
void trace_default_header(struct seq_file *m);
void print_trace_header(struct seq_file *m, struct trace_iterator *iter);
int trace_empty(struct trace_iterator *iter);

void trace_graph_return(struct ftrace_graph_ret *trace);
int trace_graph_entry(struct ftrace_graph_ent *trace);
void set_graph_array(struct trace_array *tr);

void tracing_start_cmdline_record(void);
void tracing_stop_cmdline_record(void);
int register_tracer(struct tracer *type);
int is_tracing_stopped(void);

loff_t tracing_lseek(struct file *file, loff_t offset, int whence);

extern cpumask_var_t __read_mostly tracing_buffer_mask;

#define for_each_tracing_cpu(cpu)	\
	for_each_cpu(cpu, tracing_buffer_mask)

extern unsigned long nsecs_to_usecs(unsigned long nsecs);

extern unsigned long tracing_thresh;

/* PID filtering */

extern int pid_max;

bool trace_find_filtered_pid(struct trace_pid_list *filtered_pids,
			     pid_t search_pid);
bool trace_ignore_this_task(struct trace_pid_list *filtered_pids,
			    struct task_struct *task);
void trace_filter_add_remove_task(struct trace_pid_list *pid_list,
				  struct task_struct *self,
				  struct task_struct *task);
void *trace_pid_next(struct trace_pid_list *pid_list, void *v, loff_t *pos);
void *trace_pid_start(struct trace_pid_list *pid_list, loff_t *pos);
int trace_pid_show(struct seq_file *m, void *v);
void trace_free_pid_list(struct trace_pid_list *pid_list);
int trace_pid_write(struct trace_pid_list *filtered_pids,
		    struct trace_pid_list **new_pid_list,
		    const char __user *ubuf, size_t cnt);

#ifdef CONFIG_TRACER_MAX_TRACE
void update_max_tr(struct trace_array *tr, struct task_struct *tsk, int cpu);
void update_max_tr_single(struct trace_array *tr,
			  struct task_struct *tsk, int cpu);
#endif /* CONFIG_TRACER_MAX_TRACE */

#ifdef CONFIG_STACKTRACE
void ftrace_trace_userstack(struct ring_buffer *buffer, unsigned long flags,
			    int pc);

void __trace_stack(struct trace_array *tr, unsigned long flags, int skip,
		   int pc);
#else
static inline void ftrace_trace_userstack(struct ring_buffer *buffer,
					  unsigned long flags, int pc)
{
}

static inline void __trace_stack(struct trace_array *tr, unsigned long flags,
				 int skip, int pc)
{
}
#endif /* CONFIG_STACKTRACE */

extern cycle_t ftrace_now(int cpu);

extern void trace_find_cmdline(int pid, char comm[]);
extern void trace_event_follow_fork(struct trace_array *tr, bool enable);
extern int trace_find_tgid(int pid);

#ifdef CONFIG_DYNAMIC_FTRACE
extern unsigned long ftrace_update_tot_cnt;
#endif
#define DYN_FTRACE_TEST_NAME trace_selftest_dynamic_test_func
extern int DYN_FTRACE_TEST_NAME(void);
#define DYN_FTRACE_TEST_NAME2 trace_selftest_dynamic_test_func2
extern int DYN_FTRACE_TEST_NAME2(void);

extern bool ring_buffer_expanded;
extern bool tracing_selftest_disabled;

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
extern int trace_selftest_startup_branch(struct tracer *trace,
					 struct trace_array *tr);
/*
 * Tracer data references selftest functions that only occur
 * on boot up. These can be __init functions. Thus, when selftests
 * are enabled, then the tracers need to reference __init functions.
 */
#define __tracer_data		__refdata
#else
/* Tracers are seldom changed. Optimize when selftests are disabled. */
#define __tracer_data		__read_mostly
#endif /* CONFIG_FTRACE_STARTUP_TEST */

extern void *head_page(struct trace_array_cpu *data);
extern unsigned long long ns2usecs(cycle_t nsec);
extern int
trace_vbprintk(unsigned long ip, const char *fmt, va_list args);
extern int
trace_vprintk(unsigned long ip, const char *fmt, va_list args);
extern int
trace_array_vprintk(struct trace_array *tr,
		    unsigned long ip, const char *fmt, va_list args);
int trace_array_printk(struct trace_array *tr,
		       unsigned long ip, const char *fmt, ...);
int trace_array_printk_buf(struct ring_buffer *buffer,
			   unsigned long ip, const char *fmt, ...);
void trace_printk_seq(struct trace_seq *s);
enum print_line_t print_trace_line(struct trace_iterator *iter);

extern char trace_find_mark(unsigned long long duration);

/* Standard output formatting function used for function return traces */
#ifdef CONFIG_FUNCTION_GRAPH_TRACER

/* Flag options */
#define TRACE_GRAPH_PRINT_OVERRUN       0x1
#define TRACE_GRAPH_PRINT_CPU           0x2
#define TRACE_GRAPH_PRINT_OVERHEAD      0x4
#define TRACE_GRAPH_PRINT_PROC          0x8
#define TRACE_GRAPH_PRINT_DURATION      0x10
#define TRACE_GRAPH_PRINT_ABS_TIME      0x20
#define TRACE_GRAPH_PRINT_IRQS          0x40
#define TRACE_GRAPH_PRINT_TAIL          0x80
#define TRACE_GRAPH_SLEEP_TIME		0x100
#define TRACE_GRAPH_GRAPH_TIME		0x200
#define TRACE_GRAPH_PRINT_FILL_SHIFT	28
#define TRACE_GRAPH_PRINT_FILL_MASK	(0x3 << TRACE_GRAPH_PRINT_FILL_SHIFT)

extern void ftrace_graph_sleep_time_control(bool enable);
extern void ftrace_graph_graph_time_control(bool enable);

extern enum print_line_t
print_graph_function_flags(struct trace_iterator *iter, u32 flags);
extern void print_graph_headers_flags(struct seq_file *s, u32 flags);
extern void
trace_print_graph_duration(unsigned long long duration, struct trace_seq *s);
extern void graph_trace_open(struct trace_iterator *iter);
extern void graph_trace_close(struct trace_iterator *iter);
extern int __trace_graph_entry(struct trace_array *tr,
			       struct ftrace_graph_ent *trace,
			       unsigned long flags, int pc);
extern void __trace_graph_return(struct trace_array *tr,
				 struct ftrace_graph_ret *trace,
				 unsigned long flags, int pc);


#ifdef CONFIG_DYNAMIC_FTRACE
/* TODO: make this variable */
#define FTRACE_GRAPH_MAX_FUNCS		32
extern int ftrace_graph_count;
extern unsigned long ftrace_graph_funcs[FTRACE_GRAPH_MAX_FUNCS];
extern int ftrace_graph_notrace_count;
extern unsigned long ftrace_graph_notrace_funcs[FTRACE_GRAPH_MAX_FUNCS];

static inline int ftrace_graph_addr(unsigned long addr)
{
	int i;

	if (!ftrace_graph_count)
		return 1;

	for (i = 0; i < ftrace_graph_count; i++) {
		if (addr == ftrace_graph_funcs[i]) {
			/*
			 * If no irqs are to be traced, but a set_graph_function
			 * is set, and called by an interrupt handler, we still
			 * want to trace it.
			 */
			if (in_irq())
				trace_recursion_set(TRACE_IRQ_BIT);
			else
				trace_recursion_clear(TRACE_IRQ_BIT);
			return 1;
		}
	}

	return 0;
}

static inline int ftrace_graph_notrace_addr(unsigned long addr)
{
	int i;

	if (!ftrace_graph_notrace_count)
		return 0;

	for (i = 0; i < ftrace_graph_notrace_count; i++) {
		if (addr == ftrace_graph_notrace_funcs[i])
			return 1;
	}

	return 0;
}
#else
static inline int ftrace_graph_addr(unsigned long addr)
{
	return 1;
}

static inline int ftrace_graph_notrace_addr(unsigned long addr)
{
	return 0;
}
#endif /* CONFIG_DYNAMIC_FTRACE */
#else /* CONFIG_FUNCTION_GRAPH_TRACER */
static inline enum print_line_t
print_graph_function_flags(struct trace_iterator *iter, u32 flags)
{
	return TRACE_TYPE_UNHANDLED;
}
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */

extern struct list_head ftrace_pids;

#ifdef CONFIG_FUNCTION_TRACER
extern bool ftrace_filter_param __initdata;
static inline int ftrace_trace_task(struct trace_array *tr)
{
	return !this_cpu_read(tr->trace_buffer.data->ftrace_ignore_pid);
}
extern int ftrace_is_dead(void);
int ftrace_create_function_files(struct trace_array *tr,
				 struct dentry *parent);
void ftrace_destroy_function_files(struct trace_array *tr);
void ftrace_init_global_array_ops(struct trace_array *tr);
void ftrace_init_array_ops(struct trace_array *tr, ftrace_func_t func);
void ftrace_reset_array_ops(struct trace_array *tr);
int using_ftrace_ops_list_func(void);
void ftrace_init_tracefs(struct trace_array *tr, struct dentry *d_tracer);
void ftrace_init_tracefs_toplevel(struct trace_array *tr,
				  struct dentry *d_tracer);
#else
static inline int ftrace_trace_task(struct trace_array *tr)
{
	return 1;
}
static inline int ftrace_is_dead(void) { return 0; }
static inline int
ftrace_create_function_files(struct trace_array *tr,
			     struct dentry *parent)
{
	return 0;
}
static inline void ftrace_destroy_function_files(struct trace_array *tr) { }
static inline __init void
ftrace_init_global_array_ops(struct trace_array *tr) { }
static inline void ftrace_reset_array_ops(struct trace_array *tr) { }
static inline void ftrace_init_tracefs(struct trace_array *tr, struct dentry *d) { }
static inline void ftrace_init_tracefs_toplevel(struct trace_array *tr, struct dentry *d) { }
/* ftace_func_t type is not defined, use macro instead of static inline */
#define ftrace_init_array_ops(tr, func) do { } while (0)
#endif /* CONFIG_FUNCTION_TRACER */

#if defined(CONFIG_FUNCTION_TRACER) && defined(CONFIG_DYNAMIC_FTRACE)
void ftrace_create_filter_files(struct ftrace_ops *ops,
				struct dentry *parent);
void ftrace_destroy_filter_files(struct ftrace_ops *ops);
#else
/*
 * The ops parameter passed in is usually undefined.
 * This must be a macro.
 */
#define ftrace_create_filter_files(ops, parent) do { } while (0)
#define ftrace_destroy_filter_files(ops) do { } while (0)
#endif /* CONFIG_FUNCTION_TRACER && CONFIG_DYNAMIC_FTRACE */

bool ftrace_event_is_function(struct trace_event_call *call);

/*
 * struct trace_parser - servers for reading the user input separated by spaces
 * @cont: set if the input is not complete - no final space char was found
 * @buffer: holds the parsed user input
 * @idx: user input length
 * @size: buffer size
 */
struct trace_parser {
	bool		cont;
	char		*buffer;
	unsigned	idx;
	unsigned	size;
};

static inline bool trace_parser_loaded(struct trace_parser *parser)
{
	return (parser->idx != 0);
}

static inline bool trace_parser_cont(struct trace_parser *parser)
{
	return parser->cont;
}

static inline void trace_parser_clear(struct trace_parser *parser)
{
	parser->cont = false;
	parser->idx = 0;
}

extern int trace_parser_get_init(struct trace_parser *parser, int size);
extern void trace_parser_put(struct trace_parser *parser);
extern int trace_get_user(struct trace_parser *parser, const char __user *ubuf,
	size_t cnt, loff_t *ppos);

/*
 * Only create function graph options if function graph is configured.
 */
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
# define FGRAPH_FLAGS						\
		C(DISPLAY_GRAPH,	"display-graph"),
#else
# define FGRAPH_FLAGS
#endif

#ifdef CONFIG_BRANCH_TRACER
# define BRANCH_FLAGS					\
		C(BRANCH,		"branch"),
#else
# define BRANCH_FLAGS
#endif

#ifdef CONFIG_FUNCTION_TRACER
# define FUNCTION_FLAGS						\
		C(FUNCTION,		"function-trace"),
# define FUNCTION_DEFAULT_FLAGS		TRACE_ITER_FUNCTION
#else
# define FUNCTION_FLAGS
# define FUNCTION_DEFAULT_FLAGS		0UL
#endif

#ifdef CONFIG_STACKTRACE
# define STACK_FLAGS				\
		C(STACKTRACE,		"stacktrace"),
#else
# define STACK_FLAGS
#endif

/*
 * trace_iterator_flags is an enumeration that defines bit
 * positions into trace_flags that controls the output.
 *
 * NOTE: These bits must match the trace_options array in
 *       trace.c (this macro guarantees it).
 */
#define TRACE_FLAGS						\
		C(PRINT_PARENT,		"print-parent"),	\
		C(SYM_OFFSET,		"sym-offset"),		\
		C(SYM_ADDR,		"sym-addr"),		\
		C(VERBOSE,		"verbose"),		\
		C(RAW,			"raw"),			\
		C(HEX,			"hex"),			\
		C(BIN,			"bin"),			\
		C(BLOCK,		"block"),		\
		C(PRINTK,		"trace_printk"),	\
		C(ANNOTATE,		"annotate"),		\
		C(USERSTACKTRACE,	"userstacktrace"),	\
		C(SYM_USEROBJ,		"sym-userobj"),		\
		C(PRINTK_MSGONLY,	"printk-msg-only"),	\
		C(CONTEXT_INFO,		"context-info"),   /* Print pid/cpu/time */ \
		C(LATENCY_FMT,		"latency-format"),	\
		C(RECORD_CMD,		"record-cmd"),		\
		C(OVERWRITE,		"overwrite"),		\
		C(STOP_ON_FREE,		"disable_on_free"),	\
		C(IRQ_INFO,		"irq-info"),		\
		C(MARKERS,		"markers"),		\
		C(EVENT_FORK,		"event-fork"),		\
		FUNCTION_FLAGS					\
		FGRAPH_FLAGS					\
		STACK_FLAGS					\
		BRANCH_FLAGS					\
		C(TGID,			"print-tgid"),

/*
 * By defining C, we can make TRACE_FLAGS a list of bit names
 * that will define the bits for the flag masks.
 */
#undef C
#define C(a, b) TRACE_ITER_##a##_BIT

enum trace_iterator_bits {
	TRACE_FLAGS
	/* Make sure we don't go more than we have bits for */
	TRACE_ITER_LAST_BIT
};

/*
 * By redefining C, we can make TRACE_FLAGS a list of masks that
 * use the bits as defined above.
 */
#undef C
#define C(a, b) TRACE_ITER_##a = (1 << TRACE_ITER_##a##_BIT)

enum trace_iterator_flags { TRACE_FLAGS };

/*
 * TRACE_ITER_SYM_MASK masks the options in trace_flags that
 * control the output of kernel symbols.
 */
#define TRACE_ITER_SYM_MASK \
	(TRACE_ITER_PRINT_PARENT|TRACE_ITER_SYM_OFFSET|TRACE_ITER_SYM_ADDR)

extern struct tracer nop_trace;

#ifdef CONFIG_BRANCH_TRACER
extern int enable_branch_tracing(struct trace_array *tr);
extern void disable_branch_tracing(void);
static inline int trace_branch_enable(struct trace_array *tr)
{
	if (tr->trace_flags & TRACE_ITER_BRANCH)
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

struct ftrace_event_field {
	struct list_head	link;
	const char		*name;
	const char		*type;
	int			filter_type;
	int			offset;
	int			size;
	int			is_signed;
};

struct event_filter {
	int			n_preds;	/* Number assigned */
	int			a_preds;	/* allocated */
	struct filter_pred	*preds;
	struct filter_pred	*root;
	char			*filter_string;
};

struct event_subsystem {
	struct list_head	list;
	const char		*name;
	struct event_filter	*filter;
	int			ref_count;
};

struct trace_subsystem_dir {
	struct list_head		list;
	struct event_subsystem		*subsystem;
	struct trace_array		*tr;
	struct dentry			*entry;
	int				ref_count;
	int				nr_events;
};

extern int call_filter_check_discard(struct trace_event_call *call, void *rec,
				     struct ring_buffer *buffer,
				     struct ring_buffer_event *event);

void trace_buffer_unlock_commit_regs(struct trace_array *tr,
				     struct ring_buffer *buffer,
				     struct ring_buffer_event *event,
				     unsigned long flags, int pc,
				     struct pt_regs *regs);

static inline void trace_buffer_unlock_commit(struct trace_array *tr,
					      struct ring_buffer *buffer,
					      struct ring_buffer_event *event,
					      unsigned long flags, int pc)
{
	trace_buffer_unlock_commit_regs(tr, buffer, event, flags, pc, NULL);
}

DECLARE_PER_CPU(struct ring_buffer_event *, trace_buffered_event);
DECLARE_PER_CPU(int, trace_buffered_event_cnt);
void trace_buffered_event_disable(void);
void trace_buffered_event_enable(void);

static inline void
__trace_event_discard_commit(struct ring_buffer *buffer,
			     struct ring_buffer_event *event)
{
	if (this_cpu_read(trace_buffered_event) == event) {
		/* Simply release the temp buffer */
		this_cpu_dec(trace_buffered_event_cnt);
		return;
	}
	ring_buffer_discard_commit(buffer, event);
}

/*
 * Helper function for event_trigger_unlock_commit{_regs}().
 * If there are event triggers attached to this event that requires
 * filtering against its fields, then they wil be called as the
 * entry already holds the field information of the current event.
 *
 * It also checks if the event should be discarded or not.
 * It is to be discarded if the event is soft disabled and the
 * event was only recorded to process triggers, or if the event
 * filter is active and this event did not match the filters.
 *
 * Returns true if the event is discarded, false otherwise.
 */
static inline bool
__event_trigger_test_discard(struct trace_event_file *file,
			     struct ring_buffer *buffer,
			     struct ring_buffer_event *event,
			     void *entry,
			     enum event_trigger_type *tt)
{
	unsigned long eflags = file->flags;

	if (eflags & EVENT_FILE_FL_TRIGGER_COND)
		*tt = event_triggers_call(file, entry);

	if (test_bit(EVENT_FILE_FL_SOFT_DISABLED_BIT, &file->flags) ||
	    (unlikely(file->flags & EVENT_FILE_FL_FILTERED) &&
	     !filter_match_preds(file->filter, entry))) {
		__trace_event_discard_commit(buffer, event);
		return true;
	}

	return false;
}

/**
 * event_trigger_unlock_commit - handle triggers and finish event commit
 * @file: The file pointer assoctiated to the event
 * @buffer: The ring buffer that the event is being written to
 * @event: The event meta data in the ring buffer
 * @entry: The event itself
 * @irq_flags: The state of the interrupts at the start of the event
 * @pc: The state of the preempt count at the start of the event.
 *
 * This is a helper function to handle triggers that require data
 * from the event itself. It also tests the event against filters and
 * if the event is soft disabled and should be discarded.
 */
static inline void
event_trigger_unlock_commit(struct trace_event_file *file,
			    struct ring_buffer *buffer,
			    struct ring_buffer_event *event,
			    void *entry, unsigned long irq_flags, int pc)
{
	enum event_trigger_type tt = ETT_NONE;

	if (!__event_trigger_test_discard(file, buffer, event, entry, &tt))
		trace_buffer_unlock_commit(file->tr, buffer, event, irq_flags, pc);

	if (tt)
		event_triggers_post_call(file, tt, entry);
}

/**
 * event_trigger_unlock_commit_regs - handle triggers and finish event commit
 * @file: The file pointer assoctiated to the event
 * @buffer: The ring buffer that the event is being written to
 * @event: The event meta data in the ring buffer
 * @entry: The event itself
 * @irq_flags: The state of the interrupts at the start of the event
 * @pc: The state of the preempt count at the start of the event.
 *
 * This is a helper function to handle triggers that require data
 * from the event itself. It also tests the event against filters and
 * if the event is soft disabled and should be discarded.
 *
 * Same as event_trigger_unlock_commit() but calls
 * trace_buffer_unlock_commit_regs() instead of trace_buffer_unlock_commit().
 */
static inline void
event_trigger_unlock_commit_regs(struct trace_event_file *file,
				 struct ring_buffer *buffer,
				 struct ring_buffer_event *event,
				 void *entry, unsigned long irq_flags, int pc,
				 struct pt_regs *regs)
{
	enum event_trigger_type tt = ETT_NONE;

	if (!__event_trigger_test_discard(file, buffer, event, entry, &tt))
		trace_buffer_unlock_commit_regs(file->tr, buffer, event,
						irq_flags, pc, regs);

	if (tt)
		event_triggers_post_call(file, tt, entry);
}

#define FILTER_PRED_INVALID	((unsigned short)-1)
#define FILTER_PRED_IS_RIGHT	(1 << 15)
#define FILTER_PRED_FOLD	(1 << 15)

/*
 * The max preds is the size of unsigned short with
 * two flags at the MSBs. One bit is used for both the IS_RIGHT
 * and FOLD flags. The other is reserved.
 *
 * 2^14 preds is way more than enough.
 */
#define MAX_FILTER_PRED		16384

struct filter_pred;
struct regex;

typedef int (*filter_pred_fn_t) (struct filter_pred *pred, void *event);

typedef int (*regex_match_func)(char *str, struct regex *r, int len);

enum regex_type {
	MATCH_FULL = 0,
	MATCH_FRONT_ONLY,
	MATCH_MIDDLE_ONLY,
	MATCH_END_ONLY,
};

struct regex {
	char			pattern[MAX_FILTER_STR_VAL];
	int			len;
	int			field_len;
	regex_match_func	match;
};

struct filter_pred {
	filter_pred_fn_t 	fn;
	u64 			val;
	struct regex		regex;
	unsigned short		*ops;
	struct ftrace_event_field *field;
	int 			offset;
	int 			not;
	int 			op;
	unsigned short		index;
	unsigned short		parent;
	unsigned short		left;
	unsigned short		right;
};

static inline bool is_string_field(struct ftrace_event_field *field)
{
	return field->filter_type == FILTER_DYN_STRING ||
	       field->filter_type == FILTER_STATIC_STRING ||
	       field->filter_type == FILTER_PTR_STRING;
}

static inline bool is_function_field(struct ftrace_event_field *field)
{
	return field->filter_type == FILTER_TRACE_FN;
}

extern enum regex_type
filter_parse_regex(char *buff, int len, char **search, int *not);
extern void print_event_filter(struct trace_event_file *file,
			       struct trace_seq *s);
extern int apply_event_filter(struct trace_event_file *file,
			      char *filter_string);
extern int apply_subsystem_event_filter(struct trace_subsystem_dir *dir,
					char *filter_string);
extern void print_subsystem_event_filter(struct event_subsystem *system,
					 struct trace_seq *s);
extern int filter_assign_type(const char *type);
extern int create_event_filter(struct trace_event_call *call,
			       char *filter_str, bool set_str,
			       struct event_filter **filterp);
extern void free_event_filter(struct event_filter *filter);

struct ftrace_event_field *
trace_find_event_field(struct trace_event_call *call, char *name);

extern void trace_event_enable_cmd_record(bool enable);
extern int event_trace_add_tracer(struct dentry *parent, struct trace_array *tr);
extern int event_trace_del_tracer(struct trace_array *tr);

extern struct trace_event_file *find_event_file(struct trace_array *tr,
						const char *system,
						const char *event);

static inline void *event_file_data(struct file *filp)
{
	return ACCESS_ONCE(file_inode(filp)->i_private);
}

extern struct mutex event_mutex;
extern struct list_head ftrace_events;

extern const struct file_operations event_trigger_fops;
extern const struct file_operations event_hist_fops;

#ifdef CONFIG_HIST_TRIGGERS
extern int register_trigger_hist_cmd(void);
extern int register_trigger_hist_enable_disable_cmds(void);
#else
static inline int register_trigger_hist_cmd(void) { return 0; }
static inline int register_trigger_hist_enable_disable_cmds(void) { return 0; }
#endif

extern int register_trigger_cmds(void);
extern void clear_event_triggers(struct trace_array *tr);

struct event_trigger_data {
	unsigned long			count;
	int				ref;
	struct event_trigger_ops	*ops;
	struct event_command		*cmd_ops;
	struct event_filter __rcu	*filter;
	char				*filter_str;
	void				*private_data;
	bool				paused;
	bool				paused_tmp;
	struct list_head		list;
	char				*name;
	struct list_head		named_list;
	struct event_trigger_data	*named_data;
};

/* Avoid typos */
#define ENABLE_EVENT_STR	"enable_event"
#define DISABLE_EVENT_STR	"disable_event"
#define ENABLE_HIST_STR		"enable_hist"
#define DISABLE_HIST_STR	"disable_hist"

struct enable_trigger_data {
	struct trace_event_file		*file;
	bool				enable;
	bool				hist;
};

extern int event_enable_trigger_print(struct seq_file *m,
				      struct event_trigger_ops *ops,
				      struct event_trigger_data *data);
extern void event_enable_trigger_free(struct event_trigger_ops *ops,
				      struct event_trigger_data *data);
extern int event_enable_trigger_func(struct event_command *cmd_ops,
				     struct trace_event_file *file,
				     char *glob, char *cmd, char *param);
extern int event_enable_register_trigger(char *glob,
					 struct event_trigger_ops *ops,
					 struct event_trigger_data *data,
					 struct trace_event_file *file);
extern void event_enable_unregister_trigger(char *glob,
					    struct event_trigger_ops *ops,
					    struct event_trigger_data *test,
					    struct trace_event_file *file);
extern void trigger_data_free(struct event_trigger_data *data);
extern int event_trigger_init(struct event_trigger_ops *ops,
			      struct event_trigger_data *data);
extern int trace_event_trigger_enable_disable(struct trace_event_file *file,
					      int trigger_enable);
extern void update_cond_flag(struct trace_event_file *file);
extern void unregister_trigger(char *glob, struct event_trigger_ops *ops,
			       struct event_trigger_data *test,
			       struct trace_event_file *file);
extern int set_trigger_filter(char *filter_str,
			      struct event_trigger_data *trigger_data,
			      struct trace_event_file *file);
extern struct event_trigger_data *find_named_trigger(const char *name);
extern bool is_named_trigger(struct event_trigger_data *test);
extern int save_named_trigger(const char *name,
			      struct event_trigger_data *data);
extern void del_named_trigger(struct event_trigger_data *data);
extern void pause_named_trigger(struct event_trigger_data *data);
extern void unpause_named_trigger(struct event_trigger_data *data);
extern void set_named_trigger_data(struct event_trigger_data *data,
				   struct event_trigger_data *named_data);
extern int register_event_command(struct event_command *cmd);
extern int unregister_event_command(struct event_command *cmd);
extern int register_trigger_hist_enable_disable_cmds(void);

/**
 * struct event_trigger_ops - callbacks for trace event triggers
 *
 * The methods in this structure provide per-event trigger hooks for
 * various trigger operations.
 *
 * All the methods below, except for @init() and @free(), must be
 * implemented.
 *
 * @func: The trigger 'probe' function called when the triggering
 *	event occurs.  The data passed into this callback is the data
 *	that was supplied to the event_command @reg() function that
 *	registered the trigger (see struct event_command) along with
 *	the trace record, rec.
 *
 * @init: An optional initialization function called for the trigger
 *	when the trigger is registered (via the event_command reg()
 *	function).  This can be used to perform per-trigger
 *	initialization such as incrementing a per-trigger reference
 *	count, for instance.  This is usually implemented by the
 *	generic utility function @event_trigger_init() (see
 *	trace_event_triggers.c).
 *
 * @free: An optional de-initialization function called for the
 *	trigger when the trigger is unregistered (via the
 *	event_command @reg() function).  This can be used to perform
 *	per-trigger de-initialization such as decrementing a
 *	per-trigger reference count and freeing corresponding trigger
 *	data, for instance.  This is usually implemented by the
 *	generic utility function @event_trigger_free() (see
 *	trace_event_triggers.c).
 *
 * @print: The callback function invoked to have the trigger print
 *	itself.  This is usually implemented by a wrapper function
 *	that calls the generic utility function @event_trigger_print()
 *	(see trace_event_triggers.c).
 */
struct event_trigger_ops {
	void			(*func)(struct event_trigger_data *data,
					void *rec);
	int			(*init)(struct event_trigger_ops *ops,
					struct event_trigger_data *data);
	void			(*free)(struct event_trigger_ops *ops,
					struct event_trigger_data *data);
	int			(*print)(struct seq_file *m,
					 struct event_trigger_ops *ops,
					 struct event_trigger_data *data);
};

/**
 * struct event_command - callbacks and data members for event commands
 *
 * Event commands are invoked by users by writing the command name
 * into the 'trigger' file associated with a trace event.  The
 * parameters associated with a specific invocation of an event
 * command are used to create an event trigger instance, which is
 * added to the list of trigger instances associated with that trace
 * event.  When the event is hit, the set of triggers associated with
 * that event is invoked.
 *
 * The data members in this structure provide per-event command data
 * for various event commands.
 *
 * All the data members below, except for @post_trigger, must be set
 * for each event command.
 *
 * @name: The unique name that identifies the event command.  This is
 *	the name used when setting triggers via trigger files.
 *
 * @trigger_type: A unique id that identifies the event command
 *	'type'.  This value has two purposes, the first to ensure that
 *	only one trigger of the same type can be set at a given time
 *	for a particular event e.g. it doesn't make sense to have both
 *	a traceon and traceoff trigger attached to a single event at
 *	the same time, so traceon and traceoff have the same type
 *	though they have different names.  The @trigger_type value is
 *	also used as a bit value for deferring the actual trigger
 *	action until after the current event is finished.  Some
 *	commands need to do this if they themselves log to the trace
 *	buffer (see the @post_trigger() member below).  @trigger_type
 *	values are defined by adding new values to the trigger_type
 *	enum in include/linux/trace_events.h.
 *
 * @flags: See the enum event_command_flags below.
 *
 * All the methods below, except for @set_filter() and @unreg_all(),
 * must be implemented.
 *
 * @func: The callback function responsible for parsing and
 *	registering the trigger written to the 'trigger' file by the
 *	user.  It allocates the trigger instance and registers it with
 *	the appropriate trace event.  It makes use of the other
 *	event_command callback functions to orchestrate this, and is
 *	usually implemented by the generic utility function
 *	@event_trigger_callback() (see trace_event_triggers.c).
 *
 * @reg: Adds the trigger to the list of triggers associated with the
 *	event, and enables the event trigger itself, after
 *	initializing it (via the event_trigger_ops @init() function).
 *	This is also where commands can use the @trigger_type value to
 *	make the decision as to whether or not multiple instances of
 *	the trigger should be allowed.  This is usually implemented by
 *	the generic utility function @register_trigger() (see
 *	trace_event_triggers.c).
 *
 * @unreg: Removes the trigger from the list of triggers associated
 *	with the event, and disables the event trigger itself, after
 *	initializing it (via the event_trigger_ops @free() function).
 *	This is usually implemented by the generic utility function
 *	@unregister_trigger() (see trace_event_triggers.c).
 *
 * @unreg_all: An optional function called to remove all the triggers
 *	from the list of triggers associated with the event.  Called
 *	when a trigger file is opened in truncate mode.
 *
 * @set_filter: An optional function called to parse and set a filter
 *	for the trigger.  If no @set_filter() method is set for the
 *	event command, filters set by the user for the command will be
 *	ignored.  This is usually implemented by the generic utility
 *	function @set_trigger_filter() (see trace_event_triggers.c).
 *
 * @get_trigger_ops: The callback function invoked to retrieve the
 *	event_trigger_ops implementation associated with the command.
 */
struct event_command {
	struct list_head	list;
	char			*name;
	enum event_trigger_type	trigger_type;
	int			flags;
	int			(*func)(struct event_command *cmd_ops,
					struct trace_event_file *file,
					char *glob, char *cmd, char *params);
	int			(*reg)(char *glob,
				       struct event_trigger_ops *ops,
				       struct event_trigger_data *data,
				       struct trace_event_file *file);
	void			(*unreg)(char *glob,
					 struct event_trigger_ops *ops,
					 struct event_trigger_data *data,
					 struct trace_event_file *file);
	void			(*unreg_all)(struct trace_event_file *file);
	int			(*set_filter)(char *filter_str,
					      struct event_trigger_data *data,
					      struct trace_event_file *file);
	struct event_trigger_ops *(*get_trigger_ops)(char *cmd, char *param);
};

/**
 * enum event_command_flags - flags for struct event_command
 *
 * @POST_TRIGGER: A flag that says whether or not this command needs
 *	to have its action delayed until after the current event has
 *	been closed.  Some triggers need to avoid being invoked while
 *	an event is currently in the process of being logged, since
 *	the trigger may itself log data into the trace buffer.  Thus
 *	we make sure the current event is committed before invoking
 *	those triggers.  To do that, the trigger invocation is split
 *	in two - the first part checks the filter using the current
 *	trace record; if a command has the @post_trigger flag set, it
 *	sets a bit for itself in the return value, otherwise it
 *	directly invokes the trigger.  Once all commands have been
 *	either invoked or set their return flag, the current record is
 *	either committed or discarded.  At that point, if any commands
 *	have deferred their triggers, those commands are finally
 *	invoked following the close of the current event.  In other
 *	words, if the event_trigger_ops @func() probe implementation
 *	itself logs to the trace buffer, this flag should be set,
 *	otherwise it can be left unspecified.
 *
 * @NEEDS_REC: A flag that says whether or not this command needs
 *	access to the trace record in order to perform its function,
 *	regardless of whether or not it has a filter associated with
 *	it (filters make a trigger require access to the trace record
 *	but are not always present).
 */
enum event_command_flags {
	EVENT_CMD_FL_POST_TRIGGER	= 1,
	EVENT_CMD_FL_NEEDS_REC		= 2,
};

static inline bool event_command_post_trigger(struct event_command *cmd_ops)
{
	return cmd_ops->flags & EVENT_CMD_FL_POST_TRIGGER;
}

static inline bool event_command_needs_rec(struct event_command *cmd_ops)
{
	return cmd_ops->flags & EVENT_CMD_FL_NEEDS_REC;
}

extern int trace_event_enable_disable(struct trace_event_file *file,
				      int enable, int soft_disable);
extern int tracing_alloc_snapshot(void);

extern const char *__start___trace_bprintk_fmt[];
extern const char *__stop___trace_bprintk_fmt[];

extern const char *__start___tracepoint_str[];
extern const char *__stop___tracepoint_str[];

void trace_printk_control(bool enabled);
void trace_printk_init_buffers(void);
void trace_printk_start_comm(void);
int trace_keep_overwrite(struct tracer *tracer, u32 mask, int set);
int set_tracer_flag(struct trace_array *tr, unsigned int mask, int enabled);

/*
 * Normal trace_printk() and friends allocates special buffers
 * to do the manipulation, as well as saves the print formats
 * into sections to display. But the trace infrastructure wants
 * to use these without the added overhead at the price of being
 * a bit slower (used mainly for warnings, where we don't care
 * about performance). The internal_trace_puts() is for such
 * a purpose.
 */
#define internal_trace_puts(str) __trace_puts(_THIS_IP_, str, strlen(str))

#undef FTRACE_ENTRY
#define FTRACE_ENTRY(call, struct_name, id, tstruct, print, filter)	\
	extern struct trace_event_call					\
	__aligned(4) event_##call;
#undef FTRACE_ENTRY_DUP
#define FTRACE_ENTRY_DUP(call, struct_name, id, tstruct, print, filter)	\
	FTRACE_ENTRY(call, struct_name, id, PARAMS(tstruct), PARAMS(print), \
		     filter)
#undef FTRACE_ENTRY_PACKED
#define FTRACE_ENTRY_PACKED(call, struct_name, id, tstruct, print, filter) \
	FTRACE_ENTRY(call, struct_name, id, PARAMS(tstruct), PARAMS(print), \
		     filter)

#include "trace_entries.h"

#if defined(CONFIG_PERF_EVENTS) && defined(CONFIG_FUNCTION_TRACER)
int perf_ftrace_event_register(struct trace_event_call *call,
			       enum trace_reg type, void *data);
#else
#define perf_ftrace_event_register NULL
#endif

#ifdef CONFIG_FTRACE_SYSCALLS
void init_ftrace_syscalls(void);
const char *get_syscall_name(int syscall);
#else
static inline void init_ftrace_syscalls(void) { }
static inline const char *get_syscall_name(int syscall)
{
	return NULL;
}
#endif

#ifdef CONFIG_EVENT_TRACING
void trace_event_init(void);
void trace_event_enum_update(struct trace_enum_map **map, int len);
#else
static inline void __init trace_event_init(void) { }
static inline void trace_event_enum_update(struct trace_enum_map **map, int len) { }
#endif

extern struct trace_iterator *tracepoint_print_iter;

#endif /* _LINUX_KERNEL_TRACE_H */
