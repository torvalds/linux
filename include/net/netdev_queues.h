/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_NET_QUEUES_H
#define _LINUX_NET_QUEUES_H

#include <linux/netdevice.h>

/**
 * DOC: Lockless queue stopping / waking helpers.
 *
 * The netif_txq_maybe_stop() and __netif_txq_completed_wake()
 * macros are designed to safely implement stopping
 * and waking netdev queues without full lock protection.
 *
 * We assume that there can be no concurrent stop attempts and no concurrent
 * wake attempts. The try-stop should happen from the xmit handler,
 * while wake up should be triggered from NAPI poll context.
 * The two may run concurrently (single producer, single consumer).
 *
 * The try-stop side is expected to run from the xmit handler and therefore
 * it does not reschedule Tx (netif_tx_start_queue() instead of
 * netif_tx_wake_queue()). Uses of the ``stop`` macros outside of the xmit
 * handler may lead to xmit queue being enabled but not run.
 * The waking side does not have similar context restrictions.
 *
 * The macros guarantee that rings will not remain stopped if there's
 * space available, but they do *not* prevent false wake ups when
 * the ring is full! Drivers should check for ring full at the start
 * for the xmit handler.
 *
 * All descriptor ring indexes (and other relevant shared state) must
 * be updated before invoking the macros.
 */

#define netif_txq_try_stop(txq, get_desc, start_thrs)			\
	({								\
		int _res;						\
									\
		netif_tx_stop_queue(txq);				\
		/* Producer index and stop bit must be visible		\
		 * to consumer before we recheck.			\
		 * Pairs with a barrier in __netif_txq_completed_wake(). \
		 */							\
		smp_mb__after_atomic();					\
									\
		/* We need to check again in a case another		\
		 * CPU has just made room available.			\
		 */							\
		_res = 0;						\
		if (unlikely(get_desc >= start_thrs)) {			\
			netif_tx_start_queue(txq);			\
			_res = -1;					\
		}							\
		_res;							\
	})								\

/**
 * netif_txq_maybe_stop() - locklessly stop a Tx queue, if needed
 * @txq:	struct netdev_queue to stop/start
 * @get_desc:	get current number of free descriptors (see requirements below!)
 * @stop_thrs:	minimal number of available descriptors for queue to be left
 *		enabled
 * @start_thrs:	minimal number of descriptors to re-enable the queue, can be
 *		equal to @stop_thrs or higher to avoid frequent waking
 *
 * All arguments may be evaluated multiple times, beware of side effects.
 * @get_desc must be a formula or a function call, it must always
 * return up-to-date information when evaluated!
 * Expected to be used from ndo_start_xmit, see the comment on top of the file.
 *
 * Returns:
 *	 0 if the queue was stopped
 *	 1 if the queue was left enabled
 *	-1 if the queue was re-enabled (raced with waking)
 */
#define netif_txq_maybe_stop(txq, get_desc, stop_thrs, start_thrs)	\
	({								\
		int _res;						\
									\
		_res = 1;						\
		if (unlikely(get_desc < stop_thrs))			\
			_res = netif_txq_try_stop(txq, get_desc, start_thrs); \
		_res;							\
	})								\

/* Variant of netdev_tx_completed_queue() which guarantees smp_mb() if
 * @bytes != 0, regardless of kernel config.
 */
static inline void
netdev_txq_completed_mb(struct netdev_queue *dev_queue,
			unsigned int pkts, unsigned int bytes)
{
	if (IS_ENABLED(CONFIG_BQL))
		netdev_tx_completed_queue(dev_queue, pkts, bytes);
	else if (bytes)
		smp_mb();
}

/**
 * __netif_txq_completed_wake() - locklessly wake a Tx queue, if needed
 * @txq:	struct netdev_queue to stop/start
 * @pkts:	number of packets completed
 * @bytes:	number of bytes completed
 * @get_desc:	get current number of free descriptors (see requirements below!)
 * @start_thrs:	minimal number of descriptors to re-enable the queue
 * @down_cond:	down condition, predicate indicating that the queue should
 *		not be woken up even if descriptors are available
 *
 * All arguments may be evaluated multiple times.
 * @get_desc must be a formula or a function call, it must always
 * return up-to-date information when evaluated!
 * Reports completed pkts/bytes to BQL.
 *
 * Returns:
 *	 0 if the queue was woken up
 *	 1 if the queue was already enabled (or disabled but @down_cond is true)
 *	-1 if the queue was left unchanged (@start_thrs not reached)
 */
#define __netif_txq_completed_wake(txq, pkts, bytes,			\
				   get_desc, start_thrs, down_cond)	\
	({								\
		int _res;						\
									\
		/* Report to BQL and piggy back on its barrier.		\
		 * Barrier makes sure that anybody stopping the queue	\
		 * after this point sees the new consumer index.	\
		 * Pairs with barrier in netif_txq_try_stop().		\
		 */							\
		netdev_txq_completed_mb(txq, pkts, bytes);		\
									\
		_res = -1;						\
		if (pkts && likely(get_desc >= start_thrs)) {		\
			_res = 1;					\
			if (unlikely(netif_tx_queue_stopped(txq)) &&	\
			    !(down_cond)) {				\
				netif_tx_wake_queue(txq);		\
				_res = 0;				\
			}						\
		}							\
		_res;							\
	})

#define netif_txq_completed_wake(txq, pkts, bytes, get_desc, start_thrs) \
	__netif_txq_completed_wake(txq, pkts, bytes, get_desc, start_thrs, false)

/* subqueue variants follow */

#define netif_subqueue_try_stop(dev, idx, get_desc, start_thrs)		\
	({								\
		struct netdev_queue *txq;				\
									\
		txq = netdev_get_tx_queue(dev, idx);			\
		netif_txq_try_stop(txq, get_desc, start_thrs);		\
	})

#define netif_subqueue_maybe_stop(dev, idx, get_desc, stop_thrs, start_thrs) \
	({								\
		struct netdev_queue *txq;				\
									\
		txq = netdev_get_tx_queue(dev, idx);			\
		netif_txq_maybe_stop(txq, get_desc, stop_thrs, start_thrs); \
	})

#define netif_subqueue_completed_wake(dev, idx, pkts, bytes,		\
				      get_desc, start_thrs)		\
	({								\
		struct netdev_queue *txq;				\
									\
		txq = netdev_get_tx_queue(dev, idx);			\
		netif_txq_completed_wake(txq, pkts, bytes,		\
					 get_desc, start_thrs);		\
	})

#endif
