/*
 * TCP Veno congestion control
 *
 * This is based on the congestion detection/avoidance scheme described in
 *    C. P. Fu, S. C. Liew.
 *    "TCP Veno: TCP Enhancement for Transmission over Wireless Access Networks."
 *    IEEE Journal on Selected Areas in Communication,
 *    Feb. 2003.
 * 	See http://www.ie.cuhk.edu.hk/fileadmin/staff_upload/soung/Journal/J3.pdf
 */

#include <linux/mm.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/inet_diag.h>

#include <net/tcp.h>

/* Default values of the Veno variables, in fixed-point representation
 * with V_PARAM_SHIFT bits to the right of the binary point.
 */
#define V_PARAM_SHIFT 1
static const int beta = 3 << V_PARAM_SHIFT;

/* Veno variables */
struct veno {
	u8 doing_veno_now;	/* if true, do veno for this rtt */
	u16 cntrtt;		/* # of rtts measured within last rtt */
	u32 minrtt;		/* min of rtts measured within last rtt (in usec) */
	u32 basertt;		/* the min of all Veno rtt measurements seen (in usec) */
	u32 inc;		/* decide whether to increase cwnd */
	u32 diff;		/* calculate the diff rate */
};

/* There are several situations when we must "re-start" Veno:
 *
 *  o when a connection is established
 *  o after an RTO
 *  o after fast recovery
 *  o when we send a packet and there is no outstanding
 *    unacknowledged data (restarting an idle connection)
 *
 */
static inline void veno_enable(struct sock *sk)
{
	struct veno *veno = inet_csk_ca(sk);

	/* turn on Veno */
	veno->doing_veno_now = 1;

	veno->minrtt = 0x7fffffff;
}

static inline void veno_disable(struct sock *sk)
{
	struct veno *veno = inet_csk_ca(sk);

	/* turn off Veno */
	veno->doing_veno_now = 0;
}

static void tcp_veno_init(struct sock *sk)
{
	struct veno *veno = inet_csk_ca(sk);

	veno->basertt = 0x7fffffff;
	veno->inc = 1;
	veno_enable(sk);
}

/* Do rtt sampling needed for Veno. */
static void tcp_veno_pkts_acked(struct sock *sk, u32 cnt, s32 rtt_us)
{
	struct veno *veno = inet_csk_ca(sk);
	u32 vrtt;

	if (rtt_us < 0)
		return;

	/* Never allow zero rtt or baseRTT */
	vrtt = rtt_us + 1;

	/* Filter to find propagation delay: */
	if (vrtt < veno->basertt)
		veno->basertt = vrtt;

	/* Find the min rtt during the last rtt to find
	 * the current prop. delay + queuing delay:
	 */
	veno->minrtt = min(veno->minrtt, vrtt);
	veno->cntrtt++;
}

static void tcp_veno_state(struct sock *sk, u8 ca_state)
{
	if (ca_state == TCP_CA_Open)
		veno_enable(sk);
	else
		veno_disable(sk);
}

/*
 * If the connection is idle and we are restarting,
 * then we don't want to do any Veno calculations
 * until we get fresh rtt samples.  So when we
 * restart, we reset our Veno state to a clean
 * state. After we get acks for this flight of
 * packets, _then_ we can make Veno calculations
 * again.
 */
static void tcp_veno_cwnd_event(struct sock *sk, enum tcp_ca_event event)
{
	if (event == CA_EVENT_CWND_RESTART || event == CA_EVENT_TX_START)
		tcp_veno_init(sk);
}

static void tcp_veno_cong_avoid(struct sock *sk, u32 ack, u32 in_flight)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct veno *veno = inet_csk_ca(sk);

	if (!veno->doing_veno_now) {
		tcp_reno_cong_avoid(sk, ack, in_flight);
		return;
	}

	/* limited by applications */
	if (!tcp_is_cwnd_limited(sk, in_flight))
		return;

	/* We do the Veno calculations only if we got enough rtt samples */
	if (veno->cntrtt <= 2) {
		/* We don't have enough rtt samples to do the Veno
		 * calculation, so we'll behave like Reno.
		 */
		tcp_reno_cong_avoid(sk, ack, in_flight);
	} else {
		u64 target_cwnd;
		u32 rtt;

		/* We have enough rtt samples, so, using the Veno
		 * algorithm, we determine the state of the network.
		 */

		rtt = veno->minrtt;

		target_cwnd = (tp->snd_cwnd * veno->basertt);
		target_cwnd <<= V_PARAM_SHIFT;
		do_div(target_cwnd, rtt);

		veno->diff = (tp->snd_cwnd << V_PARAM_SHIFT) - target_cwnd;

		if (tp->snd_cwnd <= tp->snd_ssthresh) {
			/* Slow start.  */
			tcp_slow_start(tp);
		} else {
			/* Congestion avoidance. */
			if (veno->diff < beta) {
				/* In the "non-congestive state", increase cwnd
				 *  every rtt.
				 */
				tcp_cong_avoid_ai(tp, tp->snd_cwnd);
			} else {
				/* In the "congestive state", increase cwnd
				 * every other rtt.
				 */
				if (tp->snd_cwnd_cnt >= tp->snd_cwnd) {
					if (veno->inc &&
					    tp->snd_cwnd < tp->snd_cwnd_clamp) {
						tp->snd_cwnd++;
						veno->inc = 0;
					} else
						veno->inc = 1;
					tp->snd_cwnd_cnt = 0;
				} else
					tp->snd_cwnd_cnt++;
			}

		}
		if (tp->snd_cwnd < 2)
			tp->snd_cwnd = 2;
		else if (tp->snd_cwnd > tp->snd_cwnd_clamp)
			tp->snd_cwnd = tp->snd_cwnd_clamp;
	}
	/* Wipe the slate clean for the next rtt. */
	/* veno->cntrtt = 0; */
	veno->minrtt = 0x7fffffff;
}

/* Veno MD phase */
static u32 tcp_veno_ssthresh(struct sock *sk)
{
	const struct tcp_sock *tp = tcp_sk(sk);
	struct veno *veno = inet_csk_ca(sk);

	if (veno->diff < beta)
		/* in "non-congestive state", cut cwnd by 1/5 */
		return max(tp->snd_cwnd * 4 / 5, 2U);
	else
		/* in "congestive state", cut cwnd by 1/2 */
		return max(tp->snd_cwnd >> 1U, 2U);
}

static struct tcp_congestion_ops tcp_veno = {
	.flags		= TCP_CONG_RTT_STAMP,
	.init		= tcp_veno_init,
	.ssthresh	= tcp_veno_ssthresh,
	.cong_avoid	= tcp_veno_cong_avoid,
	.pkts_acked	= tcp_veno_pkts_acked,
	.set_state	= tcp_veno_state,
	.cwnd_event	= tcp_veno_cwnd_event,

	.owner		= THIS_MODULE,
	.name		= "veno",
};

static int __init tcp_veno_register(void)
{
	BUILD_BUG_ON(sizeof(struct veno) > ICSK_CA_PRIV_SIZE);
	tcp_register_congestion_control(&tcp_veno);
	return 0;
}

static void __exit tcp_veno_unregister(void)
{
	tcp_unregister_congestion_control(&tcp_veno);
}

module_init(tcp_veno_register);
module_exit(tcp_veno_unregister);

MODULE_AUTHOR("Bin Zhou, Cheng Peng Fu");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TCP Veno");
