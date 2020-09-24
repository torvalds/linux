// SPDX-License-Identifier: GPL-2.0
/* Multipath TCP
 *
 * Copyright (c) 2017 - 2019, Intel Corporation.
 */

#define pr_fmt(fmt) "MPTCP: " fmt

#include <linux/kernel.h>
#include <crypto/sha.h>
#include <net/tcp.h>
#include <net/mptcp.h>
#include "protocol.h"
#include "mib.h"

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

	case MPTCPOPT_MP_JOIN:
		mp_opt->mp_join = 1;
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
			pr_warn("MP_JOIN bad option size");
			mp_opt->mp_join = 0;
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

	case MPTCPOPT_ADD_ADDR:
		mp_opt->echo = (*ptr++) & MPTCP_ADDR_ECHO;
		if (!mp_opt->echo) {
			if (opsize == TCPOLEN_MPTCP_ADD_ADDR ||
			    opsize == TCPOLEN_MPTCP_ADD_ADDR_PORT)
				mp_opt->family = MPTCP_ADDR_IPVERSION_4;
#if IS_ENABLED(CONFIG_MPTCP_IPV6)
			else if (opsize == TCPOLEN_MPTCP_ADD_ADDR6 ||
				 opsize == TCPOLEN_MPTCP_ADD_ADDR6_PORT)
				mp_opt->family = MPTCP_ADDR_IPVERSION_6;
#endif
			else
				break;
		} else {
			if (opsize == TCPOLEN_MPTCP_ADD_ADDR_BASE ||
			    opsize == TCPOLEN_MPTCP_ADD_ADDR_BASE_PORT)
				mp_opt->family = MPTCP_ADDR_IPVERSION_4;
#if IS_ENABLED(CONFIG_MPTCP_IPV6)
			else if (opsize == TCPOLEN_MPTCP_ADD_ADDR6_BASE ||
				 opsize == TCPOLEN_MPTCP_ADD_ADDR6_BASE_PORT)
				mp_opt->family = MPTCP_ADDR_IPVERSION_6;
#endif
			else
				break;
		}

		mp_opt->add_addr = 1;
		mp_opt->port = 0;
		mp_opt->addr_id = *ptr++;
		pr_debug("ADD_ADDR: id=%d, echo=%d", mp_opt->addr_id, mp_opt->echo);
		if (mp_opt->family == MPTCP_ADDR_IPVERSION_4) {
			memcpy((u8 *)&mp_opt->addr.s_addr, (u8 *)ptr, 4);
			ptr += 4;
			if (opsize == TCPOLEN_MPTCP_ADD_ADDR_PORT ||
			    opsize == TCPOLEN_MPTCP_ADD_ADDR_BASE_PORT) {
				mp_opt->port = get_unaligned_be16(ptr);
				ptr += 2;
			}
		}
#if IS_ENABLED(CONFIG_MPTCP_IPV6)
		else {
			memcpy(mp_opt->addr6.s6_addr, (u8 *)ptr, 16);
			ptr += 16;
			if (opsize == TCPOLEN_MPTCP_ADD_ADDR6_PORT ||
			    opsize == TCPOLEN_MPTCP_ADD_ADDR6_BASE_PORT) {
				mp_opt->port = get_unaligned_be16(ptr);
				ptr += 2;
			}
		}
#endif
		if (!mp_opt->echo) {
			mp_opt->ahmac = get_unaligned_be64(ptr);
			ptr += 8;
		}
		break;

	case MPTCPOPT_RM_ADDR:
		if (opsize != TCPOLEN_MPTCP_RM_ADDR_BASE)
			break;

		ptr++;

		mp_opt->rm_addr = 1;
		mp_opt->rm_id = *ptr++;
		pr_debug("RM_ADDR: id=%d", mp_opt->rm_id);
		break;

	default:
		break;
	}
}

void mptcp_get_options(const struct sk_buff *skb,
		       struct mptcp_options_received *mp_opt)
{
	const struct tcphdr *th = tcp_hdr(skb);
	const unsigned char *ptr;
	int length;

