/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __MAPLE_SHARED_H__
#define __MAPLE_SHARED_H__

#define CONFIG_DEBUG_MAPLE_TREE
#define CONFIG_MAPLE_SEARCH
#define MAPLE_32BIT (MAPLE_NODE_SLOTS > 31)
#include "shared.h"
#include <stdlib.h>
#include <time.h>
#include <assert.h>
#include "linux/init.h"

void maple_rcu_cb(struct rcu_head *head);
#define rcu_cb		maple_rcu_cb

#define kfree_rcu(_struct, _memb)		\
do {                                            \
    typeof(_struct) _p_struct = (_struct);      \
                                                \
    call_rcu(&((_p_struct)->_memb), rcu_cb);    \
} while(0);

/* Make WARN() fatal */
#undef WARN
#undef WARN_ON
#undef WARN_ONCE
#undef WARN_ON_ONCE
#define WARN(condition, format...) ({				\
	int __ret_warn_on = !!(condition);			\
	if (unlikely(__ret_warn_on)) {				\
		__WARN_printf(format);				\
		assert(0);					\
	}							\
	unlikely(__ret_warn_on);				\
})
#define WARN_ON(condition)					\
	WARN(condition, "assertion failed at %s:%d\n", __FILE__, __LINE__)
#define WARN_ONCE(condition, format...)	WARN(condition, format)
#define WARN_ON_ONCE(condition)		WARN_ON(condition)

/* Once state for the debug macros */
#ifndef __ONCE_LITE_IF
#define __ONCE_LITE_IF(condition)				\
	({							\
		static bool __already_done;			\
		bool __ret_once = false;			\
								\
		if ((condition) && !__already_done) {		\
			__already_done = true;			\
			__ret_once = true;			\
		}						\
		__ret_once;					\
	})
#endif

#endif /* __MAPLE_SHARED_H__ */
