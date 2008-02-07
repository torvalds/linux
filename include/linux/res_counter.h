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
	unsigned long usage;
	/*
	 * the limit that usage cannot exceed
	 */
	unsigned long limit;
	/*
	 * the number of unsuccessful attempts to consume the resource
	 */
	unsigned long failcnt;
	/*
	 * the lock to protect all of the above.
	 * the routines below consider this to be IRQ-safe
	 */
	spinlock_t lock;
};

/*
 * Helpers to interact with userspace
 * res_counter_read/_write - put/get the specified fields from the
 * res_counter struct to/from the user
 *
 * @counter:     the counter in question
 * @member:  the field to work with (see RES_xxx below)
 * @buf:     the buffer to opeate on,...
 * @nbytes:  its size...
 * @pos:     and the offset.
 */

ssize_t res_counter_read(struct res_counter *counter, int member,
		const char __user *buf, size_t nbytes, loff_t *pos);
ssize_t res_counter_write(struct res_counter *counter, int member,
		const char __user *buf, size_t nbytes, loff_t *pos);

/*
 * the field descriptors. one for each member of res_counter
 */

enum {
	RES_USAGE,
	RES_LIMIT,
	RES_FAILCNT,
};

/*
 * helpers for accounting
 */

void res_counter_init(struct res_counter *counter);

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

int res_counter_charge_locked(struct res_counter *counter, unsigned long val);
int res_counter_charge(struct res_counter *counter, unsigned long val);

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

#endif
