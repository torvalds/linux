/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _TCP_ECN_H
#define _TCP_ECN_H

#include <linux/tcp.h>
#include <linux/skbuff.h>
#include <linux/bitfield.h>

#include <net/inet_connection_sock.h>
#include <net/sock.h>
#include <net/tcp.h>
#include <net/inet_ecn.h>

/* The highest ECN variant (Accurate ECN, ECN, or no ECN) that is
 * attemped to be negotiated and requested for incoming connection
 * and outgoing connection, respectively.
 */
enum tcp_ecn_mode {
	TCP_ECN_IN_NOECN_OUT_NOECN = 0,
	TCP_ECN_IN_ECN_OUT_ECN = 1,
	TCP_ECN_IN_ECN_OUT_NOECN = 2,
	TCP_ECN_IN_ACCECN_OUT_ACCECN = 3,
	TCP_ECN_IN_ACCECN_OUT_ECN = 4,
	TCP_ECN_IN_ACCECN_OUT_NOECN = 5,
};

/* AccECN option sending when AccECN has been successfully negotiated */
enum tcp_accecn_option {
	TCP_ACCECN_OPTION_DISABLED = 0,
	TCP_ACCECN_OPTION_MINIMUM = 1,
	TCP_ACCECN_OPTION_FULL = 2,
};

static inline void tcp_ecn_queue_cwr(struct tcp_sock *tp)
{
	/* Do not set CWR if in AccECN mode! */
	if (tcp_ecn_mode_rfc3168(tp))
		tp->ecn_flags |= TCP_ECN_QUEUE_CWR;
}

static inline void tcp_ecn_accept_cwr(struct sock *sk,
				      const struct sk_buff *skb)
{
	struct tcp_sock *tp = tcp_sk(sk);

	if (tcp_ecn_mode_rfc3168(tp) && tcp_hdr(skb)->cwr) {
		tp->ecn_flags &= ~TCP_ECN_DEMAND_CWR;

		/* If the sender is telling us it has entered CWR, then its
		 * cwnd may be very low (even just 1 packet), so we should ACK
		 * immediately.
		 */
		if (TCP_SKB_CB(skb)->seq != TCP_SKB_CB(skb)->end_seq)
			inet_csk(sk)->icsk_ack.pending |= ICSK_ACK_NOW;
	}
}

static inline void tcp_ecn_withdraw_cwr(struct tcp_sock *tp)
{
	tp->ecn_flags &= ~TCP_ECN_QUEUE_CWR;
}

/* tp->accecn_fail_mode */
#define TCP_ACCECN_ACE_FAIL_SEND	BIT(0)
#define TCP_ACCECN_ACE_FAIL_RECV	BIT(1)
#define TCP_ACCECN_OPT_FAIL_SEND	BIT(2)
#define TCP_ACCECN_OPT_FAIL_RECV	BIT(3)

static inline bool tcp_accecn_ace_fail_send(const struct tcp_sock *tp)
{
	return tp->accecn_fail_mode & TCP_ACCECN_ACE_FAIL_SEND;
}

static inline bool tcp_accecn_ace_fail_recv(const struct tcp_sock *tp)
{
	return tp->accecn_fail_mode & TCP_ACCECN_ACE_FAIL_RECV;
}

static inline bool tcp_accecn_opt_fail_send(const struct tcp_sock *tp)
{
	return tp->accecn_fail_mode & TCP_ACCECN_OPT_FAIL_SEND;
}

static inline bool tcp_accecn_opt_fail_recv(const struct tcp_sock *tp)
{
	return tp->accecn_fail_mode & TCP_ACCECN_OPT_FAIL_RECV;
}

static inline void tcp_accecn_fail_mode_set(struct tcp_sock *tp, u8 mode)
{
	tp->accecn_fail_mode |= mode;
}

#define TCP_ACCECN_OPT_NOT_SEEN		0x0
#define TCP_ACCECN_OPT_EMPTY_SEEN	0x1
#define TCP_ACCECN_OPT_COUNTER_SEEN	0x2
#define TCP_ACCECN_OPT_FAIL_SEEN	0x3

