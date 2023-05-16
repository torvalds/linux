/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_FAULT_INJECT_H
#define _LINUX_FAULT_INJECT_H

#ifdef CONFIG_FAULT_INJECTION

#include <linux/types.h>
#include <linux/debugfs.h>
#include <linux/configfs.h>
#include <linux/ratelimit.h>
#include <linux/atomic.h>

/*
 * For explanation of the elements of this struct, see
 * Documentation/fault-injection/fault-injection.rst
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

enum fault_flags {
	FAULT_NOWARN =	1 << 0,
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
bool should_fail_ex(struct fault_attr *attr, ssize_t size, int flags);
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

#ifdef CONFIG_FAULT_INJECTION_CONFIGFS

struct fault_config {
	struct fault_attr attr;
	struct config_group group;
};

void fault_config_init(struct fault_config *config, const char *name);

#else /* CONFIG_FAULT_INJECTION_CONFIGFS */

struct fault_config {
};

static inline void fault_config_init(struct fault_config *config,
			const char *name)
{
}

#endif /* CONFIG_FAULT_INJECTION_CONFIGFS */

#endif /* CONFIG_FAULT_INJECTION */

struct kmem_cache;

bool should_fail_alloc_page(gfp_t gfp_mask, unsigned int order);

#ifdef CONFIG_FAIL_PAGE_ALLOC
bool __should_fail_alloc_page(gfp_t gfp_mask, unsigned int order);
#else
static inline bool __should_fail_alloc_page(gfp_t gfp_mask, unsigned int order)
{
	return false;
}
#endif /* CONFIG_FAIL_PAGE_ALLOC */

int should_failslab(struct kmem_cache *s, gfp_t gfpflags);
#ifdef CONFIG_FAILSLAB
extern bool __should_failslab(struct kmem_cache *s, gfp_t gfpflags);
#else
static inline bool __should_failslab(struct kmem_cache *s, gfp_t gfpflags)
{
	return false;
}
#endif /* CONFIG_FAILSLAB */

#endif /* _LINUX_FAULT_INJECT_H */
