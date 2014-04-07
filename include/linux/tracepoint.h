#ifndef _LINUX_TRACEPOINT_H
#define _LINUX_TRACEPOINT_H

/*
 * Kernel Tracepoint API.
 *
 * See Documentation/trace/tracepoints.txt.
 *
 * (C) Copyright 2008 Mathieu Desnoyers <mathieu.desnoyers@polymtl.ca>
 *
 * Heavily inspired from the Linux Kernel Markers.
 *
 * This file is released under the GPLv2.
 * See the file COPYING for more details.
 */

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/rcupdate.h>
#include <linux/static_key.h>

struct module;
struct tracepoint;

struct tracepoint_func {
	void *func;
	void *data;
};

struct tracepoint {
	const char *name;		/* Tracepoint name */
	struct static_key key;
	void (*regfunc)(void);
	void (*unregfunc)(void);
	struct tracepoint_func __rcu *funcs;
};

/*
 * Connect a probe to a tracepoint.
 * Internal API, should not be used directly.
 */
extern int tracepoint_probe_register(const char *name, void *probe, void *data);

/*
 * Disconnect a probe from a tracepoint.
 * Internal API, should not be used directly.
 */
extern int
tracepoint_probe_unregister(const char *name, void *probe, void *data);

extern int tracepoint_probe_register_noupdate(const char *name, void *probe,
					      void *data);
extern int tracepoint_probe_unregister_noupdate(const char *name, void *probe,
						void *data);
extern void tracepoint_probe_update_all(void);

#ifdef CONFIG_MODULES
struct tp_module {
	struct list_head list;
	unsigned int num_tracepoints;
	struct tracepoint * const *tracepoints_ptrs;
};
bool trace_module_has_bad_taint(struct module *mod);
#else
static inline bool trace_module_has_bad_taint(struct module *mod)
{
	return false;
}
#endif /* CONFIG_MODULES */

struct tracepoint_iter {
#ifdef CONFIG_MODULES
	struct tp_module *module;
#endif /* CONFIG_MODULES */
	struct tracepoint * const *tracepoint;
};

extern void tracepoint_iter_start(struct tracepoint_iter *iter);
extern void tracepoint_iter_next(struct tracepoint_iter *iter);
extern void tracepoint_iter_stop(struct tracepoint_iter *iter);
extern void tracepoint_iter_reset(struct tracepoint_iter *iter);

/*
 * tracepoint_synchronize_unregister must be called between the last tracepoint
 * probe unregistration and the end of module exit to make sure there is no
 * caller executing a probe when it is freed.
 */
static inline void tracepoint_synchronize_unregister(void)
{
	synchronize_sched();
}

#define PARAMS(args...) args

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

#ifdef CONFIG_TRACEPOINTS

/*
 * it_func[0] is never NULL because there is at least one element in the array
 * when the array itself is non NULL.
 *
 * Note, the proto and args passed in includes "__data" as the first parameter.
 * The reason for this is to handle the "void" prototype. If a tracepoint
 * has a "void" prototype, then it is invalid to declare a function
 * as "(void *, void)". The DECLARE_TRACE_NOARGS() will pass in just
 * "void *data", where as the DECLARE_TRACE() will pass in "void *data, proto".
 */
#define __DO_TRACE(tp, proto, args, cond, prercu, postrcu)		\
	do {								\
		struct tracepoint_func *it_func_ptr;			\
		void *it_func;						\
		void *__data;						\
									\
		if (!(cond))						\
			return;						\
		prercu;							\
		rcu_read_lock_sched_notrace();				\
		it_func_ptr = rcu_dereference_sched((tp)->funcs);	\
		if (it_func_ptr) {					\
			do {						\
				it_func = (it_func_ptr)->func;		\
				__data = (it_func_ptr)->data;		\
				((void(*)(proto))(it_func))(args);	\
			} while ((++it_func_ptr)->func);		\
		}							\
		rcu_read_unlock_sched_notrace();			\
		postrcu;						\
	} while (0)

/*
 * Make sure the alignment of the structure in the __tracepoints section will
 * not add unwanted padding between the beginning of the section and the
 * structure. Force alignment to the same alignment as the section start.
 */
