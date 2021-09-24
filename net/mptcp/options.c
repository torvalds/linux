// SPDX-License-Identifier: GPL-2.0
/* Multipath TCP
 *
 * Copyright (c) 2017 - 2019, Intel Corporation.
 */

#define pr_fmt(fmt) "MPTCP: " fmt

#include <linux/kernel.h>
#include <crypto/sha2.h>
#include <net/tcp.h>
#include <net/mptcp.h>
#include "protocol.h"
#include "mib.h"

#include <trace/events/mptcp.h>

static bool mptcp_cap_flag_sha256(u8 flags)
{
	return (flags & MPTCP_CAP_FLAG_MASK) == MPTCP_CAP_HMAC_SHA256;
}

static void mptcp_parse_option(const struct sk_buff *skb,
			       const unsigned char *ptr, int opsize,
			       struct mptcp_options_received *mp_opt)
{
	u8 subtype = *ptr >> 4;
	int expected_opsize;
	u8 version;
	u8 flags;
	u8 i;

	switch (subtype) {
	case MPTCPOPT_MP_CAPABLE:
		/* strict size checking */
		if (!(TCP_SKB_CB(skb)->tcp_flags & TCPHDR_SYN)) {
			if (skb->len > tcp_hdr(skb)->doff << 2)
				expected_opsize = TCPOLEN_MPTCP_MPC_ACK_DATA;
			else
				expected_opsize = TCPOLEN_MPTCP_MPC_ACK;
		} else {
			if (TCP_SKB_CB(skb)->tcp_flags & TCPHDR_ACK)
				expected_opsize = TCPOLEN_MPTCP_MPC_SYNACK;
			else
				expected_opsize = TCPOLEN_MPTCP_MPC_SYN;
		}

		/* Cfr RFC 8684 Section 3.3.0:
		 * If a checksum is present but its use had
		 * not been negotiated in the MP_CAPABLE handshake, the receiver MUST
		 * close the subflow with a RST, as it is not behaving as negotiated.
		 * If a checksum is not present when its use has been negotiated, the
		 * receiver MUST close the subflow with a RST, as it is considered
		 * broken
		 * We parse even option with mismatching csum presence, so that
		 * later in subflow_data_ready we can trigger the reset.
		 */
		if (opsize != expected_opsize &&
		    (expected_opsize != TCPOLEN_MPTCP_MPC_ACK_DATA ||
		     opsize != TCPOLEN_MPTCP_MPC_ACK_DATA_CSUM))
			break;

		/* try to be gentle vs future versions on the initial syn */
		version = *ptr++ & MPTCP_VERSION_MASK;
		if (opsize != TCPOLEN_MPTCP_MPC_SYN) {
			if (version != MPTCP_SUPPORTED_VERSION)
				break;
		} else if (version < MPTCP_SUPPORTED_VERSION) {
			break;
		}

		flags = *ptr++;
		if (!mptcp_cap_flag_sha256(flags) ||
		    (flags & MPTCP_CAP_EXTENSIBILITY))
			break;

		/* RFC 6824, Section 3.1:
		 * "For the Checksum Required bit (labeled "A"), if either
		 * host requires the use of checksums, checksums MUST be used.
		 * In other words, the only way for checksums not to be used
		 * is if both hosts in their SYNs set A=0."
		 */
		if (flags & MPTCP_CAP_CHECKSUM_REQD)
			mp_opt->suboptions |= OPTION_MPTCP_CSUMREQD;

		mp_opt->deny_join_id0 = !!(flags & MPTCP_CAP_DENY_JOIN_ID0);

		mp_opt->suboptions |= OPTIONS_MPTCP_MPC;
		if (opsize >= TCPOLEN_MPTCP_MPC_SYNACK) {
			mp_opt->sndr_key = get_unaligned_be64(ptr);
			ptr += 8;
		}
		if (opsize >= TCPOLEN_MPTCP_MPC_ACK) {
			mp_opt->rcvr_key = get_unaligned_be64(ptr);
			ptr += 8;
		}
		if (opsize >= TCPOLEN_MPTCP_MPC_ACK_DATA) {
			/* Section 3.1.:
			 * "the data parameters in a MP_CAPABLE are semantically
			 * equivalent to those in a DSS option and can be used
			 * interchangeably."
			 */
			mp_opt->suboptions |= OPTION_MPTCP_DSS;
			mp_opt->use_map = 1;
			mp_opt->mpc_map = 1;
			mp_opt->data_len = get_unaligned_be16(ptr);
			ptr += 2;
		}
		if (opsize == TCPOLEN_MPTCP_MPC_ACK_DATA_CSUM) {
			mp_opt->csum = (__force __sum16)get_unaligned_be16(ptr);
			mp_opt->suboptions |= OPTION_MPTCP_CSUMREQD;
			ptr += 2;
		}
		pr_debug("MP_CAPABLE version=%x, flags=%x, optlen=%d sndr=%llu, rcvr=%llu len=%d csum=%u",
			 version, flags, opsize, mp_opt->sndr_key,
			 mp_opt->rcvr_key, mp_opt->data_len, mp_opt->csum);
		break;

	case MPTCPOPT_MP_JOIN:
		mp_opt->suboptions |= OPTIONS_MPTCP_MPJ;
		if (opsize == TCPOLEN_MPTCP_MPJ_SYN) {
			mp_opt->backup = *ptr++ & MPTCPOPT_BACKUP;
			mp_opt->join_id = *ptr++;
			mp_opt->token = get_unaligned_be32(ptr);
			ptr += 4;
			mp_opt->nonce = get_unaligned_be32(ptr);
			ptr += 4;
			pr_debug("MP_JOIN bkup=%u, id=%u, token=%u, nonce=%u",
				 mp_opt->backup, mp_opt->join_id,
				 mp_opt->token, mp_opt->nonce);
		} else if (opsize == TCPOLEN_MPTCP_MPJ_SYNACK) {
			mp_opt->backup = *ptr++ & MPTCPOPT_BACKUP;
			mp_opt->join_id = *ptr++;
			mp_opt->thmac = get_unaligned_be64(ptr);
			ptr += 8;
			mp_opt->nonce = get_unaligned_be32(ptr);
			ptr += 4;
			pr_debug("MP_JOIN bkup=%u, id=%u, thmac=%llu, nonce=%u",
				 mp_opt->backup, mp_opt->join_id,
				 mp_opt->thmac, mp_opt->nonce);
		} else if (opsize == TCPOLEN_MPTCP_MPJ_ACK) {
			ptr += 2;
			memcpy(mp_opt->hmac, ptr, MPTCPOPT_HMAC_LEN);
			pr_debug("MP_JOIN hmac");
		} else {
			mp_opt->suboptions &= ~OPTIONS_MPTCP_MPJ;
		}
		break;

	case MPTCPOPT_DSS:
		pr_debug("DSS");
		ptr++;

		/* we must clear 'mpc_map' be able to detect MP_CAPABLE
		 * map vs DSS map in mptcp_incoming_options(), and reconstruct
		 * map info accordingly
		 */
		mp_opt->mpc_map = 0;
		flags = (*ptr++) & MPTCP_DSS_FLAG_MASK;
		mp_opt->data_fin = (flags & MPTCP_DSS_DATA_FIN) != 0;
		mp_opt->dsn64 = (flags & MPTCP_DSS_DSN64) != 0;
		mp_opt->use_map = (flags & MPTCP_DSS_HAS_MAP) != 0;
		mp_opt->ack64 = (flags & MPTCP_DSS_ACK64) != 0;
		mp_opt->use_ack = (flags & MPTCP_DSS_HAS_ACK);

		pr_debug("data_fin=%d dsn64=%d use_map=%d ack64=%d use_ack=%d",
			 mp_opt->data_fin, mp_opt->dsn64,
			 mp_opt->use_map, mp_opt->ack64,
			 mp_opt->use_ack);

		expected_opsize = TCPOLEN_MPTCP_DSS_BASE;

		if (mp_opt->use_ack) {
			if (mp_opt->ack64)
				expected_opsize += TCPOLEN_MPTCP_DSS_ACK64;
			else
				expected_opsize += TCPOLEN_MPTCP_DSS_ACK32;
		}

		if (mp_opt->use_map) {
			if (mp_opt->dsn64)
				expected_opsize += TCPOLEN_MPTCP_DSS_MAP64;
			else
				expected_opsize += TCPOLEN_MPTCP_DSS_MAP32;
		}

		/* Always parse any csum presence combination, we will enforce
		 * RFC 8684 Section 3.3.0 checks later in subflow_data_ready
		 */
		if (opsize != expected_opsize &&
		    opsize != expected_opsize + TCPOLEN_MPTCP_DSS_CHECKSUM)
			break;

		mp_opt->suboptions |= OPTION_MPTCP_DSS;
		if (mp_opt->use_ack) {
			if (mp_opt->ack64) {
				mp_opt->data_ack = get_unaligned_be64(ptr);
				ptr += 8;
			} else {
				mp_opt->data_ack = get_unaligned_be32(ptr);
				ptr += 4;
			}

			pr_debug("data_ack=%llu", mp_opt->data_ack);
		}

		if (mp_opt->use_map) {
			if (mp_opt->dsn64) {
				mp_opt->data_seq = get_unaligned_be64(ptr);
				ptr += 8;
			} else {
				mp_opt->data_seq = get_unaligned_be32(ptr);
				ptr += 4;
			}

			mp_opt->subflow_seq = get_unaligned_be32(ptr);
			ptr += 4;

			mp_opt->data_len = get_unaligned_be16(ptr);
			ptr += 2;

			if (opsize == expected_opsize + TCPOLEN_MPTCP_DSS_CHECKSUM) {
				mp_opt->suboptions |= OPTION_MPTCP_CSUMREQD;
				mp_opt->csum = (__force __sum16)get_unaligned_be16(ptr);
				ptr += 2;
			}

			pr_debug("data_seq=%llu subflow_seq=%u data_len=%u csum=%d:%u",
				 mp_opt->data_seq, mp_opt->subflow_seq,
				 mp_opt->data_len, !!(mp_opt->suboptions & OPTION_MPTCP_CSUMREQD),
				 mp_opt->csum);
		}

		break;

	case MPTCPOPT_ADD_ADDR:
		mp_opt->echo = (*ptr++) & MPTCP_ADDR_ECHO;
		if (!mp_opt->echo) {
			if (opsize == TCPOLEN_MPTCP_ADD_ADDR ||
			    opsize == TCPOLEN_MPTCP_ADD_ADDR_PORT)
				mp_opt->addr.family = AF_INET;
#if IS_ENABLED(CONFIG_MPTCP_IPV6)
			else if (opsize == TCPOLEN_MPTCP_ADD_ADDR6 ||
				 opsize == TCPOLEN_MPTCP_ADD_ADDR6_PORT)
				mp_opt->addr.family = AF_INET6;
#endif
			else
				break;
		} else {
			if (opsize == TCPOLEN_MPTCP_ADD_ADDR_BASE ||
			    opsize == TCPOLEN_MPTCP_ADD_ADDR_BASE_PORT)
				mp_opt->addr.family = AF_INET;
#if IS_ENABLED(CONFIG_MPTCP_IPV6)
			else if (opsize == TCPOLEN_MPTCP_ADD_ADDR6_BASE ||
				 opsize == TCPOLEN_MPTCP_ADD_ADDR6_BASE_PORT)
				mp_opt->addr.family = AF_INET6;
#endif
			else
				break;
		}

		mp_opt->suboptions |= OPTION_MPTCP_ADD_ADDR;
		mp_opt->addr.id = *ptr++;
		mp_opt->addr.port = 0;
		mp_opt->ahmac = 0;
		if (mp_opt->addr.family == AF_INET) {
			memcpy((u8 *)&mp_opt->addr.addr.s_addr, (u8 *)ptr, 4);
			ptr += 4;
			if (opsize == TCPOLEN_MPTCP_ADD_ADDR_PORT ||
			    opsize == TCPOLEN_MPTCP_ADD_ADDR_BASE_PORT) {
				mp_opt->addr.port = htons(get_unaligned_be16(ptr));
				ptr += 2;
			}
		}
#if IS_ENABLED(CONFIG_MPTCP_IPV6)
		else {
			memcpy(mp_opt->addr.addr6.s6_addr, (u8 *)ptr, 16);
			ptr += 16;
			if (opsize == TCPOLEN_MPTCP_ADD_ADDR6_PORT ||
			    opsize == TCPOLEN_MPTCP_ADD_ADDR6_BASE_PORT) {
				mp_opt->addr.port = htons(get_unaligned_be16(ptr));
				ptr += 2;
			}
		}
#endif
		if (!mp_opt->echo) {
			mp_opt->ahmac = get_unaligned_be64(ptr);
			ptr += 8;
		}
		pr_debug("ADD_ADDR%s: id=%d, ahmac=%llu, echo=%d, port=%d",
			 (mp_opt->addr.family == AF_INET6) ? "6" : "",
			 mp_opt->addr.id, mp_opt->ahmac, mp_opt->echo, ntohs(mp_opt->addr.port));
		break;

	case MPTCPOPT_RM_ADDR:
		if (opsize < TCPOLEN_MPTCP_RM_ADDR_BASE + 1 ||
		    opsize > TCPOLEN_MPTCP_RM_ADDR_BASE + MPTCP_RM_IDS_MAX)
			break;

		ptr++;

		mp_opt->suboptions |= OPTION_MPTCP_RM_ADDR;
		mp_opt->rm_list.nr = opsize - TCPOLEN_MPTCP_RM_ADDR_BASE;
		for (i = 0; i < mp_opt->rm_list.nr; i++)
			mp_opt->rm_list.ids[i] = *ptr++;
		pr_debug("RM_ADDR: rm_list_nr=%d", mp_opt->rm_list.nr);
		break;

	case MPTCPOPT_MP_PRIO:
		if (opsize != TCPOLEN_MPTCP_PRIO)
			break;

		mp_opt->suboptions |= OPTION_MPTCP_PRIO;
		mp_opt->backup = *ptr++ & MPTCP_PRIO_BKUP;
		pr_debug("MP_PRIO: prio=%d", mp_opt->backup);
		break;

	case MPTCPOPT_MP_FASTCLOSE:
		if (opsize != TCPOLEN_MPTCP_FASTCLOSE)
			break;

		ptr += 2;
		mp_opt->rcvr_key = get_unaligned_be64(ptr);
		ptr += 8;
		mp_opt->suboptions |= OPTION_MPTCP_FASTCLOSE;
		break;

	case MPTCPOPT_RST:
		if (opsize != TCPOLEN_MPTCP_RST)
			break;

		if (!(TCP_SKB_CB(skb)->tcp_flags & TCPHDR_RST))
			break;

		mp_opt->suboptions |= OPTION_MPTCP_RST;
		flags = *ptr++;
		mp_opt->reset_transient = flags & MPTCP_RST_TRANSIENT;
		mp_opt->reset_reason = *ptr;
		break;

	case MPTCPOPT_MP_FAIL:
		if (opsize != TCPOLEN_MPTCP_FAIL)
			break;

		ptr += 2;
		mp_opt->suboptions |= OPTION_MPTCP_FAIL;
		mp_opt->fail_seq = get_unaligned_be64(ptr);
		pr_debug("MP_FAIL: data_seq=%llu", mp_opt->fail_seq);
		break;

	default:
		break;
	}
}

