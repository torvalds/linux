/* agilesd is a Loss-Based Congestion Control Algorithm for TCP v1.0.
 * agilesd is designed for high-speed and short-distance networks such as data center networks and LANs.
 * agilesd has been created by Mohamed A. Alrshah, Department of Communication Technology and Networks,
 * Faculty of Computer Science and Information Technology, Universiti Putra Malaysia.
 * 
 * agilesd is based on the article, which is published in 2015 as below:
 * 
 * Alrshah, M.A., Othman, M., Ali, B. and Hanapi, Z.M., 2015. 
 * Agile-SD: a Linux-based TCP congestion control algorithm for supporting high-speed and short-distance networks. 
 * Journal of Network and Computer Applications, 55, pp.181-190.
 */

/* These includes are very important to operate the algorithm under NS2. */
//#define NS_PROTOCOL "tcp_agilesd.c"
//#include "../ns-linux-c.h"
//#include "../ns-linux-util.h"
//#include <math.h>
/* These includes are very important to operate the algorithm under NS2. */

/* These includes are very important to operate the algorithm under Linux OS. */
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/math64.h>
#include <net/tcp.h>
//#include <linux/skbuff.h>    // optional
//#include <linux/inet_diag.h> // optional
/* These includes are very important to operate the algorithm under Linux OS. */

#define SCALE   1000		/* Scale factor to avoid fractions */
#define Double_SCALE 1000000	/* Double_SCALE must be equal to SCALE^2 */
#define beta    900		/* beta for multiplicative decrease */

static int initial_ssthresh __read_mostly;
//static int beta __read_mostly = 900; /*the initial value of beta is equal to 90%*/

module_param(initial_ssthresh, int, 0644);
MODULE_PARM_DESC(initial_ssthresh, "initial value of slow start threshold");
//module_param(beta, int, 0444);
//MODULE_PARM_DESC(beta, "beta for multiplicative decrease");

/* agilesd Parameters */
struct agilesdtcp {
	u32		loss_cwnd;    // congestion window at last loss.
	u32		frac_tracer;  // This is to trace the fractions of the increment.
	u32 	degraded_loss_cwnd;   // loss_cwnd after degradation.
	enum 	dystate{SS=0, CA=1} agilesd_tcp_status;
};

static inline void agilesdtcp_reset(struct sock *sk)
{
	/*After timeout loss cntRTT and baseRTT must be reset to the initial values as below */
}

/* This function is called after the first acknowledgment is received and before the congestion
 * control algorithm will be called for the first time. If the congestion control algorithm has
 * private data, it should initialize its private date here. */
static void agilesdtcp_init(struct sock *sk)
{
	struct agilesdtcp *ca = inet_csk_ca(sk);

	// If the value of initial_ssthresh is not set, snd_ssthresh will be initialized by a large value.
	if (initial_ssthresh)
		tcp_sk(sk)->snd_ssthresh = initial_ssthresh;
	else
		tcp_sk(sk)->snd_ssthresh = 0x7fffffff;

	ca->loss_cwnd 		= 0;
	ca->frac_tracer		= 0;
	ca->agilesd_tcp_status 	= SS;
}

/* This function is called whenever an ack is received and the congestion window can be increased.
 * This is equivalent to opencwnd in tcp.cc.
 * ack is the number of bytes that are acknowledged in the latest acknowledgment;
 * rtt is the the rtt measured by the latest acknowledgment;
 * in_flight is the packet in flight before the latest acknowledgment;
 * good_ack is an indicator whether the current situation is normal (no duplicate ack, no loss and no SACK). */
static void agilesdtcp_cong_avoid(struct sock *sk, u32 ack, u32 in_flight) 					//For Linux Kernel Use.
//static void agilesdtcp_cong_avoid(struct sock *sk, u32 ack, u32 seq_rtt, u32 in_flight, int data_acked) 	//For NS2 Use.
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct agilesdtcp *ca = inet_csk_ca(sk);
	u32 inc_factor;
	u32 ca_inc;
	u32 current_gap, total_gap;
	/* The value of inc_factor is limited by lower_fl and upper_fl.
	 * The lower_fl must be always = 1. The greater the upper_fl the higher the aggressiveness.
	 * But, if upper_fl set to 1, agilesd will work exactly as newreno.
	 * We have already designed an equation to calculate the optimum upper_fl based on the given beta.
	 * This equation will be revealed once its article is published*/
	u32 lower_fl = 1 * SCALE;
	u32 upper_fl = 3 * SCALE;

	//if (!tcp_is_cwnd_limited(sk, in_flight)) return; 	//For NS-2 Use, if the in flight packets not >= cwnd do nothing//
	if (!tcp_is_cwnd_limited(sk)) return;			// For Linux Kernel Use.
	
	if (tp->snd_cwnd < tp->snd_ssthresh){
		ca->agilesd_tcp_status = SS;
		//tcp_slow_start(tp); 				//For NS-2 Use
		tcp_slow_start(tp, in_flight);			// For Linux Kernel Use.
	}
	else {
		ca->agilesd_tcp_status = CA;

		if (ca->loss_cwnd > ca->degraded_loss_cwnd)
			total_gap = ca->loss_cwnd - ca->degraded_loss_cwnd;
		else
			total_gap = 1;

		if (ca->loss_cwnd >  tp->snd_cwnd)
			current_gap = ca->loss_cwnd - tp->snd_cwnd;
		else
			current_gap = 0;

		inc_factor = min(max(((upper_fl * current_gap) / total_gap), lower_fl), upper_fl);

		ca_inc = ((inc_factor * SCALE) / tp->snd_cwnd); /* SCALE is used to avoid fractions*/

		ca->frac_tracer += ca_inc;    			/* This in order to take the fraction increase into account */
		if (ca->frac_tracer >= Double_SCALE) 	/* To take factor scale into account */
		{
			tp->snd_cwnd += 1;
			ca->frac_tracer -= Double_SCALE;
		}
	}
}

