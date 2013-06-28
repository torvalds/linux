/*
 * Ftrace header.  For implementation details beyond the random comments
 * scattered below, see: Documentation/trace/ftrace-design.txt
 */

#ifndef _LINUX_FTRACE_H
#define _LINUX_FTRACE_H

#include <linux/trace_clock.h>
#include <linux/kallsyms.h>
#include <linux/linkage.h>
#include <linux/bitops.h>
#include <linux/ptrace.h>
#include <linux/ktime.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/fs.h>

#include <asm/ftrace.h>

/*
 * If the arch supports passing the variable contents of
 * function_trace_op as the third parameter back from the
 * mcount call, then the arch should define this as 1.
 */
#ifndef ARCH_SUPPORTS_FTRACE_OPS
#define ARCH_SUPPORTS_FTRACE_OPS 0
#endif

/*
 * If the arch's mcount caller does not support all of ftrace's
 * features, then it must call an indirect function that
 * does. Or at least does enough to prevent any unwelcomed side effects.
 */
#if !defined(CONFIG_HAVE_FUNCTION_TRACE_MCOUNT_TEST) || \
	!ARCH_SUPPORTS_FTRACE_OPS
# define FTRACE_FORCE_LIST_FUNC 1
#else
# define FTRACE_FORCE_LIST_FUNC 0
#endif


struct module;
struct ftrace_hash;

#ifdef CONFIG_FUNCTION_TRACER

extern int ftrace_enabled;
extern int
ftrace_enable_sysctl(struct ctl_table *table, int write,
		     void __user *buffer, size_t *lenp,
		     loff_t *ppos);

struct ftrace_ops;

typedef void (*ftrace_func_t)(unsigned long ip, unsigned long parent_ip,
			      struct ftrace_ops *op, struct pt_regs *regs);

/*
 * FTRACE_OPS_FL_* bits denote the state of ftrace_ops struct and are
 * set in the flags member.
 *
 * ENABLED - set/unset when ftrace_ops is registered/unregistered
 * GLOBAL  - set manualy by ftrace_ops user to denote the ftrace_ops
 *           is part of the global tracers sharing the same filter
 *           via set_ftrace_* debugfs files.
 * DYNAMIC - set when ftrace_ops is registered to denote dynamically
 *           allocated ftrace_ops which need special care
 * CONTROL - set manualy by ftrace_ops user to denote the ftrace_ops
 *           could be controled by following calls:
 *             ftrace_function_local_enable
 *             ftrace_function_local_disable
 * SAVE_REGS - The ftrace_ops wants regs saved at each function called
 *            and passed to the callback. If this flag is set, but the
 *            architecture does not support passing regs
 *            (CONFIG_DYNAMIC_FTRACE_WITH_REGS is not defined), then the
 *            ftrace_ops will fail to register, unless the next flag
 *            is set.
 * SAVE_REGS_IF_SUPPORTED - This is the same as SAVE_REGS, but if the
 *            handler can handle an arch that does not save regs
 *            (the handler tests if regs == NULL), then it can set
 *            this flag instead. It will not fail registering the ftrace_ops
 *            but, the regs field will be NULL if the arch does not support
 *            passing regs to the handler.
 *            Note, if this flag is set, the SAVE_REGS flag will automatically
 *            get set upon registering the ftrace_ops, if the arch supports it.
 * RECURSION_SAFE - The ftrace_ops can set this to tell the ftrace infrastructure
 *            that the call back has its own recursion protection. If it does
 *            not set this, then the ftrace infrastructure will add recursion
 *            protection for the caller.
 * STUB   - The ftrace_ops is just a place holder.
 * INITIALIZED - The ftrace_ops has already been initialized (first use time
 *            register_ftrace_function() is called, it will initialized the ops)
 */
