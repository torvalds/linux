/*
 * TCP CUBIC: Binary Increase Congestion control for TCP v2.0
 *
 * This is from the implementation of CUBIC TCP in
 * Injong Rhee, Lisong Xu.
 *  "CUBIC: A New TCP-Friendly High-Speed TCP Variant
 *  in PFLDnet 2005
 * Available from:
 *  http://www.csc.ncsu.edu/faculty/rhee/export/bitcp/cubic-paper.pdf
 *
 * Unless CUBIC is enabled and congestion window is large
 * this behaves the same as the original Reno.
 */

#include <linux/config.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <net/tcp.h>


#define BICTCP_BETA_SCALE    1024	/* Scale factor beta calculation
					 * max_cwnd = snd_cwnd * beta
					 */
#define BICTCP_B		4	 /*
					  * In binary search,
					  * go to point (max+min)/N
					  */
#define	BICTCP_HZ		10	/* BIC HZ 2^10 = 1024 */

static int fast_convergence = 1;
static int max_increment = 16;
static int beta = 819;		/* = 819/1024 (BICTCP_BETA_SCALE) */
static int initial_ssthresh = 100;
static int bic_scale = 41;
static int tcp_friendliness = 1;

module_param(fast_convergence, int, 0644);
MODULE_PARM_DESC(fast_convergence, "turn on/off fast convergence");
module_param(max_increment, int, 0644);
MODULE_PARM_DESC(max_increment, "Limit on increment allowed during binary search");
module_param(beta, int, 0644);
MODULE_PARM_DESC(beta, "beta for multiplicative increase");
module_param(initial_ssthresh, int, 0644);
MODULE_PARM_DESC(initial_ssthresh, "initial value of slow start threshold");
module_param(bic_scale, int, 0644);
MODULE_PARM_DESC(bic_scale, "scale (scaled by 1024) value for bic function (bic_scale/1024)");
module_param(tcp_friendliness, int, 0644);
MODULE_PARM_DESC(tcp_friendliness, "turn on/off tcp friendliness");


/* BIC TCP Parameters */
struct bictcp {
	u32	cnt;		/* increase cwnd by 1 after ACKs */
	u32 	last_max_cwnd;	/* last maximum snd_cwnd */
	u32	loss_cwnd;	/* congestion window at last loss */
	u32	last_cwnd;	/* the last snd_cwnd */
	u32	last_time;	/* time when updated last_cwnd */
	u32	bic_origin_point;/* origin point of bic function */
	u32	bic_K;		/* time to origin point from the beginning of the current epoch */
	u32	delay_min;	/* min delay */
	u32	epoch_start;	/* beginning of an epoch */
	u32	ack_cnt;	/* number of acks */
	u32	tcp_cwnd;	/* estimated tcp cwnd */
#define ACK_RATIO_SHIFT	4
	u32	delayed_ack;	/* estimate the ratio of Packets/ACKs << 4 */
};

static inline void bictcp_reset(struct bictcp *ca)
{
	ca->cnt = 0;
	ca->last_max_cwnd = 0;
	ca->loss_cwnd = 0;
	ca->last_cwnd = 0;
	ca->last_time = 0;
	ca->bic_origin_point = 0;
	ca->bic_K = 0;
	ca->delay_min = 0;
	ca->epoch_start = 0;
	ca->delayed_ack = 2 << ACK_RATIO_SHIFT;
	ca->ack_cnt = 0;
	ca->tcp_cwnd = 0;
}

static void bictcp_init(struct sock *sk)
{
	bictcp_reset(inet_csk_ca(sk));
	if (initial_ssthresh)
		tcp_sk(sk)->snd_ssthresh = initial_ssthresh;
}

/* 65536 times the cubic root */
static const u64 cubic_table[8]
	= {0, 65536, 82570, 94519, 104030, 112063, 119087, 125367};