void mptcp_get_options(const struct sock *sk,
		       const struct sk_buff *skb,
		       struct mptcp_options_received *mp_opt)
{
	const struct tcphdr *th = tcp_hdr(skb);
	const unsigned char *ptr;
	int length;

	/* initialize option status */
	mp_opt->suboptions = 0;

	length = (th->doff * 4) - sizeof(struct tcphdr);
	ptr = (const unsigned char *)(th + 1);

	while (length > 0) {
		int opcode = *ptr++;
		int opsize;

		switch (opcode) {
		case TCPOPT_EOL:
			return;
		case TCPOPT_NOP:	/* Ref: RFC 793 section 3.1 */
			length--;
			continue;
		default:
			if (length < 2)
				return;
			opsize = *ptr++;
			if (opsize < 2) /* "silly options" */
				return;
			if (opsize > length)
				return;	/* don't parse partial options */
			if (opcode == TCPOPT_MPTCP)
				mptcp_parse_option(skb, ptr, opsize, mp_opt);
			ptr += opsize - 2;
			length -= opsize;
		}
	}
}

bool mptcp_syn_options(struct sock *sk, const struct sk_buff *skb,
		       unsigned int *size, struct mptcp_out_options *opts)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(sk);

	/* we will use snd_isn to detect first pkt [re]transmission
	 * in mptcp_established_options_mp()
	 */
	subflow->snd_isn = TCP_SKB_CB(skb)->end_seq;
	if (subflow->request_mptcp) {
		opts->suboptions = OPTION_MPTCP_MPC_SYN;
		opts->csum_reqd = mptcp_is_checksum_enabled(sock_net(sk));
		opts->allow_join_id0 = mptcp_allow_join_id0(sock_net(sk));
		*size = TCPOLEN_MPTCP_MPC_SYN;
		return true;
	} else if (subflow->request_join) {
		pr_debug("remote_token=%u, nonce=%u", subflow->remote_token,
			 subflow->local_nonce);
		opts->suboptions = OPTION_MPTCP_MPJ_SYN;
		opts->join_id = subflow->local_id;
		opts->token = subflow->remote_token;
		opts->nonce = subflow->local_nonce;
		opts->backup = subflow->request_bkup;
		*size = TCPOLEN_MPTCP_MPJ_SYN;
		return true;
	}
	return false;
}