	/* initialize option status */
	mp_opt->mp_capable = 0;
	mp_opt->mp_join = 0;
	mp_opt->add_addr = 0;
	mp_opt->rm_addr = 0;
	mp_opt->dss = 0;

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
					 unsigned int *size,
					 unsigned int remaining,
					 struct mptcp_out_options *opts)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(sk);
	struct mptcp_ext *mpext;
	unsigned int data_len;

	/* When skb is not available, we better over-estimate the emitted
	 * options len. A full DSS option (28 bytes) is longer than
	 * TCPOLEN_MPTCP_MPC_ACK_DATA(22) or TCPOLEN_MPTCP_MPJ_ACK(24), so
	 * tell the caller to defer the estimate to
	 * mptcp_established_options_dss(), which will reserve enough space.
	 */
	if (!skb)
		return false;

	/* MPC/MPJ needed only on 3rd ack packet */
	if (subflow->fully_established ||
	    subflow->snd_isn != TCP_SKB_CB(skb)->seq)
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
	u64 data_fin_tx_seq = READ_ONCE(mptcp_sk(subflow->conn)->write_seq);

	if (!ext->use_map || !skb->len) {
		/* RFC6824 requires a DSS mapping with specific values
		 * if DATA_FIN is set but no data payload is mapped
		 */
		ext->data_fin = 1;
		ext->use_map = 1;
		ext->dsn64 = 1;
		/* The write_seq value has already been incremented, so
		 * the actual sequence number for the DATA_FIN is one less.
		 */
		ext->data_seq = data_fin_tx_seq - 1;
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
					  unsigned int *size,
					  unsigned int remaining,
					  struct mptcp_out_options *opts)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(sk);
	struct mptcp_sock *msk = mptcp_sk(subflow->conn);
	unsigned int dss_size = 0;
	u64 snd_data_fin_enable;
	struct mptcp_ext *mpext;
	unsigned int ack_size;
	bool ret = false;

	mpext = skb ? mptcp_get_ext(skb) : NULL;
	snd_data_fin_enable = READ_ONCE(msk->snd_data_fin_enable);

	if (!skb || (mpext && mpext->use_map) || snd_data_fin_enable) {
		unsigned int map_size;

		map_size = TCPOLEN_MPTCP_DSS_BASE + TCPOLEN_MPTCP_DSS_MAP64;

		remaining -= map_size;
		dss_size = map_size;
		if (mpext)
			opts->ext_copy = *mpext;

		if (skb && snd_data_fin_enable)
			mptcp_write_data_fin(subflow, skb, &opts->ext_copy);
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

	if (subflow->use_64bit_ack) {
		ack_size = TCPOLEN_MPTCP_DSS_ACK64;
		opts->ext_copy.data_ack = msk->ack_seq;
		opts->ext_copy.ack64 = 1;
	} else {
		ack_size = TCPOLEN_MPTCP_DSS_ACK32;
		opts->ext_copy.data_ack32 = (uint32_t)(msk->ack_seq);
		opts->ext_copy.ack64 = 0;
	}
	opts->ext_copy.use_ack = 1;

	/* Add kind/length/subtype/flag overhead if mapping is not populated */
	if (dss_size == 0)
		ack_size += TCPOLEN_MPTCP_DSS_BASE;

	dss_size += ack_size;

	*size = ALIGN(dss_size, 4);
	return true;
}

static u64 add_addr_generate_hmac(u64 key1, u64 key2, u8 addr_id,
				  struct in_addr *addr)
{
	u8 hmac[SHA256_DIGEST_SIZE];
	u8 msg[7];

	msg[0] = addr_id;
	memcpy(&msg[1], &addr->s_addr, 4);
	msg[5] = 0;
	msg[6] = 0;

	mptcp_crypto_hmac_sha(key1, key2, msg, 7, hmac);

	return get_unaligned_be64(&hmac[SHA256_DIGEST_SIZE - sizeof(u64)]);
}

#if IS_ENABLED(CONFIG_MPTCP_IPV6)
static u64 add_addr6_generate_hmac(u64 key1, u64 key2, u8 addr_id,
				   struct in6_addr *addr)
{
	u8 hmac[SHA256_DIGEST_SIZE];
	u8 msg[19];

	msg[0] = addr_id;
	memcpy(&msg[1], &addr->s6_addr, 16);
	msg[17] = 0;
	msg[18] = 0;

	mptcp_crypto_hmac_sha(key1, key2, msg, 19, hmac);

	return get_unaligned_be64(&hmac[SHA256_DIGEST_SIZE - sizeof(u64)]);
}
#endif

