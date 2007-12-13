/*
 *  net/dccp/options.c
 *
 *  An implementation of the DCCP protocol
 *  Copyright (c) 2005 Aristeu Sergio Rozanski Filho <aris@cathedrallabs.org>
 *  Copyright (c) 2005 Arnaldo Carvalho de Melo <acme@ghostprotocols.net>
 *  Copyright (c) 2005 Ian McDonald <ian.mcdonald@jandi.co.nz>
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */
#include <linux/dccp.h>
#include <linux/module.h>
#include <linux/types.h>
#include <asm/unaligned.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>

#include "ackvec.h"
#include "ccid.h"
#include "dccp.h"
#include "feat.h"

int sysctl_dccp_feat_sequence_window = DCCPF_INITIAL_SEQUENCE_WINDOW;
int sysctl_dccp_feat_rx_ccid	      = DCCPF_INITIAL_CCID;
int sysctl_dccp_feat_tx_ccid	      = DCCPF_INITIAL_CCID;
int sysctl_dccp_feat_ack_ratio	      = DCCPF_INITIAL_ACK_RATIO;
int sysctl_dccp_feat_send_ack_vector = DCCPF_INITIAL_SEND_ACK_VECTOR;
int sysctl_dccp_feat_send_ndp_count  = DCCPF_INITIAL_SEND_NDP_COUNT;

static u32 dccp_decode_value_var(const unsigned char *bf, const u8 len)
{
	u32 value = 0;

	if (len > 3)
		value += *bf++ << 24;
	if (len > 2)
		value += *bf++ << 16;
	if (len > 1)
		value += *bf++ << 8;
	if (len > 0)
		value += *bf;

	return value;
}

/**
 * dccp_parse_options  -  Parse DCCP options present in @skb
 * @sk: client|server|listening dccp socket (when @dreq != NULL)
 * @dreq: request socket to use during connection setup, or NULL
 */
int dccp_parse_options(struct sock *sk, struct dccp_request_sock *dreq,
		       struct sk_buff *skb)
{
	struct dccp_sock *dp = dccp_sk(sk);
	const struct dccp_hdr *dh = dccp_hdr(skb);
	const u8 pkt_type = DCCP_SKB_CB(skb)->dccpd_type;
	u64 ackno = DCCP_SKB_CB(skb)->dccpd_ack_seq;
	unsigned char *options = (unsigned char *)dh + dccp_hdr_len(skb);
	unsigned char *opt_ptr = options;
	const unsigned char *opt_end = (unsigned char *)dh +
					(dh->dccph_doff * 4);
	struct dccp_options_received *opt_recv = &dp->dccps_options_received;
	unsigned char opt, len;
	unsigned char *value;
	u32 elapsed_time;
	__be32 opt_val;
	int rc;
	int mandatory = 0;

	memset(opt_recv, 0, sizeof(*opt_recv));

