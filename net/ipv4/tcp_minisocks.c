/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Implementation of the Transmission Control Protocol(TCP).
 *
 * Version:	$Id: tcp_minisocks.c,v 1.15 2002/02/01 22:01:04 davem Exp $
 *
 * Authors:	Ross Biro
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Mark Evans, <evansmp@uhura.aston.ac.uk>
 *		Corey Minyard <wf-rch!minyard@relay.EU.net>
 *		Florian La Roche, <flla@stud.uni-sb.de>
 *		Charles Hedrick, <hedrick@klinzhai.rutgers.edu>
 *		Linus Torvalds, <torvalds@cs.helsinki.fi>
 *		Alan Cox, <gw4pts@gw4pts.ampr.org>
 *		Matthew Dillon, <dillon@apollo.west.oic.com>
 *		Arnt Gulbrandsen, <agulbra@nvg.unit.no>
 *		Jorge Cwik, <jorge@laser.satlink.net>
 */

#include <linux/config.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/sysctl.h>
#include <linux/workqueue.h>
#include <net/tcp.h>
#include <net/inet_common.h>
#include <net/xfrm.h>

#ifdef CONFIG_SYSCTL
#define SYNC_INIT 0 /* let the user enable it */
#else
#define SYNC_INIT 1
#endif

int sysctl_tcp_tw_recycle;
int sysctl_tcp_max_tw_buckets = NR_FILE*2;

int sysctl_tcp_syncookies = SYNC_INIT; 
int sysctl_tcp_abort_on_overflow;

static void tcp_tw_schedule(struct inet_timewait_sock *tw, int timeo);

static __inline__ int tcp_in_window(u32 seq, u32 end_seq, u32 s_win, u32 e_win)
{
	if (seq == s_win)
		return 1;
	if (after(end_seq, s_win) && before(seq, e_win))
		return 1;
	return (seq == e_win && seq == end_seq);
}

/* New-style handling of TIME_WAIT sockets. */

int tcp_tw_count;

/* 
 * * Main purpose of TIME-WAIT state is to close connection gracefully,
 *   when one of ends sits in LAST-ACK or CLOSING retransmitting FIN
 *   (and, probably, tail of data) and one or more our ACKs are lost.
 * * What is TIME-WAIT timeout? It is associated with maximal packet
 *   lifetime in the internet, which results in wrong conclusion, that
 *   it is set to catch "old duplicate segments" wandering out of their path.
 *   It is not quite correct. This timeout is calculated so that it exceeds
 *   maximal retransmission timeout enough to allow to lose one (or more)
 *   segments sent by peer and our ACKs. This time may be calculated from RTO.
 * * When TIME-WAIT socket receives RST, it means that another end
 *   finally closed and we are allowed to kill TIME-WAIT too.
 * * Second purpose of TIME-WAIT is catching old duplicate segments.
 *   Well, certainly it is pure paranoia, but if we load TIME-WAIT
 *   with this semantics, we MUST NOT kill TIME-WAIT state with RSTs.
 * * If we invented some more clever way to catch duplicates
 *   (f.e. based on PAWS), we could truncate TIME-WAIT to several RTOs.
 *
 * The algorithm below is based on FORMAL INTERPRETATION of RFCs.
 * When you compare it to RFCs, please, read section SEGMENT ARRIVES
 * from the very beginning.
 *
 * NOTE. With recycling (and later with fin-wait-2) TW bucket
 * is _not_ stateless. It means, that strictly speaking we must
 * spinlock it. I do not want! Well, probability of misbehaviour
 * is ridiculously low and, seems, we could use some mb() tricks
 * to avoid misread sequence numbers, states etc.  --ANK
 */
