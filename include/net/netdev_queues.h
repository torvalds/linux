/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_NET_QUEUES_H
#define _LINUX_NET_QUEUES_H

#include <linux/netdevice.h>

/* See the netdev.yaml spec for definition of each statistic */
struct netdev_queue_stats_rx {
	u64 bytes;
	u64 packets;
	u64 alloc_fail;

	u64 hw_drops;
	u64 hw_drop_overruns;

	u64 csum_unnecessary;
	u64 csum_none;
	u64 csum_bad;

	u64 hw_gro_packets;
	u64 hw_gro_bytes;
	u64 hw_gro_wire_packets;
	u64 hw_gro_wire_bytes;

	u64 hw_drop_ratelimits;
};

struct netdev_queue_stats_tx {
	u64 bytes;
	u64 packets;

	u64 hw_drops;
	u64 hw_drop_errors;

	u64 csum_none;
	u64 needs_csum;

	u64 hw_gso_packets;
	u64 hw_gso_bytes;
	u64 hw_gso_wire_packets;
	u64 hw_gso_wire_bytes;

	u64 hw_drop_ratelimits;

	u64 stop;
	u64 wake;
};

/**
 * struct netdev_stat_ops - netdev ops for fine grained stats
 * @get_queue_stats_rx:	get stats for a given Rx queue
 * @get_queue_stats_tx:	get stats for a given Tx queue
 * @get_base_stats:	get base stats (not belonging to any live instance)
 *
 * Query stats for a given object. The values of the statistics are undefined
 * on entry (specifically they are *not* zero-initialized). Drivers should
 * assign values only to the statistics they collect. Statistics which are not
 * collected must be left undefined.
 *
 * Queue objects are not necessarily persistent, and only currently active
 * queues are queried by the per-queue callbacks. This means that per-queue
 * statistics will not generally add up to the total number of events for
 * the device. The @get_base_stats callback allows filling in the delta
 * between events for currently live queues and overall device history.
 * @get_base_stats can also be used to report any miscellaneous packets
 * transferred outside of the main set of queues used by the networking stack.
 * When the statistics for the entire device are queried, first @get_base_stats
 * is issued to collect the delta, and then a series of per-queue callbacks.
 * Only statistics which are set in @get_base_stats will be reported
 * at the device level, meaning that unlike in queue callbacks, setting
 * a statistic to zero in @get_base_stats is a legitimate thing to do.
 * This is because @get_base_stats has a second function of designating which
 * statistics are in fact correct for the entire device (e.g. when history
 * for some of the events is not maintained, and reliable "total" cannot
 * be provided).
 *
 * Device drivers can assume that when collecting total device stats,
 * the @get_base_stats and subsequent per-queue calls are performed
 * "atomically" (without releasing the rtnl_lock).
 *
 * Device drivers are encouraged to reset the per-queue statistics when
 * number of queues change. This is because the primary use case for
 * per-queue statistics is currently to detect traffic imbalance.
 */
struct netdev_stat_ops {
	void (*get_queue_stats_rx)(struct net_device *dev, int idx,
				   struct netdev_queue_stats_rx *stats);
	void (*get_queue_stats_tx)(struct net_device *dev, int idx,
				   struct netdev_queue_stats_tx *stats);
	void (*get_base_stats)(struct net_device *dev,
			       struct netdev_queue_stats_rx *rx,
			       struct netdev_queue_stats_tx *tx);
};

/**
 * struct netdev_queue_mgmt_ops - netdev ops for queue management
 *
 * @ndo_queue_mem_size: Size of the struct that describes a queue's memory.
 *
 * @ndo_queue_mem_alloc: Allocate memory for an RX queue at the specified index.
 *			 The new memory is written at the specified address.
 *
 * @ndo_queue_mem_free:	Free memory from an RX queue.
 *
 * @ndo_queue_start:	Start an RX queue with the specified memory and at the
 *			specified index.
 *
 * @ndo_queue_stop:	Stop the RX queue at the specified index. The stopped
 *			queue's memory is written at the specified address.
 */
struct netdev_queue_mgmt_ops {
	size_t			ndo_queue_mem_size;
	int			(*ndo_queue_mem_alloc)(struct net_device *dev,
						       void *per_queue_mem,
						       int idx);
	void			(*ndo_queue_mem_free)(struct net_device *dev,
						      void *per_queue_mem);
	int			(*ndo_queue_start)(struct net_device *dev,
						   void *per_queue_mem,
						   int idx);
	int			(*ndo_queue_stop)(struct net_device *dev,
						  void *per_queue_mem,
						  int idx);
};

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