/*
 * calculate the cubic root of x
 * the basic idea is that x can be expressed as i*8^j
 * so cubic_root(x) = cubic_root(i)*2^j
 *  in the following code, x is i, and y is 2^j
 *  because of integer calculation, there are errors in calculation
 *  so finally use binary search to find out the exact solution
 */
static u32 cubic_root(u64 x)
{
        u64 y, app, target, start, end, mid, start_diff, end_diff;

        if (x == 0)
                return 0;

        target = x;

        /* first estimate lower and upper bound */
        y = 1;
        while (x >= 8){
                x = (x >> 3);
                y = (y << 1);
        }
        start = (y*cubic_table[x])>>16;
        if (x==7)
                end = (y<<1);
        else
                end = (y*cubic_table[x+1]+65535)>>16;

        /* binary search for more accurate one */
        while (start < end-1) {
                mid = (start+end) >> 1;
                app = mid*mid*mid;
                if (app < target)
                        start = mid;
                else if (app > target)
                        end = mid;
                else
                        return mid;
        }

        /* find the most accurate one from start and end */
        app = start*start*start;
        if (app < target)
                start_diff = target - app;
        else
                start_diff = app - target;
        app = end*end*end;
        if (app < target)
                end_diff = target - app;
        else
                end_diff = app - target;

        if (start_diff < end_diff)
                return (u32)start;
        else
                return (u32)end;
}

static inline u32 bictcp_K(u32 dist, u32 srtt)
{
        u64 d64;
        u32 d32;
        u32 count;
        u32 result;

        /* calculate the "K" for (wmax-cwnd) = c/rtt * K^3
           so K = cubic_root( (wmax-cwnd)*rtt/c )
           the unit of K is bictcp_HZ=2^10, not HZ

           c = bic_scale >> 10
           rtt = (tp->srtt >> 3 ) / HZ

           the following code has been designed and tested for
           cwnd < 1 million packets
           RTT < 100 seconds
           HZ < 1,000,00  (corresponding to 10 nano-second)

        */

        /* 1/c * 2^2*bictcp_HZ */
        d32 = (1 << (10+2*BICTCP_HZ)) / bic_scale;
        d64 = (__u64)d32;

        /* srtt * 2^count / HZ
           1) to get a better accuracy of the following d32,
           the larger the "count", the better the accuracy
           2) and avoid overflow of the following d64
           the larger the "count", the high possibility of overflow
           3) so find a "count" between bictcp_hz-3 and bictcp_hz
           "count" may be less than bictcp_HZ,
           then d64 becomes 0. that is OK
        */
        d32 = srtt;
        count = 0;
        while (((d32 & 0x80000000)==0) && (count < BICTCP_HZ)){
                d32 = d32 << 1;
                count++;
        }
        d32 = d32 / HZ;

        /* (wmax-cwnd) * (srtt>>3 / HZ) / c * 2^(3*bictcp_HZ)  */
        d64 = (d64 * dist * d32) >> (count+3-BICTCP_HZ);

        /* cubic root */
        d64 = cubic_root(d64);

        result = (u32)d64;
        return result;
}

/*
 * Compute congestion window to use.
 */
