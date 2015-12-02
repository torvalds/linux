#ifndef TRACEPOINT_DEFS_H
#define TRACEPOINT_DEFS_H 1

/*
 * File can be included directly by headers who only want to access
 * tracepoint->key to guard out of line trace calls. Otherwise
 * linux/tracepoint.h should be used.
 */

#include <linux/atomic.h>
#include <linux/static_key.h>

struct tracepoint_func {
	void *func;
	void *data;
	int prio;
};

struct tracepoint {
	const char *name;		/* Tracepoint name */
	struct static_key key;
	void (*regfunc)(void);
	void (*unregfunc)(void);
	struct tracepoint_func __rcu *funcs;
};

#endif
