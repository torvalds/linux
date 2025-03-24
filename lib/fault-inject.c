// SPDX-License-Identifier: GPL-2.0-only
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/prandom.h>
#include <linux/debugfs.h>
#include <linux/sched.h>
#include <linux/stat.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/export.h>
#include <linux/interrupt.h>
#include <linux/stacktrace.h>
#include <linux/fault-inject.h>

/*
 * The should_fail() functions use prandom instead of the normal Linux RNG
 * since they don't need cryptographically secure random numbers.
 */
static DEFINE_PER_CPU(struct rnd_state, fault_rnd_state);

static u32 fault_prandom_u32_below_100(void)
{
	struct rnd_state *state;
	u32 res;

	state = &get_cpu_var(fault_rnd_state);
	res = prandom_u32_state(state);
	put_cpu_var(fault_rnd_state);

	return res % 100;
}

/*
 * setup_fault_attr() is a helper function for various __setup handlers, so it
 * returns 0 on error, because that is what __setup handlers do.
 */
int setup_fault_attr(struct fault_attr *attr, char *str)
{
	unsigned long probability;
	unsigned long interval;
	int times;
	int space;

	/* "<interval>,<probability>,<space>,<times>" */
	if (sscanf(str, "%lu,%lu,%d,%d",
			&interval, &probability, &space, &times) < 4) {
		printk(KERN_WARNING
			"FAULT_INJECTION: failed to parse arguments\n");
		return 0;
	}

	prandom_init_once(&fault_rnd_state);

	attr->probability = probability;
	attr->interval = interval;
	atomic_set(&attr->times, times);
	atomic_set(&attr->space, space);

	return 1;
}
EXPORT_SYMBOL_GPL(setup_fault_attr);

static void fail_dump(struct fault_attr *attr)
{
	if (attr->verbose > 0 && __ratelimit(&attr->ratelimit_state)) {
		printk(KERN_NOTICE "FAULT_INJECTION: forcing a failure.\n"
		       "name %pd, interval %lu, probability %lu, "
		       "space %d, times %d\n", attr->dname,
		       attr->interval, attr->probability,
		       atomic_read(&attr->space),
		       atomic_read(&attr->times));
		if (attr->verbose > 1)
			dump_stack();
	}
}

#define atomic_dec_not_zero(v)		atomic_add_unless((v), -1, 0)

static bool fail_task(struct fault_attr *attr, struct task_struct *task)
{
	return in_task() && task->make_it_fail;
}

#define MAX_STACK_TRACE_DEPTH 32

#ifdef CONFIG_FAULT_INJECTION_STACKTRACE_FILTER

static bool fail_stacktrace(struct fault_attr *attr)
{
	int depth = attr->stacktrace_depth;
	unsigned long entries[MAX_STACK_TRACE_DEPTH];
	int n, nr_entries;
	bool found = (attr->require_start == 0 && attr->require_end == ULONG_MAX);

	if (depth == 0 || (found && !attr->reject_start && !attr->reject_end))
		return found;

	nr_entries = stack_trace_save(entries, depth, 1);
	for (n = 0; n < nr_entries; n++) {
		if (attr->reject_start <= entries[n] &&
			       entries[n] < attr->reject_end)
			return false;
		if (attr->require_start <= entries[n] &&
			       entries[n] < attr->require_end)
			found = true;
	}
	return found;
}

#else

static inline bool fail_stacktrace(struct fault_attr *attr)
{
	return true;
}

#endif /* CONFIG_FAULT_INJECTION_STACKTRACE_FILTER */

/*
 * This code is stolen from failmalloc-1.0
 * http://www.nongnu.org/failmalloc/
 */

bool should_fail_ex(struct fault_attr *attr, ssize_t size, int flags)
{
	bool stack_checked = false;

	if (in_task()) {
		unsigned int fail_nth = READ_ONCE(current->fail_nth);

		if (fail_nth) {
			if (!fail_stacktrace(attr))
				return false;

			stack_checked = true;
			fail_nth--;
			WRITE_ONCE(current->fail_nth, fail_nth);
			if (!fail_nth)
				goto fail;

			return false;
		}
	}

	/* No need to check any other properties if the probability is 0 */
	if (attr->probability == 0)
		return false;

	if (attr->task_filter && !fail_task(attr, current))
		return false;

	if (atomic_read(&attr->times) == 0)
		return false;

	if (!stack_checked && !fail_stacktrace(attr))
		return false;

	if (atomic_read(&attr->space) > size) {
		atomic_sub(size, &attr->space);
		return false;
	}

	if (attr->interval > 1) {
		attr->count++;
		if (attr->count % attr->interval)
			return false;
	}

	if (attr->probability <= fault_prandom_u32_below_100())
		return false;

fail:
	if (!(flags & FAULT_NOWARN))
		fail_dump(attr);

	if (atomic_read(&attr->times) != -1)
		atomic_dec_not_zero(&attr->times);

	return true;
}

bool should_fail(struct fault_attr *attr, ssize_t size)
{
	return should_fail_ex(attr, size, 0);
}
EXPORT_SYMBOL_GPL(should_fail);

