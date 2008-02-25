#ifndef _LINUX_FTRACE_H
#define _LINUX_FTRACE_H

#ifdef CONFIG_FTRACE

#include <linux/linkage.h>
#include <linux/fs.h>

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
extern void mcount(void);

#else /* !CONFIG_FTRACE */
# define register_ftrace_function(ops) do { } while (0)
# define unregister_ftrace_function(ops) do { } while (0)
# define clear_ftrace_function(ops) do { } while (0)
#endif /* CONFIG_FTRACE */

#ifdef CONFIG_DYNAMIC_FTRACE
# define FTRACE_HASHBITS	10
# define FTRACE_HASHSIZE	(1<<FTRACE_HASHBITS)

enum {
	FTRACE_FL_FREE		= (1 << 0),
	FTRACE_FL_FAILED	= (1 << 1),
	FTRACE_FL_FILTER	= (1 << 2),
	FTRACE_FL_ENABLED	= (1 << 3),
};

struct dyn_ftrace {
	struct hlist_node node;
	unsigned long	  ip;
	unsigned long	  flags;
};

int ftrace_force_update(void);
void ftrace_set_filter(unsigned char *buf, int len, int reset);

/* defined in arch */
extern int ftrace_ip_converted(unsigned long ip);
extern unsigned char *ftrace_nop_replace(void);
extern unsigned char *ftrace_call_replace(unsigned long ip, unsigned long addr);
extern int ftrace_dyn_arch_init(void *data);
extern int ftrace_mcount_set(unsigned long *data);
extern int ftrace_modify_code(unsigned long ip, unsigned char *old_code,
			      unsigned char *new_code);
extern int ftrace_update_ftrace_func(ftrace_func_t func);
extern void ftrace_caller(void);
extern void ftrace_call(void);
extern void mcount_call(void);
#else
# define ftrace_force_update()			({ 0; })
# define ftrace_set_filter(buf, len, reset)	do { } while (0)
#endif

/* totally disable ftrace - can not re-enable after this */
void ftrace_kill(void);

static inline void tracer_disable(void)
{
#ifdef CONFIG_FTRACE
	ftrace_enabled = 0;
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

#ifdef CONFIG_CONTEXT_SWITCH_TRACER
extern void
ftrace_special(unsigned long arg1, unsigned long arg2, unsigned long arg3);
#else
static inline void
ftrace_special(unsigned long arg1, unsigned long arg2, unsigned long arg3) { }
#endif

#endif /* _LINUX_FTRACE_H */