static inline u8 tcp_accecn_ace(const struct tcphdr *th)
{
	return (th->ae << 2) | (th->cwr << 1) | th->ece;
}

/* Infer the ECT value our SYN arrived with from the echoed ACE field */
static inline int tcp_accecn_extract_syn_ect(u8 ace)
{
	/* Below is an excerpt from the 1st block of Table 2 of AccECN spec */
	static const int ace_to_ecn[8] = {
		INET_ECN_ECT_0,		/* 0b000 (Undefined) */
		INET_ECN_ECT_1,		/* 0b001 (Undefined) */
		INET_ECN_NOT_ECT,	/* 0b010 (Not-ECT is received) */
		INET_ECN_ECT_1,		/* 0b011 (ECT-1 is received) */
		INET_ECN_ECT_0,		/* 0b100 (ECT-0 is received) */
		INET_ECN_ECT_1,		/* 0b101 (Reserved) */
		INET_ECN_CE,		/* 0b110 (CE is received) */
		INET_ECN_ECT_1		/* 0b111 (Undefined) */
	};

	return ace_to_ecn[ace & 0x7];
}

/* Check ECN field transition to detect invalid transitions */
static inline bool tcp_ect_transition_valid(u8 snt, u8 rcv)
{
	if (rcv == snt)
		return true;

	/* Non-ECT altered to something or something became non-ECT */
	if (snt == INET_ECN_NOT_ECT || rcv == INET_ECN_NOT_ECT)
		return false;
	/* CE -> ECT(0/1)? */
	if (snt == INET_ECN_CE)
		return false;
	return true;
}

static inline bool tcp_accecn_validate_syn_feedback(struct sock *sk, u8 ace,
						    u8 sent_ect)
{
	u8 ect = tcp_accecn_extract_syn_ect(ace);
	struct tcp_sock *tp = tcp_sk(sk);

	if (!READ_ONCE(sock_net(sk)->ipv4.sysctl_tcp_ecn_fallback))
		return true;

	if (!tcp_ect_transition_valid(sent_ect, ect)) {
		tcp_accecn_fail_mode_set(tp, TCP_ACCECN_ACE_FAIL_RECV);
		return false;
	}

	return true;
}

static inline void tcp_accecn_saw_opt_fail_recv(struct tcp_sock *tp,
						u8 saw_opt)
{
	tp->saw_accecn_opt = saw_opt;
	if (tp->saw_accecn_opt == TCP_ACCECN_OPT_FAIL_SEEN)
		tcp_accecn_fail_mode_set(tp, TCP_ACCECN_OPT_FAIL_RECV);
}

/* Validate the 3rd ACK based on the ACE field, see Table 4 of AccECN spec */
static inline void tcp_accecn_third_ack(struct sock *sk,
					const struct sk_buff *skb, u8 sent_ect)
{
	u8 ace = tcp_accecn_ace(tcp_hdr(skb));
	struct tcp_sock *tp = tcp_sk(sk);

	switch (ace) {
	case 0x0:
		/* Invalid value */
		tcp_accecn_fail_mode_set(tp, TCP_ACCECN_ACE_FAIL_RECV);
		break;
	case 0x7:
	case 0x5:
	case 0x1:
		/* Unused but legal values */
		break;
	default:
		/* Validation only applies to first non-data packet */
		if (TCP_SKB_CB(skb)->seq == TCP_SKB_CB(skb)->end_seq &&
		    !TCP_SKB_CB(skb)->sacked &&
		    tcp_accecn_validate_syn_feedback(sk, ace, sent_ect)) {
			if ((tcp_accecn_extract_syn_ect(ace) == INET_ECN_CE) &&
			    !tp->delivered_ce)
				tp->delivered_ce++;
		}
		break;
	}
}