#ifdef CONFIG_FAULT_INJECTION_DEBUG_FS

static int debugfs_ul_set(void *data, u64 val)
{
	*(unsigned long *)data = val;
	return 0;
}

static int debugfs_ul_get(void *data, u64 *val)
{
	*val = *(unsigned long *)data;
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(fops_ul, debugfs_ul_get, debugfs_ul_set, "%llu\n");

static void debugfs_create_ul(const char *name, umode_t mode,
			      struct dentry *parent, unsigned long *value)
{
	debugfs_create_file(name, mode, parent, value, &fops_ul);
}

#ifdef CONFIG_FAULT_INJECTION_STACKTRACE_FILTER

static int debugfs_stacktrace_depth_set(void *data, u64 val)
{
	*(unsigned long *)data =
		min_t(unsigned long, val, MAX_STACK_TRACE_DEPTH);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(fops_stacktrace_depth, debugfs_ul_get,
			debugfs_stacktrace_depth_set, "%llu\n");

static void debugfs_create_stacktrace_depth(const char *name, umode_t mode,
					    struct dentry *parent,
					    unsigned long *value)
{
	debugfs_create_file(name, mode, parent, value, &fops_stacktrace_depth);
}

#endif /* CONFIG_FAULT_INJECTION_STACKTRACE_FILTER */

struct dentry *fault_create_debugfs_attr(const char *name,
			struct dentry *parent, struct fault_attr *attr)
{
	umode_t mode = S_IFREG | S_IRUSR | S_IWUSR;
	struct dentry *dir;

	dir = debugfs_create_dir(name, parent);
	if (IS_ERR(dir))
		return dir;

	prandom_init_once(&fault_rnd_state);

	debugfs_create_ul("probability", mode, dir, &attr->probability);
	debugfs_create_ul("interval", mode, dir, &attr->interval);
	debugfs_create_atomic_t("times", mode, dir, &attr->times);
	debugfs_create_atomic_t("space", mode, dir, &attr->space);
	debugfs_create_ul("verbose", mode, dir, &attr->verbose);
	debugfs_create_u32("verbose_ratelimit_interval_ms", mode, dir,
			   &attr->ratelimit_state.interval);
	debugfs_create_u32("verbose_ratelimit_burst", mode, dir,
			   &attr->ratelimit_state.burst);
	debugfs_create_bool("task-filter", mode, dir, &attr->task_filter);

#ifdef CONFIG_FAULT_INJECTION_STACKTRACE_FILTER
	debugfs_create_stacktrace_depth("stacktrace-depth", mode, dir,
					&attr->stacktrace_depth);
	debugfs_create_xul("require-start", mode, dir, &attr->require_start);
	debugfs_create_xul("require-end", mode, dir, &attr->require_end);
	debugfs_create_xul("reject-start", mode, dir, &attr->reject_start);
	debugfs_create_xul("reject-end", mode, dir, &attr->reject_end);
#endif /* CONFIG_FAULT_INJECTION_STACKTRACE_FILTER */

	attr->dname = dget(dir);
	return dir;
}
EXPORT_SYMBOL_GPL(fault_create_debugfs_attr);

#endif /* CONFIG_FAULT_INJECTION_DEBUG_FS */

#ifdef CONFIG_FAULT_INJECTION_CONFIGFS

/* These configfs attribute utilities are copied from drivers/block/null_blk/main.c */

static ssize_t fault_uint_attr_show(unsigned int val, char *page)
{
	return snprintf(page, PAGE_SIZE, "%u\n", val);
}

static ssize_t fault_ulong_attr_show(unsigned long val, char *page)
{
	return snprintf(page, PAGE_SIZE, "%lu\n", val);
}

static ssize_t fault_bool_attr_show(bool val, char *page)
{
	return snprintf(page, PAGE_SIZE, "%u\n", val);
}

static ssize_t fault_atomic_t_attr_show(atomic_t val, char *page)
{
	return snprintf(page, PAGE_SIZE, "%d\n", atomic_read(&val));
}

static ssize_t fault_uint_attr_store(unsigned int *val, const char *page, size_t count)
{
	unsigned int tmp;
	int result;

	result = kstrtouint(page, 0, &tmp);
	if (result < 0)
		return result;

	*val = tmp;
	return count;
}

static ssize_t fault_ulong_attr_store(unsigned long *val, const char *page, size_t count)
{
	int result;
	unsigned long tmp;

	result = kstrtoul(page, 0, &tmp);
	if (result < 0)
		return result;

	*val = tmp;
	return count;
}

static ssize_t fault_bool_attr_store(bool *val, const char *page, size_t count)
{
	bool tmp;
	int result;

	result = kstrtobool(page, &tmp);
	if (result < 0)
		return result;

	*val = tmp;
	return count;
}

static ssize_t fault_atomic_t_attr_store(atomic_t *val, const char *page, size_t count)
{
	int tmp;
	int result;

	result = kstrtoint(page, 0, &tmp);
	if (result < 0)
		return result;

	atomic_set(val, tmp);
	return count;
}

#define CONFIGFS_ATTR_NAMED(_pfx, _name, _attr_name)	\
static struct configfs_attribute _pfx##attr_##_name = {	\
	.ca_name	= _attr_name,			\
	.ca_mode	= 0644,				\
	.ca_owner	= THIS_MODULE,			\
	.show		= _pfx##_name##_show,		\
	.store		= _pfx##_name##_store,		\
}

static struct fault_config *to_fault_config(struct config_item *item)
{
	return container_of(to_config_group(item), struct fault_config, group);
}

#define FAULT_CONFIGFS_ATTR_NAMED(NAME, ATTR_NAME, MEMBER, TYPE)				\
static ssize_t fault_##NAME##_show(struct config_item *item, char *page)			\
{												\
	return fault_##TYPE##_attr_show(to_fault_config(item)->attr.MEMBER, page);		\
}												\
static ssize_t fault_##NAME##_store(struct config_item *item, const char *page, size_t count)	\
{												\
	struct fault_config *config = to_fault_config(item);					\
	return fault_##TYPE##_attr_store(&config->attr.MEMBER, page, count);			\
}												\
CONFIGFS_ATTR_NAMED(fault_, NAME, ATTR_NAME)

#define FAULT_CONFIGFS_ATTR(NAME, TYPE)	\
	FAULT_CONFIGFS_ATTR_NAMED(NAME, __stringify(NAME), NAME, TYPE)

FAULT_CONFIGFS_ATTR(probability, ulong);
FAULT_CONFIGFS_ATTR(interval, ulong);
FAULT_CONFIGFS_ATTR(times, atomic_t);
FAULT_CONFIGFS_ATTR(space, atomic_t);
FAULT_CONFIGFS_ATTR(verbose, ulong);
FAULT_CONFIGFS_ATTR_NAMED(ratelimit_interval, "verbose_ratelimit_interval_ms",
		ratelimit_state.interval, uint);
FAULT_CONFIGFS_ATTR_NAMED(ratelimit_burst, "verbose_ratelimit_burst",
		ratelimit_state.burst, uint);
FAULT_CONFIGFS_ATTR_NAMED(task_filter, "task-filter", task_filter, bool);

#ifdef CONFIG_FAULT_INJECTION_STACKTRACE_FILTER

static ssize_t fault_stacktrace_depth_show(struct config_item *item, char *page)
{
	return fault_ulong_attr_show(to_fault_config(item)->attr.stacktrace_depth, page);
}

static ssize_t fault_stacktrace_depth_store(struct config_item *item, const char *page,
		size_t count)
{
	int result;
	unsigned long tmp;

	result = kstrtoul(page, 0, &tmp);
	if (result < 0)
		return result;

	to_fault_config(item)->attr.stacktrace_depth =
		min_t(unsigned long, tmp, MAX_STACK_TRACE_DEPTH);

	return count;
}

CONFIGFS_ATTR_NAMED(fault_, stacktrace_depth, "stacktrace-depth");

static ssize_t fault_xul_attr_show(unsigned long val, char *page)
{
	return snprintf(page, PAGE_SIZE,
			sizeof(val) == sizeof(u32) ? "0x%08lx\n" : "0x%016lx\n", val);
}

static ssize_t fault_xul_attr_store(unsigned long *val, const char *page, size_t count)
{
	return fault_ulong_attr_store(val, page, count);
}

FAULT_CONFIGFS_ATTR_NAMED(require_start, "require-start", require_start, xul);
FAULT_CONFIGFS_ATTR_NAMED(require_end, "require-end", require_end, xul);
FAULT_CONFIGFS_ATTR_NAMED(reject_start, "reject-start", reject_start, xul);
FAULT_CONFIGFS_ATTR_NAMED(reject_end, "reject-end", reject_end, xul);

#endif /* CONFIG_FAULT_INJECTION_STACKTRACE_FILTER */

static struct configfs_attribute *fault_config_attrs[] = {
	&fault_attr_probability,
	&fault_attr_interval,
	&fault_attr_times,
	&fault_attr_space,
	&fault_attr_verbose,
	&fault_attr_ratelimit_interval,
	&fault_attr_ratelimit_burst,
	&fault_attr_task_filter,
#ifdef CONFIG_FAULT_INJECTION_STACKTRACE_FILTER
	&fault_attr_stacktrace_depth,
	&fault_attr_require_start,
	&fault_attr_require_end,
	&fault_attr_reject_start,
	&fault_attr_reject_end,
#endif /* CONFIG_FAULT_INJECTION_STACKTRACE_FILTER */
	NULL,
};

static const struct config_item_type fault_config_type = {
	.ct_attrs	= fault_config_attrs,
	.ct_owner	= THIS_MODULE,
};

void fault_config_init(struct fault_config *config, const char *name)
{
	prandom_init_once(&fault_rnd_state);

	config_group_init_type_name(&config->group, name, &fault_config_type);
}
EXPORT_SYMBOL_GPL(fault_config_init);

#endif /* CONFIG_FAULT_INJECTION_CONFIGFS */
