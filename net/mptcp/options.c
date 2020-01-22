// SPDX-License-Identifier: GPL-2.0
/* Multipath TCP
 *
 * Copyright (c) 2017 - 2019, Intel Corporation.
 */

#include <linux/kernel.h>
#include <net/tcp.h>
#include <net/mptcp.h>
#include "protocol.h"

void mptcp_parse_option(const unsigned char *ptr, int opsize,
			struct tcp_options_received *opt_rx)
{
	struct mptcp_options_received *mp_opt = &opt_rx->mptcp;
	u8 subtype = *ptr >> 4;
	u8 version;
	u8 flags;

	switch (subtype) {
	case MPTCPOPT_MP_CAPABLE:
		if (opsize != TCPOLEN_MPTCP_MPC_SYN &&
		    opsize != TCPOLEN_MPTCP_MPC_ACK)
			break;

		version = *ptr++ & MPTCP_VERSION_MASK;
		if (version != MPTCP_SUPPORTED_VERSION)
			break;

		flags = *ptr++;
		if (!((flags & MPTCP_CAP_FLAG_MASK) == MPTCP_CAP_HMAC_SHA1) ||
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
		mp_opt->sndr_key = get_unaligned_be64(ptr);
		ptr += 8;

		if (opsize == TCPOLEN_MPTCP_MPC_ACK) {
			mp_opt->rcvr_key = get_unaligned_be64(ptr);
			ptr += 8;
			pr_debug("MP_CAPABLE sndr=%llu, rcvr=%llu",
				 mp_opt->sndr_key, mp_opt->rcvr_key);
		} else {
			pr_debug("MP_CAPABLE sndr=%llu", mp_opt->sndr_key);
		}
		break;

	case MPTCPOPT_DSS:
		pr_debug("DSS");
		mp_opt->dss = 1;
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
				mptcp_parse_option(ptr, opsize, opt_rx);
			ptr += opsize - 2;
			length -= opsize;
		}
	}
}

bool mptcp_syn_options(struct sock *sk, unsigned int *size,
		       struct mptcp_out_options *opts)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(sk);

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
		subflow->remote_key = tp->rx_opt.mptcp.sndr_key;
	} else {
		tcp_sk(sk)->is_mptcp = 0;
	}
}

bool mptcp_established_options(struct sock *sk, struct sk_buff *skb,
			       unsigned int *size, unsigned int remaining,
			       struct mptcp_out_options *opts)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(sk);

	if (subflow->mp_capable && !subflow->fourth_ack) {
		opts->suboptions = OPTION_MPTCP_MPC_ACK;
		opts->sndr_key = subflow->local_key;
		opts->rcvr_key = subflow->remote_key;
		*size = TCPOLEN_MPTCP_MPC_ACK;
		subflow->fourth_ack = 1;
		pr_debug("subflow=%p, local_key=%llu, remote_key=%llu",
			 subflow, subflow->local_key, subflow->remote_key);
		return true;
	}
	return false;
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

void mptcp_write_options(__be32 *ptr, struct mptcp_out_options *opts)
{
	if ((OPTION_MPTCP_MPC_SYN |
	     OPTION_MPTCP_MPC_SYNACK |
	     OPTION_MPTCP_MPC_ACK) & opts->suboptions) {
		u8 len;

		if (OPTION_MPTCP_MPC_SYN & opts->suboptions)
			len = TCPOLEN_MPTCP_MPC_SYN;
		else if (OPTION_MPTCP_MPC_SYNACK & opts->suboptions)
			len = TCPOLEN_MPTCP_MPC_SYNACK;
		else
			len = TCPOLEN_MPTCP_MPC_ACK;

		*ptr++ = htonl((TCPOPT_MPTCP << 24) | (len << 16) |
			       (MPTCPOPT_MP_CAPABLE << 12) |
			       (MPTCP_SUPPORTED_VERSION << 8) |
			       MPTCP_CAP_HMAC_SHA1);
		put_unaligned_be64(opts->sndr_key, ptr);
		ptr += 2;
		if (OPTION_MPTCP_MPC_ACK & opts->suboptions) {
			put_unaligned_be64(opts->rcvr_key, ptr);
			ptr += 2;
		}
	}
}