enum tcp_tw_status
tcp_timewait_state_process(struct inet_timewait_sock *tw, struct sk_buff *skb,
			   const struct tcphdr *th)
{
	struct tcp_timewait_sock *tcptw = tcp_twsk((struct sock *)tw);
	struct tcp_options_received tmp_opt;
	int paws_reject = 0;

	tmp_opt.saw_tstamp = 0;
	if (th->doff > (sizeof(*th) >> 2) && tcptw->tw_ts_recent_stamp) {
		tcp_parse_options(skb, &tmp_opt, 0);

		if (tmp_opt.saw_tstamp) {
			tmp_opt.ts_recent	= tcptw->tw_ts_recent;
			tmp_opt.ts_recent_stamp	= tcptw->tw_ts_recent_stamp;
			paws_reject = tcp_paws_check(&tmp_opt, th->rst);
		}
	}

	if (tw->tw_substate == TCP_FIN_WAIT2) {
		/* Just repeat all the checks of tcp_rcv_state_process() */

		/* Out of window, send ACK */
		if (paws_reject ||
		    !tcp_in_window(TCP_SKB_CB(skb)->seq, TCP_SKB_CB(skb)->end_seq,
				   tcptw->tw_rcv_nxt,
				   tcptw->tw_rcv_nxt + tcptw->tw_rcv_wnd))
			return TCP_TW_ACK;

		if (th->rst)
			goto kill;

		if (th->syn && !before(TCP_SKB_CB(skb)->seq, tcptw->tw_rcv_nxt))
			goto kill_with_rst;

		/* Dup ACK? */
		if (!after(TCP_SKB_CB(skb)->end_seq, tcptw->tw_rcv_nxt) ||
		    TCP_SKB_CB(skb)->end_seq == TCP_SKB_CB(skb)->seq) {
			inet_twsk_put(tw);
			return TCP_TW_SUCCESS;
		}

		/* New data or FIN. If new data arrive after half-duplex close,
		 * reset.
		 */
		if (!th->fin ||
		    TCP_SKB_CB(skb)->end_seq != tcptw->tw_rcv_nxt + 1) {
kill_with_rst:
			tcp_tw_deschedule(tw);
			inet_twsk_put(tw);
			return TCP_TW_RST;
		}

		/* FIN arrived, enter true time-wait state. */
		tw->tw_substate	  = TCP_TIME_WAIT;
		tcptw->tw_rcv_nxt = TCP_SKB_CB(skb)->end_seq;
		if (tmp_opt.saw_tstamp) {
			tcptw->tw_ts_recent_stamp = xtime.tv_sec;
			tcptw->tw_ts_recent	  = tmp_opt.rcv_tsval;
		}

		/* I am shamed, but failed to make it more elegant.
		 * Yes, it is direct reference to IP, which is impossible
		 * to generalize to IPv6. Taking into account that IPv6
		 * do not undertsnad recycling in any case, it not
		 * a big problem in practice. --ANK */
		if (tw->tw_family == AF_INET &&
		    sysctl_tcp_tw_recycle && tcptw->tw_ts_recent_stamp &&
		    tcp_v4_tw_remember_stamp(tw))
			tcp_tw_schedule(tw, tw->tw_timeout);
		else
			tcp_tw_schedule(tw, TCP_TIMEWAIT_LEN);
		return TCP_TW_ACK;
	}

	/*
	 *	Now real TIME-WAIT state.
	 *
	 *	RFC 1122:
	 *	"When a connection is [...] on TIME-WAIT state [...]
	 *	[a TCP] MAY accept a new SYN from the remote TCP to
	 *	reopen the connection directly, if it:
	 *	
	 *	(1)  assigns its initial sequence number for the new
	 *	connection to be larger than the largest sequence
	 *	number it used on the previous connection incarnation,
	 *	and
	 *
	 *	(2)  returns to TIME-WAIT state if the SYN turns out 
	 *	to be an old duplicate".
	 */

	if (!paws_reject &&
	    (TCP_SKB_CB(skb)->seq == tcptw->tw_rcv_nxt &&
	     (TCP_SKB_CB(skb)->seq == TCP_SKB_CB(skb)->end_seq || th->rst))) {
		/* In window segment, it may be only reset or bare ack. */

		if (th->rst) {
			/* This is TIME_WAIT assasination, in two flavors.
			 * Oh well... nobody has a sufficient solution to this
			 * protocol bug yet.
			 */
			if (sysctl_tcp_rfc1337 == 0) {
kill:
				tcp_tw_deschedule(tw);
				inet_twsk_put(tw);
				return TCP_TW_SUCCESS;
			}
		}
		tcp_tw_schedule(tw, TCP_TIMEWAIT_LEN);

		if (tmp_opt.saw_tstamp) {
			tcptw->tw_ts_recent	  = tmp_opt.rcv_tsval;
			tcptw->tw_ts_recent_stamp = xtime.tv_sec;
		}

		inet_twsk_put(tw);
		return TCP_TW_SUCCESS;
	}

	/* Out of window segment.

	   All the segments are ACKed immediately.

	   The only exception is new SYN. We accept it, if it is
	   not old duplicate and we are not in danger to be killed
	   by delayed old duplicates. RFC check is that it has
	   newer sequence number works at rates <40Mbit/sec.
	   However, if paws works, it is reliable AND even more,
	   we even may relax silly seq space cutoff.

	   RED-PEN: we violate main RFC requirement, if this SYN will appear
	   old duplicate (i.e. we receive RST in reply to SYN-ACK),
	   we must return socket to time-wait state. It is not good,
	   but not fatal yet.
	 */

	if (th->syn && !th->rst && !th->ack && !paws_reject &&
	    (after(TCP_SKB_CB(skb)->seq, tcptw->tw_rcv_nxt) ||
	     (tmp_opt.saw_tstamp &&
	      (s32)(tcptw->tw_ts_recent - tmp_opt.rcv_tsval) < 0))) {
		u32 isn = tcptw->tw_snd_nxt + 65535 + 2;
		if (isn == 0)
			isn++;
		TCP_SKB_CB(skb)->when = isn;
		return TCP_TW_SYN;
	}

	if (paws_reject)
		NET_INC_STATS_BH(LINUX_MIB_PAWSESTABREJECTED);

	if(!th->rst) {
		/* In this case we must reset the TIMEWAIT timer.
		 *
		 * If it is ACKless SYN it may be both old duplicate
		 * and new good SYN with random sequence number <rcv_nxt.
		 * Do not reschedule in the last case.
		 */
		if (paws_reject || th->ack)
			tcp_tw_schedule(tw, TCP_TIMEWAIT_LEN);

		/* Send ACK. Note, we do not put the bucket,
		 * it will be released by caller.
		 */
		return TCP_TW_ACK;
	}
	inet_twsk_put(tw);
	return TCP_TW_SUCCESS;
}

