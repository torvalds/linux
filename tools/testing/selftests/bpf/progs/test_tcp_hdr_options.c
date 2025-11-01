// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */

#include <stddef.h>
#include <errno.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <linux/tcp.h>
#include <linux/socket.h>
#include <linux/bpf.h>
#include <linux/types.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>
#define BPF_PROG_TEST_TCP_HDR_OPTIONS
#include "test_tcp_hdr_options.h"
#include "bpf_misc.h"

__u8 test_kind = TCPOPT_EXP;
__u16 test_magic = 0xeB9F;
__u32 inherit_cb_flags = 0;

struct bpf_test_option passive_synack_out = {};
struct bpf_test_option passive_fin_out	= {};

struct bpf_test_option passive_estab_in = {};
struct bpf_test_option passive_fin_in	= {};

struct bpf_test_option active_syn_out	= {};
struct bpf_test_option active_fin_out	= {};

struct bpf_test_option active_estab_in	= {};
struct bpf_test_option active_fin_in	= {};

struct {
	__uint(type, BPF_MAP_TYPE_SK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, struct hdr_stg);
} hdr_stg_map SEC(".maps");

static bool skops_want_cookie(const struct bpf_sock_ops *skops)
{
	return skops->args[0] == BPF_WRITE_HDR_TCP_SYNACK_COOKIE;
}

static bool skops_current_mss(const struct bpf_sock_ops *skops)
{
	return skops->args[0] == BPF_WRITE_HDR_TCP_CURRENT_MSS;
}

static __u8 option_total_len(__u8 flags)
{
	__u8 i, len = 1; /* +1 for flags */

	if (!flags)
		return 0;

	/* RESEND bit does not use a byte */
	for (i = OPTION_RESEND + 1; i < __NR_OPTION_FLAGS; i++)
		len += !!TEST_OPTION_FLAGS(flags, i);

	if (test_kind == TCPOPT_EXP)
		return len + TCP_BPF_EXPOPT_BASE_LEN;
	else
		return len + 2; /* +1 kind, +1 kind-len */
}

static void write_test_option(const struct bpf_test_option *test_opt,
			      __u8 *data)
{
	__u8 offset = 0;

	data[offset++] = test_opt->flags;
	if (TEST_OPTION_FLAGS(test_opt->flags, OPTION_MAX_DELACK_MS))
		data[offset++] = test_opt->max_delack_ms;

	if (TEST_OPTION_FLAGS(test_opt->flags, OPTION_RAND))
		data[offset++] = test_opt->rand;
}

static int store_option(struct bpf_sock_ops *skops,
			const struct bpf_test_option *test_opt)
{
	union {
		struct tcp_exprm_opt exprm;
		struct tcp_opt regular;
	} write_opt;
	int err;

	if (test_kind == TCPOPT_EXP) {
		write_opt.exprm.kind = TCPOPT_EXP;
		write_opt.exprm.len = option_total_len(test_opt->flags);
		write_opt.exprm.magic = __bpf_htons(test_magic);
		write_opt.exprm.data32 = 0;
		write_test_option(test_opt, write_opt.exprm.data);
		err = bpf_store_hdr_opt(skops, &write_opt.exprm,
					sizeof(write_opt.exprm), 0);
	} else {
		write_opt.regular.kind = test_kind;
		write_opt.regular.len = option_total_len(test_opt->flags);
		write_opt.regular.data32 = 0;
		write_test_option(test_opt, write_opt.regular.data);
		err = bpf_store_hdr_opt(skops, &write_opt.regular,
					sizeof(write_opt.regular), 0);
	}

	if (err)
		RET_CG_ERR(err);

	return CG_OK;
}

static int parse_test_option(struct bpf_test_option *opt, const __u8 *start)
{
	opt->flags = *start++;

	if (TEST_OPTION_FLAGS(opt->flags, OPTION_MAX_DELACK_MS))
		opt->max_delack_ms = *start++;

	if (TEST_OPTION_FLAGS(opt->flags, OPTION_RAND))
		opt->rand = *start++;

	return 0;
}

static int load_option(struct bpf_sock_ops *skops,
		       struct bpf_test_option *test_opt, bool from_syn)
{
	union {
		struct tcp_exprm_opt exprm;
		struct tcp_opt regular;
	} search_opt;
	int ret, load_flags = from_syn ? BPF_LOAD_HDR_OPT_TCP_SYN : 0;

