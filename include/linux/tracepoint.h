/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LINUX_TRACEPOINT_H
#define _LINUX_TRACEPOINT_H

/*
 * Kernel Tracepoint API.
 *
 * See Documentation/trace/tracepoints.rst.
 *
 * Copyright (C) 2008-2014 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * Heavily inspired from the Linux Kernel Markers.
 */

#include <linux/smp.h>
#include <linux/srcu.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/cpumask.h>
#include <linux/rcupdate.h>
#include <linux/tracepoint-defs.h>
#include <linux/static_call.h>

struct module;
struct tracepoint;
struct notifier_block;

struct trace_eval_map {
	const char		*system;
	const char		*eval_string;
	unsigned long		eval_value;
};

#define TRACEPOINT_DEFAULT_PRIO	10

extern struct srcu_struct tracepoint_srcu;

extern int
tracepoint_probe_register(struct tracepoint *tp, void *probe, void *data);
extern int
tracepoint_probe_register_prio(struct tracepoint *tp, void *probe, void *data,
			       int prio);
extern int
tracepoint_probe_unregister(struct tracepoint *tp, void *probe, void *data);
extern void
for_each_kernel_tracepoint(void (*fct)(struct tracepoint *tp, void *priv),
		void *priv);

#ifdef CONFIG_MODULES
struct tp_module {
	struct list_head list;
	struct module *mod;
};

bool trace_module_has_bad_taint(struct module *mod);
extern int register_tracepoint_module_notifier(struct notifier_block *nb);
extern int unregister_tracepoint_module_notifier(struct notifier_block *nb);
#else
static inline bool trace_module_has_bad_taint(struct module *mod)
{
	return false;
}
static inline
int register_tracepoint_module_notifier(struct notifier_block *nb)
{
	return 0;
}
static inline
int unregister_tracepoint_module_notifier(struct notifier_block *nb)
{
	return 0;
}
#endif /* CONFIG_MODULES */

/*
 * tracepoint_synchronize_unregister must be called between the last tracepoint
 * probe unregistration and the end of module exit to make sure there is no
 * caller executing a probe when it is freed.
 */
#ifdef CONFIG_TRACEPOINTS
static inline void tracepoint_synchronize_unregister(void)
{
	synchronize_srcu(&tracepoint_srcu);
	synchronize_rcu();
}
#else
static inline void tracepoint_synchronize_unregister(void)
{ }
#endif

#ifdef CONFIG_HAVE_SYSCALL_TRACEPOINTS
extern int syscall_regfunc(void);
extern void syscall_unregfunc(void);
#endif /* CONFIG_HAVE_SYSCALL_TRACEPOINTS */

#ifndef PARAMS
#define PARAMS(args...) args
#endif

#define TRACE_DEFINE_ENUM(x)
#define TRACE_DEFINE_SIZEOF(x)

#ifdef CONFIG_HAVE_ARCH_PREL32_RELOCATIONS
static inline struct tracepoint *tracepoint_ptr_deref(tracepoint_ptr_t *p)
{
	return offset_to_ptr(p);
}

#define __TRACEPOINT_ENTRY(name)					\
	asm("	.section \"__tracepoints_ptrs\", \"a\"		\n"	\
	    "	.balign 4					\n"	\
	    "	.long 	__tracepoint_" #name " - .		\n"	\
	    "	.previous					\n")
#else
static inline struct tracepoint *tracepoint_ptr_deref(tracepoint_ptr_t *p)
{
	return *p;
}

#define __TRACEPOINT_ENTRY(name)					 \
	static tracepoint_ptr_t __tracepoint_ptr_##name __used		 \
	__section("__tracepoints_ptrs") = &__tracepoint_##name
#endif

#endif /* _LINUX_TRACEPOINT_H */

/*
 * Note: we keep the TRACE_EVENT and DECLARE_TRACE outside the include
 *  file ifdef protection.
 *  This is due to the way trace events work. If a file includes two
 *  trace event headers under one "CREATE_TRACE_POINTS" the first include
 *  will override the TRACE_EVENT and break the second include.
 */

#ifndef DECLARE_TRACE

#define TP_PROTO(args...)	args
#define TP_ARGS(args...)	args
#define TP_CONDITION(args...)	args

/*
 * Individual subsystem my have a separate configuration to
 * enable their tracepoints. By default, this file will create
 * the tracepoints if CONFIG_TRACEPOINT is defined. If a subsystem
 * wants to be able to disable its tracepoints from being created
 * it can define NOTRACE before including the tracepoint headers.
 */