/* 
 * Move a socket to time-wait or dead fin-wait-2 state.
 */ 
void tcp_time_wait(struct sock *sk, int state, int timeo)
{
	struct inet_timewait_sock *tw = NULL;
	const struct tcp_sock *tp = tcp_sk(sk);
	int recycle_ok = 0;

	if (sysctl_tcp_tw_recycle && tp->rx_opt.ts_recent_stamp)
		recycle_ok = tp->af_specific->remember_stamp(sk);

	if (tcp_tw_count < sysctl_tcp_max_tw_buckets)
		tw = inet_twsk_alloc(sk, state);

	if (tw != NULL) {
		struct tcp_timewait_sock *tcptw = tcp_twsk((struct sock *)tw);
		const struct inet_connection_sock *icsk = inet_csk(sk);
		const int rto = (icsk->icsk_rto << 2) - (icsk->icsk_rto >> 1);

		tw->tw_rcv_wscale	= tp->rx_opt.rcv_wscale;
		tcptw->tw_rcv_nxt	= tp->rcv_nxt;
		tcptw->tw_snd_nxt	= tp->snd_nxt;
		tcptw->tw_rcv_wnd	= tcp_receive_window(tp);
		tcptw->tw_ts_recent	= tp->rx_opt.ts_recent;
		tcptw->tw_ts_recent_stamp = tp->rx_opt.ts_recent_stamp;

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
		if (tw->tw_family == PF_INET6) {
			struct ipv6_pinfo *np = inet6_sk(sk);
			struct tcp6_timewait_sock *tcp6tw = tcp6_twsk((struct sock *)tw);

			ipv6_addr_copy(&tcp6tw->tw_v6_daddr, &np->daddr);
			ipv6_addr_copy(&tcp6tw->tw_v6_rcv_saddr, &np->rcv_saddr);
			tw->tw_ipv6only = np->ipv6only;
		}
#endif
		/* Linkage updates. */
		__inet_twsk_hashdance(tw, sk, &tcp_hashinfo);

		/* Get the TIME_WAIT timeout firing. */
		if (timeo < rto)
			timeo = rto;

		if (recycle_ok) {
			tw->tw_timeout = rto;
		} else {
			tw->tw_timeout = TCP_TIMEWAIT_LEN;
			if (state == TCP_TIME_WAIT)
				timeo = TCP_TIMEWAIT_LEN;
		}

		tcp_tw_schedule(tw, timeo);
		inet_twsk_put(tw);
	} else {
		/* Sorry, if we're out of memory, just CLOSE this
		 * socket up.  We've got bigger problems than
		 * non-graceful socket closings.
		 */
		if (net_ratelimit())
			printk(KERN_INFO "TCP: time wait bucket table overflow\n");
	}

	tcp_update_metrics(sk);
	tcp_done(sk);
}

/* Kill off TIME_WAIT sockets once their lifetime has expired. */
static int tcp_tw_death_row_slot;

static void tcp_twkill(unsigned long);

/* TIME_WAIT reaping mechanism. */
#define TCP_TWKILL_SLOTS	8	/* Please keep this a power of 2. */
#define TCP_TWKILL_PERIOD	(TCP_TIMEWAIT_LEN/TCP_TWKILL_SLOTS)

#define TCP_TWKILL_QUOTA	100

static struct hlist_head tcp_tw_death_row[TCP_TWKILL_SLOTS];
static DEFINE_SPINLOCK(tw_death_lock);
static struct timer_list tcp_tw_timer = TIMER_INITIALIZER(tcp_twkill, 0, 0);
static void twkill_work(void *);
static DECLARE_WORK(tcp_twkill_work, twkill_work, NULL);
static u32 twkill_thread_slots;