enum {
	FTRACE_OPS_FL_ENABLED			= 1 << 0,
	FTRACE_OPS_FL_GLOBAL			= 1 << 1,
	FTRACE_OPS_FL_DYNAMIC			= 1 << 2,
	FTRACE_OPS_FL_CONTROL			= 1 << 3,
	FTRACE_OPS_FL_SAVE_REGS			= 1 << 4,
	FTRACE_OPS_FL_SAVE_REGS_IF_SUPPORTED	= 1 << 5,
	FTRACE_OPS_FL_RECURSION_SAFE		= 1 << 6,
	FTRACE_OPS_FL_STUB			= 1 << 7,
	FTRACE_OPS_FL_INITIALIZED		= 1 << 8,
};

struct ftrace_ops {
	ftrace_func_t			func;
	struct ftrace_ops		*next;
	unsigned long			flags;
	int __percpu			*disabled;
#ifdef CONFIG_DYNAMIC_FTRACE
	struct ftrace_hash		*notrace_hash;
	struct ftrace_hash		*filter_hash;
	struct mutex			regex_lock;
#endif
};

extern int function_trace_stop;

/*
 * Type of the current tracing.
 */
enum ftrace_tracing_type_t {
	FTRACE_TYPE_ENTER = 0, /* Hook the call of the function */
	FTRACE_TYPE_RETURN,	/* Hook the return of the function */
};

/* Current tracing type, default is FTRACE_TYPE_ENTER */
extern enum ftrace_tracing_type_t ftrace_tracing_type;

/**
 * ftrace_stop - stop function tracer.
 *
 * A quick way to stop the function tracer. Note this an on off switch,
 * it is not something that is recursive like preempt_disable.
 * This does not disable the calling of mcount, it only stops the
 * calling of functions from mcount.
 */
static inline void ftrace_stop(void)
{
	function_trace_stop = 1;
}

/**
 * ftrace_start - start the function tracer.
 *
 * This function is the inverse of ftrace_stop. This does not enable
 * the function tracing if the function tracer is disabled. This only
 * sets the function tracer flag to continue calling the functions
 * from mcount.
 */
static inline void ftrace_start(void)
{
	function_trace_stop = 0;
}

/*
 * The ftrace_ops must be a static and should also
 * be read_mostly.  These functions do modify read_mostly variables
 * so use them sparely. Never free an ftrace_op or modify the
 * next pointer after it has been registered. Even after unregistering
 * it, the next pointer may still be used internally.
 */
int register_ftrace_function(struct ftrace_ops *ops);
int unregister_ftrace_function(struct ftrace_ops *ops);
void clear_ftrace_function(void);

/**
 * ftrace_function_local_enable - enable controlled ftrace_ops on current cpu
 *
 * This function enables tracing on current cpu by decreasing
 * the per cpu control variable.
 * It must be called with preemption disabled and only on ftrace_ops
 * registered with FTRACE_OPS_FL_CONTROL. If called without preemption
 * disabled, this_cpu_ptr will complain when CONFIG_DEBUG_PREEMPT is enabled.
 */
static inline void ftrace_function_local_enable(struct ftrace_ops *ops)
{
	if (WARN_ON_ONCE(!(ops->flags & FTRACE_OPS_FL_CONTROL)))
		return;

	(*this_cpu_ptr(ops->disabled))--;
}

/**
 * ftrace_function_local_disable - enable controlled ftrace_ops on current cpu
 *
 * This function enables tracing on current cpu by decreasing
 * the per cpu control variable.
 * It must be called with preemption disabled and only on ftrace_ops
 * registered with FTRACE_OPS_FL_CONTROL. If called without preemption
 * disabled, this_cpu_ptr will complain when CONFIG_DEBUG_PREEMPT is enabled.
 */
static inline void ftrace_function_local_disable(struct ftrace_ops *ops)
{
	if (WARN_ON_ONCE(!(ops->flags & FTRACE_OPS_FL_CONTROL)))
		return;

	(*this_cpu_ptr(ops->disabled))++;
}

