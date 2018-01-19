/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NET_PKT_SCHED_H
#define __NET_PKT_SCHED_H

#include <linux/jiffies.h>
#include <linux/ktime.h>
#include <linux/if_vlan.h>
#include <net/sch_generic.h>
#include <uapi/linux/pkt_sched.h>

#define DEFAULT_TX_QUEUE_LEN	1000

struct qdisc_walker {
	int	stop;
	int	skip;
	int	count;
	int	(*fn)(struct Qdisc *, unsigned long cl, struct qdisc_walker *);
};

#define QDISC_ALIGNTO		64
#define QDISC_ALIGN(len)	(((len) + QDISC_ALIGNTO-1) & ~(QDISC_ALIGNTO-1))

static inline void *qdisc_priv(struct Qdisc *q)
{
	return (char *) q + QDISC_ALIGN(sizeof(struct Qdisc));
}

/* 
   Timer resolution MUST BE < 10% of min_schedulable_packet_size/bandwidth
   
   Normal IP packet size ~ 512byte, hence:

   0.5Kbyte/1Mbyte/sec = 0.5msec, so that we need 50usec timer for
   10Mbit ethernet.

   10msec resolution -> <50Kbit/sec.
   
   The result: [34]86 is not good choice for QoS router :-(

   The things are not so bad, because we may use artificial
   clock evaluated by integration of network data flow
   in the most critical places.
 */

typedef u64	psched_time_t;
typedef long	psched_tdiff_t;

/* Avoid doing 64 bit divide */
#define PSCHED_SHIFT			6
#define PSCHED_TICKS2NS(x)		((s64)(x) << PSCHED_SHIFT)
#define PSCHED_NS2TICKS(x)		((x) >> PSCHED_SHIFT)

#define PSCHED_TICKS_PER_SEC		PSCHED_NS2TICKS(NSEC_PER_SEC)
#define PSCHED_PASTPERFECT		0

static inline psched_time_t psched_get_time(void)
{
	return PSCHED_NS2TICKS(ktime_get_ns());
}

static inline psched_tdiff_t
psched_tdiff_bounded(psched_time_t tv1, psched_time_t tv2, psched_time_t bound)
{
	return min(tv1 - tv2, bound);
}

struct qdisc_watchdog {
	u64		last_expires;
	struct hrtimer	timer;
	struct Qdisc	*qdisc;
};

void qdisc_watchdog_init(struct qdisc_watchdog *wd, struct Qdisc *qdisc);
void qdisc_watchdog_schedule_ns(struct qdisc_watchdog *wd, u64 expires);

static inline void qdisc_watchdog_schedule(struct qdisc_watchdog *wd,
					   psched_time_t expires)
{
	qdisc_watchdog_schedule_ns(wd, PSCHED_TICKS2NS(expires));
}

void qdisc_watchdog_cancel(struct qdisc_watchdog *wd);

extern struct Qdisc_ops pfifo_qdisc_ops;
extern struct Qdisc_ops bfifo_qdisc_ops;
extern struct Qdisc_ops pfifo_head_drop_qdisc_ops;

int fifo_set_limit(struct Qdisc *q, unsigned int limit);
struct Qdisc *fifo_create_dflt(struct Qdisc *sch, struct Qdisc_ops *ops,
			       unsigned int limit);

int register_qdisc(struct Qdisc_ops *qops);
int unregister_qdisc(struct Qdisc_ops *qops);
void qdisc_get_default(char *id, size_t len);
int qdisc_set_default(const char *id);

void qdisc_hash_add(struct Qdisc *q, bool invisible);
void qdisc_hash_del(struct Qdisc *q);
struct Qdisc *qdisc_lookup(struct net_device *dev, u32 handle);
struct Qdisc *qdisc_lookup_class(struct net_device *dev, u32 handle);
struct qdisc_rate_table *qdisc_get_rtab(struct tc_ratespec *r,
					struct nlattr *tab);
void qdisc_put_rtab(struct qdisc_rate_table *tab);
void qdisc_put_stab(struct qdisc_size_table *tab);
void qdisc_warn_nonwc(const char *txt, struct Qdisc *qdisc);
int sch_direct_xmit(struct sk_buff *skb, struct Qdisc *q,
		    struct net_device *dev, struct netdev_queue *txq,
		    spinlock_t *root_lock, bool validate);

void __qdisc_run(struct Qdisc *q);

static inline void qdisc_run(struct Qdisc *q)
{
	if (qdisc_run_begin(q))
		__qdisc_run(q);
}

static inline __be16 tc_skb_protocol(const struct sk_buff *skb)
{
	/* We need to take extra care in case the skb came via
	 * vlan accelerated path. In that case, use skb->vlan_proto
	 * as the original vlan header was already stripped.
	 */
	if (skb_vlan_tag_present(skb))
		return skb->vlan_proto;
	return skb->protocol;
}

/* Calculate maximal size of packet seen by hard_start_xmit
   routine of this device.
 */
static inline unsigned int psched_mtu(const struct net_device *dev)
{
	return dev->mtu + dev->hard_header_len;
}

static inline bool is_classid_clsact_ingress(u32 classid)
{
	/* This also returns true for ingress qdisc */
	return TC_H_MAJ(classid) == TC_H_MAJ(TC_H_CLSACT) &&
	       TC_H_MIN(classid) != TC_H_MIN(TC_H_MIN_EGRESS);
}

static inline bool is_classid_clsact_egress(u32 classid)
{
	return TC_H_MAJ(classid) == TC_H_MAJ(TC_H_CLSACT) &&
	       TC_H_MIN(classid) == TC_H_MIN(TC_H_MIN_EGRESS);
}

#endif
