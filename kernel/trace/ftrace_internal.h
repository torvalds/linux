/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_KERNEL_FTRACE_INTERNAL_H
#define  _LINUX_KERNEL_FTRACE_INTERNAL_H

#ifdef CONFIG_FUNCTION_TRACER

/*
 * Traverse the ftrace_global_list, invoking all entries.  The reason that we
 * can use rcu_dereference_raw_check() is that elements removed from this list
 * are simply leaked, so there is no need to interact with a grace-period
 * mechanism.  The rcu_dereference_raw_check() calls are needed to handle
 * concurrent insertions into the ftrace_global_list.
 *
 * Silly Alpha and silly pointer-speculation compiler optimizations!
 */
#define do_for_each_ftrace_op(op, list)			\
	op = rcu_dereference_raw_check(list);			\
	do

/*
 * Optimized for just a single item in the list (as that is the normal case).
 */
#define while_for_each_ftrace_op(op)				\
	while (likely(op = rcu_dereference_raw_check((op)->next)) &&	\
	       unlikely((op) != &ftrace_list_end))

extern struct ftrace_ops __rcu *ftrace_ops_list;
extern struct ftrace_ops ftrace_list_end;
extern struct mutex ftrace_lock;
extern struct ftrace_ops global_ops;

#ifdef CONFIG_DYNAMIC_FTRACE

int ftrace_startup(struct ftrace_ops *ops, int command);
int ftrace_shutdown(struct ftrace_ops *ops, int command);
int ftrace_ops_test(struct ftrace_ops *ops, unsigned long ip, void *regs);

#else /* !CONFIG_DYNAMIC_FTRACE */

int __register_ftrace_function(struct ftrace_ops *ops);
int __unregister_ftrace_function(struct ftrace_ops *ops);
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
