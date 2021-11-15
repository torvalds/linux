// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */

#include <stddef.h>
#include <errno.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <linux/ipv6.h>
#include <linux/tcp.h>
#include <linux/socket.h>
#include <linux/bpf.h>
#include <linux/types.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>
#define BPF_PROG_TEST_TCP_HDR_OPTIONS
#include "test_tcp_hdr_options.h"

__u16 last_addr16_n = __bpf_htons(1);
__u16 active_lport_n = 0;
__u16 active_lport_h = 0;
__u16 passive_lport_n = 0;
__u16 passive_lport_h = 0;

/* options received at passive side */
unsigned int nr_pure_ack = 0;
unsigned int nr_data = 0;
unsigned int nr_syn = 0;
unsigned int nr_fin = 0;

/* Check the header received from the active side */
static int __check_active_hdr_in(struct bpf_sock_ops *skops, bool check_syn)
{
	union {
		struct tcphdr th;
		struct ipv6hdr ip6;
		struct tcp_exprm_opt exprm_opt;
		struct tcp_opt reg_opt;
		__u8 data[100]; /* IPv6 (40) + Max TCP hdr (60) */
	} hdr = {};
	__u64 load_flags = check_syn ? BPF_LOAD_HDR_OPT_TCP_SYN : 0;
	struct tcphdr *pth;
	int ret;

	hdr.reg_opt.kind = 0xB9;

	/* The option is 4 bytes long instead of 2 bytes */
	ret = bpf_load_hdr_opt(skops, &hdr.reg_opt, 2, load_flags);
	if (ret != -ENOSPC)
		RET_CG_ERR(ret);

	/* Test searching magic with regular kind */
	hdr.reg_opt.len = 4;
	ret = bpf_load_hdr_opt(skops, &hdr.reg_opt, sizeof(hdr.reg_opt),
			       load_flags);
	if (ret != -EINVAL)
		RET_CG_ERR(ret);

	hdr.reg_opt.len = 0;
	ret = bpf_load_hdr_opt(skops, &hdr.reg_opt, sizeof(hdr.reg_opt),
			       load_flags);
	if (ret != 4 || hdr.reg_opt.len != 4 || hdr.reg_opt.kind != 0xB9 ||
	    hdr.reg_opt.data[0] != 0xfa || hdr.reg_opt.data[1] != 0xce)
		RET_CG_ERR(ret);

	/* Test searching experimental option with invalid kind length */
	hdr.exprm_opt.kind = TCPOPT_EXP;
	hdr.exprm_opt.len = 5;
	hdr.exprm_opt.magic = 0;
	ret = bpf_load_hdr_opt(skops, &hdr.exprm_opt, sizeof(hdr.exprm_opt),
			       load_flags);
	if (ret != -EINVAL)
		RET_CG_ERR(ret);

	/* Test searching experimental option with 0 magic value */
	hdr.exprm_opt.len = 4;
	ret = bpf_load_hdr_opt(skops, &hdr.exprm_opt, sizeof(hdr.exprm_opt),
			       load_flags);
	if (ret != -ENOMSG)
		RET_CG_ERR(ret);

	hdr.exprm_opt.magic = __bpf_htons(0xeB9F);
	ret = bpf_load_hdr_opt(skops, &hdr.exprm_opt, sizeof(hdr.exprm_opt),
			       load_flags);
	if (ret != 4 || hdr.exprm_opt.len != 4 ||
	    hdr.exprm_opt.kind != TCPOPT_EXP ||
	    hdr.exprm_opt.magic != __bpf_htons(0xeB9F))
		RET_CG_ERR(ret);

	if (!check_syn)
		return CG_OK;

	/* Test loading from skops->syn_skb if sk_state == TCP_NEW_SYN_RECV
	 *
	 * Test loading from tp->saved_syn for other sk_state.
	 */
	ret = bpf_getsockopt(skops, SOL_TCP, TCP_BPF_SYN_IP, &hdr.ip6,
			     sizeof(hdr.ip6));
	if (ret != -ENOSPC)
		RET_CG_ERR(ret);

	if (hdr.ip6.saddr.s6_addr16[7] != last_addr16_n ||
	    hdr.ip6.daddr.s6_addr16[7] != last_addr16_n)
		RET_CG_ERR(0);

	ret = bpf_getsockopt(skops, SOL_TCP, TCP_BPF_SYN_IP, &hdr, sizeof(hdr));
	if (ret < 0)
		RET_CG_ERR(ret);

	pth = (struct tcphdr *)(&hdr.ip6 + 1);
	if (pth->dest != passive_lport_n || pth->source != active_lport_n)
		RET_CG_ERR(0);

	ret = bpf_getsockopt(skops, SOL_TCP, TCP_BPF_SYN, &hdr, sizeof(hdr));
	if (ret < 0)
		RET_CG_ERR(ret);

	if (hdr.th.dest != passive_lport_n || hdr.th.source != active_lport_n)
		RET_CG_ERR(0);

	return CG_OK;
}

static int check_active_syn_in(struct bpf_sock_ops *skops)
{
	return __check_active_hdr_in(skops, true);
}

static int check_active_hdr_in(struct bpf_sock_ops *skops)
{
	struct tcphdr *th;

	if (__check_active_hdr_in(skops, false) == CG_ERR)
		return CG_ERR;

	th = skops->skb_data;
	if (th + 1 > skops->skb_data_end)
		RET_CG_ERR(0);

	if (tcp_hdrlen(th) < skops->skb_len)
		nr_data++;

	if (th->fin)
		nr_fin++;

	if (th->ack && !th->fin && tcp_hdrlen(th) == skops->skb_len)
		nr_pure_ack++;

	return CG_OK;
}