/* Demand the minimum # to send AccECN optnio */
static inline void tcp_accecn_opt_demand_min(struct sock *sk,
					     u8 opt_demand_min)
{
	struct tcp_sock *tp = tcp_sk(sk);
	u8 opt_demand;

	opt_demand = max_t(u8, opt_demand_min, tp->accecn_opt_demand);
	tp->accecn_opt_demand = opt_demand;
}

/* Maps IP ECN field ECT/CE code point to AccECN option field number, given
 * we are sending fields with Accurate ECN Order 1: ECT(1), CE, ECT(0).
 */
static inline u8 tcp_ecnfield_to_accecn_optfield(u8 ecnfield)
{
	switch (ecnfield & INET_ECN_MASK) {
	case INET_ECN_NOT_ECT:
		return 0;	/* AccECN does not send counts of NOT_ECT */
	case INET_ECN_ECT_1:
		return 1;
	case INET_ECN_CE:
		return 2;
	case INET_ECN_ECT_0:
		return 3;
	}
	return 0;
}

/* Maps IP ECN field ECT/CE code point to AccECN option field value offset.
 * Some fields do not start from zero, to detect zeroing by middleboxes.
 */
static inline u32 tcp_accecn_field_init_offset(u8 ecnfield)
{
	switch (ecnfield & INET_ECN_MASK) {
	case INET_ECN_NOT_ECT:
		return 0;	/* AccECN does not send counts of NOT_ECT */
	case INET_ECN_ECT_1:
		return TCP_ACCECN_E1B_INIT_OFFSET;
	case INET_ECN_CE:
		return TCP_ACCECN_CEB_INIT_OFFSET;
	case INET_ECN_ECT_0:
		return TCP_ACCECN_E0B_INIT_OFFSET;
	}
	return 0;
}

/* Maps AccECN option field #nr to IP ECN field ECT/CE bits */
static inline unsigned int tcp_accecn_optfield_to_ecnfield(unsigned int option,
							   bool order)
{
	/* Based on Table 5 of the AccECN spec to map (option, order) to
	 * the corresponding ECN conuters (ECT-1, ECT-0, or CE).
	 */
	static const u8 optfield_lookup[2][3] = {
		/* order = 0: 1st field ECT-0, 2nd field CE, 3rd field ECT-1 */
		{ INET_ECN_ECT_0, INET_ECN_CE, INET_ECN_ECT_1 },
		/* order = 1: 1st field ECT-1, 2nd field CE, 3rd field ECT-0 */
		{ INET_ECN_ECT_1, INET_ECN_CE, INET_ECN_ECT_0 }
	};

	return optfield_lookup[order][option % 3];
}

/* Handles AccECN option ECT and CE 24-bit byte counters update into
 * the u32 value in tcp_sock. As we're processing TCP options, it is
 * safe to access from - 1.
 */
static inline s32 tcp_update_ecn_bytes(u32 *cnt, const char *from,
				       u32 init_offset)
{
	u32 truncated = (get_unaligned_be32(from - 1) - init_offset) &
			0xFFFFFFU;
	u32 delta = (truncated - *cnt) & 0xFFFFFFU;

	/* If delta has the highest bit set (24th bit) indicating
	 * negative, sign extend to correct an estimation using
	 * sign_extend32(delta, 24 - 1)
	 */
	delta = sign_extend32(delta, 23);
	*cnt += delta;
	return (s32)delta;
}

