/*
 * TCP Westwood+: end-to-end bandwidth estimation for TCP
 *
 *      Angelo Dell'Aera: author of the first version of TCP Westwood+ in Linux 2.4
 *
 * Support at http://c3lab.poliba.it/index.php/Westwood
 * Main references in literature:
 *
 * - Mascolo S, Casetti, M. Gerla et al.
 *   "TCP Westwood: bandwidth estimation for TCP" Proc. ACM Mobicom 2001
 *
 * - A. Grieco, s. Mascolo
 *   "Performance evaluation of New Reno, Vegas, Westwood+ TCP" ACM Computer
 *     Comm. Review, 2004
 *
 * - A. Dell'Aera, L. Grieco, S. Mascolo.
 *   "Linux 2.4 Implementation of Westwood+ TCP with Rate-Halving :
 *    A Performance Evaluation Over the Internet" (ICC 2004), Paris, June 2004
 *
 * Westwood+ employs end-to-end bandwidth measurement to set cwnd and
 * ssthresh after packet loss. The probing phase is as the original Reno.
 */

#include <linux/mm.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/inet_diag.h>
#include <net/tcp.h>

/* TCP Westwood structure */
struct westwood {
	u32    bw_ns_est;        /* first bandwidth estimation..not too smoothed 8) */
	u32    bw_est;           /* bandwidth estimate */
	u32    rtt_win_sx;       /* here starts a new evaluation... */
	u32    bk;
	u32    snd_una;          /* used for evaluating the number of acked bytes */
	u32    cumul_ack;
	u32    accounted;
	u32    rtt;
	u32    rtt_min;          /* minimum observed RTT */
	u8     first_ack;        /* flag which infers that this is the first ack */
	u8     reset_rtt_min;    /* Reset RTT min to next RTT sample*/
};

/* TCP Westwood functions and constants */
#define TCP_WESTWOOD_RTT_MIN   (HZ/20)	/* 50ms */
#define TCP_WESTWOOD_INIT_RTT  (20*HZ)	/* maybe too conservative?! */

/*
 * @tcp_westwood_create
 * This function initializes fields used in TCP Westwood+,
 * it is called after the initial SYN, so the sequence numbers
 * are correct but new passive connections we have no
 * information about RTTmin at this time so we simply set it to
 * TCP_WESTWOOD_INIT_RTT. This value was chosen to be too conservative
 * since in this way we're sure it will be updated in a consistent
 * way as soon as possible. It will reasonably happen within the first
 * RTT period of the connection lifetime.
 */
static void tcp_westwood_init(struct sock *sk)
{
	struct westwood *w = inet_csk_ca(sk);

	w->bk = 0;
	w->bw_ns_est = 0;
	w->bw_est = 0;
	w->accounted = 0;
	w->cumul_ack = 0;
	w->reset_rtt_min = 1;
	w->rtt_min = w->rtt = TCP_WESTWOOD_INIT_RTT;
	w->rtt_win_sx = tcp_time_stamp;
	w->snd_una = tcp_sk(sk)->snd_una;
	w->first_ack = 1;
}

/*
 * @westwood_do_filter
 * Low-pass filter. Implemented using constant coefficients.
 */
static inline u32 westwood_do_filter(u32 a, u32 b)
{
	return ((7 * a) + b) >> 3;
}

static void westwood_filter(struct westwood *w, u32 delta)
{
	/* If the filter is empty fill it with the first sample of bandwidth  */
	if (w->bw_ns_est == 0 && w->bw_est == 0) {
		w->bw_ns_est = w->bk / delta;
		w->bw_est = w->bw_ns_est;
	} else {
		w->bw_ns_est = westwood_do_filter(w->bw_ns_est, w->bk / delta);
		w->bw_est = westwood_do_filter(w->bw_est, w->bw_ns_est);
	}
}

/*
 * @westwood_pkts_acked
 * Called after processing group of packets.
 * but all westwood needs is the last sample of srtt.
 */
static void tcp_westwood_pkts_acked(struct sock *sk, u32 cnt, s32 rtt)
{
	struct westwood *w = inet_csk_ca(sk);

	if (rtt > 0)
		w->rtt = usecs_to_jiffies(rtt);
}

/*
 * @westwood_update_window
 * It updates RTT evaluation window if it is the right moment to do
 * it. If so it calls filter for evaluating bandwidth.
 */
static void westwood_update_window(struct sock *sk)
{
	struct westwood *w = inet_csk_ca(sk);
	s32 delta = tcp_time_stamp - w->rtt_win_sx;

	/* Initialize w->snd_una with the first acked sequence number in order
	 * to fix mismatch between tp->snd_una and w->snd_una for the first
	 * bandwidth sample
	 */
	if (w->first_ack) {
		w->snd_una = tcp_sk(sk)->snd_una;
		w->first_ack = 0;
	}

	/*
	 * See if a RTT-window has passed.
	 * Be careful since if RTT is less than
	 * 50ms we don't filter but we continue 'building the sample'.
	 * This minimum limit was chosen since an estimation on small
	 * time intervals is better to avoid...
	 * Obviously on a LAN we reasonably will always have
	 * right_bound = left_bound + WESTWOOD_RTT_MIN
	 */
	if (w->rtt && delta > max_t(u32, w->rtt, TCP_WESTWOOD_RTT_MIN)) {
		westwood_filter(w, delta);

		w->bk = 0;
		w->rtt_win_sx = tcp_time_stamp;
	}
}