	if (test_kind == TCPOPT_EXP) {
		search_opt.exprm.kind = TCPOPT_EXP;
		search_opt.exprm.len = 4;
		search_opt.exprm.magic = __bpf_htons(test_magic);
		search_opt.exprm.data32 = 0;
		ret = bpf_load_hdr_opt(skops, &search_opt.exprm,
				       sizeof(search_opt.exprm), load_flags);
		if (ret < 0)
			return ret;
		return parse_test_option(test_opt, search_opt.exprm.data);
	} else {
		search_opt.regular.kind = test_kind;
		search_opt.regular.len = 0;
		search_opt.regular.data32 = 0;
		ret = bpf_load_hdr_opt(skops, &search_opt.regular,
				       sizeof(search_opt.regular), load_flags);
		if (ret < 0)
			return ret;
		return parse_test_option(test_opt, search_opt.regular.data);
	}
}

static int synack_opt_len(struct bpf_sock_ops *skops)
{
	struct bpf_test_option test_opt = {};
	__u8 optlen;
	int err;

	if (!passive_synack_out.flags)
		return CG_OK;

	err = load_option(skops, &test_opt, true);

	/* bpf_test_option is not found */
	if (err == -ENOMSG)
		return CG_OK;

	if (err)
		RET_CG_ERR(err);

	optlen = option_total_len(passive_synack_out.flags);
	if (optlen) {
		err = bpf_reserve_hdr_opt(skops, optlen, 0);
		if (err)
			RET_CG_ERR(err);
	}

	return CG_OK;
}

static int write_synack_opt(struct bpf_sock_ops *skops)
{
	struct bpf_test_option opt;

	if (!passive_synack_out.flags)
		/* We should not even be called since no header
		 * space has been reserved.
		 */
		RET_CG_ERR(0);

	opt = passive_synack_out;
	if (skops_want_cookie(skops))
		SET_OPTION_FLAGS(opt.flags, OPTION_RESEND);

	return store_option(skops, &opt);
}

static int syn_opt_len(struct bpf_sock_ops *skops)
{
	__u8 optlen;
	int err;

	if (!active_syn_out.flags)
		return CG_OK;

	optlen = option_total_len(active_syn_out.flags);
	if (optlen) {
		err = bpf_reserve_hdr_opt(skops, optlen, 0);
		if (err)
			RET_CG_ERR(err);
	}

	return CG_OK;
}

static int write_syn_opt(struct bpf_sock_ops *skops)
{
	if (!active_syn_out.flags)
		RET_CG_ERR(0);

	return store_option(skops, &active_syn_out);
}

static int fin_opt_len(struct bpf_sock_ops *skops)
{
	struct bpf_test_option *opt;
	struct hdr_stg *hdr_stg;
	__u8 optlen;
	int err;

	if (!skops->sk)
		RET_CG_ERR(0);

	hdr_stg = bpf_sk_storage_get(&hdr_stg_map, skops->sk, NULL, 0);
	if (!hdr_stg)
		RET_CG_ERR(0);

	if (hdr_stg->active)
		opt = &active_fin_out;
	else
		opt = &passive_fin_out;

	optlen = option_total_len(opt->flags);
	if (optlen) {
		err = bpf_reserve_hdr_opt(skops, optlen, 0);
		if (err)
			RET_CG_ERR(err);
	}

	return CG_OK;
}

static int write_fin_opt(struct bpf_sock_ops *skops)
{
	struct bpf_test_option *opt;
	struct hdr_stg *hdr_stg;

	if (!skops->sk)
		RET_CG_ERR(0);

	hdr_stg = bpf_sk_storage_get(&hdr_stg_map, skops->sk, NULL, 0);
	if (!hdr_stg)
		RET_CG_ERR(0);

	if (hdr_stg->active)
		opt = &active_fin_out;
	else
		opt = &passive_fin_out;

	if (!opt->flags)
		RET_CG_ERR(0);

	return store_option(skops, opt);
}

static int resend_in_ack(struct bpf_sock_ops *skops)
{
	struct hdr_stg *hdr_stg;

	if (!skops->sk)
		return -1;

	hdr_stg = bpf_sk_storage_get(&hdr_stg_map, skops->sk, NULL, 0);
	if (!hdr_stg)
		return -1;

	return !!hdr_stg->resend_syn;
}

static int nodata_opt_len(struct bpf_sock_ops *skops)
{
	int resend;

	resend = resend_in_ack(skops);
	if (resend < 0)
		RET_CG_ERR(0);

	if (resend)
		return syn_opt_len(skops);

	return CG_OK;
}

static int write_nodata_opt(struct bpf_sock_ops *skops)
{
	int resend;

	resend = resend_in_ack(skops);
	if (resend < 0)
		RET_CG_ERR(0);

	if (resend)
		return write_syn_opt(skops);

	return CG_OK;
}

static int data_opt_len(struct bpf_sock_ops *skops)
{
	/* Same as the nodata version.  Mostly to show
	 * an example usage on skops->skb_len.
	 */
	return nodata_opt_len(skops);
}

static int write_data_opt(struct bpf_sock_ops *skops)
{
	return write_nodata_opt(skops);
}