/* Updates Accurate ECN received counters from the received IP ECN field */
static inline void tcp_ecn_received_counters(struct sock *sk,
					     const struct sk_buff *skb, u32 len)
{
	u8 ecnfield = TCP_SKB_CB(skb)->ip_dsfield & INET_ECN_MASK;
	u8 is_ce = INET_ECN_is_ce(ecnfield);
	struct tcp_sock *tp = tcp_sk(sk);
	bool ecn_edge;

	if (!INET_ECN_is_not_ect(ecnfield)) {
		u32 pcount = is_ce * max_t(u16, 1, skb_shinfo(skb)->gso_segs);

		/* As for accurate ECN, the TCP_ECN_SEEN flag is set by
		 * tcp_ecn_received_counters() when the ECN codepoint of
		 * received TCP data or ACK contains ECT(0), ECT(1), or CE.
		 */
		if (!tcp_ecn_mode_rfc3168(tp))
			tp->ecn_flags |= TCP_ECN_SEEN;

		/* ACE counter tracks *all* segments including pure ACKs */
		tp->received_ce += pcount;
		tp->received_ce_pending = min(tp->received_ce_pending + pcount,
					      0xfU);

		if (len > 0) {
			u8 minlen = tcp_ecnfield_to_accecn_optfield(ecnfield);
			u32 oldbytes = tp->received_ecn_bytes[ecnfield - 1];
			u32 bytes_mask = GENMASK_U32(31, 22);

			tp->received_ecn_bytes[ecnfield - 1] += len;
			tp->accecn_minlen = max_t(u8, tp->accecn_minlen,
						  minlen);

			/* Send AccECN option at least once per 2^22-byte
			 * increase in any ECN byte counter.
			 */
			if ((tp->received_ecn_bytes[ecnfield - 1] ^ oldbytes) &
			    bytes_mask) {
				tcp_accecn_opt_demand_min(sk, 1);
			}
		}
	}

	ecn_edge = tp->prev_ecnfield != ecnfield;
	if (ecn_edge || is_ce) {
		tp->prev_ecnfield = ecnfield;
		/* Demand Accurate ECN change-triggered ACKs. Two ACK are
		 * demanded to indicate unambiguously the ecnfield value
		 * in the latter ACK.
		 */
		if (tcp_ecn_mode_accecn(tp)) {
			if (ecn_edge)
				inet_csk(sk)->icsk_ack.pending |= ICSK_ACK_NOW;
			tp->accecn_opt_demand = 2;
		}
	}
}

/* AccECN specification, 2.2: [...] A Data Receiver maintains four counters
 * initialized at the start of	the half-connection. [...] These byte counters
 * reflect only the TCP payload length, excluding TCP header and TCP options.
 */
static inline void tcp_ecn_received_counters_payload(struct sock *sk,
						     const struct sk_buff *skb)
{
	const struct tcphdr *th = (const struct tcphdr *)skb->data;

	tcp_ecn_received_counters(sk, skb, skb->len - th->doff * 4);
}

/* AccECN specification, 5.1: [...] a server can determine that it
 * negotiated AccECN as [...] if the ACK contains an ACE field with
 * the value 0b010 to 0b111 (decimal 2 to 7).
 */
static inline bool cookie_accecn_ok(const struct tcphdr *th)
{
	return tcp_accecn_ace(th) > 0x1;
}

/* Used to form the ACE flags for SYN/ACK */
static inline u16 tcp_accecn_reflector_flags(u8 ect)
{
	/* TCP ACE flags of SYN/ACK are set based on IP-ECN received from SYN.
	 * Below is an excerpt from the 1st block of Table 2 of AccECN spec,
	 * in which TCP ACE flags are encoded as: (AE << 2) | (CWR << 1) | ECE
	 */
	static const u8 ecn_to_ace_flags[4] = {
		0b010,	/* Not-ECT is received */
		0b011,	/* ECT(1) is received */
		0b100,	/* ECT(0) is received */
		0b110	/* CE is received */
	};

	return FIELD_PREP(TCPHDR_ACE, ecn_to_ace_flags[ect & 0x3]);
}

/* AccECN specification, 3.1.2: If a TCP server that implements AccECN
 * receives a SYN with the three TCP header flags (AE, CWR and ECE) set
 * to any combination other than 000, 011 or 111, it MUST negotiate the
 * use of AccECN as if they had been set to 111.
 */
static inline bool tcp_accecn_syn_requested(const struct tcphdr *th)
{
	u8 ace = tcp_accecn_ace(th);

	return ace && ace != 0x3;
}

