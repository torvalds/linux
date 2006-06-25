#ifndef __NET_PKT_SCHED_H
#define __NET_PKT_SCHED_H

#include <linux/jiffies.h>
#include <net/sch_generic.h>

struct qdisc_walker
{
	int	stop;
	int	skip;
	int	count;
	int	(*fn)(struct Qdisc *, unsigned long cl, struct qdisc_walker *);
};

extern rwlock_t qdisc_tree_lock;

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

   Note: we do not use fastgettimeofday.
   The reason is that, when it is not the same thing as
   gettimeofday, it returns invalid timestamp, which is
   not updated, when net_bh is active.
 */

/* General note about internal clock.

   Any clock source returns time intervals, measured in units
   close to 1usec. With source CONFIG_NET_SCH_CLK_GETTIMEOFDAY it is precisely
   microseconds, otherwise something close but different chosen to minimize
   arithmetic cost. Ratio usec/internal untis in form nominator/denominator
   may be read from /proc/net/psched.
 */


#ifdef CONFIG_NET_SCH_CLK_GETTIMEOFDAY

typedef struct timeval	psched_time_t;
typedef long		psched_tdiff_t;

#define PSCHED_GET_TIME(stamp) do_gettimeofday(&(stamp))
#define PSCHED_US2JIFFIE(usecs) usecs_to_jiffies(usecs)
#define PSCHED_JIFFIE2US(delay) jiffies_to_usecs(delay)

#else /* !CONFIG_NET_SCH_CLK_GETTIMEOFDAY */

typedef u64	psched_time_t;
typedef long	psched_tdiff_t;

#ifdef CONFIG_NET_SCH_CLK_JIFFIES

#if HZ < 96
#define PSCHED_JSCALE 14
#elif HZ >= 96 && HZ < 192
#define PSCHED_JSCALE 13
#elif HZ >= 192 && HZ < 384
#define PSCHED_JSCALE 12
#elif HZ >= 384 && HZ < 768
#define PSCHED_JSCALE 11
#elif HZ >= 768
#define PSCHED_JSCALE 10
#endif

#define PSCHED_GET_TIME(stamp) ((stamp) = (get_jiffies_64()<<PSCHED_JSCALE))
#define PSCHED_US2JIFFIE(delay) (((delay)+(1<<PSCHED_JSCALE)-1)>>PSCHED_JSCALE)
#define PSCHED_JIFFIE2US(delay) ((delay)<<PSCHED_JSCALE)

#endif /* CONFIG_NET_SCH_CLK_JIFFIES */
#ifdef CONFIG_NET_SCH_CLK_CPU
#include <asm/timex.h>

extern psched_tdiff_t psched_clock_per_hz;
extern int psched_clock_scale;
extern psched_time_t psched_time_base;
extern cycles_t psched_time_mark;

#define PSCHED_GET_TIME(stamp)						\
do {									\
	cycles_t cur = get_cycles();					\
	if (sizeof(cycles_t) == sizeof(u32)) {				\
		if (cur <= psched_time_mark)				\
			psched_time_base += 0x100000000ULL;		\
		psched_time_mark = cur;					\
		(stamp) = (psched_time_base + cur)>>psched_clock_scale;	\
	} else {							\
		(stamp) = cur>>psched_clock_scale;			\
	}								\
} while (0)
#define PSCHED_US2JIFFIE(delay) (((delay)+psched_clock_per_hz-1)/psched_clock_per_hz)
#define PSCHED_JIFFIE2US(delay) ((delay)*psched_clock_per_hz)

#endif /* CONFIG_NET_SCH_CLK_CPU */

#endif /* !CONFIG_NET_SCH_CLK_GETTIMEOFDAY */

#ifdef CONFIG_NET_SCH_CLK_GETTIMEOFDAY
#define PSCHED_TDIFF(tv1, tv2) \
({ \
	   int __delta_sec = (tv1).tv_sec - (tv2).tv_sec; \
	   int __delta = (tv1).tv_usec - (tv2).tv_usec; \
	   if (__delta_sec) { \
	           switch (__delta_sec) { \
		   default: \
			   __delta = 0; \
		   case 2: \
			   __delta += USEC_PER_SEC; \
		   case 1: \
			   __delta += USEC_PER_SEC; \
	           } \
	   } \
	   __delta; \
})