	opt = len = 0;
	while (opt_ptr != opt_end) {
		opt   = *opt_ptr++;
		len   = 0;
		value = NULL;

		/* Check if this isn't a single byte option */
		if (opt > DCCPO_MAX_RESERVED) {
			if (opt_ptr == opt_end)
				goto out_invalid_option;

			len = *opt_ptr++;
			if (len < 3)
				goto out_invalid_option;
			/*
			 * Remove the type and len fields, leaving
			 * just the value size
			 */
			len	-= 2;
			value	= opt_ptr;
			opt_ptr += len;

			if (opt_ptr > opt_end)
				goto out_invalid_option;
		}

		/*
		 * CCID-Specific Options (from RFC 4340, sec. 10.3):
		 *
		 * Option numbers 128 through 191 are for options sent from the
		 * HC-Sender to the HC-Receiver; option numbers 192 through 255
		 * are for options sent from the HC-Receiver to	the HC-Sender.
		 *
		 * CCID-specific options are ignored during connection setup, as
		 * negotiation may still be in progress (see RFC 4340, 10.3).
		 *
		 */
		if (dreq != NULL && opt >= 128)
			goto ignore_option;

		switch (opt) {
		case DCCPO_PADDING:
			break;
		case DCCPO_MANDATORY:
			if (mandatory)
				goto out_invalid_option;
			if (pkt_type != DCCP_PKT_DATA)
				mandatory = 1;
			break;
		case DCCPO_NDP_COUNT:
			if (len > 3)
				goto out_invalid_option;

			opt_recv->dccpor_ndp = dccp_decode_value_var(value, len);
			dccp_pr_debug("%s rx opt: NDP count=%d\n", dccp_role(sk),
				      opt_recv->dccpor_ndp);
			break;
		case DCCPO_CHANGE_L:
			/* fall through */
		case DCCPO_CHANGE_R:
			if (pkt_type == DCCP_PKT_DATA)
				break;
			if (len < 2)
				goto out_invalid_option;
			rc = dccp_feat_change_recv(sk, opt, *value, value + 1,
						   len - 1);
			/*
			 * When there is a change error, change_recv is
			 * responsible for dealing with it.  i.e. reply with an
			 * empty confirm.
			 * If the change was mandatory, then we need to die.
			 */
			if (rc && mandatory)
				goto out_invalid_option;
			break;
		case DCCPO_CONFIRM_L:
			/* fall through */
		case DCCPO_CONFIRM_R:
			if (pkt_type == DCCP_PKT_DATA)
				break;
			if (len < 2)	/* FIXME this disallows empty confirm */
				goto out_invalid_option;
			if (dccp_feat_confirm_recv(sk, opt, *value,
						   value + 1, len - 1))
				goto out_invalid_option;
			break;
		case DCCPO_ACK_VECTOR_0:
		case DCCPO_ACK_VECTOR_1:
			if (dccp_packet_without_ack(skb))   /* RFC 4340, 11.4 */
				break;

			if (dccp_msk(sk)->dccpms_send_ack_vector &&
			    dccp_ackvec_parse(sk, skb, &ackno, opt, value, len))
				goto out_invalid_option;
			break;
		case DCCPO_TIMESTAMP:
			if (len != 4)
				goto out_invalid_option;
			/*
			 * RFC 4340 13.1: "The precise time corresponding to
			 * Timestamp Value zero is not specified". We use
			 * zero to indicate absence of a meaningful timestamp.
			 */
			opt_val = get_unaligned((__be32 *)value);
			if (unlikely(opt_val == 0)) {
				DCCP_WARN("Timestamp with zero value\n");
				break;
			}

			if (dreq != NULL) {
				dreq->dreq_timestamp_echo = ntohl(opt_val);
				dreq->dreq_timestamp_time = dccp_timestamp();
			} else {
				opt_recv->dccpor_timestamp =
					dp->dccps_timestamp_echo = ntohl(opt_val);
				dp->dccps_timestamp_time = dccp_timestamp();
			}
			dccp_pr_debug("%s rx opt: TIMESTAMP=%u, ackno=%llu\n",
				      dccp_role(sk), ntohl(opt_val),
				      (unsigned long long)
				      DCCP_SKB_CB(skb)->dccpd_ack_seq);
			break;
		case DCCPO_TIMESTAMP_ECHO:
			if (len != 4 && len != 6 && len != 8)
				goto out_invalid_option;

			opt_val = get_unaligned((__be32 *)value);
			opt_recv->dccpor_timestamp_echo = ntohl(opt_val);

			dccp_pr_debug("%s rx opt: TIMESTAMP_ECHO=%u, len=%d, "
				      "ackno=%llu", dccp_role(sk),
				      opt_recv->dccpor_timestamp_echo,
				      len + 2,
				      (unsigned long long)
				      DCCP_SKB_CB(skb)->dccpd_ack_seq);

			value += 4;

			if (len == 4) {		/* no elapsed time included */
				dccp_pr_debug_cat("\n");
				break;
			}

			if (len == 6) {		/* 2-byte elapsed time */
				__be16 opt_val2 = get_unaligned((__be16 *)value);
				elapsed_time = ntohs(opt_val2);
			} else {		/* 4-byte elapsed time */
				opt_val = get_unaligned((__be32 *)value);
				elapsed_time = ntohl(opt_val);
			}

			dccp_pr_debug_cat(", ELAPSED_TIME=%u\n", elapsed_time);

			/* Give precedence to the biggest ELAPSED_TIME */
			if (elapsed_time > opt_recv->dccpor_elapsed_time)
				opt_recv->dccpor_elapsed_time = elapsed_time;
			break;
		case DCCPO_ELAPSED_TIME:
			if (dccp_packet_without_ack(skb))   /* RFC 4340, 13.2 */
				break;

			if (len == 2) {
				__be16 opt_val2 = get_unaligned((__be16 *)value);
				elapsed_time = ntohs(opt_val2);
			} else if (len == 4) {
				opt_val = get_unaligned((__be32 *)value);
				elapsed_time = ntohl(opt_val);
			} else {
				goto out_invalid_option;
			}

			if (elapsed_time > opt_recv->dccpor_elapsed_time)
				opt_recv->dccpor_elapsed_time = elapsed_time;

			dccp_pr_debug("%s rx opt: ELAPSED_TIME=%d\n",
				      dccp_role(sk), elapsed_time);
			break;
		case 128 ... 191: {
			const u16 idx = value - options;

			if (ccid_hc_rx_parse_options(dp->dccps_hc_rx_ccid, sk,
						     opt, len, idx,
						     value) != 0)
				goto out_invalid_option;
		}
			break;
		case 192 ... 255: {
			const u16 idx = value - options;

			if (ccid_hc_tx_parse_options(dp->dccps_hc_tx_ccid, sk,
						     opt, len, idx,
						     value) != 0)
				goto out_invalid_option;
		}
			break;
		default:
			DCCP_CRIT("DCCP(%p): option %d(len=%d) not "
				  "implemented, ignoring", sk, opt, len);
			break;
		}
ignore_option:
		if (opt != DCCPO_MANDATORY)
			mandatory = 0;
	}