static inline void __tcp_accecn_init_bytes_counters(int *counter_array)
{
	BUILD_BUG_ON(INET_ECN_ECT_1 != 0x1);
	BUILD_BUG_ON(INET_ECN_ECT_0 != 0x2);
	BUILD_BUG_ON(INET_ECN_CE != 0x3);

	counter_array[INET_ECN_ECT_1 - 1] = 0;
	counter_array[INET_ECN_ECT_0 - 1] = 0;
	counter_array[INET_ECN_CE - 1] = 0;
}

static inline void tcp_accecn_init_counters(struct tcp_sock *tp)
{
	tp->received_ce = 0;
	tp->received_ce_pending = 0;
	__tcp_accecn_init_bytes_counters(tp->received_ecn_bytes);
	__tcp_accecn_init_bytes_counters(tp->delivered_ecn_bytes);
	tp->accecn_minlen = 0;
	tp->accecn_opt_demand = 0;
	tp->est_ecnfield = 0;
}

/* Used for make_synack to form the ACE flags */
static inline void tcp_accecn_echo_syn_ect(struct tcphdr *th, u8 ect)
{
	/* TCP ACE flags of SYN/ACK are set based on IP-ECN codepoint received
	 * from SYN. Below is an excerpt from Table 2 of the AccECN spec:
	 * +====================+====================================+
	 * |  IP-ECN codepoint  |  Respective ACE falgs on SYN/ACK   |
	 * |   received on SYN  |       AE       CWR       ECE       |
	 * +====================+====================================+
	 * |      Not-ECT       |       0         1         0        |
	 * |      ECT(1)        |       0         1         1        |
	 * |      ECT(0)        |       1         0         0        |
	 * |        CE          |       1         1         0        |
	 * +====================+====================================+
	 */
	th->ae = !!(ect & INET_ECN_ECT_0);
	th->cwr = ect != INET_ECN_ECT_0;
	th->ece = ect == INET_ECN_ECT_1;
}

static inline void tcp_accecn_set_ace(struct tcp_sock *tp, struct sk_buff *skb,
				      struct tcphdr *th)
{
	u32 wire_ace;

	/* The final packet of the 3WHS or anything like it must reflect
	 * the SYN/ACK ECT instead of putting CEP into ACE field, such
	 * case show up in tcp_flags.
	 */
	if (likely(!(TCP_SKB_CB(skb)->tcp_flags & TCPHDR_ACE))) {
		wire_ace = tp->received_ce + TCP_ACCECN_CEP_INIT_OFFSET;
		th->ece = !!(wire_ace & 0x1);
		th->cwr = !!(wire_ace & 0x2);
		th->ae = !!(wire_ace & 0x4);
		tp->received_ce_pending = 0;
	}
}

static inline u8 tcp_accecn_option_init(const struct sk_buff *skb,
					u8 opt_offset)
{
	u8 *ptr = skb_transport_header(skb) + opt_offset;
	unsigned int optlen = ptr[1] - 2;

	if (WARN_ON_ONCE(ptr[0] != TCPOPT_ACCECN0 && ptr[0] != TCPOPT_ACCECN1))
		return TCP_ACCECN_OPT_FAIL_SEEN;
	ptr += 2;

	/* Detect option zeroing: an AccECN connection "MAY check that the
	 * initial value of the EE0B field or the EE1B field is non-zero"
	 */
	if (optlen < TCPOLEN_ACCECN_PERFIELD)
		return TCP_ACCECN_OPT_EMPTY_SEEN;
	if (get_unaligned_be24(ptr) == 0)
		return TCP_ACCECN_OPT_FAIL_SEEN;
	if (optlen < TCPOLEN_ACCECN_PERFIELD * 3)
		return TCP_ACCECN_OPT_COUNTER_SEEN;
	ptr += TCPOLEN_ACCECN_PERFIELD * 2;
	if (get_unaligned_be24(ptr) == 0)
		return TCP_ACCECN_OPT_FAIL_SEEN;

	return TCP_ACCECN_OPT_COUNTER_SEEN;
}