static int current_mss_opt_len(struct bpf_sock_ops *skops)
{
	/* Reserve maximum that may be needed */
	int err;

	err = bpf_reserve_hdr_opt(skops, option_total_len(OPTION_MASK), 0);
	if (err)
		RET_CG_ERR(err);

	return CG_OK;
}

static int handle_hdr_opt_len(struct bpf_sock_ops *skops)
{
	__u8 tcp_flags = skops_tcp_flags(skops);

	if ((tcp_flags & TCPHDR_SYNACK) == TCPHDR_SYNACK)
		return synack_opt_len(skops);

	if (tcp_flags & TCPHDR_SYN)
		return syn_opt_len(skops);

	if (tcp_flags & TCPHDR_FIN)
		return fin_opt_len(skops);

	if (skops_current_mss(skops))
		/* The kernel is calculating the MSS */
		return current_mss_opt_len(skops);

	if (skops->skb_len)
		return data_opt_len(skops);

	return nodata_opt_len(skops);
}

static int handle_write_hdr_opt(struct bpf_sock_ops *skops)
{
	__u8 tcp_flags = skops_tcp_flags(skops);
	struct tcphdr *th;

	if ((tcp_flags & TCPHDR_SYNACK) == TCPHDR_SYNACK)
		return write_synack_opt(skops);

	if (tcp_flags & TCPHDR_SYN)
		return write_syn_opt(skops);

	if (tcp_flags & TCPHDR_FIN)
		return write_fin_opt(skops);

	th = skops->skb_data;
	if (th + 1 > skops->skb_data_end)
		RET_CG_ERR(0);

	if (skops->skb_len > tcp_hdrlen(th))
		return write_data_opt(skops);

	return write_nodata_opt(skops);
}

static int set_delack_max(struct bpf_sock_ops *skops, __u8 max_delack_ms)
{
	__u32 max_delack_us = max_delack_ms * 1000;

	return bpf_setsockopt(skops, SOL_TCP, TCP_BPF_DELACK_MAX,
			      &max_delack_us, sizeof(max_delack_us));
}

static int set_rto_min(struct bpf_sock_ops *skops, __u8 peer_max_delack_ms)
{
	__u32 min_rto_us = peer_max_delack_ms * 1000;

	return bpf_setsockopt(skops, SOL_TCP, TCP_BPF_RTO_MIN, &min_rto_us,
			      sizeof(min_rto_us));
}

static int handle_active_estab(struct bpf_sock_ops *skops)
{
	struct hdr_stg init_stg = {
		.active = true,
	};
	int err;

	err = load_option(skops, &active_estab_in, false);
	if (err && err != -ENOMSG)
		RET_CG_ERR(err);

	init_stg.resend_syn = TEST_OPTION_FLAGS(active_estab_in.flags,
						OPTION_RESEND);
	if (!skops->sk || !bpf_sk_storage_get(&hdr_stg_map, skops->sk,
					      &init_stg,
					      BPF_SK_STORAGE_GET_F_CREATE))
		RET_CG_ERR(0);

	if (init_stg.resend_syn)
		/* Don't clear the write_hdr cb now because
		 * the ACK may get lost and retransmit may
		 * be needed.
		 *
		 * PARSE_ALL_HDR cb flag is set to learn if this
		 * resend_syn option has received by the peer.
		 *
		 * The header option will be resent until a valid
		 * packet is received at handle_parse_hdr()
		 * and all hdr cb flags will be cleared in
		 * handle_parse_hdr().
		 */
		set_parse_all_hdr_cb_flags(skops);
	else if (!active_fin_out.flags)
		/* No options will be written from now */
		clear_hdr_cb_flags(skops);

	if (active_syn_out.max_delack_ms) {
		err = set_delack_max(skops, active_syn_out.max_delack_ms);
		if (err)
			RET_CG_ERR(err);
	}

	if (active_estab_in.max_delack_ms) {
		err = set_rto_min(skops, active_estab_in.max_delack_ms);
		if (err)
			RET_CG_ERR(err);
	}

	return CG_OK;
}

