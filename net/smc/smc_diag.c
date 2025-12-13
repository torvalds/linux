// SPDX-License-Identifier: GPL-2.0-only
/*
 * Shared Memory Communications over RDMA (SMC-R) and RoCE
 *
 * Monitoring SMC transport protocol sockets
 *
 * Copyright IBM Corp. 2016
 *
 * Author(s):  Ursula Braun <ubraun@linux.vnet.ibm.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/sock_diag.h>
#include <linux/inet_diag.h>
#include <linux/smc_diag.h>
#include <net/netlink.h>
#include <net/smc.h>

#include "smc.h"
#include "smc_core.h"
#include "smc_ism.h"

struct smc_diag_dump_ctx {
	int pos[2];
};

static struct smc_diag_dump_ctx *smc_dump_context(struct netlink_callback *cb)
{
	return (struct smc_diag_dump_ctx *)cb->ctx;
}

static void smc_diag_msg_common_fill(struct smc_diag_msg *r, struct sock *sk)
{
	struct smc_sock *smc = smc_sk(sk);

	memset(r, 0, sizeof(*r));
	r->diag_family = sk->sk_family;
	sock_diag_save_cookie(sk, r->id.idiag_cookie);
	if (!smc->clcsock)
		return;
	r->id.idiag_sport = htons(smc->clcsock->sk->sk_num);
	r->id.idiag_dport = smc->clcsock->sk->sk_dport;
	r->id.idiag_if = smc->clcsock->sk->sk_bound_dev_if;
	if (sk->sk_protocol == SMCPROTO_SMC) {
		r->id.idiag_src[0] = smc->clcsock->sk->sk_rcv_saddr;
		r->id.idiag_dst[0] = smc->clcsock->sk->sk_daddr;
#if IS_ENABLED(CONFIG_IPV6)
	} else if (sk->sk_protocol == SMCPROTO_SMC6) {
		memcpy(&r->id.idiag_src, &smc->clcsock->sk->sk_v6_rcv_saddr,
		       sizeof(smc->clcsock->sk->sk_v6_rcv_saddr));
		memcpy(&r->id.idiag_dst, &smc->clcsock->sk->sk_v6_daddr,
		       sizeof(smc->clcsock->sk->sk_v6_daddr));
#endif
	}
}

static int smc_diag_msg_attrs_fill(struct sock *sk, struct sk_buff *skb,
				   struct smc_diag_msg *r,
				   struct user_namespace *user_ns)
{
	if (nla_put_u8(skb, SMC_DIAG_SHUTDOWN, sk->sk_shutdown))
		return 1;

	r->diag_uid = from_kuid_munged(user_ns, sk_uid(sk));
	r->diag_inode = sock_i_ino(sk);
	return 0;
}

static int __smc_diag_dump(struct sock *sk, struct sk_buff *skb,
			   struct netlink_callback *cb,
			   const struct smc_diag_req *req,
			   struct nlattr *bc)
{
	struct smc_sock *smc = smc_sk(sk);
	struct smc_diag_fallback fallback;
	struct user_namespace *user_ns;
	struct smc_diag_msg *r;
	struct nlmsghdr *nlh;

	nlh = nlmsg_put(skb, NETLINK_CB(cb->skb).portid, cb->nlh->nlmsg_seq,
			cb->nlh->nlmsg_type, sizeof(*r), NLM_F_MULTI);
	if (!nlh)
		return -EMSGSIZE;

	r = nlmsg_data(nlh);
	smc_diag_msg_common_fill(r, sk);
	r->diag_state = sk->sk_state;
	if (smc->use_fallback)
		r->diag_mode = SMC_DIAG_MODE_FALLBACK_TCP;
	else if (smc_conn_lgr_valid(&smc->conn) && smc->conn.lgr->is_smcd)
		r->diag_mode = SMC_DIAG_MODE_SMCD;
	else
		r->diag_mode = SMC_DIAG_MODE_SMCR;
	user_ns = sk_user_ns(NETLINK_CB(cb->skb).sk);
	if (smc_diag_msg_attrs_fill(sk, skb, r, user_ns))
		goto errout;

	fallback.reason = smc->fallback_rsn;
	fallback.peer_diagnosis = smc->peer_diagnosis;
	if (nla_put(skb, SMC_DIAG_FALLBACK, sizeof(fallback), &fallback) < 0)
		goto errout;

	if ((req->diag_ext & (1 << (SMC_DIAG_CONNINFO - 1))) &&
	    smc->conn.alert_token_local) {
		struct smc_connection *conn = &smc->conn;
		struct smc_diag_conninfo cinfo = {
			.token = conn->alert_token_local,
			.sndbuf_size = conn->sndbuf_desc ?
				conn->sndbuf_desc->len : 0,
			.rmbe_size = conn->rmb_desc ? conn->rmb_desc->len : 0,
			.peer_rmbe_size = conn->peer_rmbe_size,

			.rx_prod.wrap = conn->local_rx_ctrl.prod.wrap,
			.rx_prod.count = conn->local_rx_ctrl.prod.count,
			.rx_cons.wrap = conn->local_rx_ctrl.cons.wrap,
			.rx_cons.count = conn->local_rx_ctrl.cons.count,

			.tx_prod.wrap = conn->local_tx_ctrl.prod.wrap,
			.tx_prod.count = conn->local_tx_ctrl.prod.count,
			.tx_cons.wrap = conn->local_tx_ctrl.cons.wrap,
			.tx_cons.count = conn->local_tx_ctrl.cons.count,

			.tx_prod_flags =
				*(u8 *)&conn->local_tx_ctrl.prod_flags,
			.tx_conn_state_flags =
				*(u8 *)&conn->local_tx_ctrl.conn_state_flags,
			.rx_prod_flags = *(u8 *)&conn->local_rx_ctrl.prod_flags,
			.rx_conn_state_flags =
				*(u8 *)&conn->local_rx_ctrl.conn_state_flags,

			.tx_prep.wrap = conn->tx_curs_prep.wrap,
			.tx_prep.count = conn->tx_curs_prep.count,
			.tx_sent.wrap = conn->tx_curs_sent.wrap,
			.tx_sent.count = conn->tx_curs_sent.count,
			.tx_fin.wrap = conn->tx_curs_fin.wrap,
			.tx_fin.count = conn->tx_curs_fin.count,
		};

		if (nla_put(skb, SMC_DIAG_CONNINFO, sizeof(cinfo), &cinfo) < 0)
			goto errout;
	}

	if (smc_conn_lgr_valid(&smc->conn) && !smc->conn.lgr->is_smcd &&
	    (req->diag_ext & (1 << (SMC_DIAG_LGRINFO - 1))) &&
	    !list_empty(&smc->conn.lgr->list)) {
		struct smc_link *link = smc->conn.lnk;

		struct smc_diag_lgrinfo linfo = {
			.role = smc->conn.lgr->role,
			.lnk[0].ibport = link->ibport,
			.lnk[0].link_id = link->link_id,
		};

		memcpy(linfo.lnk[0].ibname, link->smcibdev->ibdev->name,
		       sizeof(link->smcibdev->ibdev->name));
		smc_gid_be16_convert(linfo.lnk[0].gid, link->gid);
		smc_gid_be16_convert(linfo.lnk[0].peer_gid, link->peer_gid);

		if (nla_put(skb, SMC_DIAG_LGRINFO, sizeof(linfo), &linfo) < 0)
			goto errout;
	}
	if (smc_conn_lgr_valid(&smc->conn) && smc->conn.lgr->is_smcd &&
	    (req->diag_ext & (1 << (SMC_DIAG_DMBINFO - 1))) &&
	    !list_empty(&smc->conn.lgr->list) && smc->conn.rmb_desc) {
		struct smc_connection *conn = &smc->conn;
		struct smcd_diag_dmbinfo dinfo;
		struct smcd_dev *smcd = conn->lgr->smcd;
		struct smcd_gid smcd_gid;

		memset(&dinfo, 0, sizeof(dinfo));

		dinfo.linkid = *((u32 *)conn->lgr->id);
		dinfo.peer_gid = conn->lgr->peer_gid.gid;
		dinfo.peer_gid_ext = conn->lgr->peer_gid.gid_ext;
		copy_to_smcdgid(&smcd_gid, &smcd->dibs->gid);
		dinfo.my_gid = smcd_gid.gid;
		dinfo.my_gid_ext = smcd_gid.gid_ext;
		dinfo.token = conn->rmb_desc->token;
		dinfo.peer_token = conn->peer_token;

		if (nla_put(skb, SMC_DIAG_DMBINFO, sizeof(dinfo), &dinfo) < 0)
			goto errout;
	}

	nlmsg_end(skb, nlh);
	return 0;

errout:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

static int smc_diag_dump_proto(struct proto *prot, struct sk_buff *skb,
			       struct netlink_callback *cb, int p_type)
{
	struct smc_diag_dump_ctx *cb_ctx = smc_dump_context(cb);
	struct net *net = sock_net(skb->sk);
	int snum = cb_ctx->pos[p_type];
	struct nlattr *bc = NULL;
	struct hlist_head *head;
	int rc = 0, num = 0;
	struct sock *sk;

	read_lock(&prot->h.smc_hash->lock);
	head = &prot->h.smc_hash->ht;
	if (hlist_empty(head))
		goto out;

	sk_for_each(sk, head) {
		if (!net_eq(sock_net(sk), net))
			continue;
		if (num < snum)
			goto next;
		rc = __smc_diag_dump(sk, skb, cb, nlmsg_data(cb->nlh), bc);
		if (rc < 0)
			goto out;
next:
		num++;
	}

out:
	read_unlock(&prot->h.smc_hash->lock);
	cb_ctx->pos[p_type] = num;
	return rc;
}

static int smc_diag_dump(struct sk_buff *skb, struct netlink_callback *cb)
{
	int rc = 0;

	rc = smc_diag_dump_proto(&smc_proto, skb, cb, SMCPROTO_SMC);
	if (!rc)
		smc_diag_dump_proto(&smc_proto6, skb, cb, SMCPROTO_SMC6);
	return skb->len;
}

static int smc_diag_handler_dump(struct sk_buff *skb, struct nlmsghdr *h)
{
	struct net *net = sock_net(skb->sk);

	if (h->nlmsg_type == SOCK_DIAG_BY_FAMILY &&
	    h->nlmsg_flags & NLM_F_DUMP) {
		{
			struct netlink_dump_control c = {
				.dump = smc_diag_dump,
				.min_dump_alloc = SKB_WITH_OVERHEAD(32768),
			};
			return netlink_dump_start(net->diag_nlsk, skb, h, &c);
		}
	}
	return 0;
}

static const struct sock_diag_handler smc_diag_handler = {
	.owner = THIS_MODULE,
	.family = AF_SMC,
	.dump = smc_diag_handler_dump,
};

static int __init smc_diag_init(void)
{
	return sock_diag_register(&smc_diag_handler);
}

static void __exit smc_diag_exit(void)
{
	sock_diag_unregister(&smc_diag_handler);
}

module_init(smc_diag_init);
module_exit(smc_diag_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SMC socket monitoring via SOCK_DIAG");
MODULE_ALIAS_NET_PF_PROTO_TYPE(PF_NETLINK, NETLINK_SOCK_DIAG, 43 /* AF_SMC */);
MODULE_ALIAS_GENL_FAMILY(SMCR_GENL_FAMILY_NAME);