/* MP_JOIN client subflow must wait for 4th ack before sending any data:
 * TCP can't schedule delack timer before the subflow is fully established.
 * MPTCP uses the delack timer to do 3rd ack retransmissions
 */
static void schedule_3rdack_retransmission(struct sock *sk)
{
	struct inet_connection_sock *icsk = inet_csk(sk);
	struct tcp_sock *tp = tcp_sk(sk);
	unsigned long timeout;

	/* reschedule with a timeout above RTT, as we must look only for drop */
	if (tp->srtt_us)
		timeout = tp->srtt_us << 1;
	else
		timeout = TCP_TIMEOUT_INIT;

	WARN_ON_ONCE(icsk->icsk_ack.pending & ICSK_ACK_TIMER);
	icsk->icsk_ack.pending |= ICSK_ACK_SCHED | ICSK_ACK_TIMER;
	icsk->icsk_ack.timeout = timeout;
	sk_reset_timer(sk, &icsk->icsk_delack_timer, timeout);
}

static void clear_3rdack_retransmission(struct sock *sk)
{
	struct inet_connection_sock *icsk = inet_csk(sk);

	sk_stop_timer(sk, &icsk->icsk_delack_timer);
	icsk->icsk_ack.timeout = 0;
	icsk->icsk_ack.ato = 0;
	icsk->icsk_ack.pending &= ~(ICSK_ACK_SCHED | ICSK_ACK_TIMER);
}

static bool mptcp_established_options_mp(struct sock *sk, struct sk_buff *skb,
					 bool snd_data_fin_enable,
					 unsigned int *size,
					 unsigned int remaining,
					 struct mptcp_out_options *opts)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(sk);
	struct mptcp_sock *msk = mptcp_sk(subflow->conn);
	struct mptcp_ext *mpext;
	unsigned int data_len;
	u8 len;

	/* When skb is not available, we better over-estimate the emitted
	 * options len. A full DSS option (28 bytes) is longer than
	 * TCPOLEN_MPTCP_MPC_ACK_DATA(22) or TCPOLEN_MPTCP_MPJ_ACK(24), so
	 * tell the caller to defer the estimate to
	 * mptcp_established_options_dss(), which will reserve enough space.
	 */
	if (!skb)
		return false;

	/* MPC/MPJ needed only on 3rd ack packet, DATA_FIN and TCP shutdown take precedence */
	if (subflow->fully_established || snd_data_fin_enable ||
	    subflow->snd_isn != TCP_SKB_CB(skb)->seq ||
	    sk->sk_state != TCP_ESTABLISHED)
		return false;

	if (subflow->mp_capable) {
		mpext = mptcp_get_ext(skb);
		data_len = mpext ? mpext->data_len : 0;

		/* we will check ext_copy.data_len in mptcp_write_options() to
		 * discriminate between TCPOLEN_MPTCP_MPC_ACK_DATA and
		 * TCPOLEN_MPTCP_MPC_ACK
		 */
		opts->ext_copy.data_len = data_len;
		opts->suboptions = OPTION_MPTCP_MPC_ACK;
		opts->sndr_key = subflow->local_key;
		opts->rcvr_key = subflow->remote_key;
		opts->csum_reqd = READ_ONCE(msk->csum_enabled);
		opts->allow_join_id0 = mptcp_allow_join_id0(sock_net(sk));

		/* Section 3.1.
		 * The MP_CAPABLE option is carried on the SYN, SYN/ACK, and ACK
		 * packets that start the first subflow of an MPTCP connection,
		 * as well as the first packet that carries data
		 */
		if (data_len > 0) {
			len = TCPOLEN_MPTCP_MPC_ACK_DATA;
			if (opts->csum_reqd) {
				/* we need to propagate more info to csum the pseudo hdr */
				opts->ext_copy.data_seq = mpext->data_seq;
				opts->ext_copy.subflow_seq = mpext->subflow_seq;
				opts->ext_copy.csum = mpext->csum;
				len += TCPOLEN_MPTCP_DSS_CHECKSUM;
			}
			*size = ALIGN(len, 4);
		} else {
			*size = TCPOLEN_MPTCP_MPC_ACK;
		}

		pr_debug("subflow=%p, local_key=%llu, remote_key=%llu map_len=%d",
			 subflow, subflow->local_key, subflow->remote_key,
			 data_len);

		return true;
	} else if (subflow->mp_join) {
		opts->suboptions = OPTION_MPTCP_MPJ_ACK;
		memcpy(opts->hmac, subflow->hmac, MPTCPOPT_HMAC_LEN);
		*size = TCPOLEN_MPTCP_MPJ_ACK;
		pr_debug("subflow=%p", subflow);

		schedule_3rdack_retransmission(sk);
		return true;
	}
	return false;
}