/**
 * ftrace_function_local_disabled - returns ftrace_ops disabled value
 *                                  on current cpu
 *
 * This function returns value of ftrace_ops::disabled on current cpu.
 * It must be called with preemption disabled and only on ftrace_ops
 * registered with FTRACE_OPS_FL_CONTROL. If called without preemption
 * disabled, this_cpu_ptr will complain when CONFIG_DEBUG_PREEMPT is enabled.
 */
static inline int ftrace_function_local_disabled(struct ftrace_ops *ops)
{
	WARN_ON_ONCE(!(ops->flags & FTRACE_OPS_FL_CONTROL));
	return *this_cpu_ptr(ops->disabled);
}

extern void ftrace_stub(unsigned long a0, unsigned long a1,
			struct ftrace_ops *op, struct pt_regs *regs);

#else /* !CONFIG_FUNCTION_TRACER */
/*
 * (un)register_ftrace_function must be a macro since the ops parameter
 * must not be evaluated.
 */
#define register_ftrace_function(ops) ({ 0; })
#define unregister_ftrace_function(ops) ({ 0; })
static inline int ftrace_nr_registered_ops(void)
{
	return 0;
}
static inline void clear_ftrace_function(void) { }
static inline void ftrace_kill(void) { }
static inline void ftrace_stop(void) { }
static inline void ftrace_start(void) { }
#endif /* CONFIG_FUNCTION_TRACER */

#ifdef CONFIG_STACK_TRACER
extern int stack_tracer_enabled;
int
stack_trace_sysctl(struct ctl_table *table, int write,
		   void __user *buffer, size_t *lenp,
		   loff_t *ppos);
#endif

struct ftrace_func_command {
	struct list_head	list;
	char			*name;
	int			(*func)(struct ftrace_hash *hash,
					char *func, char *cmd,
					char *params, int enable);
};

#ifdef CONFIG_DYNAMIC_FTRACE

int ftrace_arch_code_modify_prepare(void);
int ftrace_arch_code_modify_post_process(void);

void ftrace_bug(int err, unsigned long ip);

struct seq_file;

struct ftrace_probe_ops {
	void			(*func)(unsigned long ip,
					unsigned long parent_ip,
					void **data);
	int			(*init)(struct ftrace_probe_ops *ops,
					unsigned long ip, void **data);
	void			(*free)(struct ftrace_probe_ops *ops,
					unsigned long ip, void **data);
	int			(*print)(struct seq_file *m,
					 unsigned long ip,
					 struct ftrace_probe_ops *ops,
					 void *data);
};

extern int
register_ftrace_function_probe(char *glob, struct ftrace_probe_ops *ops,
			      void *data);
extern void
unregister_ftrace_function_probe(char *glob, struct ftrace_probe_ops *ops,
				void *data);
extern void
unregister_ftrace_function_probe_func(char *glob, struct ftrace_probe_ops *ops);
extern void unregister_ftrace_function_probe_all(char *glob);

extern int ftrace_text_reserved(void *start, void *end);

extern int ftrace_nr_registered_ops(void);

/*
 * The dyn_ftrace record's flags field is split into two parts.
 * the first part which is '0-FTRACE_REF_MAX' is a counter of
 * the number of callbacks that have registered the function that
 * the dyn_ftrace descriptor represents.
 *
 * The second part is a mask:
 *  ENABLED - the function is being traced
 *  REGS    - the record wants the function to save regs
 *  REGS_EN - the function is set up to save regs.
 *
 * When a new ftrace_ops is registered and wants a function to save
 * pt_regs, the rec->flag REGS is set. When the function has been
 * set up to save regs, the REG_EN flag is set. Once a function
 * starts saving regs it will do so until all ftrace_ops are removed
 * from tracing that function.
 */
