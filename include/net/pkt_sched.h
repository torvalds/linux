#ifndef __NET_PKT_SCHED_H
#define __NET_PKT_SCHED_H

#include <linux/jiffies.h>
#include <linux/ktime.h>
#include <net/sch_generic.h>

struct qdisc_walker
{
	int	stop;
	int	skip;
	int	count;
	int	(*fn)(struct Qdisc *, unsigned long cl, struct qdisc_walker *);
};

#define QDISC_ALIGNTO		32
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

   The things are not so bad, because we may use artifical
   clock evaluated by integration of network data flow
   in the most critical places.
 */

typedef u64	psched_time_t;
typedef long	psched_tdiff_t;

/* Avoid doing 64 bit divide by 1000 */
#define PSCHED_US2NS(x)			((s64)(x) << 10)
#define PSCHED_NS2US(x)			((x) >> 10)

#define PSCHED_TICKS_PER_SEC		PSCHED_NS2US(NSEC_PER_SEC)
#define PSCHED_PASTPERFECT		0

static inline psched_time_t psched_get_time(void)
{
	return PSCHED_NS2US(ktime_to_ns(ktime_get()));
}

static inline psched_tdiff_t
psched_tdiff_bounded(psched_time_t tv1, psched_time_t tv2, psched_time_t bound)
{
	return min(tv1 - tv2, bound);
}

struct qdisc_watchdog {
	struct hrtimer	timer;
	struct Qdisc	*qdisc;
};

extern void qdisc_watchdog_init(struct qdisc_watchdog *wd, struct Qdisc *qdisc);
extern void qdisc_watchdog_schedule(struct qdisc_watchdog *wd,
				    psched_time_t expires);
extern void qdisc_watchdog_cancel(struct qdisc_watchdog *wd);

extern struct Qdisc_ops pfifo_qdisc_ops;
extern struct Qdisc_ops bfifo_qdisc_ops;

extern int fifo_set_limit(struct Qdisc *q, unsigned int limit);
extern struct Qdisc *fifo_create_dflt(struct Qdisc *sch, struct Qdisc_ops *ops,
				      unsigned int limit);

extern int register_qdisc(struct Qdisc_ops *qops);
extern int unregister_qdisc(struct Qdisc_ops *qops);
extern void qdisc_list_del(struct Qdisc *q);
extern struct Qdisc *qdisc_lookup(struct net_device *dev, u32 handle);
extern struct Qdisc *qdisc_lookup_class(struct net_device *dev, u32 handle);
extern struct qdisc_rate_table *qdisc_get_rtab(struct tc_ratespec *r,
		struct nlattr *tab);
extern void qdisc_put_rtab(struct qdisc_rate_table *tab);
extern void qdisc_put_stab(struct qdisc_size_table *tab);

extern void __qdisc_run(struct Qdisc *q);

static inline void qdisc_run(struct Qdisc *q)
{
	if (!test_and_set_bit(__QDISC_STATE_RUNNING, &q->state))
		__qdisc_run(q);
}

extern int tc_classify_compat(struct sk_buff *skb, struct tcf_proto *tp,
			      struct tcf_result *res);
extern int tc_classify(struct sk_buff *skb, struct tcf_proto *tp,
		       struct tcf_result *res);

/* Calculate maximal size of packet seen by hard_start_xmit
   routine of this device.
 */
static inline unsigned psched_mtu(const struct net_device *dev)
{
	return dev->mtu + dev->hard_header_len;
}

#endif
