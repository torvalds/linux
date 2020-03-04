// SPDX-License-Identifier: GPL-2.0
/* Multipath TCP
 *
 * Copyright (c) 2017 - 2019, Intel Corporation.
 */

#include <linux/kernel.h>
#include <net/tcp.h>
#include <net/mptcp.h>
#include "protocol.h"

static bool mptcp_cap_flag_sha256(u8 flags)
{
	return (flags & MPTCP_CAP_FLAG_MASK) == MPTCP_CAP_HMAC_SHA256;
}

void mptcp_parse_option(const struct sk_buff *skb, const unsigned char *ptr,
			int opsize, struct tcp_options_received *opt_rx)
{
	struct mptcp_options_received *mp_opt = &opt_rx->mptcp;
	u8 subtype = *ptr >> 4;
	int expected_opsize;
	u8 version;
	u8 flags;

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
		if (opsize != expected_opsize)
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
		 *
		 * Section 3.3.0:
		 * "If a checksum is not present when its use has been
		 * negotiated, the receiver MUST close the subflow with a RST as
		 * it is considered broken."
		 *
		 * We don't implement DSS checksum - fall back to TCP.
		 */
		if (flags & MPTCP_CAP_CHECKSUM_REQD)
			break;

		mp_opt->mp_capable = 1;
		if (opsize >= TCPOLEN_MPTCP_MPC_SYNACK) {
			mp_opt->sndr_key = get_unaligned_be64(ptr);
			ptr += 8;
		}
		if (opsize >= TCPOLEN_MPTCP_MPC_ACK) {
			mp_opt->rcvr_key = get_unaligned_be64(ptr);
			ptr += 8;
		}
		if (opsize == TCPOLEN_MPTCP_MPC_ACK_DATA) {
			/* Section 3.1.:
			 * "the data parameters in a MP_CAPABLE are semantically
			 * equivalent to those in a DSS option and can be used
			 * interchangeably."
			 */
			mp_opt->dss = 1;
			mp_opt->use_map = 1;
			mp_opt->mpc_map = 1;
			mp_opt->data_len = get_unaligned_be16(ptr);
			ptr += 2;
		}
		pr_debug("MP_CAPABLE version=%x, flags=%x, optlen=%d sndr=%llu, rcvr=%llu len=%d",
			 version, flags, opsize, mp_opt->sndr_key,
			 mp_opt->rcvr_key, mp_opt->data_len);
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

		/* RFC 6824, Section 3.3:
		 * If a checksum is present, but its use had
		 * not been negotiated in the MP_CAPABLE handshake,
		 * the checksum field MUST be ignored.
		 */
		if (opsize != expected_opsize &&
		    opsize != expected_opsize + TCPOLEN_MPTCP_DSS_CHECKSUM)
			break;

		mp_opt->dss = 1;

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

			pr_debug("data_seq=%llu subflow_seq=%u data_len=%u",
				 mp_opt->data_seq, mp_opt->subflow_seq,
				 mp_opt->data_len);
		}

		break;

	default:
		break;
	}
}

void mptcp_get_options(const struct sk_buff *skb,
		       struct tcp_options_received *opt_rx)
{
	const unsigned char *ptr;
	const struct tcphdr *th = tcp_hdr(skb);
	int length = (th->doff * 4) - sizeof(struct tcphdr);

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
			opsize = *ptr++;
			if (opsize < 2) /* "silly options" */
				return;
			if (opsize > length)
				return;	/* don't parse partial options */
			if (opcode == TCPOPT_MPTCP)
				mptcp_parse_option(skb, ptr, opsize, opt_rx);
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
		pr_debug("local_key=%llu", subflow->local_key);
		opts->suboptions = OPTION_MPTCP_MPC_SYN;
		opts->sndr_key = subflow->local_key;
		*size = TCPOLEN_MPTCP_MPC_SYN;
		return true;
	}
	return false;
}

void mptcp_rcv_synsent(struct sock *sk)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(sk);
	struct tcp_sock *tp = tcp_sk(sk);

	pr_debug("subflow=%p", subflow);
	if (subflow->request_mptcp && tp->rx_opt.mptcp.mp_capable) {
		subflow->mp_capable = 1;
		subflow->can_ack = 1;
		subflow->remote_key = tp->rx_opt.mptcp.sndr_key;
	} else {
		tcp_sk(sk)->is_mptcp = 0;
	}
}