static bool mptcp_established_options_add_addr(struct sock *sk,
					       unsigned int *size,
					       unsigned int remaining,
					       struct mptcp_out_options *opts)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(sk);
	struct mptcp_sock *msk = mptcp_sk(subflow->conn);
	struct mptcp_addr_info saddr;
	bool echo;
	int len;

	if (!mptcp_pm_should_add_signal(msk) ||
	    !(mptcp_pm_add_addr_signal(msk, remaining, &saddr, &echo)))
		return false;

	len = mptcp_add_addr_len(saddr.family);
	if (remaining < len)
		return false;

	*size = len;
	opts->addr_id = saddr.id;
	if (saddr.family == AF_INET) {
		opts->suboptions |= OPTION_MPTCP_ADD_ADDR;
		opts->addr = saddr.addr;
		if (!echo) {
			opts->ahmac = add_addr_generate_hmac(msk->local_key,
							     msk->remote_key,
							     opts->addr_id,
							     &opts->addr);
		}
	}
#if IS_ENABLED(CONFIG_MPTCP_IPV6)
	else if (saddr.family == AF_INET6) {
		opts->suboptions |= OPTION_MPTCP_ADD_ADDR6;
		opts->addr6 = saddr.addr6;
		if (!echo) {
			opts->ahmac = add_addr6_generate_hmac(msk->local_key,
							      msk->remote_key,
							      opts->addr_id,
							      &opts->addr6);
		}
	}
#endif
	pr_debug("addr_id=%d, ahmac=%llu, echo=%d", opts->addr_id, opts->ahmac, echo);

	return true;
}

static bool mptcp_established_options_rm_addr(struct sock *sk,
					      unsigned int *size,
					      unsigned int remaining,
					      struct mptcp_out_options *opts)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(sk);
	struct mptcp_sock *msk = mptcp_sk(subflow->conn);
	u8 rm_id;

	if (!mptcp_pm_should_rm_signal(msk) ||
	    !(mptcp_pm_rm_addr_signal(msk, remaining, &rm_id)))
		return false;

	if (remaining < TCPOLEN_MPTCP_RM_ADDR_BASE)
		return false;

	*size = TCPOLEN_MPTCP_RM_ADDR_BASE;
	opts->suboptions |= OPTION_MPTCP_RM_ADDR;
	opts->rm_id = rm_id;

	pr_debug("rm_id=%d", opts->rm_id);

	return true;
}

bool mptcp_established_options(struct sock *sk, struct sk_buff *skb,
			       unsigned int *size, unsigned int remaining,
			       struct mptcp_out_options *opts)
{
	unsigned int opt_size = 0;
	bool ret = false;

	opts->suboptions = 0;

	if (unlikely(mptcp_check_fallback(sk)))
		return false;

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
	if (mptcp_established_options_add_addr(sk, &opt_size, remaining, opts)) {
		*size += opt_size;
		remaining -= opt_size;
		ret = true;
	} else if (mptcp_established_options_rm_addr(sk, &opt_size, remaining, opts)) {
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

static bool check_fully_established(struct mptcp_sock *msk, struct sock *sk,
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
		    subflow->mp_join && mp_opt->mp_join &&
		    READ_ONCE(msk->pm.server_side))
			tcp_send_ack(sk);
		goto fully_established;
	}

	/* we should process OoO packets before the first subflow is fully
	 * established, but not expected for MP_JOIN subflows
	 */
	if (TCP_SKB_CB(skb)->seq != subflow->ssn_offset + 1)
		return subflow->mp_capable;

	if (mp_opt->dss && mp_opt->use_ack) {
		/* subflows are fully established as soon as we get any
		 * additional ack.
		 */
		subflow->fully_established = 1;
		WRITE_ONCE(msk->fully_established, true);
		goto fully_established;
	}

	/* If the first established packet does not contain MP_CAPABLE + data
	 * then fallback to TCP
	 */
	if (!mp_opt->mp_capable) {
		subflow->mp_capable = 0;
		pr_fallback(msk);
		__mptcp_do_fallback(msk);
		return false;
	}

	if (unlikely(!READ_ONCE(msk->pm.server_side)))
		pr_warn_once("bogus mpc option on established client sk");
	mptcp_subflow_fully_established(subflow, mp_opt);

fully_established:
	if (likely(subflow->pm_notified))
		return true;

	subflow->pm_notified = 1;
	if (subflow->mp_join) {
		clear_3rdack_retransmission(sk);
		mptcp_pm_subflow_established(msk, subflow);
	} else {
		mptcp_pm_fully_established(msk);
	}
	return true;
}

static u64 expand_ack(u64 old_ack, u64 cur_ack, bool use_64bit)
{
	u32 old_ack32, cur_ack32;

	if (use_64bit)
		return cur_ack;

	old_ack32 = (u32)old_ack;
	cur_ack32 = (u32)cur_ack;
	cur_ack = (old_ack & GENMASK_ULL(63, 32)) + cur_ack32;
	if (unlikely(before(cur_ack32, old_ack32)))
		return cur_ack + (1LL << 32);
	return cur_ack;
}