static inline void bictcp_update(struct bictcp *ca, u32 cwnd)
{
	u64 d64;
	u32 d32, t, srtt, bic_target, min_cnt, max_cnt;

	ca->ack_cnt++;	/* count the number of ACKs */

	if (ca->last_cwnd == cwnd &&
	    (s32)(tcp_time_stamp - ca->last_time) <= HZ / 32)
		return;

	ca->last_cwnd = cwnd;
	ca->last_time = tcp_time_stamp;

	srtt = (HZ << 3)/10;	/* use real time-based growth function */

	if (ca->epoch_start == 0) {
		ca->epoch_start = tcp_time_stamp;	/* record the beginning of an epoch */
		ca->ack_cnt = 1;			/* start counting */
		ca->tcp_cwnd = cwnd;			/* syn with cubic */

		if (ca->last_max_cwnd <= cwnd) {
			ca->bic_K = 0;
			ca->bic_origin_point = cwnd;
		} else {
			ca->bic_K = bictcp_K(ca->last_max_cwnd-cwnd, srtt);
			ca->bic_origin_point = ca->last_max_cwnd;
		}
	}

        /* cubic function - calc*/
        /* calculate c * time^3 / rtt,
         *  while considering overflow in calculation of time^3
	 * (so time^3 is done by using d64)
	 * and without the support of division of 64bit numbers
	 * (so all divisions are done by using d32)
         *  also NOTE the unit of those veriables
         *	  time  = (t - K) / 2^bictcp_HZ
         *	  c = bic_scale >> 10
	 * rtt  = (srtt >> 3) / HZ
	 * !!! The following code does not have overflow problems,
	 * if the cwnd < 1 million packets !!!
         */

	/* change the unit from HZ to bictcp_HZ */
        t = ((tcp_time_stamp + ca->delay_min - ca->epoch_start)
	     << BICTCP_HZ) / HZ;

        if (t < ca->bic_K)		/* t - K */
                d32 = ca->bic_K - t;
        else
                d32 = t - ca->bic_K;

        d64 = (u64)d32;
        d32 = (bic_scale << 3) * HZ / srtt;			/* 1024*c/rtt */
        d64 = (d32 * d64 * d64 * d64) >> (10+3*BICTCP_HZ);	/* c/rtt * (t-K)^3 */
        d32 = (u32)d64;
        if (t < ca->bic_K)                                	/* below origin*/
                bic_target = ca->bic_origin_point - d32;
        else                                                	/* above origin*/
                bic_target = ca->bic_origin_point + d32;

        /* cubic function - calc bictcp_cnt*/
        if (bic_target > cwnd) {
		ca->cnt = cwnd / (bic_target - cwnd);
        } else {
                ca->cnt = 100 * cwnd;              /* very small increment*/
        }

	if (ca->delay_min > 0) {
		/* max increment = Smax * rtt / 0.1  */
		min_cnt = (cwnd * HZ * 8)/(10 * max_increment * ca->delay_min);
		if (ca->cnt < min_cnt)
			ca->cnt = min_cnt;
	}

        /* slow start and low utilization  */
	if (ca->loss_cwnd == 0)		/* could be aggressive in slow start */
		ca->cnt = 50;

	/* TCP Friendly */
	if (tcp_friendliness) {
		u32 scale = 8*(BICTCP_BETA_SCALE+beta)/3/(BICTCP_BETA_SCALE-beta);
		d32 = (cwnd * scale) >> 3;
	        while (ca->ack_cnt > d32) {		/* update tcp cwnd */
	                ca->ack_cnt -= d32;
        	        ca->tcp_cwnd++;
		}

		if (ca->tcp_cwnd > cwnd){	/* if bic is slower than tcp */
			d32 = ca->tcp_cwnd - cwnd;
			max_cnt = cwnd / d32;
			if (ca->cnt > max_cnt)
				ca->cnt = max_cnt;
		}
        }

	ca->cnt = (ca->cnt << ACK_RATIO_SHIFT) / ca->delayed_ack;
	if (ca->cnt == 0)			/* cannot be zero */
		ca->cnt = 1;
}


/* Keep track of minimum rtt */
static inline void measure_delay(struct sock *sk)
{
	const struct tcp_sock *tp = tcp_sk(sk);
	struct bictcp *ca = inet_csk_ca(sk);
	u32 delay;

	/* No time stamp */
	if (!(tp->rx_opt.saw_tstamp && tp->rx_opt.rcv_tsecr) ||
	     /* Discard delay samples right after fast recovery */
	    (s32)(tcp_time_stamp - ca->epoch_start) < HZ)
		return;

	delay = tcp_time_stamp - tp->rx_opt.rcv_tsecr;
	if (delay == 0)
		delay = 1;

	/* first time call or link delay decreases */
	if (ca->delay_min == 0 || ca->delay_min > delay)
		ca->delay_min = delay;
}