	/* mandatory was the last byte in option list -> reset connection */
	if (mandatory)
		goto out_invalid_option;

	return 0;

out_invalid_option:
	DCCP_INC_STATS_BH(DCCP_MIB_INVALIDOPT);
	DCCP_SKB_CB(skb)->dccpd_reset_code = DCCP_RESET_CODE_OPTION_ERROR;
	DCCP_WARN("DCCP(%p): invalid option %d, len=%d", sk, opt, len);
	return -1;
}

EXPORT_SYMBOL_GPL(dccp_parse_options);

static void dccp_encode_value_var(const u32 value, unsigned char *to,
				  const unsigned int len)
{
	if (len > 3)
		*to++ = (value & 0xFF000000) >> 24;
	if (len > 2)
		*to++ = (value & 0xFF0000) >> 16;
	if (len > 1)
		*to++ = (value & 0xFF00) >> 8;
	if (len > 0)
		*to++ = (value & 0xFF);
}

static inline int dccp_ndp_len(const int ndp)
{
	return likely(ndp <= 0xFF) ? 1 : ndp <= 0xFFFF ? 2 : 3;
}

int dccp_insert_option(struct sock *sk, struct sk_buff *skb,
			const unsigned char option,
			const void *value, const unsigned char len)
{
	unsigned char *to;

	if (DCCP_SKB_CB(skb)->dccpd_opt_len + len + 2 > DCCP_MAX_OPT_LEN)
		return -1;

	DCCP_SKB_CB(skb)->dccpd_opt_len += len + 2;

	to    = skb_push(skb, len + 2);
	*to++ = option;
	*to++ = len + 2;

	memcpy(to, value, len);
	return 0;
}

EXPORT_SYMBOL_GPL(dccp_insert_option);

static int dccp_insert_option_ndp(struct sock *sk, struct sk_buff *skb)
{
	struct dccp_sock *dp = dccp_sk(sk);
	int ndp = dp->dccps_ndp_count;

	if (dccp_non_data_packet(skb))
		++dp->dccps_ndp_count;
	else
		dp->dccps_ndp_count = 0;

	if (ndp > 0) {
		unsigned char *ptr;
		const int ndp_len = dccp_ndp_len(ndp);
		const int len = ndp_len + 2;

		if (DCCP_SKB_CB(skb)->dccpd_opt_len + len > DCCP_MAX_OPT_LEN)
			return -1;

		DCCP_SKB_CB(skb)->dccpd_opt_len += len;

		ptr = skb_push(skb, len);
		*ptr++ = DCCPO_NDP_COUNT;
		*ptr++ = len;
		dccp_encode_value_var(ndp, ptr, ndp_len);
	}

	return 0;
}

static inline int dccp_elapsed_time_len(const u32 elapsed_time)
{
	return elapsed_time == 0 ? 0 : elapsed_time <= 0xFFFF ? 2 : 4;
}