static int active_opt_len(struct bpf_sock_ops *skops)
{
	int err;

	/* Reserve more than enough to allow the -EEXIST test in
	 * the write_active_opt().
	 */
	err = bpf_reserve_hdr_opt(skops, 12, 0);
	if (err)
		RET_CG_ERR(err);

	return CG_OK;
}

static int write_active_opt(struct bpf_sock_ops *skops)
{
	struct tcp_exprm_opt exprm_opt = {};
	struct tcp_opt win_scale_opt = {};
	struct tcp_opt reg_opt = {};
	struct tcphdr *th;
	int err, ret;

	exprm_opt.kind = TCPOPT_EXP;
	exprm_opt.len = 4;
	exprm_opt.magic = __bpf_htons(0xeB9F);

	reg_opt.kind = 0xB9;
	reg_opt.len = 4;
	reg_opt.data[0] = 0xfa;
	reg_opt.data[1] = 0xce;

	win_scale_opt.kind = TCPOPT_WINDOW;

	err = bpf_store_hdr_opt(skops, &exprm_opt, sizeof(exprm_opt), 0);
	if (err)
		RET_CG_ERR(err);

	/* Store the same exprm option */
	err = bpf_store_hdr_opt(skops, &exprm_opt, sizeof(exprm_opt), 0);
	if (err != -EEXIST)
		RET_CG_ERR(err);

	err = bpf_store_hdr_opt(skops, &reg_opt, sizeof(reg_opt), 0);
	if (err)
		RET_CG_ERR(err);
	err = bpf_store_hdr_opt(skops, &reg_opt, sizeof(reg_opt), 0);
	if (err != -EEXIST)
		RET_CG_ERR(err);

	/* Check the option has been written and can be searched */
	ret = bpf_load_hdr_opt(skops, &exprm_opt, sizeof(exprm_opt), 0);
	if (ret != 4 || exprm_opt.len != 4 || exprm_opt.kind != TCPOPT_EXP ||
	    exprm_opt.magic != __bpf_htons(0xeB9F))
		RET_CG_ERR(ret);

	reg_opt.len = 0;
	ret = bpf_load_hdr_opt(skops, &reg_opt, sizeof(reg_opt), 0);
	if (ret != 4 || reg_opt.len != 4 || reg_opt.kind != 0xB9 ||
	    reg_opt.data[0] != 0xfa || reg_opt.data[1] != 0xce)
		RET_CG_ERR(ret);

	th = skops->skb_data;
	if (th + 1 > skops->skb_data_end)
		RET_CG_ERR(0);

	if (th->syn) {
		active_lport_h = skops->local_port;
		active_lport_n = th->source;

		/* Search the win scale option written by kernel
		 * in the SYN packet.
		 */
		ret = bpf_load_hdr_opt(skops, &win_scale_opt,
				       sizeof(win_scale_opt), 0);
		if (ret != 3 || win_scale_opt.len != 3 ||
		    win_scale_opt.kind != TCPOPT_WINDOW)
			RET_CG_ERR(ret);

		/* Write the win scale option that kernel
		 * has already written.
		 */
		err = bpf_store_hdr_opt(skops, &win_scale_opt,
					sizeof(win_scale_opt), 0);
		if (err != -EEXIST)
			RET_CG_ERR(err);
	}

	return CG_OK;
}

static int handle_hdr_opt_len(struct bpf_sock_ops *skops)
{
	__u8 tcp_flags = skops_tcp_flags(skops);

	if ((tcp_flags & TCPHDR_SYNACK) == TCPHDR_SYNACK)
		/* Check the SYN from bpf_sock_ops_kern->syn_skb */
		return check_active_syn_in(skops);

	/* Passive side should have cleared the write hdr cb by now */
	if (skops->local_port == passive_lport_h)
		RET_CG_ERR(0);

	return active_opt_len(skops);
}

static int handle_write_hdr_opt(struct bpf_sock_ops *skops)
{
	if (skops->local_port == passive_lport_h)
		RET_CG_ERR(0);

	return write_active_opt(skops);
}

static int handle_parse_hdr(struct bpf_sock_ops *skops)
{
	/* Passive side is not writing any non-standard/unknown
	 * option, so the active side should never be called.
	 */
	if (skops->local_port == active_lport_h)
		RET_CG_ERR(0);

	return check_active_hdr_in(skops);
}

static int handle_passive_estab(struct bpf_sock_ops *skops)
{
	int err;

	/* No more write hdr cb */
	bpf_sock_ops_cb_flags_set(skops,
				  skops->bpf_sock_ops_cb_flags &
				  ~BPF_SOCK_OPS_WRITE_HDR_OPT_CB_FLAG);

	/* Recheck the SYN but check the tp->saved_syn this time */
	err = check_active_syn_in(skops);
	if (err == CG_ERR)
		return err;

	nr_syn++;

	/* The ack has header option written by the active side also */
	return check_active_hdr_in(skops);
}

SEC("sockops")
int misc_estab(struct bpf_sock_ops *skops)
{
	int true_val = 1;

	switch (skops->op) {
	case BPF_SOCK_OPS_TCP_LISTEN_CB:
		passive_lport_h = skops->local_port;
		passive_lport_n = __bpf_htons(passive_lport_h);
		bpf_setsockopt(skops, SOL_TCP, TCP_SAVE_SYN,
			       &true_val, sizeof(true_val));
		set_hdr_cb_flags(skops, 0);
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
	}

	return CG_OK;
}

char _license[] SEC("license") = "GPL";
