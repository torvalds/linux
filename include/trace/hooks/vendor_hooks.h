/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Note: we intentionally omit include file ifdef protection
 *  This is due to the way trace events work. If a file includes two
 *  trace event headers under one "CREATE_TRACE_POINTS" the first include
 *  will override the DECLARE_RESTRICTED_HOOK and break the second include.
 */

#include <linux/tracepoint.h>

#define DECLARE_HOOK DECLARE_TRACE

#ifdef TRACE_HEADER_MULTI_READ

#undef DECLARE_RESTRICTED_HOOK
#define DECLARE_RESTRICTED_HOOK(name, proto, args, cond) \
	DEFINE_TRACE(name)


/* prevent additional recursion */
#undef TRACE_HEADER_MULTI_READ
#else /* TRACE_HEADER_MULTI_READ */

#define DO_HOOK(tp, proto, args, cond)					\
	do {								\
		struct tracepoint_func *it_func_ptr;			\
		void *it_func;						\
		void *__data;						\
									\
		if (!(cond))						\
			return;						\
									\
		it_func_ptr = (tp)->funcs;				\
		if (it_func_ptr) {					\
			it_func = (it_func_ptr)->func;			\
			__data = (it_func_ptr)->data;			\
			((void(*)(proto))(it_func))(args);		\
			WARN_ON(((++it_func_ptr)->func));		\
		}							\
	} while (0)

#define __DECLARE_HOOK(name, proto, args, cond, data_proto, data_args)	\
	extern struct tracepoint __tracepoint_##name;			\
	static inline void trace_##name(proto)				\
	{								\
		if (static_key_false(&__tracepoint_##name.key))		\
			DO_HOOK(&__tracepoint_##name,			\
				TP_PROTO(data_proto),			\
				TP_ARGS(data_args),			\
				TP_CONDITION(cond));			\
	}								\
	static inline bool						\
	trace_##name##_enabled(void)					\
	{								\
		return static_key_false(&__tracepoint_##name.key);	\
	}								\
	static inline int						\
	register_trace_##name(void (*probe)(data_proto), void *data) 	\
	{								\
		/* only allow a single attachment */			\
		if (trace_##name##_enabled())				\
			return -EBUSY;					\
		return tracepoint_probe_register(&__tracepoint_##name,	\
						(void *)probe, data);	\
	}								\
	/* vendor hooks cannot be unregistered */			\

#undef DECLARE_RESTRICTED_HOOK
#define DECLARE_RESTRICTED_HOOK(name, proto, args, cond)		\
	__DECLARE_HOOK(name, PARAMS(proto), PARAMS(args),		\
			cond,						\
			PARAMS(void *__data, proto),			\
			PARAMS(__data, args))

#endif /* TRACE_HEADER_MULTI_READ */
