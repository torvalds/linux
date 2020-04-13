// SPDX-License-Identifier: GPL-2.0-only
/* Tom Kelly's Scalable TCP
 *
 * See http://www.deneholme.net/tom/scalable/
 *
 * John Heffner <jheffner@sc.edu>
 */

#include <linux/module.h>
#include <net/tcp.h>

/* These factors derived from the recommended values in the aer:
 * .01 and and 7/8.
 */
#define TCP_SCALABLE_AI_CNT	100U
#define TCP_SCALABLE_MD_SCALE	3

static void tcp_scalable_cong_avoid(struct sock *sk, u32 ack, u32 acked)
{
	struct tcp_sock *tp = tcp_sk(sk);

	if (!tcp_is_cwnd_limited(sk))
		return;

	if (tcp_in_slow_start(tp)) {
		acked = tcp_slow_start(tp, acked);
		if (!acked)
			return;
	}
	tcp_cong_avoid_ai(tp, min(tp->snd_cwnd, TCP_SCALABLE_AI_CNT),
			  acked);
}

static u32 tcp_scalable_ssthresh(struct sock *sk)
{
	const struct tcp_sock *tp = tcp_sk(sk);

	return max(tp->snd_cwnd - (tp->snd_cwnd>>TCP_SCALABLE_MD_SCALE), 2U);
}

static struct tcp_congestion_ops tcp_scalable __read_mostly = {
	.ssthresh	= tcp_scalable_ssthresh,
	.undo_cwnd	= tcp_reno_undo_cwnd,
	.cong_avoid	= tcp_scalable_cong_avoid,

	.owner		= THIS_MODULE,
	.name		= "scalable",
};

static int __init tcp_scalable_register(void)
{
	return tcp_register_congestion_control(&tcp_scalable);
}

static void __exit tcp_scalable_unregister(void)
{
	tcp_unregister_congestion_control(&tcp_scalable);
}

module_init(tcp_scalable_register);
module_exit(tcp_scalable_unregister);

MODULE_AUTHOR("John Heffner");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Scalable TCP");
