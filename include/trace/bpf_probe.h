/* SPDX-License-Identifier: GPL-2.0 */

#undef TRACE_SYSTEM_VAR

#ifdef CONFIG_BPF_EVENTS

#include "stages/stage6_event_callback.h"

#undef __perf_count
#define __perf_count(c)	(c)

#undef __perf_task
#define __perf_task(t)	(t)

/* cast any integer, pointer, or small struct to u64 */
#define UINTTYPE(size) \
	__typeof__(__builtin_choose_expr(size == 1,  (u8)1, \
		   __builtin_choose_expr(size == 2, (u16)2, \
		   __builtin_choose_expr(size == 4, (u32)3, \
		   __builtin_choose_expr(size == 8, (u64)4, \
					 (void)5)))))
#define __CAST_TO_U64(x) ({ \
	typeof(x) __src = (x); \
	UINTTYPE(sizeof(x)) __dst; \
	memcpy(&__dst, &__src, sizeof(__dst)); \
	(u64)__dst; })

#define __CAST1(a,...) __CAST_TO_U64(a)
#define __CAST2(a,...) __CAST_TO_U64(a), __CAST1(__VA_ARGS__)
#define __CAST3(a,...) __CAST_TO_U64(a), __CAST2(__VA_ARGS__)
#define __CAST4(a,...) __CAST_TO_U64(a), __CAST3(__VA_ARGS__)
#define __CAST5(a,...) __CAST_TO_U64(a), __CAST4(__VA_ARGS__)
#define __CAST6(a,...) __CAST_TO_U64(a), __CAST5(__VA_ARGS__)
#define __CAST7(a,...) __CAST_TO_U64(a), __CAST6(__VA_ARGS__)
#define __CAST8(a,...) __CAST_TO_U64(a), __CAST7(__VA_ARGS__)
#define __CAST9(a,...) __CAST_TO_U64(a), __CAST8(__VA_ARGS__)
#define __CAST10(a,...) __CAST_TO_U64(a), __CAST9(__VA_ARGS__)
#define __CAST11(a,...) __CAST_TO_U64(a), __CAST10(__VA_ARGS__)
#define __CAST12(a,...) __CAST_TO_U64(a), __CAST11(__VA_ARGS__)
/* tracepoints with more than 12 arguments will hit build error */
#define CAST_TO_U64(...) CONCATENATE(__CAST, COUNT_ARGS(__VA_ARGS__))(__VA_ARGS__)

#define __BPF_DECLARE_TRACE(call, proto, args)				\
static notrace void							\
__bpf_trace_##call(void *__data, proto)					\
{									\
	struct bpf_prog *prog = __data;					\
	CONCATENATE(bpf_trace_run, COUNT_ARGS(args))(prog, CAST_TO_U64(args));	\
}

#undef DECLARE_EVENT_CLASS
#define DECLARE_EVENT_CLASS(call, proto, args, tstruct, assign, print)	\
	__BPF_DECLARE_TRACE(call, PARAMS(proto), PARAMS(args))

/*
 * This part is compiled out, it is only here as a build time check
 * to make sure that if the tracepoint handling changes, the
 * bpf probe will fail to compile unless it too is updated.
 */
#define __DEFINE_EVENT(template, call, proto, args, size)		\
static inline void bpf_test_probe_##call(void)				\
{									\
	check_trace_callback_type_##call(__bpf_trace_##template);	\
}									\
typedef void (*btf_trace_##call)(void *__data, proto);			\
static union {								\
	struct bpf_raw_event_map event;					\
	btf_trace_##call handler;					\
} __bpf_trace_tp_map_##call __used					\
__section("__bpf_raw_tp_map") = {					\
	.event = {							\
		.tp		= &__tracepoint_##call,			\
		.bpf_func	= __bpf_trace_##template,		\
		.num_args	= COUNT_ARGS(args),			\
		.writable_size	= size,					\
	},								\
};

#define FIRST(x, ...) x

#define __CHECK_WRITABLE_BUF_SIZE(call, proto, args, size)		\
static inline void bpf_test_buffer_##call(void)				\
{									\
	/* BUILD_BUG_ON() is ignored if the code is completely eliminated, but \
	 * BUILD_BUG_ON_ZERO() uses a different mechanism that is not	\
	 * dead-code-eliminated.					\
	 */								\
	FIRST(proto);							\
	(void)BUILD_BUG_ON_ZERO(size != sizeof(*FIRST(args)));		\
}

#undef DEFINE_EVENT_WRITABLE
#define DEFINE_EVENT_WRITABLE(template, call, proto, args, size) \
	__CHECK_WRITABLE_BUF_SIZE(call, PARAMS(proto), PARAMS(args), size) \
	__DEFINE_EVENT(template, call, PARAMS(proto), PARAMS(args), size)

#undef DEFINE_EVENT
#define DEFINE_EVENT(template, call, proto, args)			\
	__DEFINE_EVENT(template, call, PARAMS(proto), PARAMS(args), 0)

#undef DEFINE_EVENT_PRINT
#define DEFINE_EVENT_PRINT(template, name, proto, args, print)	\
	DEFINE_EVENT(template, name, PARAMS(proto), PARAMS(args))

#undef DECLARE_TRACE
#define DECLARE_TRACE(call, proto, args)				\
	__BPF_DECLARE_TRACE(call, PARAMS(proto), PARAMS(args))		\
	__DEFINE_EVENT(call, call, PARAMS(proto), PARAMS(args), 0)

#undef DECLARE_TRACE_WRITABLE
#define DECLARE_TRACE_WRITABLE(call, proto, args, size) \
	__CHECK_WRITABLE_BUF_SIZE(call, PARAMS(proto), PARAMS(args), size) \
	__BPF_DECLARE_TRACE(call, PARAMS(proto), PARAMS(args)) \
	__DEFINE_EVENT(call, call, PARAMS(proto), PARAMS(args), size)

#include TRACE_INCLUDE(TRACE_INCLUDE_FILE)

#undef DECLARE_TRACE_WRITABLE
#undef DEFINE_EVENT_WRITABLE
#undef __CHECK_WRITABLE_BUF_SIZE
#undef __DEFINE_EVENT
#undef FIRST

#endif /* CONFIG_BPF_EVENTS */