#if defined(CONFIG_TRACEPOINTS) && !defined(NOTRACE)
#define TRACEPOINTS_ENABLED
#endif

#ifdef TRACEPOINTS_ENABLED

#ifdef CONFIG_HAVE_STATIC_CALL
#define __DO_TRACE_CALL(name)	static_call(tp_func_##name)
#else
#define __DO_TRACE_CALL(name)	__traceiter_##name
#endif /* CONFIG_HAVE_STATIC_CALL */

/*
 * it_func[0] is never NULL because there is at least one element in the array
 * when the array itself is non NULL.
 *
 * Note, the proto and args passed in includes "__data" as the first parameter.
 * The reason for this is to handle the "void" prototype. If a tracepoint
 * has a "void" prototype, then it is invalid to declare a function
 * as "(void *, void)".
 */
#define __DO_TRACE(name, proto, args, cond, rcuidle)			\
	do {								\
		struct tracepoint_func *it_func_ptr;			\
		int __maybe_unused __idx = 0;				\
		void *__data;						\
									\
		if (!(cond))						\
			return;						\
									\
		/* srcu can't be used from NMI */			\
		WARN_ON_ONCE(rcuidle && in_nmi());			\
									\
		/* keep srcu and sched-rcu usage consistent */		\
		preempt_disable_notrace();				\
									\
		/*							\
		 * For rcuidle callers, use srcu since sched-rcu	\
		 * doesn't work from the idle path.			\
		 */							\
		if (rcuidle) {						\
			__idx = srcu_read_lock_notrace(&tracepoint_srcu);\
			rcu_irq_enter_irqson();				\
		}							\
									\
		it_func_ptr =						\
			rcu_dereference_raw((&__tracepoint_##name)->funcs); \
		if (it_func_ptr) {					\
			__data = (it_func_ptr)->data;			\
			__DO_TRACE_CALL(name)(args);			\
		}							\
									\
		if (rcuidle) {						\
			rcu_irq_exit_irqson();				\
			srcu_read_unlock_notrace(&tracepoint_srcu, __idx);\
		}							\
									\
		preempt_enable_notrace();				\
	} while (0)

#ifndef MODULE
#define __DECLARE_TRACE_RCU(name, proto, args, cond, data_proto, data_args) \
	static inline void trace_##name##_rcuidle(proto)		\
	{								\
		if (static_key_false(&__tracepoint_##name.key))		\
			__DO_TRACE(name,				\
				TP_PROTO(data_proto),			\
				TP_ARGS(data_args),			\
				TP_CONDITION(cond), 1);			\
	}
#else
#define __DECLARE_TRACE_RCU(name, proto, args, cond, data_proto, data_args)
#endif

/*
 * Make sure the alignment of the structure in the __tracepoints section will
 * not add unwanted padding between the beginning of the section and the
 * structure. Force alignment to the same alignment as the section start.
 *
 * When lockdep is enabled, we make sure to always do the RCU portions of
 * the tracepoint code, regardless of whether tracing is on. However,
 * don't check if the condition is false, due to interaction with idle
 * instrumentation. This lets us find RCU issues triggered with tracepoints
 * even when this tracepoint is off. This code has no purpose other than
 * poking RCU a bit.
 */
#define __DECLARE_TRACE(name, proto, args, cond, data_proto, data_args) \
	extern int __traceiter_##name(data_proto);			\
	DECLARE_STATIC_CALL(tp_func_##name, __traceiter_##name);	\
	extern struct tracepoint __tracepoint_##name;			\
	static inline void trace_##name(proto)				\
	{								\
		if (static_key_false(&__tracepoint_##name.key))		\
			__DO_TRACE(name,				\
				TP_PROTO(data_proto),			\
				TP_ARGS(data_args),			\
				TP_CONDITION(cond), 0);			\
		if (IS_ENABLED(CONFIG_LOCKDEP) && (cond)) {		\
			rcu_read_lock_sched_notrace();			\
			rcu_dereference_sched(__tracepoint_##name.funcs);\
			rcu_read_unlock_sched_notrace();		\
		}							\
	}								\
	__DECLARE_TRACE_RCU(name, PARAMS(proto), PARAMS(args),		\
		PARAMS(cond), PARAMS(data_proto), PARAMS(data_args))	\
	static inline int						\
	register_trace_##name(void (*probe)(data_proto), void *data)	\
	{								\
		return tracepoint_probe_register(&__tracepoint_##name,	\
						(void *)probe, data);	\
	}								\
	static inline int						\
	register_trace_prio_##name(void (*probe)(data_proto), void *data,\
				   int prio)				\
	{								\
		return tracepoint_probe_register_prio(&__tracepoint_##name, \
					      (void *)probe, data, prio); \
	}								\
	static inline int						\
	unregister_trace_##name(void (*probe)(data_proto), void *data)	\
	{								\
		return tracepoint_probe_unregister(&__tracepoint_##name,\
						(void *)probe, data);	\
	}								\
	static inline void						\
	check_trace_callback_type_##name(void (*cb)(data_proto))	\
	{								\
	}								\
	static inline bool						\
	trace_##name##_enabled(void)					\
	{								\
		return static_key_false(&__tracepoint_##name.key);	\
	}

/*
 * We have no guarantee that gcc and the linker won't up-align the tracepoint
 * structures, so we create an array of pointers that will be used for iteration
 * on the tracepoints.
 */
#define DEFINE_TRACE_FN(_name, _reg, _unreg, proto, args)		\
	static const char __tpstrtab_##_name[]				\
	__section("__tracepoints_strings") = #_name;			\
	extern struct static_call_key STATIC_CALL_KEY(tp_func_##_name);	\
	int __traceiter_##_name(void *__data, proto);			\
	struct tracepoint __tracepoint_##_name	__used			\
	__section("__tracepoints") = {					\
		.name = __tpstrtab_##_name,				\
		.key = STATIC_KEY_INIT_FALSE,				\
		.static_call_key = &STATIC_CALL_KEY(tp_func_##_name),	\
		.static_call_tramp = STATIC_CALL_TRAMP_ADDR(tp_func_##_name), \
		.iterator = &__traceiter_##_name,			\
		.regfunc = _reg,					\
		.unregfunc = _unreg,					\
		.funcs = NULL };					\
	__TRACEPOINT_ENTRY(_name);					\
	int __traceiter_##_name(void *__data, proto)			\
	{								\
		struct tracepoint_func *it_func_ptr;			\
		void *it_func;						\
									\
		it_func_ptr =						\
			rcu_dereference_raw((&__tracepoint_##_name)->funcs); \
		if (it_func_ptr) {					\
			do {						\
				it_func = (it_func_ptr)->func;		\
				__data = (it_func_ptr)->data;		\
				((void(*)(void *, proto))(it_func))(__data, args); \
			} while ((++it_func_ptr)->func);		\
		}							\
		return 0;						\
	}								\
	DEFINE_STATIC_CALL(tp_func_##_name, __traceiter_##_name);

#define DEFINE_TRACE(name, proto, args)		\
	DEFINE_TRACE_FN(name, NULL, NULL, PARAMS(proto), PARAMS(args));

#define EXPORT_TRACEPOINT_SYMBOL_GPL(name)				\
	EXPORT_SYMBOL_GPL(__tracepoint_##name);				\
	EXPORT_SYMBOL_GPL(__traceiter_##name);				\
	EXPORT_STATIC_CALL_GPL(tp_func_##name)
#define EXPORT_TRACEPOINT_SYMBOL(name)					\
	EXPORT_SYMBOL(__tracepoint_##name);				\
	EXPORT_SYMBOL(__traceiter_##name);				\
	EXPORT_STATIC_CALL(tp_func_##name)


#else /* !TRACEPOINTS_ENABLED */
#define __DECLARE_TRACE(name, proto, args, cond, data_proto, data_args) \
	static inline void trace_##name(proto)				\
	{ }								\
	static inline void trace_##name##_rcuidle(proto)		\
	{ }								\
	static inline int						\
	register_trace_##name(void (*probe)(data_proto),		\
			      void *data)				\
	{								\
		return -ENOSYS;						\
	}								\
	static inline int						\
	unregister_trace_##name(void (*probe)(data_proto),		\
				void *data)				\
	{								\
		return -ENOSYS;						\
	}								\
	static inline void check_trace_callback_type_##name(void (*cb)(data_proto)) \
	{								\
	}								\
	static inline bool						\
	trace_##name##_enabled(void)					\
	{								\
		return false;						\
	}

#define DEFINE_TRACE_FN(name, reg, unreg, proto, args)
#define DEFINE_TRACE(name, proto, args)
#define EXPORT_TRACEPOINT_SYMBOL_GPL(name)
#define EXPORT_TRACEPOINT_SYMBOL(name)

#endif /* TRACEPOINTS_ENABLED */

#ifdef CONFIG_TRACING
/**
 * tracepoint_string - register constant persistent string to trace system
 * @str - a constant persistent string that will be referenced in tracepoints
 *
 * If constant strings are being used in tracepoints, it is faster and
 * more efficient to just save the pointer to the string and reference
 * that with a printf "%s" instead of saving the string in the ring buffer
 * and wasting space and time.
 *
 * The problem with the above approach is that userspace tools that read
 * the binary output of the trace buffers do not have access to the string.
 * Instead they just show the address of the string which is not very
 * useful to users.
 *
 * With tracepoint_string(), the string will be registered to the tracing
 * system and exported to userspace via the debugfs/tracing/printk_formats
 * file that maps the string address to the string text. This way userspace
 * tools that read the binary buffers have a way to map the pointers to
 * the ASCII strings they represent.
 *
 * The @str used must be a constant string and persistent as it would not
 * make sense to show a string that no longer exists. But it is still fine
 * to be used with modules, because when modules are unloaded, if they
 * had tracepoints, the ring buffers are cleared too. As long as the string
 * does not change during the life of the module, it is fine to use
 * tracepoint_string() within a module.
 */
#define tracepoint_string(str)						\
	({								\
		static const char *___tp_str __tracepoint_string = str; \
		___tp_str;						\
	})
#define __tracepoint_string	__used __section("__tracepoint_str")
#else
/*
 * tracepoint_string() is used to save the string address for userspace
 * tracing tools. When tracing isn't configured, there's no need to save
 * anything.
 */
# define tracepoint_string(str) str
# define __tracepoint_string
#endif

#define DECLARE_TRACE(name, proto, args)				\
	__DECLARE_TRACE(name, PARAMS(proto), PARAMS(args),		\
			cpu_online(raw_smp_processor_id()),		\
			PARAMS(void *__data, proto),			\
			PARAMS(__data, args))

#define DECLARE_TRACE_CONDITION(name, proto, args, cond)		\
	__DECLARE_TRACE(name, PARAMS(proto), PARAMS(args),		\
			cpu_online(raw_smp_processor_id()) && (PARAMS(cond)), \
			PARAMS(void *__data, proto),			\
			PARAMS(__data, args))

#define TRACE_EVENT_FLAGS(event, flag)

#define TRACE_EVENT_PERF_PERM(event, expr...)

#endif /* DECLARE_TRACE */

#ifndef TRACE_EVENT
/*
 * For use with the TRACE_EVENT macro:
 *
 * We define a tracepoint, its arguments, its printk format
 * and its 'fast binary record' layout.
 *
 * Firstly, name your tracepoint via TRACE_EVENT(name : the
 * 'subsystem_event' notation is fine.
 *
 * Think about this whole construct as the
 * 'trace_sched_switch() function' from now on.
 *
 *
 *  TRACE_EVENT(sched_switch,
 *
 *	*
 *	* A function has a regular function arguments
 *	* prototype, declare it via TP_PROTO():
 *	*
 *
 *	TP_PROTO(struct rq *rq, struct task_struct *prev,
 *		 struct task_struct *next),
 *
 *	*
 *	* Define the call signature of the 'function'.
 *	* (Design sidenote: we use this instead of a
 *	*  TP_PROTO1/TP_PROTO2/TP_PROTO3 ugliness.)
 *	*
 *
 *	TP_ARGS(rq, prev, next),
 *
 *	*
 *	* Fast binary tracing: define the trace record via
 *	* TP_STRUCT__entry(). You can think about it like a
 *	* regular C structure local variable definition.
 *	*
 *	* This is how the trace record is structured and will
 *	* be saved into the ring buffer. These are the fields
 *	* that will be exposed to user-space in
 *	* /sys/kernel/debug/tracing/events/<*>/format.
 *	*
 *	* The declared 'local variable' is called '__entry'
 *	*
 *	* __field(pid_t, prev_prid) is equivalent to a standard declariton:
 *	*
 *	*	pid_t	prev_pid;
 *	*
 *	* __array(char, prev_comm, TASK_COMM_LEN) is equivalent to:
 *	*
 *	*	char	prev_comm[TASK_COMM_LEN];
 *	*
 *
 *	TP_STRUCT__entry(
 *		__array(	char,	prev_comm,	TASK_COMM_LEN	)
 *		__field(	pid_t,	prev_pid			)
 *		__field(	int,	prev_prio			)
 *		__array(	char,	next_comm,	TASK_COMM_LEN	)
 *		__field(	pid_t,	next_pid			)
 *		__field(	int,	next_prio			)
 *	),
 *
 *	*
 *	* Assign the entry into the trace record, by embedding
 *	* a full C statement block into TP_fast_assign(). You
 *	* can refer to the trace record as '__entry' -
 *	* otherwise you can put arbitrary C code in here.
 *	*
 *	* Note: this C code will execute every time a trace event
 *	* happens, on an active tracepoint.
 *	*
 *
 *	TP_fast_assign(
 *		memcpy(__entry->next_comm, next->comm, TASK_COMM_LEN);
 *		__entry->prev_pid	= prev->pid;
 *		__entry->prev_prio	= prev->prio;
 *		memcpy(__entry->prev_comm, prev->comm, TASK_COMM_LEN);
 *		__entry->next_pid	= next->pid;
 *		__entry->next_prio	= next->prio;
 *	),
 *
 *	*
 *	* Formatted output of a trace record via TP_printk().
 *	* This is how the tracepoint will appear under ftrace
 *	* plugins that make use of this tracepoint.
 *	*
 *	* (raw-binary tracing wont actually perform this step.)
 *	*
 *
 *	TP_printk("task %s:%d [%d] ==> %s:%d [%d]",
 *		__entry->prev_comm, __entry->prev_pid, __entry->prev_prio,
 *		__entry->next_comm, __entry->next_pid, __entry->next_prio),
 *
 * );
 *
 * This macro construct is thus used for the regular printk format
 * tracing setup, it is used to construct a function pointer based
 * tracepoint callback (this is used by programmatic plugins and
 * can also by used by generic instrumentation like SystemTap), and
 * it is also used to expose a structured trace record in
 * /sys/kernel/debug/tracing/events/.
 *
 * A set of (un)registration functions can be passed to the variant
 * TRACE_EVENT_FN to perform any (un)registration work.
 */

#define DECLARE_EVENT_CLASS(name, proto, args, tstruct, assign, print)
#define DEFINE_EVENT(template, name, proto, args)		\
	DECLARE_TRACE(name, PARAMS(proto), PARAMS(args))
#define DEFINE_EVENT_FN(template, name, proto, args, reg, unreg)\
	DECLARE_TRACE(name, PARAMS(proto), PARAMS(args))
#define DEFINE_EVENT_PRINT(template, name, proto, args, print)	\
	DECLARE_TRACE(name, PARAMS(proto), PARAMS(args))
#define DEFINE_EVENT_CONDITION(template, name, proto,		\
			       args, cond)			\
	DECLARE_TRACE_CONDITION(name, PARAMS(proto),		\
				PARAMS(args), PARAMS(cond))

#define TRACE_EVENT(name, proto, args, struct, assign, print)	\
	DECLARE_TRACE(name, PARAMS(proto), PARAMS(args))
#define TRACE_EVENT_FN(name, proto, args, struct,		\
		assign, print, reg, unreg)			\
	DECLARE_TRACE(name, PARAMS(proto), PARAMS(args))
#define TRACE_EVENT_FN_COND(name, proto, args, cond, struct,		\
		assign, print, reg, unreg)			\
	DECLARE_TRACE_CONDITION(name, PARAMS(proto),	\
			PARAMS(args), PARAMS(cond))
#define TRACE_EVENT_CONDITION(name, proto, args, cond,		\
			      struct, assign, print)		\
	DECLARE_TRACE_CONDITION(name, PARAMS(proto),		\
				PARAMS(args), PARAMS(cond))

#define TRACE_EVENT_FLAGS(event, flag)

#define TRACE_EVENT_PERF_PERM(event, expr...)

#define DECLARE_EVENT_NOP(name, proto, args)				\
	static inline void trace_##name(proto)				\
	{ }								\
	static inline bool trace_##name##_enabled(void)			\
	{								\
		return false;						\
	}

#define TRACE_EVENT_NOP(name, proto, args, struct, assign, print)	\
	DECLARE_EVENT_NOP(name, PARAMS(proto), PARAMS(args))

#define DECLARE_EVENT_CLASS_NOP(name, proto, args, tstruct, assign, print)
#define DEFINE_EVENT_NOP(template, name, proto, args)			\
	DECLARE_EVENT_NOP(name, PARAMS(proto), PARAMS(args))

#endif /* ifdef TRACE_EVENT (see note above) */