static void update_una(struct mptcp_sock *msk,
		       struct mptcp_options_received *mp_opt)
{
	u64 new_snd_una, snd_una, old_snd_una = atomic64_read(&msk->snd_una);
	u64 write_seq = READ_ONCE(msk->write_seq);

	/* avoid ack expansion on update conflict, to reduce the risk of
	 * wrongly expanding to a future ack sequence number, which is way
	 * more dangerous than missing an ack
	 */
	new_snd_una = expand_ack(old_snd_una, mp_opt->data_ack, mp_opt->ack64);

	/* ACK for data not even sent yet? Ignore. */
	if (after64(new_snd_una, write_seq))
		new_snd_una = old_snd_una;

	while (after64(new_snd_una, old_snd_una)) {
		snd_una = old_snd_una;
		old_snd_una = atomic64_cmpxchg(&msk->snd_una, snd_una,
					       new_snd_una);
		if (old_snd_una == snd_una) {
			mptcp_data_acked((struct sock *)msk);
			break;
		}
	}
}

bool mptcp_update_rcv_data_fin(struct mptcp_sock *msk, u64 data_fin_seq)
{
	/* Skip if DATA_FIN was already received.
	 * If updating simultaneously with the recvmsg loop, values
	 * should match. If they mismatch, the peer is misbehaving and
	 * we will prefer the most recent information.
	 */
	if (READ_ONCE(msk->rcv_data_fin) || !READ_ONCE(msk->first))
		return false;

	WRITE_ONCE(msk->rcv_data_fin_seq, data_fin_seq);
	WRITE_ONCE(msk->rcv_data_fin, 1);

	return true;
}

static bool add_addr_hmac_valid(struct mptcp_sock *msk,
				struct mptcp_options_received *mp_opt)
{
	u64 hmac = 0;

	if (mp_opt->echo)
		return true;

	if (mp_opt->family == MPTCP_ADDR_IPVERSION_4)
		hmac = add_addr_generate_hmac(msk->remote_key,
					      msk->local_key,
					      mp_opt->addr_id, &mp_opt->addr);
#if IS_ENABLED(CONFIG_MPTCP_IPV6)
	else
		hmac = add_addr6_generate_hmac(msk->remote_key,
					       msk->local_key,
					       mp_opt->addr_id, &mp_opt->addr6);
#endif

	pr_debug("msk=%p, ahmac=%llu, mp_opt->ahmac=%llu\n",
		 msk, (unsigned long long)hmac,
		 (unsigned long long)mp_opt->ahmac);

	return hmac == mp_opt->ahmac;
}