#define __DECLARE_TRACE(name, proto, args, cond, data_proto, data_args) \
	extern struct tracepoint __tracepoint_##name;			\
	static inline void trace_##name(proto)				\
	{								\
		if (static_key_false(&__tracepoint_##name.key))		\
			__DO_TRACE(&__tracepoint_##name,		\
				TP_PROTO(data_proto),			\
				TP_ARGS(data_args),			\
				TP_CONDITION(cond),,);			\
	}								\
	static inline void trace_##name##_rcuidle(proto)		\
	{								\
		if (static_branch(&__tracepoint_##name.key))		\
			__DO_TRACE(&__tracepoint_##name,		\
				TP_PROTO(data_proto),			\
				TP_ARGS(data_args),			\
				TP_CONDITION(cond),			\
				rcu_idle_exit(),			\
				rcu_idle_enter());			\
	}								\
	static inline int						\
	register_trace_##name(void (*probe)(data_proto), void *data)	\
	{								\
		return tracepoint_probe_register(#name, (void *)probe,	\
						 data);			\
	}								\
	static inline int						\
	unregister_trace_##name(void (*probe)(data_proto), void *data)	\
	{								\
		return tracepoint_probe_unregister(#name, (void *)probe, \
						   data);		\
	}								\
	static inline void						\
	check_trace_callback_type_##name(void (*cb)(data_proto))	\
	{								\
	}

/*
 * We have no guarantee that gcc and the linker won't up-align the tracepoint
 * structures, so we create an array of pointers that will be used for iteration
 * on the tracepoints.
 */
#define DEFINE_TRACE_FN(name, reg, unreg)				 \
	static const char __tpstrtab_##name[]				 \
	__attribute__((section("__tracepoints_strings"))) = #name;	 \
	struct tracepoint __tracepoint_##name				 \
	__attribute__((section("__tracepoints"))) =			 \
		{ __tpstrtab_##name, STATIC_KEY_INIT_FALSE, reg, unreg, NULL };\
	static struct tracepoint * const __tracepoint_ptr_##name __used	 \
	__attribute__((section("__tracepoints_ptrs"))) =		 \
		&__tracepoint_##name;

#define DEFINE_TRACE(name)						\
	DEFINE_TRACE_FN(name, NULL, NULL);

#define EXPORT_TRACEPOINT_SYMBOL_GPL(name)				\
	EXPORT_SYMBOL_GPL(__tracepoint_##name)
#define EXPORT_TRACEPOINT_SYMBOL(name)					\
	EXPORT_SYMBOL(__tracepoint_##name)

#else /* !CONFIG_TRACEPOINTS */
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
	}

#define DEFINE_TRACE_FN(name, reg, unreg)
#define DEFINE_TRACE(name)
#define EXPORT_TRACEPOINT_SYMBOL_GPL(name)
#define EXPORT_TRACEPOINT_SYMBOL(name)

#endif /* CONFIG_TRACEPOINTS */

/*
 * The need for the DECLARE_TRACE_NOARGS() is to handle the prototype
 * (void). "void" is a special value in a function prototype and can
 * not be combined with other arguments. Since the DECLARE_TRACE()
 * macro adds a data element at the beginning of the prototype,
 * we need a way to differentiate "(void *data, proto)" from
 * "(void *data, void)". The second prototype is invalid.
 *
 * DECLARE_TRACE_NOARGS() passes "void" as the tracepoint prototype
 * and "void *__data" as the callback prototype.
 *
 * DECLARE_TRACE() passes "proto" as the tracepoint protoype and
 * "void *__data, proto" as the callback prototype.
 */
#define DECLARE_TRACE_NOARGS(name)					\
		__DECLARE_TRACE(name, void, , 1, void *__data, __data)

#define DECLARE_TRACE(name, proto, args)				\
		__DECLARE_TRACE(name, PARAMS(proto), PARAMS(args), 1,	\
				PARAMS(void *__data, proto),		\
				PARAMS(__data, args))

#define DECLARE_TRACE_CONDITION(name, proto, args, cond)		\
	__DECLARE_TRACE(name, PARAMS(proto), PARAMS(args), PARAMS(cond), \
			PARAMS(void *__data, proto),			\
			PARAMS(__data, args))

#define TRACE_EVENT_FLAGS(event, flag)

#endif /* DECLARE_TRACE */

#ifndef TRACE_EVENT
/*
 * For use with the TRACE_EVENT macro:
 *
 * We define a tracepoint, its arguments, its printk format
 * and its 'fast binay record' layout.
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
#define TRACE_EVENT_CONDITION(name, proto, args, cond,		\
			      struct, assign, print)		\
	DECLARE_TRACE_CONDITION(name, PARAMS(proto),		\
				PARAMS(args), PARAMS(cond))

#define TRACE_EVENT_FLAGS(event, flag)

#endif /* ifdef TRACE_EVENT (see note above) */