static inline int
psched_tod_diff(int delta_sec, int bound)
{
	int delta;

	if (bound <= USEC_PER_SEC || delta_sec > (0x7FFFFFFF/USEC_PER_SEC)-1)
		return bound;
	delta = delta_sec * USEC_PER_SEC;
	if (delta > bound || delta < 0)
		delta = bound;
	return delta;
}

#define PSCHED_TDIFF_SAFE(tv1, tv2, bound) \
({ \
	   int __delta_sec = (tv1).tv_sec - (tv2).tv_sec; \
	   int __delta = (tv1).tv_usec - (tv2).tv_usec; \
	   switch (__delta_sec) { \
	   default: \
		   __delta = psched_tod_diff(__delta_sec, bound);  break; \
	   case 2: \
		   __delta += USEC_PER_SEC; \
	   case 1: \
		   __delta += USEC_PER_SEC; \
	   case 0: \
 		   if (__delta > bound || __delta < 0) \
 			__delta = bound; \
	   } \
	   __delta; \
})

#define PSCHED_TLESS(tv1, tv2) (((tv1).tv_usec < (tv2).tv_usec && \
				(tv1).tv_sec <= (tv2).tv_sec) || \
				 (tv1).tv_sec < (tv2).tv_sec)

#define PSCHED_TADD2(tv, delta, tv_res) \
({ \
	   int __delta = (tv).tv_usec + (delta); \
	   (tv_res).tv_sec = (tv).tv_sec; \
	   if (__delta > USEC_PER_SEC) { (tv_res).tv_sec++; __delta -= USEC_PER_SEC; } \
	   (tv_res).tv_usec = __delta; \
})

#define PSCHED_TADD(tv, delta) \
({ \
	   (tv).tv_usec += (delta); \
	   if ((tv).tv_usec > USEC_PER_SEC) { (tv).tv_sec++; \
		 (tv).tv_usec -= USEC_PER_SEC; } \
})

/* Set/check that time is in the "past perfect";
   it depends on concrete representation of system time
 */

#define PSCHED_SET_PASTPERFECT(t)	((t).tv_sec = 0)
#define PSCHED_IS_PASTPERFECT(t)	((t).tv_sec == 0)

#define	PSCHED_AUDIT_TDIFF(t) ({ if ((t) > 2000000) (t) = 2000000; })

#else /* !CONFIG_NET_SCH_CLK_GETTIMEOFDAY */

#define PSCHED_TDIFF(tv1, tv2) (long)((tv1) - (tv2))
#define PSCHED_TDIFF_SAFE(tv1, tv2, bound) \
	min_t(long long, (tv1) - (tv2), bound)


#define PSCHED_TLESS(tv1, tv2) ((tv1) < (tv2))
#define PSCHED_TADD2(tv, delta, tv_res) ((tv_res) = (tv) + (delta))
#define PSCHED_TADD(tv, delta) ((tv) += (delta))
#define PSCHED_SET_PASTPERFECT(t)	((t) = 0)
#define PSCHED_IS_PASTPERFECT(t)	((t) == 0)
#define	PSCHED_AUDIT_TDIFF(t)

#endif /* !CONFIG_NET_SCH_CLK_GETTIMEOFDAY */

extern struct Qdisc_ops pfifo_qdisc_ops;
extern struct Qdisc_ops bfifo_qdisc_ops;

extern int register_qdisc(struct Qdisc_ops *qops);
extern int unregister_qdisc(struct Qdisc_ops *qops);
extern struct Qdisc *qdisc_lookup(struct net_device *dev, u32 handle);
extern struct Qdisc *qdisc_lookup_class(struct net_device *dev, u32 handle);
extern struct qdisc_rate_table *qdisc_get_rtab(struct tc_ratespec *r,
		struct rtattr *tab);
extern void qdisc_put_rtab(struct qdisc_rate_table *tab);

extern void __qdisc_run(struct net_device *dev);

static inline void qdisc_run(struct net_device *dev)
{
	if (!netif_queue_stopped(dev) &&
	    !test_and_set_bit(__LINK_STATE_QDISC_RUNNING, &dev->state))
		__qdisc_run(dev);
}

extern int tc_classify(struct sk_buff *skb, struct tcf_proto *tp,
	struct tcf_result *res);

/* Calculate maximal size of packet seen by hard_start_xmit
   routine of this device.
 */
static inline unsigned psched_mtu(struct net_device *dev)
{
	unsigned mtu = dev->mtu;
	return dev->hard_header ? mtu + dev->hard_header_len : mtu;
}

#endif
