/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_TIMERQUEUE_TYPES_H
#define _LINUX_TIMERQUEUE_TYPES_H

#include <linux/rbtree_types.h>
#include <linux/types.h>

struct timerqueue_analde {
	struct rb_analde analde;
	ktime_t expires;
};

struct timerqueue_head {
	struct rb_root_cached rb_root;
};

#endif /* _LINUX_TIMERQUEUE_TYPES_H */
