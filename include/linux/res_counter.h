#ifndef __RES_COUNTER_H__
#define __RES_COUNTER_H__

/*
 * Resource Counters
 * Contain common data types and routines for resource accounting
 *
 * Copyright 2007 OpenVZ SWsoft Inc
 *
 * Author: Pavel Emelianov <xemul@openvz.org>
 *
 * See Documentation/cgroups/resource_counter.txt for more
 * info about what this counter is.
 */

#include <linux/cgroup.h>

/*
 * The core object. the cgroup that wishes to account for some
 * resource may include this counter into its structures and use
 * the helpers described beyond
 */

struct res_counter {
	/*
	 * the current resource consumption level
	 */
	unsigned long long usage;
	/*
	 * the maximal value of the usage from the counter creation
	 */
	unsigned long long max_usage;
	/*
	 * the limit that usage cannot exceed
	 */
	unsigned long long limit;
	/*
	 * the limit that usage can be exceed
	 */
	unsigned long long soft_limit;
	/*
	 * the number of unsuccessful attempts to consume the resource
	 */
	unsigned long long failcnt;
	/*
	 * the lock to protect all of the above.
	 * the routines below consider this to be IRQ-safe
	 */
	spinlock_t lock;
	/*
	 * Parent counter, used for hierarchial resource accounting
	 */
	struct res_counter *parent;
};

#define RESOURCE_MAX (unsigned long long)LLONG_MAX

/**
 * Helpers to interact with userspace
 * res_counter_read_u64() - returns the value of the specified member.
 * res_counter_read/_write - put/get the specified fields from the
 * res_counter struct to/from the user
 *
 * @counter:     the counter in question
 * @member:  the field to work with (see RES_xxx below)
 * @buf:     the buffer to opeate on,...
 * @nbytes:  its size...
 * @pos:     and the offset.
 */

u64 res_counter_read_u64(struct res_counter *counter, int member);

ssize_t res_counter_read(struct res_counter *counter, int member,
		const char __user *buf, size_t nbytes, loff_t *pos,
		int (*read_strategy)(unsigned long long val, char *s));

typedef int (*write_strategy_fn)(const char *buf, unsigned long long *val);

int res_counter_memparse_write_strategy(const char *buf,
					unsigned long long *res);

int res_counter_write(struct res_counter *counter, int member,
		      const char *buffer, write_strategy_fn write_strategy);

/*
 * the field descriptors. one for each member of res_counter
 */

enum {
	RES_USAGE,
	RES_MAX_USAGE,
	RES_LIMIT,
	RES_FAILCNT,
	RES_SOFT_LIMIT,
};

/*
 * helpers for accounting
 */

void res_counter_init(struct res_counter *counter, struct res_counter *parent);

/*
 * charge - try to consume more resource.
 *
 * @counter: the counter
 * @val: the amount of the resource. each controller defines its own
 *       units, e.g. numbers, bytes, Kbytes, etc
 *
 * returns 0 on success and <0 if the counter->usage will exceed the
 * counter->limit _locked call expects the counter->lock to be taken
 */

int __must_check res_counter_charge_locked(struct res_counter *counter,
		unsigned long val);
int __must_check res_counter_charge(struct res_counter *counter,
		unsigned long val, struct res_counter **limit_fail_at);

/*
 * uncharge - tell that some portion of the resource is released
 *
 * @counter: the counter
 * @val: the amount of the resource
 *
 * these calls check for usage underflow and show a warning on the console
 * _locked call expects the counter->lock to be taken
 */

void res_counter_uncharge_locked(struct res_counter *counter, unsigned long val);
void res_counter_uncharge(struct res_counter *counter, unsigned long val);

static inline bool res_counter_limit_check_locked(struct res_counter *cnt)
{
	if (cnt->usage < cnt->limit)
		return true;

	return false;
}

static inline bool res_counter_soft_limit_check_locked(struct res_counter *cnt)
{
	if (cnt->usage < cnt->soft_limit)
		return true;

	return false;
}

/**
 * Get the difference between the usage and the soft limit
 * @cnt: The counter
 *
 * Returns 0 if usage is less than or equal to soft limit
 * The difference between usage and soft limit, otherwise.
 */
static inline unsigned long long
res_counter_soft_limit_excess(struct res_counter *cnt)
{
	unsigned long long excess;
	unsigned long flags;

	spin_lock_irqsave(&cnt->lock, flags);
	if (cnt->usage <= cnt->soft_limit)
		excess = 0;
	else
		excess = cnt->usage - cnt->soft_limit;
	spin_unlock_irqrestore(&cnt->lock, flags);
	return excess;
}

/*
 * Helper function to detect if the cgroup is within it's limit or
 * not. It's currently called from cgroup_rss_prepare()
 */
static inline bool res_counter_check_under_limit(struct res_counter *cnt)
{
	bool ret;
	unsigned long flags;

	spin_lock_irqsave(&cnt->lock, flags);
	ret = res_counter_limit_check_locked(cnt);
	spin_unlock_irqrestore(&cnt->lock, flags);
	return ret;
}

/**
 * res_counter_check_margin - check if the counter allows charging
 * @cnt: the resource counter to check
 * @bytes: the number of bytes to check the remaining space against
 *
 * Returns a boolean value on whether the counter can be charged
 * @bytes or whether this would exceed the limit.
 */
static inline bool res_counter_check_margin(struct res_counter *cnt,
					    unsigned long bytes)
{
	bool ret;
	unsigned long flags;

	spin_lock_irqsave(&cnt->lock, flags);
	ret = cnt->limit - cnt->usage >= bytes;
	spin_unlock_irqrestore(&cnt->lock, flags);
	return ret;
}

static inline bool res_counter_check_under_soft_limit(struct res_counter *cnt)
{
	bool ret;
	unsigned long flags;

	spin_lock_irqsave(&cnt->lock, flags);
	ret = res_counter_soft_limit_check_locked(cnt);
	spin_unlock_irqrestore(&cnt->lock, flags);
	return ret;
}

static inline void res_counter_reset_max(struct res_counter *cnt)
{
	unsigned long flags;

	spin_lock_irqsave(&cnt->lock, flags);
	cnt->max_usage = cnt->usage;
	spin_unlock_irqrestore(&cnt->lock, flags);
}

static inline void res_counter_reset_failcnt(struct res_counter *cnt)
{
	unsigned long flags;

	spin_lock_irqsave(&cnt->lock, flags);
	cnt->failcnt = 0;
	spin_unlock_irqrestore(&cnt->lock, flags);
}

static inline int res_counter_set_limit(struct res_counter *cnt,
		unsigned long long limit)
{
	unsigned long flags;
	int ret = -EBUSY;

	spin_lock_irqsave(&cnt->lock, flags);
	if (cnt->usage <= limit) {
		cnt->limit = limit;
		ret = 0;
	}
	spin_unlock_irqrestore(&cnt->lock, flags);
	return ret;
}

static inline int
res_counter_set_soft_limit(struct res_counter *cnt,
				unsigned long long soft_limit)
{
	unsigned long flags;

	spin_lock_irqsave(&cnt->lock, flags);
	cnt->soft_limit = soft_limit;
	spin_unlock_irqrestore(&cnt->lock, flags);
	return 0;
}

#endif
