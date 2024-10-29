// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */
#include <vmlinux.h>
#include "bpf_tracing_net.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

char _license[] SEC("license") = "GPL";

static int hlist_unhashed_lockless(const struct hlist_node *h)
{
        return !(h->pprev);
}

static int timer_pending(const struct timer_list * timer)
{
	return !hlist_unhashed_lockless(&timer->entry);
}

extern unsigned CONFIG_HZ __kconfig;

#define USER_HZ		100
#define NSEC_PER_SEC	1000000000ULL
static clock_t jiffies_to_clock_t(unsigned long x)
{
	/* The implementation here tailored to a particular
	 * setting of USER_HZ.
	 */
	u64 tick_nsec = (NSEC_PER_SEC + CONFIG_HZ/2) / CONFIG_HZ;
	u64 user_hz_nsec = NSEC_PER_SEC / USER_HZ;

	if ((tick_nsec % user_hz_nsec) == 0) {
		if (CONFIG_HZ < USER_HZ)
			return x * (USER_HZ / CONFIG_HZ);
		else
			return x / (CONFIG_HZ / USER_HZ);
	}
	return x * tick_nsec/user_hz_nsec;
}

static clock_t jiffies_delta_to_clock_t(long delta)
{
	if (delta <= 0)
		return 0;

	return jiffies_to_clock_t(delta);
}

static long sock_i_ino(const struct sock *sk)
{
	const struct socket *sk_socket = sk->sk_socket;
	const struct inode *inode;
	unsigned long ino;

	if (!sk_socket)
		return 0;

	inode = &container_of(sk_socket, struct socket_alloc, socket)->vfs_inode;
	bpf_probe_read_kernel(&ino, sizeof(ino), &inode->i_ino);
	return ino;
}

static bool
inet_csk_in_pingpong_mode(const struct inet_connection_sock *icsk)
{
	return icsk->icsk_ack.pingpong >= TCP_PINGPONG_THRESH;
}

static bool tcp_in_initial_slowstart(const struct tcp_sock *tcp)
{
	return tcp->snd_ssthresh >= TCP_INFINITE_SSTHRESH;
}

static int dump_tcp_sock(struct seq_file *seq, struct tcp_sock *tp,
			 uid_t uid, __u32 seq_num)
{
	const struct inet_connection_sock *icsk;
	const struct fastopen_queue *fastopenq;
	const struct inet_sock *inet;
	unsigned long timer_expires;
	const struct sock *sp;
	__u16 destp, srcp;
	__be32 dest, src;
	int timer_active;
	int rx_queue;
	int state;

	icsk = &tp->inet_conn;
	inet = &icsk->icsk_inet;
	sp = &inet->sk;
	fastopenq = &icsk->icsk_accept_queue.fastopenq;

	dest = inet->inet_daddr;
	src = inet->inet_rcv_saddr;
	destp = bpf_ntohs(inet->inet_dport);
	srcp = bpf_ntohs(inet->inet_sport);

	if (icsk->icsk_pending == ICSK_TIME_RETRANS ||
	    icsk->icsk_pending == ICSK_TIME_REO_TIMEOUT ||
	    icsk->icsk_pending == ICSK_TIME_LOSS_PROBE) {
		timer_active = 1;
		timer_expires = icsk->icsk_timeout;
	} else if (icsk->icsk_pending == ICSK_TIME_PROBE0) {
		timer_active = 4;
		timer_expires = icsk->icsk_timeout;
	} else if (timer_pending(&sp->sk_timer)) {
		timer_active = 2;
		timer_expires = sp->sk_timer.expires;
	} else {
		timer_active = 0;
		timer_expires = bpf_jiffies64();
	}

	state = sp->sk_state;
	if (state == TCP_LISTEN) {
		rx_queue = sp->sk_ack_backlog;
	} else {
		rx_queue = tp->rcv_nxt - tp->copied_seq;
		if (rx_queue < 0)
			rx_queue = 0;
	}

	BPF_SEQ_PRINTF(seq, "%4d: %08X:%04X %08X:%04X ",
		       seq_num, src, srcp, dest, destp);
	BPF_SEQ_PRINTF(seq, "%02X %08X:%08X %02X:%08lX %08X %5u %8d %lu %d ",
		       state,
		       tp->write_seq - tp->snd_una, rx_queue,
		       timer_active,
		       jiffies_delta_to_clock_t(timer_expires - bpf_jiffies64()),
		       icsk->icsk_retransmits, uid,
		       icsk->icsk_probes_out,
		       sock_i_ino(sp),
		       sp->sk_refcnt.refs.counter);
	BPF_SEQ_PRINTF(seq, "%pK %lu %lu %u %u %d\n",
		       tp,
		       jiffies_to_clock_t(icsk->icsk_rto),
		       jiffies_to_clock_t(icsk->icsk_ack.ato),
		       (icsk->icsk_ack.quick << 1) | inet_csk_in_pingpong_mode(icsk),
		       tp->snd_cwnd,
		       state == TCP_LISTEN ? fastopenq->max_qlen
				: (tcp_in_initial_slowstart(tp) ? -1 : tp->snd_ssthresh)
		      );

	return 0;
}