static inline void update_rtt_min(struct westwood *w)
{
	if (w->reset_rtt_min) {
		w->rtt_min = w->rtt;
		w->reset_rtt_min = 0;
	} else
		w->rtt_min = min(w->rtt, w->rtt_min);
}

/*
 * @westwood_fast_bw
 * It is called when we are in fast path. In particular it is called when
 * header prediction is successful. In such case in fact update is
 * straight forward and doesn't need any particular care.
 */
static inline void westwood_fast_bw(struct sock *sk)
{
	const struct tcp_sock *tp = tcp_sk(sk);
	struct westwood *w = inet_csk_ca(sk);

	westwood_update_window(sk);

	w->bk += tp->snd_una - w->snd_una;
	w->snd_una = tp->snd_una;
	update_rtt_min(w);
}

/*
 * @westwood_acked_count
 * This function evaluates cumul_ack for evaluating bk in case of
 * delayed or partial acks.
 */
static inline u32 westwood_acked_count(struct sock *sk)
{
	const struct tcp_sock *tp = tcp_sk(sk);
	struct westwood *w = inet_csk_ca(sk);

	w->cumul_ack = tp->snd_una - w->snd_una;

	/* If cumul_ack is 0 this is a dupack since it's not moving
	 * tp->snd_una.
	 */
	if (!w->cumul_ack) {
		w->accounted += tp->mss_cache;
		w->cumul_ack = tp->mss_cache;
	}

	if (w->cumul_ack > tp->mss_cache) {
		/* Partial or delayed ack */
		if (w->accounted >= w->cumul_ack) {
			w->accounted -= w->cumul_ack;
			w->cumul_ack = tp->mss_cache;
		} else {
			w->cumul_ack -= w->accounted;
			w->accounted = 0;
		}
	}

	w->snd_una = tp->snd_una;

	return w->cumul_ack;
}

/*
 * TCP Westwood
 * Here limit is evaluated as Bw estimation*RTTmin (for obtaining it
 * in packets we use mss_cache). Rttmin is guaranteed to be >= 2
 * so avoids ever returning 0.
 */
static u32 tcp_westwood_bw_rttmin(const struct sock *sk)
{
	const struct tcp_sock *tp = tcp_sk(sk);
	const struct westwood *w = inet_csk_ca(sk);

	return max_t(u32, (w->bw_est * w->rtt_min) / tp->mss_cache, 2);
}

static void tcp_westwood_ack(struct sock *sk, u32 ack_flags)
{
	if (ack_flags & CA_ACK_SLOWPATH) {
		struct westwood *w = inet_csk_ca(sk);

		westwood_update_window(sk);
		w->bk += westwood_acked_count(sk);

		update_rtt_min(w);
		return;
	}

	westwood_fast_bw(sk);
}

static void tcp_westwood_event(struct sock *sk, enum tcp_ca_event event)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct westwood *w = inet_csk_ca(sk);

	switch (event) {
	case CA_EVENT_COMPLETE_CWR:
		tp->snd_cwnd = tp->snd_ssthresh = tcp_westwood_bw_rttmin(sk);
		break;
	case CA_EVENT_LOSS:
		tp->snd_ssthresh = tcp_westwood_bw_rttmin(sk);
		/* Update RTT_min when next ack arrives */
		w->reset_rtt_min = 1;
		break;
	default:
		/* don't care */
		break;
	}
}

/* Extract info for Tcp socket info provided via netlink. */
static size_t tcp_westwood_info(struct sock *sk, u32 ext, int *attr,
				union tcp_cc_info *info)
{
	const struct westwood *ca = inet_csk_ca(sk);

	if (ext & (1 << (INET_DIAG_VEGASINFO - 1))) {
		info->vegas.tcpv_enabled = 1;
		info->vegas.tcpv_rttcnt	= 0;
		info->vegas.tcpv_rtt	= jiffies_to_usecs(ca->rtt),
		info->vegas.tcpv_minrtt	= jiffies_to_usecs(ca->rtt_min),

		*attr = INET_DIAG_VEGASINFO;
		return sizeof(struct tcpvegas_info);
	}
	return 0;
}

static struct tcp_congestion_ops tcp_westwood __read_mostly = {
	.init		= tcp_westwood_init,
	.ssthresh	= tcp_reno_ssthresh,
	.cong_avoid	= tcp_reno_cong_avoid,
	.cwnd_event	= tcp_westwood_event,
	.in_ack_event	= tcp_westwood_ack,
	.get_info	= tcp_westwood_info,
	.pkts_acked	= tcp_westwood_pkts_acked,

	.owner		= THIS_MODULE,
	.name		= "westwood"
};

static int __init tcp_westwood_register(void)
{
	BUILD_BUG_ON(sizeof(struct westwood) > ICSK_CA_PRIV_SIZE);
	return tcp_register_congestion_control(&tcp_westwood);
}

static void __exit tcp_westwood_unregister(void)
{
	tcp_unregister_congestion_control(&tcp_westwood);
}

module_init(tcp_westwood_register);
module_exit(tcp_westwood_unregister);

MODULE_AUTHOR("Stephen Hemminger, Angelo Dell'Aera");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TCP Westwood+");