int dccp_insert_option_elapsed_time(struct sock *sk, struct sk_buff *skb,
				    u32 elapsed_time)
{
	const int elapsed_time_len = dccp_elapsed_time_len(elapsed_time);
	const int len = 2 + elapsed_time_len;
	unsigned char *to;

	if (elapsed_time_len == 0)
		return 0;

	if (DCCP_SKB_CB(skb)->dccpd_opt_len + len > DCCP_MAX_OPT_LEN)
		return -1;

	DCCP_SKB_CB(skb)->dccpd_opt_len += len;

	to    = skb_push(skb, len);
	*to++ = DCCPO_ELAPSED_TIME;
	*to++ = len;

	if (elapsed_time_len == 2) {
		const __be16 var16 = htons((u16)elapsed_time);
		memcpy(to, &var16, 2);
	} else {
		const __be32 var32 = htonl(elapsed_time);
		memcpy(to, &var32, 4);
	}

	return 0;
}

EXPORT_SYMBOL_GPL(dccp_insert_option_elapsed_time);

int dccp_insert_option_timestamp(struct sock *sk, struct sk_buff *skb)
{
	__be32 now = htonl(dccp_timestamp());
	/* yes this will overflow but that is the point as we want a
	 * 10 usec 32 bit timer which mean it wraps every 11.9 hours */

	return dccp_insert_option(sk, skb, DCCPO_TIMESTAMP, &now, sizeof(now));
}

EXPORT_SYMBOL_GPL(dccp_insert_option_timestamp);

static int dccp_insert_option_timestamp_echo(struct dccp_sock *dp,
					     struct dccp_request_sock *dreq,
					     struct sk_buff *skb)
{
	__be32 tstamp_echo;
	unsigned char *to;
	u32 elapsed_time, elapsed_time_len, len;

	if (dreq != NULL) {
		elapsed_time = dccp_timestamp() - dreq->dreq_timestamp_time;
		tstamp_echo  = htonl(dreq->dreq_timestamp_echo);
		dreq->dreq_timestamp_echo = 0;
	} else {
		elapsed_time = dccp_timestamp() - dp->dccps_timestamp_time;
		tstamp_echo  = htonl(dp->dccps_timestamp_echo);
		dp->dccps_timestamp_echo = 0;
	}

	elapsed_time_len = dccp_elapsed_time_len(elapsed_time);
	len = 6 + elapsed_time_len;

	if (DCCP_SKB_CB(skb)->dccpd_opt_len + len > DCCP_MAX_OPT_LEN)
		return -1;

	DCCP_SKB_CB(skb)->dccpd_opt_len += len;

	to    = skb_push(skb, len);
	*to++ = DCCPO_TIMESTAMP_ECHO;
	*to++ = len;

	memcpy(to, &tstamp_echo, 4);
	to += 4;

	if (elapsed_time_len == 2) {
		const __be16 var16 = htons((u16)elapsed_time);
		memcpy(to, &var16, 2);
	} else if (elapsed_time_len == 4) {
		const __be32 var32 = htonl(elapsed_time);
		memcpy(to, &var32, 4);
	}

	return 0;
}

static int dccp_insert_feat_opt(struct sk_buff *skb, u8 type, u8 feat,
				u8 *val, u8 len)
{
	u8 *to;

	if (DCCP_SKB_CB(skb)->dccpd_opt_len + len + 3 > DCCP_MAX_OPT_LEN) {
		DCCP_WARN("packet too small for feature %d option!\n", feat);
		return -1;
	}

	DCCP_SKB_CB(skb)->dccpd_opt_len += len + 3;

	to    = skb_push(skb, len + 3);
	*to++ = type;
	*to++ = len + 3;
	*to++ = feat;

	if (len)
		memcpy(to, val, len);

	dccp_pr_debug("%s(%s (%d), ...), length %d\n",
		      dccp_feat_typename(type),
		      dccp_feat_name(feat), feat, len);
	return 0;
}