enum {
	FTRACE_FL_ENABLED	= (1UL << 29),
	FTRACE_FL_REGS		= (1UL << 30),
	FTRACE_FL_REGS_EN	= (1UL << 31)
};

#define FTRACE_FL_MASK		(0x7UL << 29)
#define FTRACE_REF_MAX		((1UL << 29) - 1)

struct dyn_ftrace {
	union {
		unsigned long		ip; /* address of mcount call-site */
		struct dyn_ftrace	*freelist;
	};
	unsigned long		flags;
	struct dyn_arch_ftrace		arch;
};

int ftrace_force_update(void);
int ftrace_set_filter_ip(struct ftrace_ops *ops, unsigned long ip,
			 int remove, int reset);
int ftrace_set_filter(struct ftrace_ops *ops, unsigned char *buf,
		       int len, int reset);
int ftrace_set_notrace(struct ftrace_ops *ops, unsigned char *buf,
			int len, int reset);
void ftrace_set_global_filter(unsigned char *buf, int len, int reset);
void ftrace_set_global_notrace(unsigned char *buf, int len, int reset);
void ftrace_free_filter(struct ftrace_ops *ops);

int register_ftrace_command(struct ftrace_func_command *cmd);
int unregister_ftrace_command(struct ftrace_func_command *cmd);

enum {
	FTRACE_UPDATE_CALLS		= (1 << 0),
	FTRACE_DISABLE_CALLS		= (1 << 1),
	FTRACE_UPDATE_TRACE_FUNC	= (1 << 2),
	FTRACE_START_FUNC_RET		= (1 << 3),
	FTRACE_STOP_FUNC_RET		= (1 << 4),
};

/*
 * The FTRACE_UPDATE_* enum is used to pass information back
 * from the ftrace_update_record() and ftrace_test_record()
 * functions. These are called by the code update routines
 * to find out what is to be done for a given function.
 *
 *  IGNORE           - The function is already what we want it to be
 *  MAKE_CALL        - Start tracing the function
 *  MODIFY_CALL      - Stop saving regs for the function
 *  MODIFY_CALL_REGS - Start saving regs for the function
 *  MAKE_NOP         - Stop tracing the function
 */
enum {
	FTRACE_UPDATE_IGNORE,
	FTRACE_UPDATE_MAKE_CALL,
	FTRACE_UPDATE_MODIFY_CALL,
	FTRACE_UPDATE_MODIFY_CALL_REGS,
	FTRACE_UPDATE_MAKE_NOP,
};

enum {
	FTRACE_ITER_FILTER	= (1 << 0),
	FTRACE_ITER_NOTRACE	= (1 << 1),
	FTRACE_ITER_PRINTALL	= (1 << 2),
	FTRACE_ITER_DO_HASH	= (1 << 3),
	FTRACE_ITER_HASH	= (1 << 4),
	FTRACE_ITER_ENABLED	= (1 << 5),
};

void arch_ftrace_update_code(int command);

struct ftrace_rec_iter;

struct ftrace_rec_iter *ftrace_rec_iter_start(void);
struct ftrace_rec_iter *ftrace_rec_iter_next(struct ftrace_rec_iter *iter);
struct dyn_ftrace *ftrace_rec_iter_record(struct ftrace_rec_iter *iter);

#define for_ftrace_rec_iter(iter)		\
	for (iter = ftrace_rec_iter_start();	\
	     iter;				\
	     iter = ftrace_rec_iter_next(iter))


int ftrace_update_record(struct dyn_ftrace *rec, int enable);
int ftrace_test_record(struct dyn_ftrace *rec, int enable);
void ftrace_run_stop_machine(int command);
unsigned long ftrace_location(unsigned long ip);

extern ftrace_func_t ftrace_trace_function;

int ftrace_regex_open(struct ftrace_ops *ops, int flag,
		  struct inode *inode, struct file *file);
ssize_t ftrace_filter_write(struct file *file, const char __user *ubuf,
			    size_t cnt, loff_t *ppos);
