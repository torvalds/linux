/*
 *  net/dccp/diag.c
 *
 *  An implementation of the DCCP protocol
 *  Arnaldo Carvalho de Melo <acme@mandriva.com>
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License version 2 as
 *	published by the Free Software Foundation.
 */


#include <linux/module.h>
#include <linux/inet_diag.h>

#include "ccid.h"
#include "dccp.h"

static void dccp_get_info(struct sock *sk, struct tcp_info *info)
{
	struct dccp_sock *dp = dccp_sk(sk);
	const struct inet_connection_sock *icsk = inet_csk(sk);

	memset(info, 0, sizeof(*info));

	info->tcpi_state	= sk->sk_state;
	info->tcpi_retransmits	= icsk->icsk_retransmits;
	info->tcpi_probes	= icsk->icsk_probes_out;
	info->tcpi_backoff	= icsk->icsk_backoff;
	info->tcpi_pmtu		= icsk->icsk_pmtu_cookie;

	if (dp->dccps_hc_rx_ackvec != NULL)
		info->tcpi_options |= TCPI_OPT_SACK;

	if (dp->dccps_hc_rx_ccid != NULL)
		ccid_hc_rx_get_info(dp->dccps_hc_rx_ccid, sk, info);

	if (dp->dccps_hc_tx_ccid != NULL)
		ccid_hc_tx_get_info(dp->dccps_hc_tx_ccid, sk, info);
}

static void dccp_diag_get_info(struct sock *sk, struct inet_diag_msg *r,
			       void *_info)
{
	r->idiag_rqueue = r->idiag_wqueue = 0;

	if (_info != NULL)
		dccp_get_info(sk, _info);
}

static void dccp_diag_dump(struct sk_buff *skb, struct netlink_callback *cb,
			   const struct inet_diag_req_v2 *r, struct nlattr *bc)
{
	inet_diag_dump_icsk(&dccp_hashinfo, skb, cb, r, bc);
}

static int dccp_diag_dump_one(struct sk_buff *in_skb,
			      const struct nlmsghdr *nlh,
			      const struct inet_diag_req_v2 *req)
{
	return inet_diag_dump_one_icsk(&dccp_hashinfo, in_skb, nlh, req);
}

static const struct inet_diag_handler dccp_diag_handler = {
	.dump		 = dccp_diag_dump,
	.dump_one	 = dccp_diag_dump_one,
	.idiag_get_info	 = dccp_diag_get_info,
	.idiag_type	 = IPPROTO_DCCP,
	.idiag_info_size = sizeof(struct tcp_info),
};

static int __init dccp_diag_init(void)
{
	return inet_diag_register(&dccp_diag_handler);
}

static void __exit dccp_diag_fini(void)
{
	inet_diag_unregister(&dccp_diag_handler);
}

module_init(dccp_diag_init);
module_exit(dccp_diag_fini);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Arnaldo Carvalho de Melo <acme@mandriva.com>");
MODULE_DESCRIPTION("DCCP inet_diag handler");
MODULE_ALIAS_NET_PF_PROTO_TYPE(PF_NETLINK, NETLINK_SOCK_DIAG, 2-33 /* AF_INET - IPPROTO_DCCP */);