static int dccp_insert_options_feat(struct sock *sk, struct sk_buff *skb)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct dccp_minisock *dmsk = dccp_msk(sk);
	struct dccp_opt_pend *opt, *next;
	int change = 0;

	/* confirm any options [NN opts] */
	list_for_each_entry_safe(opt, next, &dmsk->dccpms_conf, dccpop_node) {
		dccp_insert_feat_opt(skb, opt->dccpop_type,
				     opt->dccpop_feat, opt->dccpop_val,
				     opt->dccpop_len);
		/* fear empty confirms */
		if (opt->dccpop_val)
			kfree(opt->dccpop_val);
		kfree(opt);
	}
	INIT_LIST_HEAD(&dmsk->dccpms_conf);

	/* see which features we need to send */
	list_for_each_entry(opt, &dmsk->dccpms_pending, dccpop_node) {
		/* see if we need to send any confirm */
		if (opt->dccpop_sc) {
			dccp_insert_feat_opt(skb, opt->dccpop_type + 1,
					     opt->dccpop_feat,
					     opt->dccpop_sc->dccpoc_val,
					     opt->dccpop_sc->dccpoc_len);

			BUG_ON(!opt->dccpop_sc->dccpoc_val);
			kfree(opt->dccpop_sc->dccpoc_val);
			kfree(opt->dccpop_sc);
			opt->dccpop_sc = NULL;
		}

		/* any option not confirmed, re-send it */
		if (!opt->dccpop_conf) {
			dccp_insert_feat_opt(skb, opt->dccpop_type,
					     opt->dccpop_feat, opt->dccpop_val,
					     opt->dccpop_len);
			change++;
		}
	}

	/* Retransmit timer.
	 * If this is the master listening sock, we don't set a timer on it.  It
	 * should be fine because if the dude doesn't receive our RESPONSE
	 * [which will contain the CHANGE] he will send another REQUEST which
	 * will "retrnasmit" the change.
	 */
	if (change && dp->dccps_role != DCCP_ROLE_LISTEN) {
		dccp_pr_debug("reset feat negotiation timer %p\n", sk);

		/* XXX don't reset the timer on re-transmissions.  I.e. reset it
		 * only when sending new stuff i guess.  Currently the timer
		 * never backs off because on re-transmission it just resets it!
		 */
		inet_csk_reset_xmit_timer(sk, ICSK_TIME_RETRANS,
					  inet_csk(sk)->icsk_rto, DCCP_RTO_MAX);
	}

	return 0;
}

/* The length of all options needs to be a multiple of 4 (5.8) */
static void dccp_insert_option_padding(struct sk_buff *skb)
{
	int padding = DCCP_SKB_CB(skb)->dccpd_opt_len % 4;

	if (padding != 0) {
		padding = 4 - padding;
		memset(skb_push(skb, padding), 0, padding);
		DCCP_SKB_CB(skb)->dccpd_opt_len += padding;
	}
}

int dccp_insert_options(struct sock *sk, struct sk_buff *skb)
{
	struct dccp_sock *dp = dccp_sk(sk);
	struct dccp_minisock *dmsk = dccp_msk(sk);

	DCCP_SKB_CB(skb)->dccpd_opt_len = 0;

	if (dmsk->dccpms_send_ndp_count &&
	    dccp_insert_option_ndp(sk, skb))
		return -1;

	if (!dccp_packet_without_ack(skb)) {
		if (dmsk->dccpms_send_ack_vector &&
		    dccp_ackvec_pending(dp->dccps_hc_rx_ackvec) &&
		    dccp_insert_option_ackvec(sk, skb))
			return -1;
	}

	if (dp->dccps_hc_rx_insert_options) {
		if (ccid_hc_rx_insert_options(dp->dccps_hc_rx_ccid, sk, skb))
			return -1;
		dp->dccps_hc_rx_insert_options = 0;
	}

	/* Feature negotiation */
	/* Data packets can't do feat negotiation */
	if (DCCP_SKB_CB(skb)->dccpd_type != DCCP_PKT_DATA &&
	    DCCP_SKB_CB(skb)->dccpd_type != DCCP_PKT_DATAACK &&
	    dccp_insert_options_feat(sk, skb))
		return -1;

	/*
	 * Obtain RTT sample from Request/Response exchange.
	 * This is currently used in CCID 3 initialisation.
	 */
	if (DCCP_SKB_CB(skb)->dccpd_type == DCCP_PKT_REQUEST &&
	    dccp_insert_option_timestamp(sk, skb))
		return -1;

	if (dp->dccps_timestamp_echo != 0 &&
	    dccp_insert_option_timestamp_echo(dp, NULL, skb))
		return -1;

	dccp_insert_option_padding(skb);
	return 0;
}

int dccp_insert_options_rsk(struct dccp_request_sock *dreq, struct sk_buff *skb)
{
	DCCP_SKB_CB(skb)->dccpd_opt_len = 0;

	if (dreq->dreq_timestamp_echo != 0 &&
	    dccp_insert_option_timestamp_echo(NULL, dreq, skb))
		return -1;

	dccp_insert_option_padding(skb);
	return 0;
}