ssize_t ftrace_notrace_write(struct file *file, const char __user *ubuf,
			     size_t cnt, loff_t *ppos);
int ftrace_regex_release(struct inode *inode, struct file *file);

void __init
ftrace_set_early_filter(struct ftrace_ops *ops, char *buf, int enable);

/* defined in arch */
extern int ftrace_ip_converted(unsigned long ip);
extern int ftrace_dyn_arch_init(void *data);
extern void ftrace_replace_code(int enable);
extern int ftrace_update_ftrace_func(ftrace_func_t func);
extern void ftrace_caller(void);
extern void ftrace_regs_caller(void);
extern void ftrace_call(void);
extern void ftrace_regs_call(void);
extern void mcount_call(void);

void ftrace_modify_all_code(int command);

#ifndef FTRACE_ADDR
#define FTRACE_ADDR ((unsigned long)ftrace_caller)
#endif

#ifndef FTRACE_REGS_ADDR
#ifdef CONFIG_DYNAMIC_FTRACE_WITH_REGS
# define FTRACE_REGS_ADDR ((unsigned long)ftrace_regs_caller)
#else
# define FTRACE_REGS_ADDR FTRACE_ADDR
#endif
#endif

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
extern void ftrace_graph_caller(void);
extern int ftrace_enable_ftrace_graph_caller(void);
extern int ftrace_disable_ftrace_graph_caller(void);
#else
static inline int ftrace_enable_ftrace_graph_caller(void) { return 0; }
static inline int ftrace_disable_ftrace_graph_caller(void) { return 0; }
#endif

/**
 * ftrace_make_nop - convert code into nop
 * @mod: module structure if called by module load initialization
 * @rec: the mcount call site record
 * @addr: the address that the call site should be calling
 *
 * This is a very sensitive operation and great care needs
 * to be taken by the arch.  The operation should carefully
 * read the location, check to see if what is read is indeed
 * what we expect it to be, and then on success of the compare,
 * it should write to the location.
 *
 * The code segment at @rec->ip should be a caller to @addr
 *
 * Return must be:
 *  0 on success
 *  -EFAULT on error reading the location
 *  -EINVAL on a failed compare of the contents
 *  -EPERM  on error writing to the location
 * Any other value will be considered a failure.
 */
extern int ftrace_make_nop(struct module *mod,
			   struct dyn_ftrace *rec, unsigned long addr);

/**
 * ftrace_make_call - convert a nop call site into a call to addr
 * @rec: the mcount call site record
 * @addr: the address that the call site should call
 *
 * This is a very sensitive operation and great care needs
 * to be taken by the arch.  The operation should carefully
 * read the location, check to see if what is read is indeed
 * what we expect it to be, and then on success of the compare,
 * it should write to the location.
 *
 * The code segment at @rec->ip should be a nop
 *
 * Return must be:
 *  0 on success
 *  -EFAULT on error reading the location
 *  -EINVAL on a failed compare of the contents
 *  -EPERM  on error writing to the location
 * Any other value will be considered a failure.
 */
extern int ftrace_make_call(struct dyn_ftrace *rec, unsigned long addr);

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_REGS
/**
 * ftrace_modify_call - convert from one addr to another (no nop)
 * @rec: the mcount call site record
 * @old_addr: the address expected to be currently called to
 * @addr: the address to change to
 *
 * This is a very sensitive operation and great care needs
 * to be taken by the arch.  The operation should carefully
 * read the location, check to see if what is read is indeed
 * what we expect it to be, and then on success of the compare,
 * it should write to the location.
 *
 * The code segment at @rec->ip should be a caller to @old_addr
 *
 * Return must be:
 *  0 on success
 *  -EFAULT on error reading the location
 *  -EINVAL on a failed compare of the contents
 *  -EPERM  on error writing to the location
 * Any other value will be considered a failure.
 */
