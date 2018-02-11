/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_FAULT_INJECT_H
#define _LINUX_FAULT_INJECT_H

#ifdef CONFIG_FAULT_INJECTION

#include <linux/types.h>
#include <linux/debugfs.h>
#include <linux/ratelimit.h>
#include <linux/atomic.h>

/*
 * For explanation of the elements of this struct, see
 * Documentation/fault-injection/fault-injection.txt
 */
struct fault_attr {
	unsigned long probability;
	unsigned long interval;
	atomic_t times;
	atomic_t space;
	unsigned long verbose;
	bool task_filter;
	unsigned long stacktrace_depth;
	unsigned long require_start;
	unsigned long require_end;
	unsigned long reject_start;
	unsigned long reject_end;

	unsigned long count;
	struct ratelimit_state ratelimit_state;
	struct dentry *dname;
};

#define FAULT_ATTR_INITIALIZER {					\
		.interval = 1,						\
		.times = ATOMIC_INIT(1),				\
		.require_end = ULONG_MAX,				\
		.stacktrace_depth = 32,					\
		.ratelimit_state = RATELIMIT_STATE_INIT_DISABLED,	\
		.verbose = 2,						\
		.dname = NULL,						\
	}

#define DECLARE_FAULT_ATTR(name) struct fault_attr name = FAULT_ATTR_INITIALIZER
int setup_fault_attr(struct fault_attr *attr, char *str);
bool should_fail(struct fault_attr *attr, ssize_t size);

#ifdef CONFIG_FAULT_INJECTION_DEBUG_FS

struct dentry *fault_create_debugfs_attr(const char *name,
			struct dentry *parent, struct fault_attr *attr);

#else /* CONFIG_FAULT_INJECTION_DEBUG_FS */

static inline struct dentry *fault_create_debugfs_attr(const char *name,
			struct dentry *parent, struct fault_attr *attr)
{
	return ERR_PTR(-ENODEV);
}

#endif /* CONFIG_FAULT_INJECTION_DEBUG_FS */

#endif /* CONFIG_FAULT_INJECTION */

struct kmem_cache;

#ifdef CONFIG_FAILSLAB
extern bool should_failslab(struct kmem_cache *s, gfp_t gfpflags);
#else
static inline bool should_failslab(struct kmem_cache *s, gfp_t gfpflags)
{
	return false;
}
#endif /* CONFIG_FAILSLAB */

#endif /* _LINUX_FAULT_INJECT_H */
