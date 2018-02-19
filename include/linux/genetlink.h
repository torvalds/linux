/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_GENERIC_NETLINK_H
#define __LINUX_GENERIC_NETLINK_H

#include <uapi/linux/genetlink.h>


/* All generic netlink requests are serialized by a global lock.  */
extern void genl_lock(void);
extern void genl_unlock(void);
#ifdef CONFIG_LOCKDEP
extern bool lockdep_genl_is_held(void);
#endif

/* for synchronisation between af_netlink and genetlink */
extern atomic_t genl_sk_destructing_cnt;
extern wait_queue_head_t genl_sk_destructing_waitq;

/**
 * rcu_dereference_genl - rcu_dereference with debug checking
 * @p: The pointer to read, prior to dereferencing
 *
 * Do an rcu_dereference(p), but check caller either holds rcu_read_lock()
 * or genl mutex. Note : Please prefer genl_dereference() or rcu_dereference()
 */
#define rcu_dereference_genl(p)					\
	rcu_dereference_check(p, lockdep_genl_is_held())

/**
 * genl_dereference - fetch RCU pointer when updates are prevented by genl mutex
 * @p: The pointer to read, prior to dereferencing
 *
 * Return the value of the specified RCU-protected pointer, but omit
 * both the smp_read_barrier_depends() and the READ_ONCE(), because
 * caller holds genl mutex.
 */
#define genl_dereference(p)					\
	rcu_dereference_protected(p, lockdep_genl_is_held())

#define MODULE_ALIAS_GENL_FAMILY(family)\
 MODULE_ALIAS_NET_PF_PROTO_NAME(PF_NETLINK, NETLINK_GENERIC, "-family-" family)

#endif	/* __LINUX_GENERIC_NETLINK_H */