/* Returns non-zero if quota exceeded.  */
static int tcp_do_twkill_work(int slot, unsigned int quota)
{
	struct inet_timewait_sock *tw;
	struct hlist_node *node;
	unsigned int killed;
	int ret;

	/* NOTE: compare this to previous version where lock
	 * was released after detaching chain. It was racy,
	 * because tw buckets are scheduled in not serialized context
	 * in 2.3 (with netfilter), and with softnet it is common, because
	 * soft irqs are not sequenced.
	 */
	killed = 0;
	ret = 0;
rescan:
	inet_twsk_for_each_inmate(tw, node, &tcp_tw_death_row[slot]) {
		__inet_twsk_del_dead_node(tw);
		spin_unlock(&tw_death_lock);
		__inet_twsk_kill(tw, &tcp_hashinfo);
		inet_twsk_put(tw);
		killed++;
		spin_lock(&tw_death_lock);
		if (killed > quota) {
			ret = 1;
			break;
		}

		/* While we dropped tw_death_lock, another cpu may have
		 * killed off the next TW bucket in the list, therefore
		 * do a fresh re-read of the hlist head node with the
		 * lock reacquired.  We still use the hlist traversal
		 * macro in order to get the prefetches.
		 */
		goto rescan;
	}

	tcp_tw_count -= killed;
	NET_ADD_STATS_BH(LINUX_MIB_TIMEWAITED, killed);

	return ret;
}

static void tcp_twkill(unsigned long dummy)
{
	int need_timer, ret;

	spin_lock(&tw_death_lock);

	if (tcp_tw_count == 0)
		goto out;

	need_timer = 0;
	ret = tcp_do_twkill_work(tcp_tw_death_row_slot, TCP_TWKILL_QUOTA);
	if (ret) {
		twkill_thread_slots |= (1 << tcp_tw_death_row_slot);
		mb();
		schedule_work(&tcp_twkill_work);
		need_timer = 1;
	} else {
		/* We purged the entire slot, anything left?  */
		if (tcp_tw_count)
			need_timer = 1;
	}
	tcp_tw_death_row_slot =
		((tcp_tw_death_row_slot + 1) & (TCP_TWKILL_SLOTS - 1));
	if (need_timer)
		mod_timer(&tcp_tw_timer, jiffies + TCP_TWKILL_PERIOD);
out:
	spin_unlock(&tw_death_lock);
}

extern void twkill_slots_invalid(void);

static void twkill_work(void *dummy)
{
	int i;

	if ((TCP_TWKILL_SLOTS - 1) > (sizeof(twkill_thread_slots) * 8))
		twkill_slots_invalid();

	while (twkill_thread_slots) {
		spin_lock_bh(&tw_death_lock);
		for (i = 0; i < TCP_TWKILL_SLOTS; i++) {
			if (!(twkill_thread_slots & (1 << i)))
				continue;

			while (tcp_do_twkill_work(i, TCP_TWKILL_QUOTA) != 0) {
				if (need_resched()) {
					spin_unlock_bh(&tw_death_lock);
					schedule();
					spin_lock_bh(&tw_death_lock);
				}
			}

			twkill_thread_slots &= ~(1 << i);
		}
		spin_unlock_bh(&tw_death_lock);
	}
}

/* These are always called from BH context.  See callers in
 * tcp_input.c to verify this.
 */

/* This is for handling early-kills of TIME_WAIT sockets. */
void tcp_tw_deschedule(struct inet_timewait_sock *tw)
{
	spin_lock(&tw_death_lock);
	if (inet_twsk_del_dead_node(tw)) {
		inet_twsk_put(tw);
		if (--tcp_tw_count == 0)
			del_timer(&tcp_tw_timer);
	}
	spin_unlock(&tw_death_lock);
	__inet_twsk_kill(tw, &tcp_hashinfo);
}

/* Short-time timewait calendar */

static int tcp_twcal_hand = -1;
static int tcp_twcal_jiffie;
static void tcp_twcal_tick(unsigned long);
static struct timer_list tcp_twcal_timer =
		TIMER_INITIALIZER(tcp_twcal_tick, 0, 0);
static struct hlist_head tcp_twcal_row[TCP_TW_RECYCLE_SLOTS];