/* See Table 2 of the AccECN draft */
static inline void tcp_ecn_rcv_synack(struct sock *sk, const struct sk_buff *skb,
				      const struct tcphdr *th, u8 ip_dsfield)
{
	struct tcp_sock *tp = tcp_sk(sk);
	u8 ace = tcp_accecn_ace(th);

	switch (ace) {
	case 0x0:
	case 0x7:
		/* +========+========+============+=============+
		 * | A      | B      |  SYN/ACK   |  Feedback   |
		 * |        |        |    B->A    |  Mode of A  |
		 * |        |        | AE CWR ECE |             |
		 * +========+========+============+=============+
		 * | AccECN | No ECN | 0   0   0  |   Not ECN   |
		 * | AccECN | Broken | 1   1   1  |   Not ECN   |
		 * +========+========+============+=============+
		 */
		tcp_ecn_mode_set(tp, TCP_ECN_DISABLED);
		break;
	case 0x1:
	case 0x5:
		/* +========+========+============+=============+
		 * | A      | B      |  SYN/ACK   |  Feedback   |
		 * |        |        |    B->A    |  Mode of A  |
		 * |        |        | AE CWR ECE |             |
		 * +========+========+============+=============+
		 * | AccECN | Nonce  | 1   0   1  | (Reserved)  |
		 * | AccECN | ECN    | 0   0   1  | Classic ECN |
		 * | Nonce  | AccECN | 0   0   1  | Classic ECN |
		 * | ECN    | AccECN | 0   0   1  | Classic ECN |
		 * +========+========+============+=============+
		 */
		if (tcp_ecn_mode_pending(tp))
			/* Downgrade from AccECN, or requested initially */
			tcp_ecn_mode_set(tp, TCP_ECN_MODE_RFC3168);
		break;
	default:
		tcp_ecn_mode_set(tp, TCP_ECN_MODE_ACCECN);
		tp->syn_ect_rcv = ip_dsfield & INET_ECN_MASK;
		if (tp->rx_opt.accecn &&
		    tp->saw_accecn_opt < TCP_ACCECN_OPT_COUNTER_SEEN) {
			u8 saw_opt = tcp_accecn_option_init(skb, tp->rx_opt.accecn);

			tcp_accecn_saw_opt_fail_recv(tp, saw_opt);
			tp->accecn_opt_demand = 2;
		}
		if (INET_ECN_is_ce(ip_dsfield) &&
		    tcp_accecn_validate_syn_feedback(sk, ace,
						     tp->syn_ect_snt)) {
			tp->received_ce++;
			tp->received_ce_pending++;
		}
		break;
	}
}

static inline void tcp_ecn_rcv_syn(struct tcp_sock *tp, const struct tcphdr *th,
				   const struct sk_buff *skb)
{
	if (tcp_ecn_mode_pending(tp)) {
		if (!tcp_accecn_syn_requested(th)) {
			/* Downgrade to classic ECN feedback */
			tcp_ecn_mode_set(tp, TCP_ECN_MODE_RFC3168);
		} else {
			tp->syn_ect_rcv = TCP_SKB_CB(skb)->ip_dsfield &
					  INET_ECN_MASK;
			tp->prev_ecnfield = tp->syn_ect_rcv;
			tcp_ecn_mode_set(tp, TCP_ECN_MODE_ACCECN);
		}
	}
	if (tcp_ecn_mode_rfc3168(tp) && (!th->ece || !th->cwr))
		tcp_ecn_mode_set(tp, TCP_ECN_DISABLED);
}

static inline bool tcp_ecn_rcv_ecn_echo(const struct tcp_sock *tp,
					const struct tcphdr *th)
{
	if (th->ece && !th->syn && tcp_ecn_mode_rfc3168(tp))
		return true;
	return false;
}

