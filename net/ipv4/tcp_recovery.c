#include <linux/tcp.h>
#include <net/tcp.h>

/* Record the most recently (re)sent time among the (s)acked packets */
void tcp_rack_advance(struct tcp_sock *tp,
		      const struct skb_mstamp *xmit_time, u8 sacked)
{
	if (tp->rack.mstamp.v64 &&
	    !skb_mstamp_after(xmit_time, &tp->rack.mstamp))
		return;

	if (sacked & TCPCB_RETRANS) {
		struct skb_mstamp now;

		/* If the sacked packet was retransmitted, it's ambiguous
		 * whether the retransmission or the original (or the prior
		 * retransmission) was sacked.
		 *
		 * If the original is lost, there is no ambiguity. Otherwise
		 * we assume the original can be delayed up to aRTT + min_rtt.
		 * the aRTT term is bounded by the fast recovery or timeout,
		 * so it's at least one RTT (i.e., retransmission is at least
		 * an RTT later).
		 */
		skb_mstamp_get(&now);
		if (skb_mstamp_us_delta(&now, xmit_time) < tcp_min_rtt(tp))
			return;
	}

	tp->rack.mstamp = *xmit_time;
	tp->rack.advanced = 1;
}
