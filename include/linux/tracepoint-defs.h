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
} __aligned(32);

#endif