extern int ftrace_modify_call(struct dyn_ftrace *rec, unsigned long old_addr,
			      unsigned long addr);
#else
/* Should never be called */
static inline int ftrace_modify_call(struct dyn_ftrace *rec, unsigned long old_addr,
				     unsigned long addr)
{
	return -EINVAL;
}
#endif

/* May be defined in arch */
extern int ftrace_arch_read_dyn_info(char *buf, int size);

extern int skip_trace(unsigned long ip);

extern void ftrace_disable_daemon(void);
extern void ftrace_enable_daemon(void);
#else /* CONFIG_DYNAMIC_FTRACE */
static inline int skip_trace(unsigned long ip) { return 0; }
static inline int ftrace_force_update(void) { return 0; }
static inline void ftrace_disable_daemon(void) { }
static inline void ftrace_enable_daemon(void) { }
static inline void ftrace_release_mod(struct module *mod) {}
static inline int register_ftrace_command(struct ftrace_func_command *cmd)
{
	return -EINVAL;
}
static inline int unregister_ftrace_command(char *cmd_name)
{
	return -EINVAL;
}
static inline int ftrace_text_reserved(void *start, void *end)
{
	return 0;
}
static inline unsigned long ftrace_location(unsigned long ip)
{
	return 0;
}

/*
 * Again users of functions that have ftrace_ops may not
 * have them defined when ftrace is not enabled, but these
 * functions may still be called. Use a macro instead of inline.
 */
#define ftrace_regex_open(ops, flag, inod, file) ({ -ENODEV; })
#define ftrace_set_early_filter(ops, buf, enable) do { } while (0)
#define ftrace_set_filter_ip(ops, ip, remove, reset) ({ -ENODEV; })
#define ftrace_set_filter(ops, buf, len, reset) ({ -ENODEV; })
#define ftrace_set_notrace(ops, buf, len, reset) ({ -ENODEV; })
#define ftrace_free_filter(ops) do { } while (0)

static inline ssize_t ftrace_filter_write(struct file *file, const char __user *ubuf,
			    size_t cnt, loff_t *ppos) { return -ENODEV; }
static inline ssize_t ftrace_notrace_write(struct file *file, const char __user *ubuf,
			     size_t cnt, loff_t *ppos) { return -ENODEV; }
static inline loff_t ftrace_regex_lseek(struct file *file, loff_t offset, int whence)
{
	return -ENODEV;
}
static inline int
ftrace_regex_release(struct inode *inode, struct file *file) { return -ENODEV; }
#endif /* CONFIG_DYNAMIC_FTRACE */

loff_t ftrace_filter_lseek(struct file *file, loff_t offset, int whence);

/* totally disable ftrace - can not re-enable after this */
void ftrace_kill(void);

static inline void tracer_disable(void)
{
#ifdef CONFIG_FUNCTION_TRACER
	ftrace_enabled = 0;
#endif
}

/*
 * Ftrace disable/restore without lock. Some synchronization mechanism
 * must be used to prevent ftrace_enabled to be changed between
 * disable/restore.
 */
static inline int __ftrace_enabled_save(void)
{
#ifdef CONFIG_FUNCTION_TRACER
	int saved_ftrace_enabled = ftrace_enabled;
	ftrace_enabled = 0;
	return saved_ftrace_enabled;
#else
	return 0;
#endif
}

static inline void __ftrace_enabled_restore(int enabled)
{
#ifdef CONFIG_FUNCTION_TRACER
	ftrace_enabled = enabled;
#endif
}