static bool mptcp_established_options_mp(struct sock *sk, struct sk_buff *skb,
					 unsigned int *size,
					 unsigned int remaining,
					 struct mptcp_out_options *opts)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(sk);
	struct mptcp_ext *mpext;
	unsigned int data_len;

	pr_debug("subflow=%p fourth_ack=%d seq=%x:%x remaining=%d", subflow,
		 subflow->fourth_ack, subflow->snd_isn,
		 skb ? TCP_SKB_CB(skb)->seq : 0, remaining);

	if (subflow->mp_capable && !subflow->fourth_ack && skb &&
	    subflow->snd_isn == TCP_SKB_CB(skb)->seq) {
		/* When skb is not available, we better over-estimate the
		 * emitted options len. A full DSS option is longer than
		 * TCPOLEN_MPTCP_MPC_ACK_DATA, so let's the caller try to fit
		 * that.
		 */
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

		/* Section 3.1.
		 * The MP_CAPABLE option is carried on the SYN, SYN/ACK, and ACK
		 * packets that start the first subflow of an MPTCP connection,
		 * as well as the first packet that carries data
		 */
		if (data_len > 0)
			*size = ALIGN(TCPOLEN_MPTCP_MPC_ACK_DATA, 4);
		else
			*size = TCPOLEN_MPTCP_MPC_ACK;

		pr_debug("subflow=%p, local_key=%llu, remote_key=%llu map_len=%d",
			 subflow, subflow->local_key, subflow->remote_key,
			 data_len);

		return true;
	}
	return false;
}

static void mptcp_write_data_fin(struct mptcp_subflow_context *subflow,
				 struct mptcp_ext *ext)
{
	if (!ext->use_map) {
		/* RFC6824 requires a DSS mapping with specific values
		 * if DATA_FIN is set but no data payload is mapped
		 */
		ext->data_fin = 1;
		ext->use_map = 1;
		ext->dsn64 = 1;
		ext->data_seq = subflow->data_fin_tx_seq;
		ext->subflow_seq = 0;
		ext->data_len = 1;
	} else if (ext->data_seq + ext->data_len == subflow->data_fin_tx_seq) {
		/* If there's an existing DSS mapping and it is the
		 * final mapping, DATA_FIN consumes 1 additional byte of
		 * mapping space.
		 */
		ext->data_fin = 1;
		ext->data_len++;
	}
}

static bool mptcp_established_options_dss(struct sock *sk, struct sk_buff *skb,
					  unsigned int *size,
					  unsigned int remaining,
					  struct mptcp_out_options *opts)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(sk);
	unsigned int dss_size = 0;
	struct mptcp_ext *mpext;
	struct mptcp_sock *msk;
	unsigned int ack_size;
	bool ret = false;
	u8 tcp_fin;

	if (skb) {
		mpext = mptcp_get_ext(skb);
		tcp_fin = TCP_SKB_CB(skb)->tcp_flags & TCPHDR_FIN;
	} else {
		mpext = NULL;
		tcp_fin = 0;
	}

	if (!skb || (mpext && mpext->use_map) || tcp_fin) {
		unsigned int map_size;

		map_size = TCPOLEN_MPTCP_DSS_BASE + TCPOLEN_MPTCP_DSS_MAP64;

		remaining -= map_size;
		dss_size = map_size;
		if (mpext)
			opts->ext_copy = *mpext;

		if (skb && tcp_fin && subflow->data_fin_tx_enable)
			mptcp_write_data_fin(subflow, &opts->ext_copy);
		ret = true;
	}

	opts->ext_copy.use_ack = 0;
	msk = mptcp_sk(subflow->conn);
	if (!msk || !READ_ONCE(msk->can_ack)) {
		*size = ALIGN(dss_size, 4);
		return ret;
	}

	ack_size = TCPOLEN_MPTCP_DSS_ACK64;

	/* Add kind/length/subtype/flag overhead if mapping is not populated */
	if (dss_size == 0)
		ack_size += TCPOLEN_MPTCP_DSS_BASE;

	dss_size += ack_size;

	opts->ext_copy.data_ack = msk->ack_seq;
	opts->ext_copy.ack64 = 1;
	opts->ext_copy.use_ack = 1;

	*size = ALIGN(dss_size, 4);
	return true;
}

bool mptcp_established_options(struct sock *sk, struct sk_buff *skb,
			       unsigned int *size, unsigned int remaining,
			       struct mptcp_out_options *opts)
{
	unsigned int opt_size = 0;
	bool ret = false;

	if (mptcp_established_options_mp(sk, skb, &opt_size, remaining, opts))
		ret = true;
	else if (mptcp_established_options_dss(sk, skb, &opt_size, remaining,
					       opts))
		ret = true;

	/* we reserved enough space for the above options, and exceeding the
	 * TCP option space would be fatal
	 */
	if (WARN_ON_ONCE(opt_size > remaining))
		return false;

	*size += opt_size;
	remaining -= opt_size;

	return ret;
}

bool mptcp_synack_options(const struct request_sock *req, unsigned int *size,
			  struct mptcp_out_options *opts)
{
	struct mptcp_subflow_request_sock *subflow_req = mptcp_subflow_rsk(req);

	if (subflow_req->mp_capable) {
		opts->suboptions = OPTION_MPTCP_MPC_SYNACK;
		opts->sndr_key = subflow_req->local_key;
		*size = TCPOLEN_MPTCP_MPC_SYNACK;
		pr_debug("subflow_req=%p, local_key=%llu",
			 subflow_req, subflow_req->local_key);
		return true;
	}
	return false;
}

static bool check_fourth_ack(struct mptcp_subflow_context *subflow,
			     struct sk_buff *skb,
			     struct mptcp_options_received *mp_opt)
{
	/* here we can process OoO, in-window pkts, only in-sequence 4th ack
	 * are relevant
	 */
	if (likely(subflow->fourth_ack ||
		   TCP_SKB_CB(skb)->seq != subflow->ssn_offset + 1))
		return true;