static void mptcp_write_data_fin(struct mptcp_subflow_context *subflow,
				 struct sk_buff *skb, struct mptcp_ext *ext)
{
	/* The write_seq value has already been incremented, so the actual
	 * sequence number for the DATA_FIN is one less.
	 */
	u64 data_fin_tx_seq = READ_ONCE(mptcp_sk(subflow->conn)->write_seq) - 1;

	if (!ext->use_map || !skb->len) {
		/* RFC6824 requires a DSS mapping with specific values
		 * if DATA_FIN is set but no data payload is mapped
		 */
		ext->data_fin = 1;
		ext->use_map = 1;
		ext->dsn64 = 1;
		ext->data_seq = data_fin_tx_seq;
		ext->subflow_seq = 0;
		ext->data_len = 1;
	} else if (ext->data_seq + ext->data_len == data_fin_tx_seq) {
		/* If there's an existing DSS mapping and it is the
		 * final mapping, DATA_FIN consumes 1 additional byte of
		 * mapping space.
		 */
		ext->data_fin = 1;
		ext->data_len++;
	}
}

static bool mptcp_established_options_dss(struct sock *sk, struct sk_buff *skb,
					  bool snd_data_fin_enable,
					  unsigned int *size,
					  unsigned int remaining,
					  struct mptcp_out_options *opts)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(sk);
	struct mptcp_sock *msk = mptcp_sk(subflow->conn);
	unsigned int dss_size = 0;
	struct mptcp_ext *mpext;
	unsigned int ack_size;
	bool ret = false;
	u64 ack_seq;

	opts->csum_reqd = READ_ONCE(msk->csum_enabled);
	mpext = skb ? mptcp_get_ext(skb) : NULL;

	if (!skb || (mpext && mpext->use_map) || snd_data_fin_enable) {
		unsigned int map_size = TCPOLEN_MPTCP_DSS_BASE + TCPOLEN_MPTCP_DSS_MAP64;

		if (mpext) {
			if (opts->csum_reqd)
				map_size += TCPOLEN_MPTCP_DSS_CHECKSUM;

			opts->ext_copy = *mpext;
		}

		remaining -= map_size;
		dss_size = map_size;
		if (skb && snd_data_fin_enable)
			mptcp_write_data_fin(subflow, skb, &opts->ext_copy);
		opts->suboptions = OPTION_MPTCP_DSS;
		ret = true;
	}

	/* passive sockets msk will set the 'can_ack' after accept(), even
	 * if the first subflow may have the already the remote key handy
	 */
	opts->ext_copy.use_ack = 0;
	if (!READ_ONCE(msk->can_ack)) {
		*size = ALIGN(dss_size, 4);
		return ret;
	}

	ack_seq = READ_ONCE(msk->ack_seq);
	if (READ_ONCE(msk->use_64bit_ack)) {
		ack_size = TCPOLEN_MPTCP_DSS_ACK64;
		opts->ext_copy.data_ack = ack_seq;
		opts->ext_copy.ack64 = 1;
	} else {
		ack_size = TCPOLEN_MPTCP_DSS_ACK32;
		opts->ext_copy.data_ack32 = (uint32_t)ack_seq;
		opts->ext_copy.ack64 = 0;
	}
	opts->ext_copy.use_ack = 1;
	opts->suboptions = OPTION_MPTCP_DSS;
	WRITE_ONCE(msk->old_wspace, __mptcp_space((struct sock *)msk));

	/* Add kind/length/subtype/flag overhead if mapping is not populated */
	if (dss_size == 0)
		ack_size += TCPOLEN_MPTCP_DSS_BASE;

	dss_size += ack_size;

	*size = ALIGN(dss_size, 4);
	return true;
}

static u64 add_addr_generate_hmac(u64 key1, u64 key2,
				  struct mptcp_addr_info *addr)
{
	u16 port = ntohs(addr->port);
	u8 hmac[SHA256_DIGEST_SIZE];
	u8 msg[19];
	int i = 0;

	msg[i++] = addr->id;
	if (addr->family == AF_INET) {
		memcpy(&msg[i], &addr->addr.s_addr, 4);
		i += 4;
	}
#if IS_ENABLED(CONFIG_MPTCP_IPV6)
	else if (addr->family == AF_INET6) {
		memcpy(&msg[i], &addr->addr6.s6_addr, 16);
		i += 16;
	}
#endif
	msg[i++] = port >> 8;
	msg[i++] = port & 0xFF;

	mptcp_crypto_hmac_sha(key1, key2, msg, i, hmac);

	return get_unaligned_be64(&hmac[SHA256_DIGEST_SIZE - sizeof(u64)]);
}

static bool mptcp_established_options_add_addr(struct sock *sk, struct sk_buff *skb,
					       unsigned int *size,
					       unsigned int remaining,
					       struct mptcp_out_options *opts)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(sk);
	struct mptcp_sock *msk = mptcp_sk(subflow->conn);
	bool drop_other_suboptions = false;
	unsigned int opt_size = *size;
	bool echo;
	bool port;
	int len;

	/* add addr will strip the existing options, be sure to avoid breaking
	 * MPC/MPJ handshakes
	 */
	if (!mptcp_pm_should_add_signal(msk) ||
	    (opts->suboptions & (OPTION_MPTCP_MPJ_ACK | OPTION_MPTCP_MPC_ACK)) ||
	    !mptcp_pm_add_addr_signal(msk, skb, opt_size, remaining, &opts->addr,
		    &echo, &port, &drop_other_suboptions))
		return false;

	if (drop_other_suboptions)
		remaining += opt_size;
	len = mptcp_add_addr_len(opts->addr.family, echo, port);
	if (remaining < len)
		return false;

	*size = len;
	if (drop_other_suboptions) {
		pr_debug("drop other suboptions");
		opts->suboptions = 0;

		/* note that e.g. DSS could have written into the memory
		 * aliased by ahmac, we must reset the field here
		 * to avoid appending the hmac even for ADD_ADDR echo
		 * options
		 */
		opts->ahmac = 0;
		*size -= opt_size;
	}
	opts->suboptions |= OPTION_MPTCP_ADD_ADDR;
	if (!echo) {
		opts->ahmac = add_addr_generate_hmac(msk->local_key,
						     msk->remote_key,
						     &opts->addr);
	}
	pr_debug("addr_id=%d, ahmac=%llu, echo=%d, port=%d",
		 opts->addr.id, opts->ahmac, echo, ntohs(opts->addr.port));

	return true;
}

static bool mptcp_established_options_rm_addr(struct sock *sk,
					      unsigned int *size,
					      unsigned int remaining,
					      struct mptcp_out_options *opts)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(sk);
	struct mptcp_sock *msk = mptcp_sk(subflow->conn);
	struct mptcp_rm_list rm_list;
	int i, len;

	if (!mptcp_pm_should_rm_signal(msk) ||
	    !(mptcp_pm_rm_addr_signal(msk, remaining, &rm_list)))
		return false;

	len = mptcp_rm_addr_len(&rm_list);
	if (len < 0)
		return false;
	if (remaining < len)
		return false;

	*size = len;
	opts->suboptions |= OPTION_MPTCP_RM_ADDR;
	opts->rm_list = rm_list;

	for (i = 0; i < opts->rm_list.nr; i++)
		pr_debug("rm_list_ids[%d]=%d", i, opts->rm_list.ids[i]);

	return true;
}