#ifndef HAVE_ARCH_CALLER_ADDR
# ifdef CONFIG_FRAME_POINTER
#  define CALLER_ADDR0 ((unsigned long)__builtin_return_address(0))
#  define CALLER_ADDR1 ((unsigned long)__builtin_return_address(1))
#  define CALLER_ADDR2 ((unsigned long)__builtin_return_address(2))
#  define CALLER_ADDR3 ((unsigned long)__builtin_return_address(3))
#  define CALLER_ADDR4 ((unsigned long)__builtin_return_address(4))
#  define CALLER_ADDR5 ((unsigned long)__builtin_return_address(5))
#  define CALLER_ADDR6 ((unsigned long)__builtin_return_address(6))
# else
#  define CALLER_ADDR0 ((unsigned long)__builtin_return_address(0))
#  define CALLER_ADDR1 0UL
#  define CALLER_ADDR2 0UL
#  define CALLER_ADDR3 0UL
#  define CALLER_ADDR4 0UL
#  define CALLER_ADDR5 0UL
#  define CALLER_ADDR6 0UL
# endif
#endif /* ifndef HAVE_ARCH_CALLER_ADDR */

#ifdef CONFIG_IRQSOFF_TRACER
  extern void time_hardirqs_on(unsigned long a0, unsigned long a1);
  extern void time_hardirqs_off(unsigned long a0, unsigned long a1);
#else
  static inline void time_hardirqs_on(unsigned long a0, unsigned long a1) { }
  static inline void time_hardirqs_off(unsigned long a0, unsigned long a1) { }
#endif

#ifdef CONFIG_PREEMPT_TRACER
  extern void trace_preempt_on(unsigned long a0, unsigned long a1);
  extern void trace_preempt_off(unsigned long a0, unsigned long a1);
#else
/*
 * Use defines instead of static inlines because some arches will make code out
 * of the CALLER_ADDR, when we really want these to be a real nop.
 */
# define trace_preempt_on(a0, a1) do { } while (0)
# define trace_preempt_off(a0, a1) do { } while (0)
#endif

#ifdef CONFIG_FTRACE_MCOUNT_RECORD
extern void ftrace_init(void);
#else
static inline void ftrace_init(void) { }
#endif

/*
 * Structure that defines an entry function trace.
 */
struct ftrace_graph_ent {
	unsigned long func; /* Current function */
	int depth;
};

/*
 * Structure that defines a return function trace.
 */
struct ftrace_graph_ret {
	unsigned long func; /* Current function */
	unsigned long long calltime;
	unsigned long long rettime;
	/* Number of functions that overran the depth limit for current task */
	unsigned long overrun;
	int depth;
};

/* Type of the callback handlers for tracing function graph*/
typedef void (*trace_func_graph_ret_t)(struct ftrace_graph_ret *); /* return */
typedef int (*trace_func_graph_ent_t)(struct ftrace_graph_ent *); /* entry */

#ifdef CONFIG_FUNCTION_GRAPH_TRACER

/* for init task */
#define INIT_FTRACE_GRAPH		.ret_stack = NULL,

/*
 * Stack of return addresses for functions
 * of a thread.
 * Used in struct thread_info
 */
struct ftrace_ret_stack {
	unsigned long ret;
	unsigned long func;
	unsigned long long calltime;
	unsigned long long subtime;
	unsigned long fp;
};

/*
 * Primary handler of a function return.
 * It relays on ftrace_return_to_handler.
 * Defined in entry_32/64.S
 */
extern void return_to_handler(void);

extern int
ftrace_push_return_trace(unsigned long ret, unsigned long func, int *depth,
			 unsigned long frame_pointer);

/*
 * Sometimes we don't want to trace a function with the function
 * graph tracer but we want them to keep traced by the usual function
 * tracer if the function graph tracer is not configured.
 */
#define __notrace_funcgraph		notrace

/*
 * We want to which function is an entrypoint of a hardirq.
 * That will help us to put a signal on output.
 */
#define __irq_entry		 __attribute__((__section__(".irqentry.text")))

/* Limits of hardirq entrypoints */
extern char __irqentry_text_start[];
extern char __irqentry_text_end[];

