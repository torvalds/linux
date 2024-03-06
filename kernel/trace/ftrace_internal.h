/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_KERNEL_FTRACE_INTERNAL_H
#define  _LINUX_KERNEL_FTRACE_INTERNAL_H

int __register_ftrace_function(struct ftrace_ops *ops);
int __unregister_ftrace_function(struct ftrace_ops *ops);

#ifdef CONFIG_FUNCTION_TRACER

extern struct mutex ftrace_lock;
extern struct ftrace_ops global_ops;

#ifdef CONFIG_DYNAMIC_FTRACE

int ftrace_startup(struct ftrace_ops *ops, int command);
int ftrace_shutdown(struct ftrace_ops *ops, int command);
int ftrace_ops_test(struct ftrace_ops *ops, unsigned long ip, void *regs);

#else /* !CONFIG_DYNAMIC_FTRACE */

/* Keep as macros so we do not need to define the commands */
# define ftrace_startup(ops, command)					\
	({								\
		int ___ret = __register_ftrace_function(ops);		\
		if (!___ret)						\
			(ops)->flags |= FTRACE_OPS_FL_ENABLED;		\
		___ret;							\
	})
# define ftrace_shutdown(ops, command)					\
	({								\
		int ___ret = __unregister_ftrace_function(ops);		\
		if (!___ret)						\
			(ops)->flags &= ~FTRACE_OPS_FL_ENABLED;		\
		___ret;							\
	})
static inline int
ftrace_ops_test(struct ftrace_ops *ops, unsigned long ip, void *regs)
{
	return 1;
}
#endif /* CONFIG_DYNAMIC_FTRACE */

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
extern int ftrace_graph_active;
void update_function_graph_func(void);
#else /* !CONFIG_FUNCTION_GRAPH_TRACER */
# define ftrace_graph_active 0
static inline void update_function_graph_func(void) { }
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */

#else /* !CONFIG_FUNCTION_TRACER */
#endif /* CONFIG_FUNCTION_TRACER */

#endif