static void tcp_tw_schedule(struct inet_timewait_sock *tw, const int timeo)
{
	struct hlist_head *list;
	int slot;

	/* timeout := RTO * 3.5
	 *
	 * 3.5 = 1+2+0.5 to wait for two retransmits.
	 *
	 * RATIONALE: if FIN arrived and we entered TIME-WAIT state,
	 * our ACK acking that FIN can be lost. If N subsequent retransmitted
	 * FINs (or previous seqments) are lost (probability of such event
	 * is p^(N+1), where p is probability to lose single packet and
	 * time to detect the loss is about RTO*(2^N - 1) with exponential
	 * backoff). Normal timewait length is calculated so, that we
	 * waited at least for one retransmitted FIN (maximal RTO is 120sec).
	 * [ BTW Linux. following BSD, violates this requirement waiting
	 *   only for 60sec, we should wait at least for 240 secs.
	 *   Well, 240 consumes too much of resources 8)
	 * ]
	 * This interval is not reduced to catch old duplicate and
	 * responces to our wandering segments living for two MSLs.
	 * However, if we use PAWS to detect
	 * old duplicates, we can reduce the interval to bounds required
	 * by RTO, rather than MSL. So, if peer understands PAWS, we
	 * kill tw bucket after 3.5*RTO (it is important that this number
	 * is greater than TS tick!) and detect old duplicates with help
	 * of PAWS.
	 */
	slot = (timeo + (1<<TCP_TW_RECYCLE_TICK) - 1) >> TCP_TW_RECYCLE_TICK;

	spin_lock(&tw_death_lock);

	/* Unlink it, if it was scheduled */
	if (inet_twsk_del_dead_node(tw))
		tcp_tw_count--;
	else
		atomic_inc(&tw->tw_refcnt);

	if (slot >= TCP_TW_RECYCLE_SLOTS) {
		/* Schedule to slow timer */
		if (timeo >= TCP_TIMEWAIT_LEN) {
			slot = TCP_TWKILL_SLOTS-1;
		} else {
			slot = (timeo + TCP_TWKILL_PERIOD-1) / TCP_TWKILL_PERIOD;
			if (slot >= TCP_TWKILL_SLOTS)
				slot = TCP_TWKILL_SLOTS-1;
		}
		tw->tw_ttd = jiffies + timeo;
		slot = (tcp_tw_death_row_slot + slot) & (TCP_TWKILL_SLOTS - 1);
		list = &tcp_tw_death_row[slot];
	} else {
		tw->tw_ttd = jiffies + (slot << TCP_TW_RECYCLE_TICK);

		if (tcp_twcal_hand < 0) {
			tcp_twcal_hand = 0;
			tcp_twcal_jiffie = jiffies;
			tcp_twcal_timer.expires = tcp_twcal_jiffie + (slot<<TCP_TW_RECYCLE_TICK);
			add_timer(&tcp_twcal_timer);
		} else {
			if (time_after(tcp_twcal_timer.expires, jiffies + (slot<<TCP_TW_RECYCLE_TICK)))
				mod_timer(&tcp_twcal_timer, jiffies + (slot<<TCP_TW_RECYCLE_TICK));
			slot = (tcp_twcal_hand + slot)&(TCP_TW_RECYCLE_SLOTS-1);
		}
		list = &tcp_twcal_row[slot];
	}

	hlist_add_head(&tw->tw_death_node, list);

	if (tcp_tw_count++ == 0)
		mod_timer(&tcp_tw_timer, jiffies+TCP_TWKILL_PERIOD);
	spin_unlock(&tw_death_lock);
}

void tcp_twcal_tick(unsigned long dummy)
{
	int n, slot;
	unsigned long j;
	unsigned long now = jiffies;
	int killed = 0;
	int adv = 0;

	spin_lock(&tw_death_lock);
	if (tcp_twcal_hand < 0)
		goto out;

	slot = tcp_twcal_hand;
	j = tcp_twcal_jiffie;

	for (n=0; n<TCP_TW_RECYCLE_SLOTS; n++) {
		if (time_before_eq(j, now)) {
			struct hlist_node *node, *safe;
			struct inet_timewait_sock *tw;

			inet_twsk_for_each_inmate_safe(tw, node, safe,
						       &tcp_twcal_row[slot]) {
				__inet_twsk_del_dead_node(tw);
				__inet_twsk_kill(tw, &tcp_hashinfo);
				inet_twsk_put(tw);
				killed++;
			}
		} else {
			if (!adv) {
				adv = 1;
				tcp_twcal_jiffie = j;
				tcp_twcal_hand = slot;
			}

			if (!hlist_empty(&tcp_twcal_row[slot])) {
				mod_timer(&tcp_twcal_timer, j);
				goto out;
			}
		}
		j += (1<<TCP_TW_RECYCLE_TICK);
		slot = (slot+1)&(TCP_TW_RECYCLE_SLOTS-1);
	}
	tcp_twcal_hand = -1;

out:
	if ((tcp_tw_count -= killed) == 0)
		del_timer(&tcp_tw_timer);
	NET_ADD_STATS_BH(LINUX_MIB_TIMEWAITKILLED, killed);
	spin_unlock(&tw_death_lock);
}

