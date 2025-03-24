// SPDX-License-Identifier: GPL-2.0
/* RTT/RTO calculation.
 *
 * Adapted from TCP for AF_RXRPC by David Howells (dhowells@redhat.com)
 *
 * https://tools.ietf.org/html/rfc6298
 * https://tools.ietf.org/html/rfc1122#section-4.2.3.1
 * http://ccr.sigcomm.org/archive/1995/jan95/ccr-9501-partridge87.pdf
 */

#include <linux/net.h>
#include "ar-internal.h"

#define RXRPC_RTO_MAX	(120 * USEC_PER_SEC)
#define RXRPC_TIMEOUT_INIT ((unsigned int)(1 * USEC_PER_SEC)) /* RFC6298 2.1 initial RTO value */
#define rxrpc_jiffies32 ((u32)jiffies)		/* As rxrpc_jiffies32 */

static u32 rxrpc_rto_min_us(struct rxrpc_call *call)
{
	return 200;
}

static u32 __rxrpc_set_rto(const struct rxrpc_call *call)
{
	return (call->srtt_us >> 3) + call->rttvar_us;
}

static u32 rxrpc_bound_rto(u32 rto)
{
	return clamp(200000, rto + 100000, RXRPC_RTO_MAX);
}

/*
 * Called to compute a smoothed rtt estimate. The data fed to this
 * routine either comes from timestamps, or from segments that were
 * known _not_ to have been retransmitted [see Karn/Partridge
 * Proceedings SIGCOMM 87]. The algorithm is from the SIGCOMM 88
 * piece by Van Jacobson.
 * NOTE: the next three routines used to be one big routine.
 * To save cycles in the RFC 1323 implementation it was better to break
 * it up into three procedures. -- erics
 */
static void rxrpc_rtt_estimator(struct rxrpc_call *call, long sample_rtt_us)
{
	long m = sample_rtt_us; /* RTT */
	u32 srtt = call->srtt_us;

	/*	The following amusing code comes from Jacobson's
	 *	article in SIGCOMM '88.  Note that rtt and mdev
	 *	are scaled versions of rtt and mean deviation.
	 *	This is designed to be as fast as possible
	 *	m stands for "measurement".
	 *
	 *	On a 1990 paper the rto value is changed to:
	 *	RTO = rtt + 4 * mdev
	 *
	 * Funny. This algorithm seems to be very broken.
	 * These formulae increase RTO, when it should be decreased, increase
	 * too slowly, when it should be increased quickly, decrease too quickly
	 * etc. I guess in BSD RTO takes ONE value, so that it is absolutely
	 * does not matter how to _calculate_ it. Seems, it was trap
	 * that VJ failed to avoid. 8)
	 */
	if (srtt != 0) {
		m -= (srtt >> 3);	/* m is now error in rtt est */
		srtt += m;		/* rtt = 7/8 rtt + 1/8 new */
		if (m < 0) {
			m = -m;		/* m is now abs(error) */
			m -= (call->mdev_us >> 2);   /* similar update on mdev */
			/* This is similar to one of Eifel findings.
			 * Eifel blocks mdev updates when rtt decreases.
			 * This solution is a bit different: we use finer gain
			 * for mdev in this case (alpha*beta).
			 * Like Eifel it also prevents growth of rto,
			 * but also it limits too fast rto decreases,
			 * happening in pure Eifel.
			 */
			if (m > 0)
				m >>= 3;
		} else {
			m -= (call->mdev_us >> 2);   /* similar update on mdev */
		}

		call->mdev_us += m;		/* mdev = 3/4 mdev + 1/4 new */
		if (call->mdev_us > call->mdev_max_us) {
			call->mdev_max_us = call->mdev_us;
			if (call->mdev_max_us > call->rttvar_us)
				call->rttvar_us = call->mdev_max_us;
		}
	} else {
		/* no previous measure. */
		srtt = m << 3;		/* take the measured time to be rtt */
		call->mdev_us = m << 1;	/* make sure rto = 3*rtt */
		call->rttvar_us = umax(call->mdev_us, rxrpc_rto_min_us(call));
		call->mdev_max_us = call->rttvar_us;
	}

	call->srtt_us = umax(srtt, 1);
}