static bool mptcp_established_options_mp_prio(struct sock *sk,
					      unsigned int *size,
					      unsigned int remaining,
					      struct mptcp_out_options *opts)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(sk);

	/* can't send MP_PRIO with MPC, as they share the same option space:
	 * 'backup'. Also it makes no sense at all
	 */
	if (!subflow->send_mp_prio ||
	    ((OPTION_MPTCP_MPC_SYN | OPTION_MPTCP_MPC_SYNACK |
	      OPTION_MPTCP_MPC_ACK) & opts->suboptions))
		return false;

	/* account for the trailing 'nop' option */
	if (remaining < TCPOLEN_MPTCP_PRIO_ALIGN)
		return false;

	*size = TCPOLEN_MPTCP_PRIO_ALIGN;
	opts->suboptions |= OPTION_MPTCP_PRIO;
	opts->backup = subflow->request_bkup;

	pr_debug("prio=%d", opts->backup);

	return true;
}

static noinline bool mptcp_established_options_rst(struct sock *sk, struct sk_buff *skb,
						   unsigned int *size,
						   unsigned int remaining,
						   struct mptcp_out_options *opts)
{
	const struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(sk);

	if (remaining < TCPOLEN_MPTCP_RST)
		return false;

	*size = TCPOLEN_MPTCP_RST;
	opts->suboptions |= OPTION_MPTCP_RST;
	opts->reset_transient = subflow->reset_transient;
	opts->reset_reason = subflow->reset_reason;

	return true;
}

static bool mptcp_established_options_mp_fail(struct sock *sk,
					      unsigned int *size,
					      unsigned int remaining,
					      struct mptcp_out_options *opts)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(sk);

	if (likely(!subflow->send_mp_fail))
		return false;

	if (remaining < TCPOLEN_MPTCP_FAIL)
		return false;

	*size = TCPOLEN_MPTCP_FAIL;
	opts->suboptions |= OPTION_MPTCP_FAIL;
	opts->fail_seq = subflow->map_seq;

	pr_debug("MP_FAIL fail_seq=%llu", opts->fail_seq);

	return true;
}

bool mptcp_established_options(struct sock *sk, struct sk_buff *skb,
			       unsigned int *size, unsigned int remaining,
			       struct mptcp_out_options *opts)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(sk);
	struct mptcp_sock *msk = mptcp_sk(subflow->conn);
	unsigned int opt_size = 0;
	bool snd_data_fin;
	bool ret = false;

	opts->suboptions = 0;

	if (unlikely(__mptcp_check_fallback(msk)))
		return false;

	if (unlikely(skb && TCP_SKB_CB(skb)->tcp_flags & TCPHDR_RST)) {
		if (mptcp_established_options_mp_fail(sk, &opt_size, remaining, opts)) {
			*size += opt_size;
			remaining -= opt_size;
		}
		if (mptcp_established_options_rst(sk, skb, &opt_size, remaining, opts)) {
			*size += opt_size;
			remaining -= opt_size;
		}
		return true;
	}

	snd_data_fin = mptcp_data_fin_enabled(msk);
	if (mptcp_established_options_mp(sk, skb, snd_data_fin, &opt_size, remaining, opts))
		ret = true;
	else if (mptcp_established_options_dss(sk, skb, snd_data_fin, &opt_size, remaining, opts)) {
		ret = true;
		if (mptcp_established_options_mp_fail(sk, &opt_size, remaining, opts)) {
			*size += opt_size;
			remaining -= opt_size;
			return true;
		}
	}

	/* we reserved enough space for the above options, and exceeding the
	 * TCP option space would be fatal
	 */
	if (WARN_ON_ONCE(opt_size > remaining))
		return false;

	*size += opt_size;
	remaining -= opt_size;
	if (mptcp_established_options_add_addr(sk, skb, &opt_size, remaining, opts)) {
		*size += opt_size;
		remaining -= opt_size;
		ret = true;
	} else if (mptcp_established_options_rm_addr(sk, &opt_size, remaining, opts)) {
		*size += opt_size;
		remaining -= opt_size;
		ret = true;
	}

	if (mptcp_established_options_mp_prio(sk, &opt_size, remaining, opts)) {
		*size += opt_size;
		remaining -= opt_size;
		ret = true;
	}

	return ret;
}

bool mptcp_synack_options(const struct request_sock *req, unsigned int *size,
			  struct mptcp_out_options *opts)
{
	struct mptcp_subflow_request_sock *subflow_req = mptcp_subflow_rsk(req);

	if (subflow_req->mp_capable) {
		opts->suboptions = OPTION_MPTCP_MPC_SYNACK;
		opts->sndr_key = subflow_req->local_key;
		opts->csum_reqd = subflow_req->csum_reqd;
		opts->allow_join_id0 = subflow_req->allow_join_id0;
		*size = TCPOLEN_MPTCP_MPC_SYNACK;
		pr_debug("subflow_req=%p, local_key=%llu",
			 subflow_req, subflow_req->local_key);
		return true;
	} else if (subflow_req->mp_join) {
		opts->suboptions = OPTION_MPTCP_MPJ_SYNACK;
		opts->backup = subflow_req->backup;
		opts->join_id = subflow_req->local_id;
		opts->thmac = subflow_req->thmac;
		opts->nonce = subflow_req->local_nonce;
		pr_debug("req=%p, bkup=%u, id=%u, thmac=%llu, nonce=%u",
			 subflow_req, opts->backup, opts->join_id,
			 opts->thmac, opts->nonce);
		*size = TCPOLEN_MPTCP_MPJ_SYNACK;
		return true;
	}
	return false;
}

static bool check_fully_established(struct mptcp_sock *msk, struct sock *ssk,
				    struct mptcp_subflow_context *subflow,
				    struct sk_buff *skb,
				    struct mptcp_options_received *mp_opt)
{
	/* here we can process OoO, in-window pkts, only in-sequence 4th ack
	 * will make the subflow fully established
	 */
	if (likely(subflow->fully_established)) {
		/* on passive sockets, check for 3rd ack retransmission
		 * note that msk is always set by subflow_syn_recv_sock()
		 * for mp_join subflows
		 */
		if (TCP_SKB_CB(skb)->seq == subflow->ssn_offset + 1 &&
		    TCP_SKB_CB(skb)->end_seq == TCP_SKB_CB(skb)->seq &&
		    subflow->mp_join && (mp_opt->suboptions & OPTIONS_MPTCP_MPJ) &&
		    READ_ONCE(msk->pm.server_side))
			tcp_send_ack(ssk);
		goto fully_established;
	}

	/* we must process OoO packets before the first subflow is fully
	 * established. OoO packets are instead a protocol violation
	 * for MP_JOIN subflows as the peer must not send any data
	 * before receiving the forth ack - cfr. RFC 8684 section 3.2.
	 */
	if (TCP_SKB_CB(skb)->seq != subflow->ssn_offset + 1) {
		if (subflow->mp_join)
			goto reset;
		return subflow->mp_capable;
	}

	if (((mp_opt->suboptions & OPTION_MPTCP_DSS) && mp_opt->use_ack) ||
	    ((mp_opt->suboptions & OPTION_MPTCP_ADD_ADDR) && !mp_opt->echo)) {
		/* subflows are fully established as soon as we get any
		 * additional ack, including ADD_ADDR.
		 */
		subflow->fully_established = 1;
		WRITE_ONCE(msk->fully_established, true);
		goto fully_established;
	}

