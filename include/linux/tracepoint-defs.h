/* SPDX-License-Identifier: GPL-2.0 */
#ifndef TRACEPOINT_DEFS_H
#define TRACEPOINT_DEFS_H 1

/*
 * File can be included directly by headers who only want to access
 * tracepoint->key to guard out of line trace calls, or the definition of
 * trace_print_flags{_u64}. Otherwise linux/tracepoint.h should be used.
 */

#include <linux/atomic.h>
#include <linux/static_key.h>

struct static_call_key;

struct trace_print_flags {
	unsigned long		mask;
	const char		*name;
};

struct trace_print_flags_u64 {
	unsigned long long	mask;
	const char		*name;
};

struct tracepoint_func {
	void *func;
	void *data;
	int prio;
};

struct tracepoint {
	const char *name;		/* Tracepoint name */
	struct static_key key;
	struct static_call_key *static_call_key;
	void *static_call_tramp;
	void *iterator;
	void *probestub;
	int (*regfunc)(void);
	void (*unregfunc)(void);
	struct tracepoint_func __rcu *funcs;
};

#ifdef CONFIG_HAVE_ARCH_PREL32_RELOCATIONS
typedef const int tracepoint_ptr_t;
#else
typedef struct tracepoint * const tracepoint_ptr_t;
#endif

struct bpf_raw_event_map {
	struct tracepoint	*tp;
	void			*bpf_func;
	u32			num_args;
	u32			writable_size;
} __aligned(32);

/*
 * If a tracepoint needs to be called from a header file, it is not
 * recommended to call it directly, as tracepoints in header files
 * may cause side-effects and bloat the kernel. Instead, use
 * tracepoint_enabled() to test if the tracepoint is enabled, then if
 * it is, call a wrapper function defined in a C file that will then
 * call the tracepoint.
 *
 * For "trace_foo_bar()", you would need to create a wrapper function
 * in a C file to call trace_foo_bar():
 *   void do_trace_foo_bar(args) { trace_foo_bar(args); }
 * Then in the header file, declare the tracepoint:
 *   DECLARE_TRACEPOINT(foo_bar);
 * And call your wrapper:
 *   static inline void some_inlined_function() {
 *            [..]
 *            if (tracepoint_enabled(foo_bar))
 *                    do_trace_foo_bar(args);
 *            [..]
 *   }
 *
 * Note: tracepoint_enabled(foo_bar) is equivalent to trace_foo_bar_enabled()
 *   but is safe to have in headers, where trace_foo_bar_enabled() is not.
 */
#define DECLARE_TRACEPOINT(tp) \
	extern struct tracepoint __tracepoint_##tp

#ifdef CONFIG_TRACEPOINTS
# define tracepoint_enabled(tp) \
	static_key_false(&(__tracepoint_##tp).key)
#else
# define tracepoint_enabled(tracepoint) false
#endif

#endif