/* This is not only more efficient than what we used to do, it eliminates
 * a lot of code duplication between IPv4/IPv6 SYN recv processing. -DaveM
 *
 * Actually, we could lots of memory writes here. tp of listening
 * socket contains all necessary default parameters.
 */
struct sock *tcp_create_openreq_child(struct sock *sk, struct request_sock *req, struct sk_buff *skb)
{
	struct sock *newsk = inet_csk_clone(sk, req, GFP_ATOMIC);

	if (newsk != NULL) {
		const struct inet_request_sock *ireq = inet_rsk(req);
		struct tcp_request_sock *treq = tcp_rsk(req);
		struct inet_connection_sock *newicsk = inet_csk(sk);
		struct tcp_sock *newtp;

		/* Now setup tcp_sock */
		newtp = tcp_sk(newsk);
		newtp->pred_flags = 0;
		newtp->rcv_nxt = treq->rcv_isn + 1;
		newtp->snd_nxt = newtp->snd_una = newtp->snd_sml = treq->snt_isn + 1;

		tcp_prequeue_init(newtp);

		tcp_init_wl(newtp, treq->snt_isn, treq->rcv_isn);

		newtp->srtt = 0;
		newtp->mdev = TCP_TIMEOUT_INIT;
		newicsk->icsk_rto = TCP_TIMEOUT_INIT;

		newtp->packets_out = 0;
		newtp->left_out = 0;
		newtp->retrans_out = 0;
		newtp->sacked_out = 0;
		newtp->fackets_out = 0;
		newtp->snd_ssthresh = 0x7fffffff;

		/* So many TCP implementations out there (incorrectly) count the
		 * initial SYN frame in their delayed-ACK and congestion control
		 * algorithms that we must have the following bandaid to talk
		 * efficiently to them.  -DaveM
		 */
		newtp->snd_cwnd = 2;
		newtp->snd_cwnd_cnt = 0;

		newtp->frto_counter = 0;
		newtp->frto_highmark = 0;

		newtp->ca_ops = &tcp_reno;

		tcp_set_ca_state(newtp, TCP_CA_Open);
		tcp_init_xmit_timers(newsk);
		skb_queue_head_init(&newtp->out_of_order_queue);
		newtp->rcv_wup = treq->rcv_isn + 1;
		newtp->write_seq = treq->snt_isn + 1;
		newtp->pushed_seq = newtp->write_seq;
		newtp->copied_seq = treq->rcv_isn + 1;

		newtp->rx_opt.saw_tstamp = 0;

		newtp->rx_opt.dsack = 0;
		newtp->rx_opt.eff_sacks = 0;

		newtp->probes_out = 0;
		newtp->rx_opt.num_sacks = 0;
		newtp->urg_data = 0;

		if (sock_flag(newsk, SOCK_KEEPOPEN))
			inet_csk_reset_keepalive_timer(newsk,
						       keepalive_time_when(newtp));

		newtp->rx_opt.tstamp_ok = ireq->tstamp_ok;
		if((newtp->rx_opt.sack_ok = ireq->sack_ok) != 0) {
			if (sysctl_tcp_fack)
				newtp->rx_opt.sack_ok |= 2;
		}
		newtp->window_clamp = req->window_clamp;
		newtp->rcv_ssthresh = req->rcv_wnd;
		newtp->rcv_wnd = req->rcv_wnd;
		newtp->rx_opt.wscale_ok = ireq->wscale_ok;
		if (newtp->rx_opt.wscale_ok) {
			newtp->rx_opt.snd_wscale = ireq->snd_wscale;
			newtp->rx_opt.rcv_wscale = ireq->rcv_wscale;
		} else {
			newtp->rx_opt.snd_wscale = newtp->rx_opt.rcv_wscale = 0;
			newtp->window_clamp = min(newtp->window_clamp, 65535U);
		}
		newtp->snd_wnd = ntohs(skb->h.th->window) << newtp->rx_opt.snd_wscale;
		newtp->max_window = newtp->snd_wnd;

		if (newtp->rx_opt.tstamp_ok) {
			newtp->rx_opt.ts_recent = req->ts_recent;
			newtp->rx_opt.ts_recent_stamp = xtime.tv_sec;
			newtp->tcp_header_len = sizeof(struct tcphdr) + TCPOLEN_TSTAMP_ALIGNED;
		} else {
			newtp->rx_opt.ts_recent_stamp = 0;
			newtp->tcp_header_len = sizeof(struct tcphdr);
		}
		if (skb->len >= TCP_MIN_RCVMSS+newtp->tcp_header_len)
			newicsk->icsk_ack.last_seg_size = skb->len - newtp->tcp_header_len;
		newtp->rx_opt.mss_clamp = req->mss;
		TCP_ECN_openreq_child(newtp, req);
		if (newtp->ecn_flags&TCP_ECN_OK)
			sock_set_flag(newsk, SOCK_NO_LARGESEND);

		TCP_INC_STATS_BH(TCP_MIB_PASSIVEOPENS);
	}
	return newsk;
}