	if (mp_opt->use_ack)
		subflow->fourth_ack = 1;

	if (subflow->can_ack)
		return true;

	/* If the first established packet does not contain MP_CAPABLE + data
	 * then fallback to TCP
	 */
	if (!mp_opt->mp_capable) {
		subflow->mp_capable = 0;
		tcp_sk(mptcp_subflow_tcp_sock(subflow))->is_mptcp = 0;
		return false;
	}
	subflow->remote_key = mp_opt->sndr_key;
	subflow->can_ack = 1;
	return true;
}

void mptcp_incoming_options(struct sock *sk, struct sk_buff *skb,
			    struct tcp_options_received *opt_rx)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(sk);
	struct mptcp_options_received *mp_opt;
	struct mptcp_ext *mpext;

	mp_opt = &opt_rx->mptcp;
	if (!check_fourth_ack(subflow, skb, mp_opt))
		return;

	if (!mp_opt->dss)
		return;

	mpext = skb_ext_add(skb, SKB_EXT_MPTCP);
	if (!mpext)
		return;

	memset(mpext, 0, sizeof(*mpext));

	if (mp_opt->use_map) {
		if (mp_opt->mpc_map) {
			/* this is an MP_CAPABLE carrying MPTCP data
			 * we know this map the first chunk of data
			 */
			mptcp_crypto_key_sha(subflow->remote_key, NULL,
					     &mpext->data_seq);
			mpext->data_seq++;
			mpext->subflow_seq = 1;
			mpext->dsn64 = 1;
			mpext->mpc_map = 1;
		} else {
			mpext->data_seq = mp_opt->data_seq;
			mpext->subflow_seq = mp_opt->subflow_seq;
			mpext->dsn64 = mp_opt->dsn64;
		}
		mpext->data_len = mp_opt->data_len;
		mpext->use_map = 1;
	}

	if (mp_opt->use_ack) {
		mpext->data_ack = mp_opt->data_ack;
		mpext->use_ack = 1;
		mpext->ack64 = mp_opt->ack64;
	}

	mpext->data_fin = mp_opt->data_fin;
}

void mptcp_write_options(__be32 *ptr, struct mptcp_out_options *opts)
{
	if ((OPTION_MPTCP_MPC_SYN | OPTION_MPTCP_MPC_SYNACK |
	     OPTION_MPTCP_MPC_ACK) & opts->suboptions) {
		u8 len;

		if (OPTION_MPTCP_MPC_SYN & opts->suboptions)
			len = TCPOLEN_MPTCP_MPC_SYN;
		else if (OPTION_MPTCP_MPC_SYNACK & opts->suboptions)
			len = TCPOLEN_MPTCP_MPC_SYNACK;
		else if (opts->ext_copy.data_len)
			len = TCPOLEN_MPTCP_MPC_ACK_DATA;
		else
			len = TCPOLEN_MPTCP_MPC_ACK;

		*ptr++ = htonl((TCPOPT_MPTCP << 24) | (len << 16) |
			       (MPTCPOPT_MP_CAPABLE << 12) |
			       (MPTCP_SUPPORTED_VERSION << 8) |
			       MPTCP_CAP_HMAC_SHA256);

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

		put_unaligned_be32(opts->ext_copy.data_len << 16 |
				   TCPOPT_NOP << 8 | TCPOPT_NOP, ptr);
		ptr += 1;
	}

mp_capable_done:
	if (opts->ext_copy.use_ack || opts->ext_copy.use_map) {
		struct mptcp_ext *mpext = &opts->ext_copy;
		u8 len = TCPOLEN_MPTCP_DSS_BASE;
		u8 flags = 0;

		if (mpext->use_ack) {
			len += TCPOLEN_MPTCP_DSS_ACK64;
			flags = MPTCP_DSS_HAS_ACK | MPTCP_DSS_ACK64;
		}

		if (mpext->use_map) {
			len += TCPOLEN_MPTCP_DSS_MAP64;

			/* Use only 64-bit mapping flags for now, add
			 * support for optional 32-bit mappings later.
			 */
			flags |= MPTCP_DSS_HAS_MAP | MPTCP_DSS_DSN64;
			if (mpext->data_fin)
				flags |= MPTCP_DSS_DATA_FIN;
		}

		*ptr++ = htonl((TCPOPT_MPTCP << 24) |
			       (len  << 16) |
			       (MPTCPOPT_DSS << 12) |
			       (flags));

		if (mpext->use_ack) {
			put_unaligned_be64(mpext->data_ack, ptr);
			ptr += 2;
		}

		if (mpext->use_map) {
			put_unaligned_be64(mpext->data_seq, ptr);
			ptr += 2;
			put_unaligned_be32(mpext->subflow_seq, ptr);
			ptr += 1;
			put_unaligned_be32(mpext->data_len << 16 |
					   TCPOPT_NOP << 8 | TCPOPT_NOP, ptr);
		}
	}
}
