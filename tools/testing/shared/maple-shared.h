/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __MAPLE_SHARED_H__
#define __MAPLE_SHARED_H__

#define CONFIG_DEBUG_MAPLE_TREE
#define CONFIG_MAPLE_SEARCH
#define MAPLE_32BIT (MAPLE_NODE_SLOTS > 31)
#include "shared.h"
#include <stdlib.h>
#include <time.h>
#include "linux/init.h"

void maple_rcu_cb(struct rcu_head *head);
#define rcu_cb		maple_rcu_cb

#define kfree_rcu(_struct, _memb)		\
do {                                            \
    typeof(_struct) _p_struct = (_struct);      \
                                                \
    call_rcu(&((_p_struct)->_memb), rcu_cb);    \
} while(0);


#endif /* __MAPLE_SHARED_H__ */