	/* If the first established packet does not contain MP_CAPABLE + data
	 * then fallback to TCP. Fallback scenarios requires a reset for
	 * MP_JOIN subflows.
	 */
	if (!(mp_opt->suboptions & OPTIONS_MPTCP_MPC)) {
		if (subflow->mp_join)
			goto reset;
		subflow->mp_capable = 0;
		pr_fallback(msk);
		__mptcp_do_fallback(msk);
		return false;
	}

	if (mp_opt->deny_join_id0)
		WRITE_ONCE(msk->pm.remote_deny_join_id0, true);

	if (unlikely(!READ_ONCE(msk->pm.server_side)))
		pr_warn_once("bogus mpc option on established client sk");
	mptcp_subflow_fully_established(subflow, mp_opt);

fully_established:
	/* if the subflow is not already linked into the conn_list, we can't
	 * notify the PM: this subflow is still on the listener queue
	 * and the PM possibly acquiring the subflow lock could race with
	 * the listener close
	 */
	if (likely(subflow->pm_notified) || list_empty(&subflow->node))
		return true;

	subflow->pm_notified = 1;
	if (subflow->mp_join) {
		clear_3rdack_retransmission(ssk);
		mptcp_pm_subflow_established(msk);
	} else {
		mptcp_pm_fully_established(msk, ssk, GFP_ATOMIC);
	}
	return true;

reset:
	mptcp_subflow_reset(ssk);
	return false;
}

u64 __mptcp_expand_seq(u64 old_seq, u64 cur_seq)
{
	u32 old_seq32, cur_seq32;

	old_seq32 = (u32)old_seq;
	cur_seq32 = (u32)cur_seq;
	cur_seq = (old_seq & GENMASK_ULL(63, 32)) + cur_seq32;
	if (unlikely(cur_seq32 < old_seq32 && before(old_seq32, cur_seq32)))
		return cur_seq + (1LL << 32);

	/* reverse wrap could happen, too */
	if (unlikely(cur_seq32 > old_seq32 && after(old_seq32, cur_seq32)))
		return cur_seq - (1LL << 32);
	return cur_seq;
}

static void ack_update_msk(struct mptcp_sock *msk,
			   struct sock *ssk,
			   struct mptcp_options_received *mp_opt)
{
	u64 new_wnd_end, new_snd_una, snd_nxt = READ_ONCE(msk->snd_nxt);
	struct sock *sk = (struct sock *)msk;
	u64 old_snd_una;

	mptcp_data_lock(sk);

	/* avoid ack expansion on update conflict, to reduce the risk of
	 * wrongly expanding to a future ack sequence number, which is way
	 * more dangerous than missing an ack
	 */
	old_snd_una = msk->snd_una;
	new_snd_una = mptcp_expand_seq(old_snd_una, mp_opt->data_ack, mp_opt->ack64);

	/* ACK for data not even sent yet? Ignore.*/
	if (unlikely(after64(new_snd_una, snd_nxt)))
		new_snd_una = old_snd_una;

	new_wnd_end = new_snd_una + tcp_sk(ssk)->snd_wnd;

	if (after64(new_wnd_end, msk->wnd_end))
		msk->wnd_end = new_wnd_end;

	/* this assumes mptcp_incoming_options() is invoked after tcp_ack() */
	if (after64(msk->wnd_end, READ_ONCE(msk->snd_nxt)))
		__mptcp_check_push(sk, ssk);

	if (after64(new_snd_una, old_snd_una)) {
		msk->snd_una = new_snd_una;
		__mptcp_data_acked(sk);
	}
	mptcp_data_unlock(sk);

	trace_ack_update_msk(mp_opt->data_ack,
			     old_snd_una, new_snd_una,
			     new_wnd_end, msk->wnd_end);
}

bool mptcp_update_rcv_data_fin(struct mptcp_sock *msk, u64 data_fin_seq, bool use_64bit)
{
	/* Skip if DATA_FIN was already received.
	 * If updating simultaneously with the recvmsg loop, values
	 * should match. If they mismatch, the peer is misbehaving and
	 * we will prefer the most recent information.
	 */
	if (READ_ONCE(msk->rcv_data_fin))
		return false;

	WRITE_ONCE(msk->rcv_data_fin_seq,
		   mptcp_expand_seq(READ_ONCE(msk->ack_seq), data_fin_seq, use_64bit));
	WRITE_ONCE(msk->rcv_data_fin, 1);

	return true;
}

static bool add_addr_hmac_valid(struct mptcp_sock *msk,
				struct mptcp_options_received *mp_opt)
{
	u64 hmac = 0;

	if (mp_opt->echo)
		return true;

	hmac = add_addr_generate_hmac(msk->remote_key,
				      msk->local_key,
				      &mp_opt->addr);

	pr_debug("msk=%p, ahmac=%llu, mp_opt->ahmac=%llu\n",
		 msk, (unsigned long long)hmac,
		 (unsigned long long)mp_opt->ahmac);

	return hmac == mp_opt->ahmac;
}

