#ifndef _LINUX_FTRACE_H
#define _LINUX_FTRACE_H

#include <linux/linkage.h>
#include <linux/fs.h>
#include <linux/ktime.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/kallsyms.h>

#ifdef CONFIG_FUNCTION_TRACER

extern int ftrace_enabled;
extern int
ftrace_enable_sysctl(struct ctl_table *table, int write,
		     struct file *filp, void __user *buffer, size_t *lenp,
		     loff_t *ppos);

typedef void (*ftrace_func_t)(unsigned long ip, unsigned long parent_ip);

struct ftrace_ops {
	ftrace_func_t	  func;
	struct ftrace_ops *next;
};

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

extern void ftrace_stub(unsigned long a0, unsigned long a1);

#else /* !CONFIG_FUNCTION_TRACER */
# define register_ftrace_function(ops) do { } while (0)
# define unregister_ftrace_function(ops) do { } while (0)
# define clear_ftrace_function(ops) do { } while (0)
static inline void ftrace_kill(void) { }
#endif /* CONFIG_FUNCTION_TRACER */

#ifdef CONFIG_DYNAMIC_FTRACE

enum {
	FTRACE_FL_FREE		= (1 << 0),
	FTRACE_FL_FAILED	= (1 << 1),
	FTRACE_FL_FILTER	= (1 << 2),
	FTRACE_FL_ENABLED	= (1 << 3),
	FTRACE_FL_NOTRACE	= (1 << 4),
	FTRACE_FL_CONVERTED	= (1 << 5),
	FTRACE_FL_FROZEN	= (1 << 6),
};

struct dyn_ftrace {
	struct list_head	list;
	unsigned long		ip; /* address of mcount call-site */
	unsigned long		flags;
};

int ftrace_force_update(void);
void ftrace_set_filter(unsigned char *buf, int len, int reset);

/* defined in arch */
extern int ftrace_ip_converted(unsigned long ip);
extern unsigned char *ftrace_nop_replace(void);
extern unsigned char *ftrace_call_replace(unsigned long ip, unsigned long addr);
extern int ftrace_dyn_arch_init(void *data);
extern int ftrace_update_ftrace_func(ftrace_func_t func);
extern void ftrace_caller(void);
extern void ftrace_call(void);
extern void mcount_call(void);

/**
 * ftrace_modify_code - modify code segment
 * @ip: the address of the code segment
 * @old_code: the contents of what is expected to be there
 * @new_code: the code to patch in
 *
 * This is a very sensitive operation and great care needs
 * to be taken by the arch.  The operation should carefully
 * read the location, check to see if what is read is indeed
 * what we expect it to be, and then on success of the compare,
 * it should write to the location.
 *
 * Return must be:
 *  0 on success
 *  -EFAULT on error reading the location
 *  -EINVAL on a failed compare of the contents
 *  -EPERM  on error writing to the location
 * Any other value will be considered a failure.
 */
extern int ftrace_modify_code(unsigned long ip, unsigned char *old_code,
			      unsigned char *new_code);

extern int skip_trace(unsigned long ip);

extern void ftrace_release(void *start, unsigned long size);

extern void ftrace_disable_daemon(void);
extern void ftrace_enable_daemon(void);

#else
# define skip_trace(ip)				({ 0; })
# define ftrace_force_update()			({ 0; })
# define ftrace_set_filter(buf, len, reset)	do { } while (0)
# define ftrace_disable_daemon()		do { } while (0)
# define ftrace_enable_daemon()			do { } while (0)
static inline void ftrace_release(void *start, unsigned long size) { }
#endif /* CONFIG_DYNAMIC_FTRACE */

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

#ifdef CONFIG_FRAME_POINTER
/* TODO: need to fix this for ARM */
# define CALLER_ADDR0 ((unsigned long)__builtin_return_address(0))
# define CALLER_ADDR1 ((unsigned long)__builtin_return_address(1))
# define CALLER_ADDR2 ((unsigned long)__builtin_return_address(2))
# define CALLER_ADDR3 ((unsigned long)__builtin_return_address(3))
# define CALLER_ADDR4 ((unsigned long)__builtin_return_address(4))
# define CALLER_ADDR5 ((unsigned long)__builtin_return_address(5))
# define CALLER_ADDR6 ((unsigned long)__builtin_return_address(6))
#else
# define CALLER_ADDR0 ((unsigned long)__builtin_return_address(0))
# define CALLER_ADDR1 0UL
# define CALLER_ADDR2 0UL
# define CALLER_ADDR3 0UL
# define CALLER_ADDR4 0UL
# define CALLER_ADDR5 0UL
# define CALLER_ADDR6 0UL
#endif

#ifdef CONFIG_IRQSOFF_TRACER
  extern void time_hardirqs_on(unsigned long a0, unsigned long a1);
  extern void time_hardirqs_off(unsigned long a0, unsigned long a1);
#else
# define time_hardirqs_on(a0, a1)		do { } while (0)
# define time_hardirqs_off(a0, a1)		do { } while (0)
#endif

#ifdef CONFIG_PREEMPT_TRACER
  extern void trace_preempt_on(unsigned long a0, unsigned long a1);
  extern void trace_preempt_off(unsigned long a0, unsigned long a1);
#else
# define trace_preempt_on(a0, a1)		do { } while (0)
# define trace_preempt_off(a0, a1)		do { } while (0)
#endif

#ifdef CONFIG_TRACING
extern void
ftrace_special(unsigned long arg1, unsigned long arg2, unsigned long arg3);

/**
 * ftrace_printk - printf formatting in the ftrace buffer
 * @fmt: the printf format for printing
 *
 * Note: __ftrace_printk is an internal function for ftrace_printk and
 *       the @ip is passed in via the ftrace_printk macro.
 *
 * This function allows a kernel developer to debug fast path sections
 * that printk is not appropriate for. By scattering in various
 * printk like tracing in the code, a developer can quickly see
 * where problems are occurring.
 *
 * This is intended as a debugging tool for the developer only.
 * Please refrain from leaving ftrace_printks scattered around in
 * your code.
 */
# define ftrace_printk(fmt...) __ftrace_printk(_THIS_IP_, fmt)
extern int
__ftrace_printk(unsigned long ip, const char *fmt, ...)
	__attribute__ ((format (printf, 2, 3)));
extern void ftrace_dump(void);
#else
static inline void
ftrace_special(unsigned long arg1, unsigned long arg2, unsigned long arg3) { }
static inline int
ftrace_printk(const char *fmt, ...) __attribute__ ((format (printf, 1, 0)));

static inline int
ftrace_printk(const char *fmt, ...)
{
	return 0;
}
static inline void ftrace_dump(void) { }
#endif

#ifdef CONFIG_FTRACE_MCOUNT_RECORD
extern void ftrace_init(void);
extern void ftrace_init_module(unsigned long *start, unsigned long *end);
#else
static inline void ftrace_init(void) { }
static inline void
ftrace_init_module(unsigned long *start, unsigned long *end) { }
#endif


struct boot_trace {
	pid_t			caller;
	char			func[KSYM_NAME_LEN];
	int			result;
	unsigned long long	duration;		/* usecs */
	ktime_t			calltime;
	ktime_t			rettime;
};

#ifdef CONFIG_BOOT_TRACER
extern void trace_boot(struct boot_trace *it, initcall_t fn);
extern void start_boot_trace(void);
extern void stop_boot_trace(void);
#else
static inline void trace_boot(struct boot_trace *it, initcall_t fn) { }
static inline void start_boot_trace(void) { }
static inline void stop_boot_trace(void) { }
#endif



#endif /* _LINUX_FTRACE_H */
