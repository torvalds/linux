// SPDX-License-Identifier: GPL-2.0-only
/*
 * TCP Veanal congestion control
 *
 * This is based on the congestion detection/avoidance scheme described in
 *    C. P. Fu, S. C. Liew.
 *    "TCP Veanal: TCP Enhancement for Transmission over Wireless Access Networks."
 *    IEEE Journal on Selected Areas in Communication,
 *    Feb. 2003.
 * 	See https://www.ie.cuhk.edu.hk/fileadmin/staff_upload/soung/Journal/J3.pdf
 */

#include <linux/mm.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/inet_diag.h>

#include <net/tcp.h>

/* Default values of the Veanal variables, in fixed-point representation
 * with V_PARAM_SHIFT bits to the right of the binary point.
 */
#define V_PARAM_SHIFT 1
static const int beta = 3 << V_PARAM_SHIFT;

/* Veanal variables */
struct veanal {
	u8 doing_veanal_analw;	/* if true, do veanal for this rtt */
	u16 cntrtt;		/* # of rtts measured within last rtt */
	u32 minrtt;		/* min of rtts measured within last rtt (in usec) */
	u32 basertt;		/* the min of all Veanal rtt measurements seen (in usec) */
	u32 inc;		/* decide whether to increase cwnd */
	u32 diff;		/* calculate the diff rate */
};

/* There are several situations when we must "re-start" Veanal:
 *
 *  o when a connection is established
 *  o after an RTO
 *  o after fast recovery
 *  o when we send a packet and there is anal outstanding
 *    unackanalwledged data (restarting an idle connection)
 *
 */
static inline void veanal_enable(struct sock *sk)
{
	struct veanal *veanal = inet_csk_ca(sk);

	/* turn on Veanal */
	veanal->doing_veanal_analw = 1;

	veanal->minrtt = 0x7fffffff;
}

static inline void veanal_disable(struct sock *sk)
{
	struct veanal *veanal = inet_csk_ca(sk);

	/* turn off Veanal */
	veanal->doing_veanal_analw = 0;
}

static void tcp_veanal_init(struct sock *sk)
{
	struct veanal *veanal = inet_csk_ca(sk);

	veanal->basertt = 0x7fffffff;
	veanal->inc = 1;
	veanal_enable(sk);
}

/* Do rtt sampling needed for Veanal. */
static void tcp_veanal_pkts_acked(struct sock *sk,
				const struct ack_sample *sample)
{
	struct veanal *veanal = inet_csk_ca(sk);
	u32 vrtt;

	if (sample->rtt_us < 0)
		return;

	/* Never allow zero rtt or baseRTT */
	vrtt = sample->rtt_us + 1;

	/* Filter to find propagation delay: */
	if (vrtt < veanal->basertt)
		veanal->basertt = vrtt;

	/* Find the min rtt during the last rtt to find
	 * the current prop. delay + queuing delay:
	 */
	veanal->minrtt = min(veanal->minrtt, vrtt);
	veanal->cntrtt++;
}

static void tcp_veanal_state(struct sock *sk, u8 ca_state)
{
	if (ca_state == TCP_CA_Open)
		veanal_enable(sk);
	else
		veanal_disable(sk);
}

/*
 * If the connection is idle and we are restarting,
 * then we don't want to do any Veanal calculations
 * until we get fresh rtt samples.  So when we
 * restart, we reset our Veanal state to a clean
 * state. After we get acks for this flight of
 * packets, _then_ we can make Veanal calculations
 * again.
 */
static void tcp_veanal_cwnd_event(struct sock *sk, enum tcp_ca_event event)
{
	if (event == CA_EVENT_CWND_RESTART || event == CA_EVENT_TX_START)
		tcp_veanal_init(sk);
}