#define FTRACE_RETFUNC_DEPTH 50
#define FTRACE_RETSTACK_ALLOC_SIZE 32
extern int register_ftrace_graph(trace_func_graph_ret_t retfunc,
				trace_func_graph_ent_t entryfunc);

extern void ftrace_graph_stop(void);

/* The current handlers in use */
extern trace_func_graph_ret_t ftrace_graph_return;
extern trace_func_graph_ent_t ftrace_graph_entry;

extern void unregister_ftrace_graph(void);

extern void ftrace_graph_init_task(struct task_struct *t);
extern void ftrace_graph_exit_task(struct task_struct *t);
extern void ftrace_graph_init_idle_task(struct task_struct *t, int cpu);

static inline int task_curr_ret_stack(struct task_struct *t)
{
	return t->curr_ret_stack;
}

static inline void pause_graph_tracing(void)
{
	atomic_inc(&current->tracing_graph_pause);
}

static inline void unpause_graph_tracing(void)
{
	atomic_dec(&current->tracing_graph_pause);
}
#else /* !CONFIG_FUNCTION_GRAPH_TRACER */

#define __notrace_funcgraph
#define __irq_entry
#define INIT_FTRACE_GRAPH

static inline void ftrace_graph_init_task(struct task_struct *t) { }
static inline void ftrace_graph_exit_task(struct task_struct *t) { }
static inline void ftrace_graph_init_idle_task(struct task_struct *t, int cpu) { }

static inline int register_ftrace_graph(trace_func_graph_ret_t retfunc,
			  trace_func_graph_ent_t entryfunc)
{
	return -1;
}
static inline void unregister_ftrace_graph(void) { }

static inline int task_curr_ret_stack(struct task_struct *tsk)
{
	return -1;
}

static inline void pause_graph_tracing(void) { }
static inline void unpause_graph_tracing(void) { }
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */

#ifdef CONFIG_TRACING

/* flags for current->trace */
enum {
	TSK_TRACE_FL_TRACE_BIT	= 0,
	TSK_TRACE_FL_GRAPH_BIT	= 1,
};
enum {
	TSK_TRACE_FL_TRACE	= 1 << TSK_TRACE_FL_TRACE_BIT,
	TSK_TRACE_FL_GRAPH	= 1 << TSK_TRACE_FL_GRAPH_BIT,
};

static inline void set_tsk_trace_trace(struct task_struct *tsk)
{
	set_bit(TSK_TRACE_FL_TRACE_BIT, &tsk->trace);
}

static inline void clear_tsk_trace_trace(struct task_struct *tsk)
{
	clear_bit(TSK_TRACE_FL_TRACE_BIT, &tsk->trace);
}

static inline int test_tsk_trace_trace(struct task_struct *tsk)
{
	return tsk->trace & TSK_TRACE_FL_TRACE;
}

static inline void set_tsk_trace_graph(struct task_struct *tsk)
{
	set_bit(TSK_TRACE_FL_GRAPH_BIT, &tsk->trace);
}

static inline void clear_tsk_trace_graph(struct task_struct *tsk)
{
	clear_bit(TSK_TRACE_FL_GRAPH_BIT, &tsk->trace);
}

static inline int test_tsk_trace_graph(struct task_struct *tsk)
{
	return tsk->trace & TSK_TRACE_FL_GRAPH;
}

enum ftrace_dump_mode;

extern enum ftrace_dump_mode ftrace_dump_on_oops;

#ifdef CONFIG_PREEMPT
#define INIT_TRACE_RECURSION		.trace_recursion = 0,
#endif

#endif /* CONFIG_TRACING */

#ifndef INIT_TRACE_RECURSION
#define INIT_TRACE_RECURSION
#endif

#ifdef CONFIG_FTRACE_SYSCALLS

unsigned long arch_syscall_addr(int nr);

#endif /* CONFIG_FTRACE_SYSCALLS */

#endif /* _LINUX_FTRACE_H */