/* 
 *	Process an incoming packet for SYN_RECV sockets represented
 *	as a request_sock.
 */

struct sock *tcp_check_req(struct sock *sk,struct sk_buff *skb,
			   struct request_sock *req,
			   struct request_sock **prev)
{
	struct tcphdr *th = skb->h.th;
	struct tcp_sock *tp = tcp_sk(sk);
	u32 flg = tcp_flag_word(th) & (TCP_FLAG_RST|TCP_FLAG_SYN|TCP_FLAG_ACK);
	int paws_reject = 0;
	struct tcp_options_received tmp_opt;
	struct sock *child;

	tmp_opt.saw_tstamp = 0;
	if (th->doff > (sizeof(struct tcphdr)>>2)) {
		tcp_parse_options(skb, &tmp_opt, 0);

		if (tmp_opt.saw_tstamp) {
			tmp_opt.ts_recent = req->ts_recent;
			/* We do not store true stamp, but it is not required,
			 * it can be estimated (approximately)
			 * from another data.
			 */
			tmp_opt.ts_recent_stamp = xtime.tv_sec - ((TCP_TIMEOUT_INIT/HZ)<<req->retrans);
			paws_reject = tcp_paws_check(&tmp_opt, th->rst);
		}
	}

	/* Check for pure retransmitted SYN. */
	if (TCP_SKB_CB(skb)->seq == tcp_rsk(req)->rcv_isn &&
	    flg == TCP_FLAG_SYN &&
	    !paws_reject) {
		/*
		 * RFC793 draws (Incorrectly! It was fixed in RFC1122)
		 * this case on figure 6 and figure 8, but formal
		 * protocol description says NOTHING.
		 * To be more exact, it says that we should send ACK,
		 * because this segment (at least, if it has no data)
		 * is out of window.
		 *
		 *  CONCLUSION: RFC793 (even with RFC1122) DOES NOT
		 *  describe SYN-RECV state. All the description
		 *  is wrong, we cannot believe to it and should
		 *  rely only on common sense and implementation
		 *  experience.
		 *
		 * Enforce "SYN-ACK" according to figure 8, figure 6
		 * of RFC793, fixed by RFC1122.
		 */
		req->rsk_ops->rtx_syn_ack(sk, req, NULL);
		return NULL;
	}

	/* Further reproduces section "SEGMENT ARRIVES"
	   for state SYN-RECEIVED of RFC793.
	   It is broken, however, it does not work only
	   when SYNs are crossed.

	   You would think that SYN crossing is impossible here, since
	   we should have a SYN_SENT socket (from connect()) on our end,
	   but this is not true if the crossed SYNs were sent to both
	   ends by a malicious third party.  We must defend against this,
	   and to do that we first verify the ACK (as per RFC793, page
	   36) and reset if it is invalid.  Is this a true full defense?
	   To convince ourselves, let us consider a way in which the ACK
	   test can still pass in this 'malicious crossed SYNs' case.
	   Malicious sender sends identical SYNs (and thus identical sequence
	   numbers) to both A and B:

		A: gets SYN, seq=7
		B: gets SYN, seq=7

	   By our good fortune, both A and B select the same initial
	   send sequence number of seven :-)

		A: sends SYN|ACK, seq=7, ack_seq=8
		B: sends SYN|ACK, seq=7, ack_seq=8

	   So we are now A eating this SYN|ACK, ACK test passes.  So
	   does sequence test, SYN is truncated, and thus we consider
	   it a bare ACK.

	   If icsk->icsk_accept_queue.rskq_defer_accept, we silently drop this
	   bare ACK.  Otherwise, we create an established connection.  Both
	   ends (listening sockets) accept the new incoming connection and try
	   to talk to each other. 8-)

	   Note: This case is both harmless, and rare.  Possibility is about the
	   same as us discovering intelligent life on another plant tomorrow.

	   But generally, we should (RFC lies!) to accept ACK
	   from SYNACK both here and in tcp_rcv_state_process().
	   tcp_rcv_state_process() does not, hence, we do not too.

	   Note that the case is absolutely generic:
	   we cannot optimize anything here without
	   violating protocol. All the checks must be made
	   before attempt to create socket.
	 */