static void bictcp_cong_avoid(struct sock *sk, u32 ack,
			      u32 seq_rtt, u32 in_flight, int data_acked)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct bictcp *ca = inet_csk_ca(sk);

	if (data_acked)
		measure_delay(sk);

	if (!tcp_is_cwnd_limited(sk, in_flight))
		return;

	if (tp->snd_cwnd <= tp->snd_ssthresh)
		tcp_slow_start(tp);
	else {
		bictcp_update(ca, tp->snd_cwnd);

		/* In dangerous area, increase slowly.
		 * In theory this is tp->snd_cwnd += 1 / tp->snd_cwnd
		 */
		if (tp->snd_cwnd_cnt >= ca->cnt) {
			if (tp->snd_cwnd < tp->snd_cwnd_clamp)
				tp->snd_cwnd++;
			tp->snd_cwnd_cnt = 0;
		} else
			tp->snd_cwnd_cnt++;
	}

}

static u32 bictcp_recalc_ssthresh(struct sock *sk)
{
	const struct tcp_sock *tp = tcp_sk(sk);
	struct bictcp *ca = inet_csk_ca(sk);

	ca->epoch_start = 0;	/* end of epoch */

	/* Wmax and fast convergence */
	if (tp->snd_cwnd < ca->last_max_cwnd && fast_convergence)
		ca->last_max_cwnd = (tp->snd_cwnd * (BICTCP_BETA_SCALE + beta))
			/ (2 * BICTCP_BETA_SCALE);
	else
		ca->last_max_cwnd = tp->snd_cwnd;

	ca->loss_cwnd = tp->snd_cwnd;

	return max((tp->snd_cwnd * beta) / BICTCP_BETA_SCALE, 2U);
}

static u32 bictcp_undo_cwnd(struct sock *sk)
{
	struct bictcp *ca = inet_csk_ca(sk);

	return max(tcp_sk(sk)->snd_cwnd, ca->last_max_cwnd);
}

static u32 bictcp_min_cwnd(struct sock *sk)
{
	return tcp_sk(sk)->snd_ssthresh;
}

static void bictcp_state(struct sock *sk, u8 new_state)
{
	if (new_state == TCP_CA_Loss)
		bictcp_reset(inet_csk_ca(sk));
}

/* Track delayed acknowledgment ratio using sliding window
 * ratio = (15*ratio + sample) / 16
 */
static void bictcp_acked(struct sock *sk, u32 cnt)
{
	const struct inet_connection_sock *icsk = inet_csk(sk);

	if (cnt > 0 && icsk->icsk_ca_state == TCP_CA_Open) {
		struct bictcp *ca = inet_csk_ca(sk);
		cnt -= ca->delayed_ack >> ACK_RATIO_SHIFT;
		ca->delayed_ack += cnt;
	}
}


static struct tcp_congestion_ops cubictcp = {
	.init		= bictcp_init,
	.ssthresh	= bictcp_recalc_ssthresh,
	.cong_avoid	= bictcp_cong_avoid,
	.set_state	= bictcp_state,
	.undo_cwnd	= bictcp_undo_cwnd,
	.min_cwnd	= bictcp_min_cwnd,
	.pkts_acked     = bictcp_acked,
	.owner		= THIS_MODULE,
	.name		= "cubic",
};

static int __init cubictcp_register(void)
{
	BUG_ON(sizeof(struct bictcp) > ICSK_CA_PRIV_SIZE);
	return tcp_register_congestion_control(&cubictcp);
}

static void __exit cubictcp_unregister(void)
{
	tcp_unregister_congestion_control(&cubictcp);
}

module_init(cubictcp_register);
module_exit(cubictcp_unregister);

MODULE_AUTHOR("Sangtae Ha, Stephen Hemminger");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CUBIC TCP");
MODULE_VERSION("2.0");