void mptcp_incoming_options(struct sock *sk, struct sk_buff *skb)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(sk);
	struct mptcp_sock *msk = mptcp_sk(subflow->conn);
	struct mptcp_options_received mp_opt;
	struct mptcp_ext *mpext;

	if (__mptcp_check_fallback(msk))
		return;

	mptcp_get_options(skb, &mp_opt);
	if (!check_fully_established(msk, sk, subflow, skb, &mp_opt))
		return;

	if (mp_opt.add_addr && add_addr_hmac_valid(msk, &mp_opt)) {
		struct mptcp_addr_info addr;

		addr.port = htons(mp_opt.port);
		addr.id = mp_opt.addr_id;
		if (mp_opt.family == MPTCP_ADDR_IPVERSION_4) {
			addr.family = AF_INET;
			addr.addr = mp_opt.addr;
		}
#if IS_ENABLED(CONFIG_MPTCP_IPV6)
		else if (mp_opt.family == MPTCP_ADDR_IPVERSION_6) {
			addr.family = AF_INET6;
			addr.addr6 = mp_opt.addr6;
		}
#endif
		if (!mp_opt.echo) {
			mptcp_pm_add_addr_received(msk, &addr);
			MPTCP_INC_STATS(sock_net(sk), MPTCP_MIB_ADDADDR);
		} else {
			mptcp_pm_del_add_timer(msk, &addr);
			MPTCP_INC_STATS(sock_net(sk), MPTCP_MIB_ECHOADD);
		}
		mp_opt.add_addr = 0;
	}

	if (mp_opt.rm_addr) {
		mptcp_pm_rm_addr_received(msk, mp_opt.rm_id);
		mp_opt.rm_addr = 0;
	}

	if (!mp_opt.dss)
		return;

	/* we can't wait for recvmsg() to update the ack_seq, otherwise
	 * monodirectional flows will stuck
	 */
	if (mp_opt.use_ack)
		update_una(msk, &mp_opt);

	/* Zero-data-length packets are dropped by the caller and not
	 * propagated to the MPTCP layer, so the skb extension does not
	 * need to be allocated or populated. DATA_FIN information, if
	 * present, needs to be updated here before the skb is freed.
	 */
	if (TCP_SKB_CB(skb)->seq == TCP_SKB_CB(skb)->end_seq) {
		if (mp_opt.data_fin && mp_opt.data_len == 1 &&
		    mptcp_update_rcv_data_fin(msk, mp_opt.data_seq) &&
		    schedule_work(&msk->work))
			sock_hold(subflow->conn);

		return;
	}

	mpext = skb_ext_add(skb, SKB_EXT_MPTCP);
	if (!mpext)
		return;

	memset(mpext, 0, sizeof(*mpext));

	if (mp_opt.use_map) {
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
	}
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

		*ptr++ = mptcp_option(MPTCPOPT_MP_CAPABLE, len,
				      MPTCP_SUPPORTED_VERSION,
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
	if (OPTION_MPTCP_ADD_ADDR & opts->suboptions) {
		if (opts->ahmac)
			*ptr++ = mptcp_option(MPTCPOPT_ADD_ADDR,
					      TCPOLEN_MPTCP_ADD_ADDR, 0,
					      opts->addr_id);
		else
			*ptr++ = mptcp_option(MPTCPOPT_ADD_ADDR,
					      TCPOLEN_MPTCP_ADD_ADDR_BASE,
					      MPTCP_ADDR_ECHO,
					      opts->addr_id);
		memcpy((u8 *)ptr, (u8 *)&opts->addr.s_addr, 4);
		ptr += 1;
		if (opts->ahmac) {
			put_unaligned_be64(opts->ahmac, ptr);
			ptr += 2;
		}
	}

#if IS_ENABLED(CONFIG_MPTCP_IPV6)
	if (OPTION_MPTCP_ADD_ADDR6 & opts->suboptions) {
		if (opts->ahmac)
			*ptr++ = mptcp_option(MPTCPOPT_ADD_ADDR,
					      TCPOLEN_MPTCP_ADD_ADDR6, 0,
					      opts->addr_id);
		else
			*ptr++ = mptcp_option(MPTCPOPT_ADD_ADDR,
					      TCPOLEN_MPTCP_ADD_ADDR6_BASE,
					      MPTCP_ADDR_ECHO,
					      opts->addr_id);
		memcpy((u8 *)ptr, opts->addr6.s6_addr, 16);
		ptr += 4;
		if (opts->ahmac) {
			put_unaligned_be64(opts->ahmac, ptr);
			ptr += 2;
		}
	}
#endif

	if (OPTION_MPTCP_RM_ADDR & opts->suboptions) {
		*ptr++ = mptcp_option(MPTCPOPT_RM_ADDR,
				      TCPOLEN_MPTCP_RM_ADDR_BASE,
				      0, opts->rm_id);
	}

	if (OPTION_MPTCP_MPJ_SYN & opts->suboptions) {
		*ptr++ = mptcp_option(MPTCPOPT_MP_JOIN,
				      TCPOLEN_MPTCP_MPJ_SYN,
				      opts->backup, opts->join_id);
		put_unaligned_be32(opts->token, ptr);
		ptr += 1;
		put_unaligned_be32(opts->nonce, ptr);
		ptr += 1;
	}

	if (OPTION_MPTCP_MPJ_SYNACK & opts->suboptions) {
		*ptr++ = mptcp_option(MPTCPOPT_MP_JOIN,
				      TCPOLEN_MPTCP_MPJ_SYNACK,
				      opts->backup, opts->join_id);
		put_unaligned_be64(opts->thmac, ptr);
		ptr += 2;
		put_unaligned_be32(opts->nonce, ptr);
		ptr += 1;
	}

	if (OPTION_MPTCP_MPJ_ACK & opts->suboptions) {
		*ptr++ = mptcp_option(MPTCPOPT_MP_JOIN,
				      TCPOLEN_MPTCP_MPJ_ACK, 0, 0);
		memcpy(ptr, opts->hmac, MPTCPOPT_HMAC_LEN);
		ptr += 5;
	}

	if (opts->ext_copy.use_ack || opts->ext_copy.use_map) {
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
			put_unaligned_be32(mpext->data_len << 16 |
					   TCPOPT_NOP << 8 | TCPOPT_NOP, ptr);
		}
	}
}