static int dump_tw_sock(struct seq_file *seq, struct tcp_timewait_sock *ttw,
			uid_t uid, __u32 seq_num)
{
	struct inet_timewait_sock *tw = &ttw->tw_sk;
	__u16 destp, srcp;
	__be32 dest, src;
	long delta;

	delta = tw->tw_timer.expires - bpf_jiffies64();
	dest = tw->tw_daddr;
	src  = tw->tw_rcv_saddr;
	destp = bpf_ntohs(tw->tw_dport);
	srcp  = bpf_ntohs(tw->tw_sport);

	BPF_SEQ_PRINTF(seq, "%4d: %08X:%04X %08X:%04X ",
		       seq_num, src, srcp, dest, destp);

	BPF_SEQ_PRINTF(seq, "%02X %08X:%08X %02X:%08lX %08X %5d %8d %d %d %pK\n",
		       tw->tw_substate, 0, 0,
		       3, jiffies_delta_to_clock_t(delta), 0, 0, 0, 0,
		       tw->tw_refcnt.refs.counter, tw);

	return 0;
}

static int dump_req_sock(struct seq_file *seq, struct tcp_request_sock *treq,
			 uid_t uid, __u32 seq_num)
{
	struct inet_request_sock *irsk = &treq->req;
	struct request_sock *req = &irsk->req;
	long ttd;

	ttd = req->rsk_timer.expires - bpf_jiffies64();

	if (ttd < 0)
		ttd = 0;

	BPF_SEQ_PRINTF(seq, "%4d: %08X:%04X %08X:%04X ",
		       seq_num, irsk->ir_loc_addr,
		       irsk->ir_num, irsk->ir_rmt_addr,
		       bpf_ntohs(irsk->ir_rmt_port));
	BPF_SEQ_PRINTF(seq, "%02X %08X:%08X %02X:%08lX %08X %5d %8d %d %d %pK\n",
		       TCP_SYN_RECV, 0, 0, 1, jiffies_to_clock_t(ttd),
		       req->num_timeout, uid, 0, 0, 0, req);

	return 0;
}

SEC("iter/tcp")
int dump_tcp4(struct bpf_iter__tcp *ctx)
{
	struct sock_common *sk_common = ctx->sk_common;
	struct seq_file *seq = ctx->meta->seq;
	struct tcp_timewait_sock *tw;
	struct tcp_request_sock *req;
	struct tcp_sock *tp;
	uid_t uid = ctx->uid;
	__u32 seq_num;

	if (sk_common == (void *)0)
		return 0;

	seq_num = ctx->meta->seq_num;
	if (seq_num == 0)
		BPF_SEQ_PRINTF(seq, "  sl  "
				    "local_address "
				    "rem_address   "
				    "st tx_queue rx_queue tr tm->when retrnsmt"
				    "   uid  timeout inode\n");

	if (sk_common->skc_family != AF_INET)
		return 0;

	tp = bpf_skc_to_tcp_sock(sk_common);
	if (tp)
		return dump_tcp_sock(seq, tp, uid, seq_num);

	tw = bpf_skc_to_tcp_timewait_sock(sk_common);
	if (tw)
		return dump_tw_sock(seq, tw, uid, seq_num);

	req = bpf_skc_to_tcp_request_sock(sk_common);
	if (req)
		return dump_req_sock(seq, req, uid, seq_num);

	return 0;
}