/* Return false if a subflow has been reset, else return true */
bool mptcp_incoming_options(struct sock *sk, struct sk_buff *skb)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(sk);
	struct mptcp_sock *msk = mptcp_sk(subflow->conn);
	struct mptcp_options_received mp_opt;
	struct mptcp_ext *mpext;

	if (__mptcp_check_fallback(msk)) {
		/* Keep it simple and unconditionally trigger send data cleanup and
		 * pending queue spooling. We will need to acquire the data lock
		 * for more accurate checks, and once the lock is acquired, such
		 * helpers are cheap.
		 */
		mptcp_data_lock(subflow->conn);
		if (sk_stream_memory_free(sk))
			__mptcp_check_push(subflow->conn, sk);
		__mptcp_data_acked(subflow->conn);
		mptcp_data_unlock(subflow->conn);
		return true;
	}

	mptcp_get_options(sk, skb, &mp_opt);

	/* The subflow can be in close state only if check_fully_established()
	 * just sent a reset. If so, tell the caller to ignore the current packet.
	 */
	if (!check_fully_established(msk, sk, subflow, skb, &mp_opt))
		return sk->sk_state != TCP_CLOSE;

	if (unlikely(mp_opt.suboptions != OPTION_MPTCP_DSS)) {
		if ((mp_opt.suboptions & OPTION_MPTCP_FASTCLOSE) &&
		    msk->local_key == mp_opt.rcvr_key) {
			WRITE_ONCE(msk->rcv_fastclose, true);
			mptcp_schedule_work((struct sock *)msk);
		}

		if ((mp_opt.suboptions & OPTION_MPTCP_ADD_ADDR) &&
		    add_addr_hmac_valid(msk, &mp_opt)) {
			if (!mp_opt.echo) {
				mptcp_pm_add_addr_received(msk, &mp_opt.addr);
				MPTCP_INC_STATS(sock_net(sk), MPTCP_MIB_ADDADDR);
			} else {
				mptcp_pm_add_addr_echoed(msk, &mp_opt.addr);
				mptcp_pm_del_add_timer(msk, &mp_opt.addr, true);
				MPTCP_INC_STATS(sock_net(sk), MPTCP_MIB_ECHOADD);
			}

			if (mp_opt.addr.port)
				MPTCP_INC_STATS(sock_net(sk), MPTCP_MIB_PORTADD);
		}

		if (mp_opt.suboptions & OPTION_MPTCP_RM_ADDR)
			mptcp_pm_rm_addr_received(msk, &mp_opt.rm_list);

		if (mp_opt.suboptions & OPTION_MPTCP_PRIO) {
			mptcp_pm_mp_prio_received(sk, mp_opt.backup);
			MPTCP_INC_STATS(sock_net(sk), MPTCP_MIB_MPPRIORX);
		}

		if (mp_opt.suboptions & OPTION_MPTCP_FAIL) {
			mptcp_pm_mp_fail_received(sk, mp_opt.fail_seq);
			MPTCP_INC_STATS(sock_net(sk), MPTCP_MIB_MPFAILRX);
		}

		if (mp_opt.suboptions & OPTION_MPTCP_RST) {
			subflow->reset_seen = 1;
			subflow->reset_reason = mp_opt.reset_reason;
			subflow->reset_transient = mp_opt.reset_transient;
		}

		if (!(mp_opt.suboptions & OPTION_MPTCP_DSS))
			return true;
	}

	/* we can't wait for recvmsg() to update the ack_seq, otherwise
	 * monodirectional flows will stuck
	 */
	if (mp_opt.use_ack)
		ack_update_msk(msk, sk, &mp_opt);

	/* Zero-data-length packets are dropped by the caller and not
	 * propagated to the MPTCP layer, so the skb extension does not
	 * need to be allocated or populated. DATA_FIN information, if
	 * present, needs to be updated here before the skb is freed.
	 */
	if (TCP_SKB_CB(skb)->seq == TCP_SKB_CB(skb)->end_seq) {
		if (mp_opt.data_fin && mp_opt.data_len == 1 &&
		    mptcp_update_rcv_data_fin(msk, mp_opt.data_seq, mp_opt.dsn64) &&
		    schedule_work(&msk->work))
			sock_hold(subflow->conn);

		return true;
	}

	mpext = skb_ext_add(skb, SKB_EXT_MPTCP);
	if (!mpext)
		return true;

	memset(mpext, 0, sizeof(*mpext));

	if (likely(mp_opt.use_map)) {
		if (mp_opt.mpc_map) {
			/* this is an MP_CAPABLE carrying MPTCP data
			 * we know this map the first chunk of data
			 */
			mptcp_crypto_key_sha(subflow->remote_key, NULL,
					     &mpext->data_seq);
			mpext->data_seq++;
			mpext->subflow_seq = 1;
			mpext->dsn64 = 1;
			mpext->mpc_map = 1;
			mpext->data_fin = 0;
		} else {
			mpext->data_seq = mp_opt.data_seq;
			mpext->subflow_seq = mp_opt.subflow_seq;
			mpext->dsn64 = mp_opt.dsn64;
			mpext->data_fin = mp_opt.data_fin;
		}
		mpext->data_len = mp_opt.data_len;
		mpext->use_map = 1;
		mpext->csum_reqd = !!(mp_opt.suboptions & OPTION_MPTCP_CSUMREQD);

		if (mpext->csum_reqd)
			mpext->csum = mp_opt.csum;
	}

	return true;
}

static void mptcp_set_rwin(const struct tcp_sock *tp)
{
	const struct sock *ssk = (const struct sock *)tp;
	const struct mptcp_subflow_context *subflow;
	struct mptcp_sock *msk;
	u64 ack_seq;

	subflow = mptcp_subflow_ctx(ssk);
	msk = mptcp_sk(subflow->conn);

	ack_seq = READ_ONCE(msk->ack_seq) + tp->rcv_wnd;

	if (after64(ack_seq, READ_ONCE(msk->rcv_wnd_sent)))
		WRITE_ONCE(msk->rcv_wnd_sent, ack_seq);
}

static u16 mptcp_make_csum(const struct mptcp_ext *mpext)
{
	struct csum_pseudo_header header;
	__wsum csum;

	/* cfr RFC 8684 3.3.1.:
	 * the data sequence number used in the pseudo-header is
	 * always the 64-bit value, irrespective of what length is used in the
	 * DSS option itself.
	 */
	header.data_seq = cpu_to_be64(mpext->data_seq);
	header.subflow_seq = htonl(mpext->subflow_seq);
	header.data_len = htons(mpext->data_len);
	header.csum = 0;

	csum = csum_partial(&header, sizeof(header), ~csum_unfold(mpext->csum));
	return (__force u16)csum_fold(csum);
}

void mptcp_write_options(__be32 *ptr, const struct tcp_sock *tp,
			 struct mptcp_out_options *opts)
{
	if (unlikely(OPTION_MPTCP_FAIL & opts->suboptions)) {
		const struct sock *ssk = (const struct sock *)tp;
		struct mptcp_subflow_context *subflow;

		subflow = mptcp_subflow_ctx(ssk);
		subflow->send_mp_fail = 0;

		*ptr++ = mptcp_option(MPTCPOPT_MP_FAIL,
				      TCPOLEN_MPTCP_FAIL,
				      0, 0);
		put_unaligned_be64(opts->fail_seq, ptr);
		ptr += 2;
	}

	/* RST is mutually exclusive with everything else */
	if (unlikely(OPTION_MPTCP_RST & opts->suboptions)) {
		*ptr++ = mptcp_option(MPTCPOPT_RST,
				      TCPOLEN_MPTCP_RST,
				      opts->reset_transient,
				      opts->reset_reason);
		return;
	}