	/* RFC793 page 36: "If the connection is in any non-synchronized state ...
	 *                  and the incoming segment acknowledges something not yet
	 *                  sent (the segment carries an unaccaptable ACK) ...
	 *                  a reset is sent."
	 *
	 * Invalid ACK: reset will be sent by listening socket
	 */
	if ((flg & TCP_FLAG_ACK) &&
	    (TCP_SKB_CB(skb)->ack_seq != tcp_rsk(req)->snt_isn + 1))
		return sk;

	/* Also, it would be not so bad idea to check rcv_tsecr, which
	 * is essentially ACK extension and too early or too late values
	 * should cause reset in unsynchronized states.
	 */

	/* RFC793: "first check sequence number". */

	if (paws_reject || !tcp_in_window(TCP_SKB_CB(skb)->seq, TCP_SKB_CB(skb)->end_seq,
					  tcp_rsk(req)->rcv_isn + 1, tcp_rsk(req)->rcv_isn + 1 + req->rcv_wnd)) {
		/* Out of window: send ACK and drop. */
		if (!(flg & TCP_FLAG_RST))
			req->rsk_ops->send_ack(skb, req);
		if (paws_reject)
			NET_INC_STATS_BH(LINUX_MIB_PAWSESTABREJECTED);
		return NULL;
	}

	/* In sequence, PAWS is OK. */

	if (tmp_opt.saw_tstamp && !after(TCP_SKB_CB(skb)->seq, tcp_rsk(req)->rcv_isn + 1))
			req->ts_recent = tmp_opt.rcv_tsval;

		if (TCP_SKB_CB(skb)->seq == tcp_rsk(req)->rcv_isn) {
			/* Truncate SYN, it is out of window starting
			   at tcp_rsk(req)->rcv_isn + 1. */
			flg &= ~TCP_FLAG_SYN;
		}

		/* RFC793: "second check the RST bit" and
		 *	   "fourth, check the SYN bit"
		 */
		if (flg & (TCP_FLAG_RST|TCP_FLAG_SYN))
			goto embryonic_reset;

		/* ACK sequence verified above, just make sure ACK is
		 * set.  If ACK not set, just silently drop the packet.
		 */
		if (!(flg & TCP_FLAG_ACK))
			return NULL;

		/* If TCP_DEFER_ACCEPT is set, drop bare ACK. */
		if (inet_csk(sk)->icsk_accept_queue.rskq_defer_accept &&
		    TCP_SKB_CB(skb)->end_seq == tcp_rsk(req)->rcv_isn + 1) {
			inet_rsk(req)->acked = 1;
			return NULL;
		}

		/* OK, ACK is valid, create big socket and
		 * feed this segment to it. It will repeat all
		 * the tests. THIS SEGMENT MUST MOVE SOCKET TO
		 * ESTABLISHED STATE. If it will be dropped after
		 * socket is created, wait for troubles.
		 */
		child = tp->af_specific->syn_recv_sock(sk, skb, req, NULL);
		if (child == NULL)
			goto listen_overflow;

		inet_csk_reqsk_queue_unlink(sk, req, prev);
		inet_csk_reqsk_queue_removed(sk, req);

		inet_csk_reqsk_queue_add(sk, req, child);
		return child;

	listen_overflow:
		if (!sysctl_tcp_abort_on_overflow) {
			inet_rsk(req)->acked = 1;
			return NULL;
		}

	embryonic_reset:
		NET_INC_STATS_BH(LINUX_MIB_EMBRYONICRSTS);
		if (!(flg & TCP_FLAG_RST))
			req->rsk_ops->send_reset(skb);

		inet_csk_reqsk_queue_drop(sk, req, prev);
		return NULL;
}

/*
 * Queue segment on the new socket if the new socket is active,
 * otherwise we just shortcircuit this and continue with
 * the new socket.
 */

int tcp_child_process(struct sock *parent, struct sock *child,
		      struct sk_buff *skb)
{
	int ret = 0;
	int state = child->sk_state;

	if (!sock_owned_by_user(child)) {
		ret = tcp_rcv_state_process(child, skb, skb->h.th, skb->len);

		/* Wakeup parent, send SIGIO */
		if (state == TCP_SYN_RECV && child->sk_state != state)
			parent->sk_data_ready(parent, 0);
	} else {
		/* Alas, it is possible again, because we do lookup
		 * in main socket hash table and lock on listening
		 * socket does not protect us more.
		 */
		sk_add_backlog(child, skb);
	}

	bh_unlock_sock(child);
	sock_put(child);
	return ret;
}

EXPORT_SYMBOL(tcp_check_req);
EXPORT_SYMBOL(tcp_child_process);
EXPORT_SYMBOL(tcp_create_openreq_child);
EXPORT_SYMBOL(tcp_timewait_state_process);
EXPORT_SYMBOL(tcp_tw_deschedule);