/*
 * Calculate rto without backoff.  This is the second half of Van Jacobson's
 * routine referred to above.
 */
static void rxrpc_set_rto(struct rxrpc_call *call)
{
	u32 rto;

	/* 1. If rtt variance happened to be less 50msec, it is hallucination.
	 *    It cannot be less due to utterly erratic ACK generation made
	 *    at least by solaris and freebsd. "Erratic ACKs" has _nothing_
	 *    to do with delayed acks, because at cwnd>2 true delack timeout
	 *    is invisible. Actually, Linux-2.4 also generates erratic
	 *    ACKs in some circumstances.
	 */
	rto = __rxrpc_set_rto(call);

	/* 2. Fixups made earlier cannot be right.
	 *    If we do not estimate RTO correctly without them,
	 *    all the algo is pure shit and should be replaced
	 *    with correct one. It is exactly, which we pretend to do.
	 */

	/* NOTE: clamping at RXRPC_RTO_MIN is not required, current algo
	 * guarantees that rto is higher.
	 */
	call->rto_us = rxrpc_bound_rto(rto);
}

static void rxrpc_update_rtt_min(struct rxrpc_call *call, ktime_t resp_time, long rtt_us)
{
	/* Window size 5mins in approx usec (ipv4.sysctl_tcp_min_rtt_wlen) */
	u32 wlen_us = 5ULL * NSEC_PER_SEC / 1024;

	minmax_running_min(&call->min_rtt, wlen_us, resp_time / 1024,
			   (u32)rtt_us ? : jiffies_to_usecs(1));
}

static void rxrpc_ack_update_rtt(struct rxrpc_call *call, ktime_t resp_time, long rtt_us)
{
	if (rtt_us < 0)
		return;

	/* Update RACK min RTT [RFC8985 6.1 Step 1]. */
	rxrpc_update_rtt_min(call, resp_time, rtt_us);

	rxrpc_rtt_estimator(call, rtt_us);
	rxrpc_set_rto(call);

	/* Only reset backoff on valid RTT measurement [RFC6298]. */
	call->backoff = 0;
}

/*
 * Add RTT information to cache.  This is called in softirq mode and has
 * exclusive access to the call RTT data.
 */
void rxrpc_call_add_rtt(struct rxrpc_call *call, enum rxrpc_rtt_rx_trace why,
			int rtt_slot,
			rxrpc_serial_t send_serial, rxrpc_serial_t resp_serial,
			ktime_t send_time, ktime_t resp_time)
{
	s64 rtt_us;

	rtt_us = ktime_to_us(ktime_sub(resp_time, send_time));
	if (rtt_us < 0)
		return;

	rxrpc_ack_update_rtt(call, resp_time, rtt_us);
	if (call->rtt_count < 3)
		call->rtt_count++;
	call->rtt_taken++;

	WRITE_ONCE(call->peer->recent_srtt_us, call->srtt_us / 8);
	WRITE_ONCE(call->peer->recent_rto_us, call->rto_us);

	trace_rxrpc_rtt_rx(call, why, rtt_slot, send_serial, resp_serial,
			   rtt_us, call->srtt_us, call->rto_us);
}

/*
 * Get the retransmission timeout to set in nanoseconds, backing it off each
 * time we retransmit.
 */
ktime_t rxrpc_get_rto_backoff(struct rxrpc_call *call, bool retrans)
{
	u64 timo_us;
	u32 backoff = READ_ONCE(call->backoff);

	timo_us = call->rto_us;
	timo_us <<= backoff;
	if (retrans && timo_us * 2 <= RXRPC_RTO_MAX)
		WRITE_ONCE(call->backoff, backoff + 1);

	if (timo_us < 1)
		timo_us = 1;

	return ns_to_ktime(timo_us * NSEC_PER_USEC);
}

void rxrpc_call_init_rtt(struct rxrpc_call *call)
{
	call->rtt_last_req = KTIME_MIN;
	call->rto_us	= RXRPC_TIMEOUT_INIT;
	call->mdev_us	= RXRPC_TIMEOUT_INIT;
	call->backoff	= 0;
	//minmax_reset(&call->rtt_min, rxrpc_jiffies32, ~0U);
}