	/* DSS, MPC, MPJ and ADD_ADDR are mutually exclusive, see
	 * mptcp_established_options*()
	 */
	if (likely(OPTION_MPTCP_DSS & opts->suboptions)) {
		struct mptcp_ext *mpext = &opts->ext_copy;
		u8 len = TCPOLEN_MPTCP_DSS_BASE;
		u8 flags = 0;

		if (mpext->use_ack) {
			flags = MPTCP_DSS_HAS_ACK;
			if (mpext->ack64) {
				len += TCPOLEN_MPTCP_DSS_ACK64;
				flags |= MPTCP_DSS_ACK64;
			} else {
				len += TCPOLEN_MPTCP_DSS_ACK32;
			}
		}

		if (mpext->use_map) {
			len += TCPOLEN_MPTCP_DSS_MAP64;

			/* Use only 64-bit mapping flags for now, add
			 * support for optional 32-bit mappings later.
			 */
			flags |= MPTCP_DSS_HAS_MAP | MPTCP_DSS_DSN64;
			if (mpext->data_fin)
				flags |= MPTCP_DSS_DATA_FIN;

			if (opts->csum_reqd)
				len += TCPOLEN_MPTCP_DSS_CHECKSUM;
		}

		*ptr++ = mptcp_option(MPTCPOPT_DSS, len, 0, flags);

		if (mpext->use_ack) {
			if (mpext->ack64) {
				put_unaligned_be64(mpext->data_ack, ptr);
				ptr += 2;
			} else {
				put_unaligned_be32(mpext->data_ack32, ptr);
				ptr += 1;
			}
		}

		if (mpext->use_map) {
			put_unaligned_be64(mpext->data_seq, ptr);
			ptr += 2;
			put_unaligned_be32(mpext->subflow_seq, ptr);
			ptr += 1;
			if (opts->csum_reqd) {
				put_unaligned_be32(mpext->data_len << 16 |
						   mptcp_make_csum(mpext), ptr);
			} else {
				put_unaligned_be32(mpext->data_len << 16 |
						   TCPOPT_NOP << 8 | TCPOPT_NOP, ptr);
			}
		}
	} else if ((OPTION_MPTCP_MPC_SYN | OPTION_MPTCP_MPC_SYNACK |
		    OPTION_MPTCP_MPC_ACK) & opts->suboptions) {
		u8 len, flag = MPTCP_CAP_HMAC_SHA256;

		if (OPTION_MPTCP_MPC_SYN & opts->suboptions) {
			len = TCPOLEN_MPTCP_MPC_SYN;
		} else if (OPTION_MPTCP_MPC_SYNACK & opts->suboptions) {
			len = TCPOLEN_MPTCP_MPC_SYNACK;
		} else if (opts->ext_copy.data_len) {
			len = TCPOLEN_MPTCP_MPC_ACK_DATA;
			if (opts->csum_reqd)
				len += TCPOLEN_MPTCP_DSS_CHECKSUM;
		} else {
			len = TCPOLEN_MPTCP_MPC_ACK;
		}

		if (opts->csum_reqd)
			flag |= MPTCP_CAP_CHECKSUM_REQD;

		if (!opts->allow_join_id0)
			flag |= MPTCP_CAP_DENY_JOIN_ID0;

		*ptr++ = mptcp_option(MPTCPOPT_MP_CAPABLE, len,
				      MPTCP_SUPPORTED_VERSION,
				      flag);

		if (!((OPTION_MPTCP_MPC_SYNACK | OPTION_MPTCP_MPC_ACK) &
		    opts->suboptions))
			goto mp_capable_done;

		put_unaligned_be64(opts->sndr_key, ptr);
		ptr += 2;
		if (!((OPTION_MPTCP_MPC_ACK) & opts->suboptions))
			goto mp_capable_done;

		put_unaligned_be64(opts->rcvr_key, ptr);
		ptr += 2;
		if (!opts->ext_copy.data_len)
			goto mp_capable_done;

		if (opts->csum_reqd) {
			put_unaligned_be32(opts->ext_copy.data_len << 16 |
					   mptcp_make_csum(&opts->ext_copy), ptr);
		} else {
			put_unaligned_be32(opts->ext_copy.data_len << 16 |
					   TCPOPT_NOP << 8 | TCPOPT_NOP, ptr);
		}
		ptr += 1;

		/* MPC is additionally mutually exclusive with MP_PRIO */
		goto mp_capable_done;
	} else if (OPTION_MPTCP_MPJ_SYN & opts->suboptions) {
		*ptr++ = mptcp_option(MPTCPOPT_MP_JOIN,
				      TCPOLEN_MPTCP_MPJ_SYN,
				      opts->backup, opts->join_id);
		put_unaligned_be32(opts->token, ptr);
		ptr += 1;
		put_unaligned_be32(opts->nonce, ptr);
		ptr += 1;
	} else if (OPTION_MPTCP_MPJ_SYNACK & opts->suboptions) {
		*ptr++ = mptcp_option(MPTCPOPT_MP_JOIN,
				      TCPOLEN_MPTCP_MPJ_SYNACK,
				      opts->backup, opts->join_id);
		put_unaligned_be64(opts->thmac, ptr);
		ptr += 2;
		put_unaligned_be32(opts->nonce, ptr);
		ptr += 1;
	} else if (OPTION_MPTCP_MPJ_ACK & opts->suboptions) {
		*ptr++ = mptcp_option(MPTCPOPT_MP_JOIN,
				      TCPOLEN_MPTCP_MPJ_ACK, 0, 0);
		memcpy(ptr, opts->hmac, MPTCPOPT_HMAC_LEN);
		ptr += 5;
	} else if (OPTION_MPTCP_ADD_ADDR & opts->suboptions) {
		u8 len = TCPOLEN_MPTCP_ADD_ADDR_BASE;
		u8 echo = MPTCP_ADDR_ECHO;

#if IS_ENABLED(CONFIG_MPTCP_IPV6)
		if (opts->addr.family == AF_INET6)
			len = TCPOLEN_MPTCP_ADD_ADDR6_BASE;
#endif

		if (opts->addr.port)
			len += TCPOLEN_MPTCP_PORT_LEN;

		if (opts->ahmac) {
			len += sizeof(opts->ahmac);
			echo = 0;
		}

		*ptr++ = mptcp_option(MPTCPOPT_ADD_ADDR,
				      len, echo, opts->addr.id);
		if (opts->addr.family == AF_INET) {
			memcpy((u8 *)ptr, (u8 *)&opts->addr.addr.s_addr, 4);
			ptr += 1;
		}
#if IS_ENABLED(CONFIG_MPTCP_IPV6)
		else if (opts->addr.family == AF_INET6) {
			memcpy((u8 *)ptr, opts->addr.addr6.s6_addr, 16);
			ptr += 4;
		}
#endif

		if (!opts->addr.port) {
			if (opts->ahmac) {
				put_unaligned_be64(opts->ahmac, ptr);
				ptr += 2;
			}
		} else {
			u16 port = ntohs(opts->addr.port);

			if (opts->ahmac) {
				u8 *bptr = (u8 *)ptr;

				put_unaligned_be16(port, bptr);
				bptr += 2;
				put_unaligned_be64(opts->ahmac, bptr);
				bptr += 8;
				put_unaligned_be16(TCPOPT_NOP << 8 |
						   TCPOPT_NOP, bptr);

				ptr += 3;
			} else {
				put_unaligned_be32(port << 16 |
						   TCPOPT_NOP << 8 |
						   TCPOPT_NOP, ptr);
				ptr += 1;
			}
		}
	}

	if (OPTION_MPTCP_PRIO & opts->suboptions) {
		const struct sock *ssk = (const struct sock *)tp;
		struct mptcp_subflow_context *subflow;

		subflow = mptcp_subflow_ctx(ssk);
		subflow->send_mp_prio = 0;

		*ptr++ = mptcp_option(MPTCPOPT_MP_PRIO,
				      TCPOLEN_MPTCP_PRIO,
				      opts->backup, TCPOPT_NOP);
	}

mp_capable_done:
	if (OPTION_MPTCP_RM_ADDR & opts->suboptions) {
		u8 i = 1;

		*ptr++ = mptcp_option(MPTCPOPT_RM_ADDR,
				      TCPOLEN_MPTCP_RM_ADDR_BASE + opts->rm_list.nr,
				      0, opts->rm_list.ids[0]);

		while (i < opts->rm_list.nr) {
			u8 id1, id2, id3, id4;

			id1 = opts->rm_list.ids[i];
			id2 = i + 1 < opts->rm_list.nr ? opts->rm_list.ids[i + 1] : TCPOPT_NOP;
			id3 = i + 2 < opts->rm_list.nr ? opts->rm_list.ids[i + 2] : TCPOPT_NOP;
			id4 = i + 3 < opts->rm_list.nr ? opts->rm_list.ids[i + 3] : TCPOPT_NOP;
			put_unaligned_be32(id1 << 24 | id2 << 16 | id3 << 8 | id4, ptr);
			ptr += 1;
			i += 4;
		}
	}

	if (tp)
		mptcp_set_rwin(tp);
}

__be32 mptcp_get_reset_option(const struct sk_buff *skb)
{
	const struct mptcp_ext *ext = mptcp_get_ext(skb);
	u8 flags, reason;

	if (ext) {
		flags = ext->reset_transient;
		reason = ext->reset_reason;

		return mptcp_option(MPTCPOPT_RST, TCPOLEN_MPTCP_RST,
				    flags, reason);
	}

	return htonl(0u);
}
EXPORT_SYMBOL_GPL(mptcp_get_reset_option);
