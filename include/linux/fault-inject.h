#ifndef _LINUX_FAULT_INJECT_H
#define _LINUX_FAULT_INJECT_H

#ifdef CONFIG_FAULT_INJECTION

#include <linux/types.h>
#include <linux/debugfs.h>
#include <asm/atomic.h>

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
	u32 task_filter;
	unsigned long stacktrace_depth;
	unsigned long require_start;
	unsigned long require_end;
	unsigned long reject_start;
	unsigned long reject_end;

	unsigned long count;

#ifdef CONFIG_FAULT_INJECTION_DEBUG_FS

	struct {
		struct dentry *dir;

		struct dentry *probability_file;
		struct dentry *interval_file;
		struct dentry *times_file;
		struct dentry *space_file;
		struct dentry *verbose_file;
		struct dentry *task_filter_file;
		struct dentry *stacktrace_depth_file;
		struct dentry *require_start_file;
		struct dentry *require_end_file;
		struct dentry *reject_start_file;
		struct dentry *reject_end_file;
	} dentries;

#endif
};

#define FAULT_ATTR_INITIALIZER {				\
		.interval = 1,					\
		.times = ATOMIC_INIT(1),			\
		.require_end = ULONG_MAX,			\
		.stacktrace_depth = 32,				\
		.verbose = 2,					\
	}

#define DECLARE_FAULT_ATTR(name) struct fault_attr name = FAULT_ATTR_INITIALIZER
int setup_fault_attr(struct fault_attr *attr, char *str);
void should_fail_srandom(unsigned long entropy);
bool should_fail(struct fault_attr *attr, ssize_t size);

#ifdef CONFIG_FAULT_INJECTION_DEBUG_FS

int init_fault_attr_dentries(struct fault_attr *attr, const char *name);
void cleanup_fault_attr_dentries(struct fault_attr *attr);

#else /* CONFIG_FAULT_INJECTION_DEBUG_FS */

static inline int init_fault_attr_dentries(struct fault_attr *attr,
					  const char *name)
{
	return -ENODEV;
}

static inline void cleanup_fault_attr_dentries(struct fault_attr *attr)
{
}

#endif /* CONFIG_FAULT_INJECTION_DEBUG_FS */

#endif /* CONFIG_FAULT_INJECTION */

#endif /* _LINUX_FAULT_INJECT_H */