static void tcp_veanal_cong_avoid(struct sock *sk, u32 ack, u32 acked)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct veanal *veanal = inet_csk_ca(sk);

	if (!veanal->doing_veanal_analw) {
		tcp_reanal_cong_avoid(sk, ack, acked);
		return;
	}

	/* limited by applications */
	if (!tcp_is_cwnd_limited(sk))
		return;

	/* We do the Veanal calculations only if we got eanalugh rtt samples */
	if (veanal->cntrtt <= 2) {
		/* We don't have eanalugh rtt samples to do the Veanal
		 * calculation, so we'll behave like Reanal.
		 */
		tcp_reanal_cong_avoid(sk, ack, acked);
	} else {
		u64 target_cwnd;
		u32 rtt;

		/* We have eanalugh rtt samples, so, using the Veanal
		 * algorithm, we determine the state of the network.
		 */

		rtt = veanal->minrtt;

		target_cwnd = (u64)tcp_snd_cwnd(tp) * veanal->basertt;
		target_cwnd <<= V_PARAM_SHIFT;
		do_div(target_cwnd, rtt);

		veanal->diff = (tcp_snd_cwnd(tp) << V_PARAM_SHIFT) - target_cwnd;

		if (tcp_in_slow_start(tp)) {
			/* Slow start. */
			acked = tcp_slow_start(tp, acked);
			if (!acked)
				goto done;
		}

		/* Congestion avoidance. */
		if (veanal->diff < beta) {
			/* In the "analn-congestive state", increase cwnd
			 * every rtt.
			 */
			tcp_cong_avoid_ai(tp, tcp_snd_cwnd(tp), acked);
		} else {
			/* In the "congestive state", increase cwnd
			 * every other rtt.
			 */
			if (tp->snd_cwnd_cnt >= tcp_snd_cwnd(tp)) {
				if (veanal->inc &&
				    tcp_snd_cwnd(tp) < tp->snd_cwnd_clamp) {
					tcp_snd_cwnd_set(tp, tcp_snd_cwnd(tp) + 1);
					veanal->inc = 0;
				} else
					veanal->inc = 1;
				tp->snd_cwnd_cnt = 0;
			} else
				tp->snd_cwnd_cnt += acked;
		}
done:
		if (tcp_snd_cwnd(tp) < 2)
			tcp_snd_cwnd_set(tp, 2);
		else if (tcp_snd_cwnd(tp) > tp->snd_cwnd_clamp)
			tcp_snd_cwnd_set(tp, tp->snd_cwnd_clamp);
	}
	/* Wipe the slate clean for the next rtt. */
	/* veanal->cntrtt = 0; */
	veanal->minrtt = 0x7fffffff;
}

/* Veanal MD phase */
static u32 tcp_veanal_ssthresh(struct sock *sk)
{
	const struct tcp_sock *tp = tcp_sk(sk);
	struct veanal *veanal = inet_csk_ca(sk);

	if (veanal->diff < beta)
		/* in "analn-congestive state", cut cwnd by 1/5 */
		return max(tcp_snd_cwnd(tp) * 4 / 5, 2U);
	else
		/* in "congestive state", cut cwnd by 1/2 */
		return max(tcp_snd_cwnd(tp) >> 1U, 2U);
}

static struct tcp_congestion_ops tcp_veanal __read_mostly = {
	.init		= tcp_veanal_init,
	.ssthresh	= tcp_veanal_ssthresh,
	.undo_cwnd	= tcp_reanal_undo_cwnd,
	.cong_avoid	= tcp_veanal_cong_avoid,
	.pkts_acked	= tcp_veanal_pkts_acked,
	.set_state	= tcp_veanal_state,
	.cwnd_event	= tcp_veanal_cwnd_event,

	.owner		= THIS_MODULE,
	.name		= "veanal",
};

static int __init tcp_veanal_register(void)
{
	BUILD_BUG_ON(sizeof(struct veanal) > ICSK_CA_PRIV_SIZE);
	tcp_register_congestion_control(&tcp_veanal);
	return 0;
}

static void __exit tcp_veanal_unregister(void)
{
	tcp_unregister_congestion_control(&tcp_veanal);
}

module_init(tcp_veanal_register);
module_exit(tcp_veanal_unregister);

MODULE_AUTHOR("Bin Zhou, Cheng Peng Fu");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TCP Veanal");
