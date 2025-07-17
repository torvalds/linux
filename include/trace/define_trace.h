/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Trace files that want to automate creation of all tracepoints defined
 * in their file should include this file. The following are macros that the
 * trace file may define:
 *
 * TRACE_SYSTEM defines the system the tracepoint is for
 *
 * TRACE_INCLUDE_FILE if the file name is something other than TRACE_SYSTEM.h
 *     This macro may be defined to tell define_trace.h what file to include.
 *     Note, leave off the ".h".
 *
 * TRACE_INCLUDE_PATH if the path is something other than core kernel include/trace
 *     then this macro can define the path to use. Note, the path is relative to
 *     define_trace.h, not the file including it. Full path names for out of tree
 *     modules must be used.
 */

#ifdef CREATE_TRACE_POINTS

/* Prevent recursion */
#undef CREATE_TRACE_POINTS

#include <linux/stringify.h>

#undef TRACE_EVENT
#define TRACE_EVENT(name, proto, args, tstruct, assign, print)	\
	DEFINE_TRACE(name, PARAMS(proto), PARAMS(args))

#undef TRACE_EVENT_CONDITION
#define TRACE_EVENT_CONDITION(name, proto, args, cond, tstruct, assign, print) \
	TRACE_EVENT(name,						\
		PARAMS(proto),						\
		PARAMS(args),						\
		PARAMS(tstruct),					\
		PARAMS(assign),						\
		PARAMS(print))

#undef TRACE_EVENT_FN
#define TRACE_EVENT_FN(name, proto, args, tstruct,		\
		assign, print, reg, unreg)			\
	DEFINE_TRACE_FN(name, reg, unreg, PARAMS(proto), PARAMS(args))

#undef TRACE_EVENT_FN_COND
#define TRACE_EVENT_FN_COND(name, proto, args, cond, tstruct,		\
		assign, print, reg, unreg)			\
	DEFINE_TRACE_FN(name, reg, unreg, PARAMS(proto), PARAMS(args))

#undef TRACE_EVENT_SYSCALL
#define TRACE_EVENT_SYSCALL(name, proto, args, struct, assign, print, reg, unreg) \
	DEFINE_TRACE_SYSCALL(name, reg, unreg, PARAMS(proto), PARAMS(args))

#undef TRACE_EVENT_NOP
#define TRACE_EVENT_NOP(name, proto, args, struct, assign, print)

#undef DEFINE_EVENT_NOP
#define DEFINE_EVENT_NOP(template, name, proto, args)

#undef DEFINE_EVENT
#define DEFINE_EVENT(template, name, proto, args) \
	DEFINE_TRACE(name, PARAMS(proto), PARAMS(args))

#undef DEFINE_EVENT_FN
#define DEFINE_EVENT_FN(template, name, proto, args, reg, unreg) \
	DEFINE_TRACE_FN(name, reg, unreg, PARAMS(proto), PARAMS(args))

#undef DEFINE_EVENT_PRINT
#define DEFINE_EVENT_PRINT(template, name, proto, args, print)	\
	DEFINE_TRACE(name, PARAMS(proto), PARAMS(args))

#undef DEFINE_EVENT_CONDITION
#define DEFINE_EVENT_CONDITION(template, name, proto, args, cond) \
	DEFINE_EVENT(template, name, PARAMS(proto), PARAMS(args))

#undef DECLARE_TRACE
#define DECLARE_TRACE(name, proto, args)	\
	DEFINE_TRACE(name##_tp, PARAMS(proto), PARAMS(args))

#undef DECLARE_TRACE_CONDITION
#define DECLARE_TRACE_CONDITION(name, proto, args, cond)	\
	DEFINE_TRACE(name##_tp, PARAMS(proto), PARAMS(args))

#undef DECLARE_TRACE_EVENT
#define DECLARE_TRACE_EVENT(name, proto, args)	\
	DEFINE_TRACE(name, PARAMS(proto), PARAMS(args))

#undef DECLARE_TRACE_EVENT_CONDITION
#define DECLARE_TRACE_EVENT_CONDITION(name, proto, args, cond)	\
	DEFINE_TRACE(name, PARAMS(proto), PARAMS(args))

/* If requested, create helpers for calling these tracepoints from Rust. */
#ifdef CREATE_RUST_TRACE_POINTS
#undef DEFINE_RUST_DO_TRACE
#define DEFINE_RUST_DO_TRACE(name, proto, args)	\
	__DEFINE_RUST_DO_TRACE(name, PARAMS(proto), PARAMS(args))
#endif

#undef TRACE_INCLUDE
#undef __TRACE_INCLUDE

#ifndef TRACE_INCLUDE_FILE
# define TRACE_INCLUDE_FILE TRACE_SYSTEM
# define UNDEF_TRACE_INCLUDE_FILE
#endif

#ifndef TRACE_INCLUDE_PATH
# define __TRACE_INCLUDE(system) <trace/events/system.h>
# define UNDEF_TRACE_INCLUDE_PATH
#else
# define __TRACE_INCLUDE(system) __stringify(TRACE_INCLUDE_PATH/system.h)
#endif

# define TRACE_INCLUDE(system) __TRACE_INCLUDE(system)

/* Let the trace headers be reread */
#define TRACE_HEADER_MULTI_READ

#include TRACE_INCLUDE(TRACE_INCLUDE_FILE)

/* Make all open coded DECLARE_TRACE nops */
#undef DECLARE_TRACE
#define DECLARE_TRACE(name, proto, args)
#undef DECLARE_TRACE_CONDITION
#define DECLARE_TRACE_CONDITION(name, proto, args, cond)

#undef DECLARE_TRACE_EVENT
#define DECLARE_TRACE_EVENT(name, proto, args)
#undef DECLARE_TRACE_EVENT_CONDITION
#define DECLARE_TRACE_EVENT_CONDITION(name, proto, args, cond)

#ifdef TRACEPOINTS_ENABLED
#include <trace/trace_events.h>
#include <trace/perf.h>
#include <trace/bpf_probe.h>
#endif

#undef TRACE_EVENT
#undef TRACE_EVENT_FN
#undef TRACE_EVENT_FN_COND
#undef TRACE_EVENT_SYSCALL
#undef TRACE_EVENT_CONDITION
#undef TRACE_EVENT_NOP
#undef DEFINE_EVENT_NOP
#undef DECLARE_EVENT_CLASS
#undef DEFINE_EVENT
#undef DEFINE_EVENT_FN
#undef DEFINE_EVENT_PRINT
#undef DEFINE_EVENT_CONDITION
#undef TRACE_HEADER_MULTI_READ
#undef DECLARE_TRACE
#undef DECLARE_TRACE_CONDITION
#undef DECLARE_TRACE_EVENT
#undef DECLARE_TRACE_EVENT_CONDITION

/* Only undef what we defined in this file */
#ifdef UNDEF_TRACE_INCLUDE_FILE
# undef TRACE_INCLUDE_FILE
# undef UNDEF_TRACE_INCLUDE_FILE
#endif

#ifdef UNDEF_TRACE_INCLUDE_PATH
# undef TRACE_INCLUDE_PATH
# undef UNDEF_TRACE_INCLUDE_PATH
#endif

#ifdef CREATE_RUST_TRACE_POINTS
# undef DEFINE_RUST_DO_TRACE
# define DEFINE_RUST_DO_TRACE(name, proto, args)
#endif

/* We may be processing more files */
#define CREATE_TRACE_POINTS

#endif /* CREATE_TRACE_POINTS */