static int handle_passive_estab(struct bpf_sock_ops *skops)
{
	struct hdr_stg init_stg = {};
	struct tcphdr *th;
	int err;

	inherit_cb_flags = skops->bpf_sock_ops_cb_flags;

	err = load_option(skops, &passive_estab_in, true);
	if (err == -ENOENT) {
		/* saved_syn is not found. It was in syncookie mode.
		 * We have asked the active side to resend the options
		 * in ACK, so try to find the bpf_test_option from ACK now.
		 */
		err = load_option(skops, &passive_estab_in, false);
		init_stg.syncookie = true;
	}

	/* ENOMSG: The bpf_test_option is not found which is fine.
	 * Bail out now for all other errors.
	 */
	if (err && err != -ENOMSG)
		RET_CG_ERR(err);

	th = skops->skb_data;
	if (th + 1 > skops->skb_data_end)
		RET_CG_ERR(0);

	if (th->syn) {
		/* Fastopen */

		/* Cannot clear cb_flags to stop write_hdr cb.
		 * synack is not sent yet for fast open.
		 * Even it was, the synack may need to be retransmitted.
		 *
		 * PARSE_ALL_HDR cb flag is set to learn
		 * if synack has reached the peer.
		 * All cb_flags will be cleared in handle_parse_hdr().
		 */
		set_parse_all_hdr_cb_flags(skops);
		init_stg.fastopen = true;
	} else if (!passive_fin_out.flags) {
		/* No options will be written from now */
		clear_hdr_cb_flags(skops);
	}

	if (!skops->sk ||
	    !bpf_sk_storage_get(&hdr_stg_map, skops->sk, &init_stg,
				BPF_SK_STORAGE_GET_F_CREATE))
		RET_CG_ERR(0);

	if (passive_synack_out.max_delack_ms) {
		err = set_delack_max(skops, passive_synack_out.max_delack_ms);
		if (err)
			RET_CG_ERR(err);
	}

	if (passive_estab_in.max_delack_ms) {
		err = set_rto_min(skops, passive_estab_in.max_delack_ms);
		if (err)
			RET_CG_ERR(err);
	}

	return CG_OK;
}

static int handle_parse_hdr(struct bpf_sock_ops *skops)
{
	struct hdr_stg *hdr_stg;
	struct tcphdr *th;

	if (!skops->sk)
		RET_CG_ERR(0);

	th = skops->skb_data;
	if (th + 1 > skops->skb_data_end)
		RET_CG_ERR(0);

	hdr_stg = bpf_sk_storage_get(&hdr_stg_map, skops->sk, NULL, 0);
	if (!hdr_stg)
		RET_CG_ERR(0);

	if (hdr_stg->resend_syn || hdr_stg->fastopen)
		/* The PARSE_ALL_HDR cb flag was turned on
		 * to ensure that the previously written
		 * options have reached the peer.
		 * Those previously written option includes:
		 *     - Active side: resend_syn in ACK during syncookie
		 *      or
		 *     - Passive side: SYNACK during fastopen
		 *
		 * A valid packet has been received here after
		 * the 3WHS, so the PARSE_ALL_HDR cb flag
		 * can be cleared now.
		 */
		clear_parse_all_hdr_cb_flags(skops);

	if (hdr_stg->resend_syn && !active_fin_out.flags)
		/* Active side resent the syn option in ACK
		 * because the server was in syncookie mode.
		 * A valid packet has been received, so
		 * clear header cb flags if there is no
		 * more option to send.
		 */
		clear_hdr_cb_flags(skops);

	if (hdr_stg->fastopen && !passive_fin_out.flags)
		/* Passive side was in fastopen.
		 * A valid packet has been received, so
		 * the SYNACK has reached the peer.
		 * Clear header cb flags if there is no more
		 * option to send.
		 */
		clear_hdr_cb_flags(skops);

	if (th->fin) {
		struct bpf_test_option *fin_opt;
		int err;

		if (hdr_stg->active)
			fin_opt = &active_fin_in;
		else
			fin_opt = &passive_fin_in;

		err = load_option(skops, fin_opt, false);
		if (err && err != -ENOMSG)
			RET_CG_ERR(err);
	}

	return CG_OK;
}

SEC("sockops")
int estab(struct bpf_sock_ops *skops)
{
	int true_val = 1;

	switch (skops->op) {
	case BPF_SOCK_OPS_TCP_LISTEN_CB:
		bpf_setsockopt(skops, SOL_TCP, TCP_SAVE_SYN,
			       &true_val, sizeof(true_val));
		set_hdr_cb_flags(skops, BPF_SOCK_OPS_STATE_CB_FLAG);
		break;
	case BPF_SOCK_OPS_TCP_CONNECT_CB:
		set_hdr_cb_flags(skops, 0);
		break;
	case BPF_SOCK_OPS_PARSE_HDR_OPT_CB:
		return handle_parse_hdr(skops);
	case BPF_SOCK_OPS_HDR_OPT_LEN_CB:
		return handle_hdr_opt_len(skops);
	case BPF_SOCK_OPS_WRITE_HDR_OPT_CB:
		return handle_write_hdr_opt(skops);
	case BPF_SOCK_OPS_PASSIVE_ESTABLISHED_CB:
		return handle_passive_estab(skops);
	case BPF_SOCK_OPS_ACTIVE_ESTABLISHED_CB:
		return handle_active_estab(skops);
	}

	return CG_OK;
}

char _license[] SEC("license") = "GPL";