/* This function is called when the TCP flow detects a loss.
 * It returns the slow start threshold of a flow, after a packet loss is detected. */
static u32 agilesdtcp_recalc_ssthresh(struct sock *sk)
{
	const struct tcp_sock *tp = tcp_sk(sk);
	struct agilesdtcp *ca = inet_csk_ca(sk);

	ca->loss_cwnd = tp->snd_cwnd;

	if (ca->agilesd_tcp_status == CA)
		ca->degraded_loss_cwnd = max((tp->snd_cwnd * beta) / SCALE, 2U);
	else
		ca->degraded_loss_cwnd = max((tp->snd_cwnd * beta) / SCALE, 2U);

	ca->frac_tracer = 0;

	return ca->degraded_loss_cwnd;
}

static u32 agilesdtcp_undo_cwnd(struct sock *sk)
{
	const struct tcp_sock *tp = tcp_sk(sk);
	const struct agilesdtcp *ca = inet_csk_ca(sk);
	return max(tp->snd_cwnd, ca->loss_cwnd);
}

/* This function is called when the congestion state of the TCP is changed.
 * newstate is the state code for the state that TCP is going to be in.
 * The possible states are listed below:
 * The current congestion control state, which can be one of the followings:
 * TCP_CA_Open: normal state
 * TCP_CA_Recovery: Loss Recovery after a Fast Transmission
 * TCP_CA_Loss: Loss Recovery after a  Timeout
 * (The following two states are not effective in TCP-Linux but is effective in Linux)
 * TCP_CA_Disorder: duplicate packets detected, but haven't reach the threshold. So TCP  shall assume that  packet reordering is happening.
 * TCP_CA_CWR: the state that congestion window is decreasing (after local congestion in NIC, or ECN and etc).
 * It is to notify the congestion control algorithm and is used by some
 * algorithms which turn off their special control during loss recovery. */
static void agilesdtcp_state(struct sock *sk, u8 new_state)
{
	if (new_state == TCP_CA_Loss)
		agilesdtcp_reset(inet_csk_ca(sk));
}

/* This function is called when there is an acknowledgment that acknowledges some new packets.
 * num_acked is the number of packets that are acknowledged by this acknowledgments. */
//static void agilesdtcp_acked(struct sock *sk, u32 num_acked, ktime_t rtt_us) 			//For NS2 Use.
static void agilesdtcp_acked(struct sock *sk, u32 num_acked, s32 rtt_us) 			//For Linux Kernel Use.
{

}

static struct tcp_congestion_ops agilesdtcp __read_mostly = {
	.init		= agilesdtcp_init,
	.ssthresh	= agilesdtcp_recalc_ssthresh, 	//REQUIRED
	.cong_avoid	= agilesdtcp_cong_avoid, 	//REQUIRED
	.set_state	= agilesdtcp_state,
	.undo_cwnd	= agilesdtcp_undo_cwnd,
	.pkts_acked	= agilesdtcp_acked,
	.owner		= THIS_MODULE,
	.name		= "agilesd", 			//REQUIRED
	//.min_cwnd	= agilesdtcp_min_cwnd, 		//NOT REQUIRED
};

static int __init agilesdtcp_register(void)
{
	BUILD_BUG_ON(sizeof(struct agilesdtcp) > ICSK_CA_PRIV_SIZE);
	return tcp_register_congestion_control(&agilesdtcp);
}

static void __exit agilesdtcp_unregister(void)
{
	tcp_unregister_congestion_control(&agilesdtcp);
}

module_init(agilesdtcp_register);
module_exit(agilesdtcp_unregister);

MODULE_AUTHOR("Mohamed A. Alrshah");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("agilesd is a Loss-Based Congestion Control Algorithm for TCP v1.0. By Mohamed A. Alrshah");
MODULE_VERSION("1.0");
