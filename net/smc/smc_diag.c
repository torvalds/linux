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

static void smc_gid_be16_convert(__u8 *buf, u8 *gid_raw)
{
	sprintf(buf, "%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x",
		be16_to_cpu(((__be16 *)gid_raw)[0]),
		be16_to_cpu(((__be16 *)gid_raw)[1]),
		be16_to_cpu(((__be16 *)gid_raw)[2]),
		be16_to_cpu(((__be16 *)gid_raw)[3]),
		be16_to_cpu(((__be16 *)gid_raw)[4]),
		be16_to_cpu(((__be16 *)gid_raw)[5]),
		be16_to_cpu(((__be16 *)gid_raw)[6]),
		be16_to_cpu(((__be16 *)gid_raw)[7]));
}

static void smc_diag_msg_common_fill(struct smc_diag_msg *r, struct sock *sk)
{
	struct smc_sock *smc = smc_sk(sk);

	if (!smc->clcsock)
		return;
	r->id.idiag_sport = htons(smc->clcsock->sk->sk_num);
	r->id.idiag_dport = smc->clcsock->sk->sk_dport;
	r->id.idiag_if = smc->clcsock->sk->sk_bound_dev_if;
	sock_diag_save_cookie(sk, r->id.idiag_cookie);
	if (sk->sk_protocol == SMCPROTO_SMC) {
		r->diag_family = PF_INET;
		memset(&r->id.idiag_src, 0, sizeof(r->id.idiag_src));
		memset(&r->id.idiag_dst, 0, sizeof(r->id.idiag_dst));
		r->id.idiag_src[0] = smc->clcsock->sk->sk_rcv_saddr;
		r->id.idiag_dst[0] = smc->clcsock->sk->sk_daddr;
#if IS_ENABLED(CONFIG_IPV6)
	} else if (sk->sk_protocol == SMCPROTO_SMC6) {
		r->diag_family = PF_INET6;
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

	r->diag_uid = from_kuid_munged(user_ns, sock_i_uid(sk));
	r->diag_inode = sock_i_ino(sk);
	return 0;
}

static int __smc_diag_dump(struct sock *sk, struct sk_buff *skb,
			   struct netlink_callback *cb,
			   const struct smc_diag_req *req,
			   struct nlattr *bc)
{
	struct smc_sock *smc = smc_sk(sk);
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
	r->diag_fallback = smc->use_fallback;
	user_ns = sk_user_ns(NETLINK_CB(cb->skb).sk);
	if (smc_diag_msg_attrs_fill(sk, skb, r, user_ns))
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

	if ((req->diag_ext & (1 << (SMC_DIAG_LGRINFO - 1))) && smc->conn.lgr &&
	    !list_empty(&smc->conn.lgr->list)) {
		struct smc_diag_lgrinfo linfo = {
			.role = smc->conn.lgr->role,
			.lnk[0].ibport = smc->conn.lgr->lnk[0].ibport,
			.lnk[0].link_id = smc->conn.lgr->lnk[0].link_id,
		};

		memcpy(linfo.lnk[0].ibname,
		       smc->conn.lgr->lnk[0].smcibdev->ibdev->name,
		       sizeof(smc->conn.lgr->lnk[0].smcibdev->ibdev->name));
		smc_gid_be16_convert(linfo.lnk[0].gid,
				     smc->conn.lgr->lnk[0].gid.raw);
		smc_gid_be16_convert(linfo.lnk[0].peer_gid,
				     smc->conn.lgr->lnk[0].peer_gid);

		if (nla_put(skb, SMC_DIAG_LGRINFO, sizeof(linfo), &linfo) < 0)
			goto errout;
	}

	nlmsg_end(skb, nlh);
	return 0;

errout:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

static int smc_diag_dump_proto(struct proto *prot, struct sk_buff *skb,
			       struct netlink_callback *cb)
{
	struct net *net = sock_net(skb->sk);
	struct nlattr *bc = NULL;
	struct hlist_head *head;
	struct sock *sk;
	int rc = 0;

	read_lock(&prot->h.smc_hash->lock);
	head = &prot->h.smc_hash->ht;
	if (hlist_empty(head))
		goto out;

	sk_for_each(sk, head) {
		if (!net_eq(sock_net(sk), net))
			continue;
		rc = __smc_diag_dump(sk, skb, cb, nlmsg_data(cb->nlh), bc);
		if (rc)
			break;
	}

out:
	read_unlock(&prot->h.smc_hash->lock);
	return rc;
}

static int smc_diag_dump(struct sk_buff *skb, struct netlink_callback *cb)
{
	int rc = 0;

	rc = smc_diag_dump_proto(&smc_proto, skb, cb);
	if (!rc)
		rc = smc_diag_dump_proto(&smc_proto6, skb, cb);
	return rc;
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
MODULE_ALIAS_NET_PF_PROTO_TYPE(PF_NETLINK, NETLINK_SOCK_DIAG, 43 /* AF_SMC */);