/* Packet ECN state for a SYN-ACK */
static inline void tcp_ecn_send_synack(struct sock *sk, struct sk_buff *skb)
{
	struct tcp_sock *tp = tcp_sk(sk);

	TCP_SKB_CB(skb)->tcp_flags &= ~TCPHDR_CWR;
	if (tcp_ecn_disabled(tp))
		TCP_SKB_CB(skb)->tcp_flags &= ~TCPHDR_ECE;
	else if (tcp_ca_needs_ecn(sk) ||
		 tcp_bpf_ca_needs_ecn(sk))
		INET_ECN_xmit(sk);

	if (tp->ecn_flags & TCP_ECN_MODE_ACCECN) {
		TCP_SKB_CB(skb)->tcp_flags &= ~TCPHDR_ACE;
		TCP_SKB_CB(skb)->tcp_flags |=
			tcp_accecn_reflector_flags(tp->syn_ect_rcv);
		tp->syn_ect_snt = inet_sk(sk)->tos & INET_ECN_MASK;
	}
}

/* Packet ECN state for a SYN.  */
static inline void tcp_ecn_send_syn(struct sock *sk, struct sk_buff *skb)
{
	struct tcp_sock *tp = tcp_sk(sk);
	bool bpf_needs_ecn = tcp_bpf_ca_needs_ecn(sk);
	bool use_ecn, use_accecn;
	u8 tcp_ecn = READ_ONCE(sock_net(sk)->ipv4.sysctl_tcp_ecn);

	use_accecn = tcp_ecn == TCP_ECN_IN_ACCECN_OUT_ACCECN;
	use_ecn = tcp_ecn == TCP_ECN_IN_ECN_OUT_ECN ||
		  tcp_ecn == TCP_ECN_IN_ACCECN_OUT_ECN ||
		  tcp_ca_needs_ecn(sk) || bpf_needs_ecn || use_accecn;

	if (!use_ecn) {
		const struct dst_entry *dst = __sk_dst_get(sk);

		if (dst && dst_feature(dst, RTAX_FEATURE_ECN))
			use_ecn = true;
	}

	tp->ecn_flags = 0;

	if (use_ecn) {
		if (tcp_ca_needs_ecn(sk) || bpf_needs_ecn)
			INET_ECN_xmit(sk);

		TCP_SKB_CB(skb)->tcp_flags |= TCPHDR_ECE | TCPHDR_CWR;
		if (use_accecn) {
			TCP_SKB_CB(skb)->tcp_flags |= TCPHDR_AE;
			tcp_ecn_mode_set(tp, TCP_ECN_MODE_PENDING);
			tp->syn_ect_snt = inet_sk(sk)->tos & INET_ECN_MASK;
		} else {
			tcp_ecn_mode_set(tp, TCP_ECN_MODE_RFC3168);
		}
	}
}

static inline void tcp_ecn_clear_syn(struct sock *sk, struct sk_buff *skb)
{
	if (READ_ONCE(sock_net(sk)->ipv4.sysctl_tcp_ecn_fallback)) {
		/* tp->ecn_flags are cleared at a later point in time when
		 * SYN ACK is ultimatively being received.
		 */
		TCP_SKB_CB(skb)->tcp_flags &= ~TCPHDR_ACE;
	}
}

static inline void
tcp_ecn_make_synack(const struct request_sock *req, struct tcphdr *th)
{
	if (tcp_rsk(req)->accecn_ok)
		tcp_accecn_echo_syn_ect(th, tcp_rsk(req)->syn_ect_rcv);
	else if (inet_rsk(req)->ecn_ok)
		th->ece = 1;
}

static inline bool tcp_accecn_option_beacon_check(const struct sock *sk)
{
	u32 ecn_beacon = READ_ONCE(sock_net(sk)->ipv4.sysctl_tcp_ecn_option_beacon);
	const struct tcp_sock *tp = tcp_sk(sk);

	if (!ecn_beacon)
		return false;

	return tcp_stamp_us_delta(tp->tcp_mstamp, tp->accecn_opt_tstamp) * ecn_beacon >=
	       (tp->srtt_us >> 3);
}

#endif /* _LINUX_TCP_ECN_H */
