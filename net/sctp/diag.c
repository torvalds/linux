// SPDX-License-Identifier: GPL-2.0-or-later
/* SCTP kernel implementation
 * (C) Copyright Red Hat Inc. 2017
 *
 * This file is part of the SCTP kernel implementation
 *
 * These functions implement sctp diag support.
 *
 * Please send any bug reports or fixes you make to the
 * email addresched(es):
 *    lksctp developers <linux-sctp@vger.kernel.org>
 *
 * Written or modified by:
 *    Xin Long <lucien.xin@gmail.com>
 */

#include <linux/module.h>
#include <linux/inet_diag.h>
#include <linux/sock_diag.h>
#include <net/sctp/sctp.h>

static void sctp_diag_get_info(struct sock *sk, struct inet_diag_msg *r,
			       void *info);

/* define some functions to make asoc/ep fill look clean */
static void inet_diag_msg_sctpasoc_fill(struct inet_diag_msg *r,
					struct sock *sk,
					struct sctp_association *asoc)
{
	union sctp_addr laddr, paddr;
	struct dst_entry *dst;
	struct timer_list *t3_rtx = &asoc->peer.primary_path->T3_rtx_timer;

	laddr = list_entry(asoc->base.bind_addr.address_list.next,
			   struct sctp_sockaddr_entry, list)->a;
	paddr = asoc->peer.primary_path->ipaddr;
	dst = asoc->peer.primary_path->dst;

	r->idiag_family = sk->sk_family;
	r->id.idiag_sport = htons(asoc->base.bind_addr.port);
	r->id.idiag_dport = htons(asoc->peer.port);
	r->id.idiag_if = dst ? dst->dev->ifindex : 0;
	sock_diag_save_cookie(sk, r->id.idiag_cookie);

#if IS_ENABLED(CONFIG_IPV6)
	if (sk->sk_family == AF_INET6) {
		*(struct in6_addr *)r->id.idiag_src = laddr.v6.sin6_addr;
		*(struct in6_addr *)r->id.idiag_dst = paddr.v6.sin6_addr;
	} else
#endif
	{
		memset(&r->id.idiag_src, 0, sizeof(r->id.idiag_src));
		memset(&r->id.idiag_dst, 0, sizeof(r->id.idiag_dst));

		r->id.idiag_src[0] = laddr.v4.sin_addr.s_addr;
		r->id.idiag_dst[0] = paddr.v4.sin_addr.s_addr;
	}

	r->idiag_state = asoc->state;
	if (timer_pending(t3_rtx)) {
		r->idiag_timer = SCTP_EVENT_TIMEOUT_T3_RTX;
		r->idiag_retrans = asoc->rtx_data_chunks;
		r->idiag_expires = jiffies_to_msecs(t3_rtx->expires - jiffies);
	} else {
		r->idiag_timer = 0;
		r->idiag_retrans = 0;
		r->idiag_expires = 0;
	}
}

static int inet_diag_msg_sctpladdrs_fill(struct sk_buff *skb,
					 struct list_head *address_list)
{
	struct sctp_sockaddr_entry *laddr;
	int addrlen = sizeof(struct sockaddr_storage);
	int addrcnt = 0;
	struct nlattr *attr;
	void *info = NULL;

	list_for_each_entry_rcu(laddr, address_list, list)
		addrcnt++;

	attr = nla_reserve(skb, INET_DIAG_LOCALS, addrlen * addrcnt);
	if (!attr)
		return -EMSGSIZE;

	info = nla_data(attr);
	list_for_each_entry_rcu(laddr, address_list, list) {
		memcpy(info, &laddr->a, sizeof(laddr->a));
		memset(info + sizeof(laddr->a), 0, addrlen - sizeof(laddr->a));
		info += addrlen;
	}

	return 0;
}

static int inet_diag_msg_sctpaddrs_fill(struct sk_buff *skb,
					struct sctp_association *asoc)
{
	int addrlen = sizeof(struct sockaddr_storage);
	struct sctp_transport *from;
	struct nlattr *attr;
	void *info = NULL;

	attr = nla_reserve(skb, INET_DIAG_PEERS,
			   addrlen * asoc->peer.transport_count);
	if (!attr)
		return -EMSGSIZE;

	info = nla_data(attr);
	list_for_each_entry(from, &asoc->peer.transport_addr_list,
			    transports) {
		memcpy(info, &from->ipaddr, sizeof(from->ipaddr));
		memset(info + sizeof(from->ipaddr), 0,
		       addrlen - sizeof(from->ipaddr));
		info += addrlen;
	}

	return 0;
}

/* sctp asoc/ep fill*/
static int inet_sctp_diag_fill(struct sock *sk, struct sctp_association *asoc,
			       struct sk_buff *skb,
			       const struct inet_diag_req_v2 *req,
			       struct user_namespace *user_ns,
			       int portid, u32 seq, u16 nlmsg_flags,
			       const struct nlmsghdr *unlh,
			       bool net_admin)
{
	struct sctp_endpoint *ep = sctp_sk(sk)->ep;
	struct list_head *addr_list;
	struct inet_diag_msg *r;
	struct nlmsghdr  *nlh;
	int ext = req->idiag_ext;
	struct sctp_infox infox;
	void *info = NULL;

	nlh = nlmsg_put(skb, portid, seq, unlh->nlmsg_type, sizeof(*r),
			nlmsg_flags);
	if (!nlh)
		return -EMSGSIZE;

	r = nlmsg_data(nlh);
	BUG_ON(!sk_fullsock(sk));

	if (asoc) {
		inet_diag_msg_sctpasoc_fill(r, sk, asoc);
	} else {
		inet_diag_msg_common_fill(r, sk);
		r->idiag_state = sk->sk_state;
		r->idiag_timer = 0;
		r->idiag_retrans = 0;
	}

	if (inet_diag_msg_attrs_fill(sk, skb, r, ext, user_ns, net_admin))
		goto errout;

	if (ext & (1 << (INET_DIAG_SKMEMINFO - 1))) {
		u32 mem[SK_MEMINFO_VARS];
		int amt;

		if (asoc && asoc->ep->sndbuf_policy)
			amt = asoc->sndbuf_used;
		else
			amt = sk_wmem_alloc_get(sk);
		mem[SK_MEMINFO_WMEM_ALLOC] = amt;
		if (asoc && asoc->ep->rcvbuf_policy)
			amt = atomic_read(&asoc->rmem_alloc);
		else
			amt = sk_rmem_alloc_get(sk);
		mem[SK_MEMINFO_RMEM_ALLOC] = amt;
		mem[SK_MEMINFO_RCVBUF] = sk->sk_rcvbuf;
		mem[SK_MEMINFO_SNDBUF] = sk->sk_sndbuf;
		mem[SK_MEMINFO_FWD_ALLOC] = sk->sk_forward_alloc;
		mem[SK_MEMINFO_WMEM_QUEUED] = sk->sk_wmem_queued;
		mem[SK_MEMINFO_OPTMEM] = atomic_read(&sk->sk_omem_alloc);
		mem[SK_MEMINFO_BACKLOG] = READ_ONCE(sk->sk_backlog.len);
		mem[SK_MEMINFO_DROPS] = atomic_read(&sk->sk_drops);

		if (nla_put(skb, INET_DIAG_SKMEMINFO, sizeof(mem), &mem) < 0)
			goto errout;
	}

	if (ext & (1 << (INET_DIAG_INFO - 1))) {
		struct nlattr *attr;

		attr = nla_reserve_64bit(skb, INET_DIAG_INFO,
					 sizeof(struct sctp_info),
					 INET_DIAG_PAD);
		if (!attr)
			goto errout;

		info = nla_data(attr);
	}
	infox.sctpinfo = (struct sctp_info *)info;
	infox.asoc = asoc;
	sctp_diag_get_info(sk, r, &infox);

	addr_list = asoc ? &asoc->base.bind_addr.address_list
			 : &ep->base.bind_addr.address_list;
	if (inet_diag_msg_sctpladdrs_fill(skb, addr_list))
		goto errout;

	if (asoc && (ext & (1 << (INET_DIAG_CONG - 1))))
		if (nla_put_string(skb, INET_DIAG_CONG, "reno") < 0)
			goto errout;

	if (asoc && inet_diag_msg_sctpaddrs_fill(skb, asoc))
		goto errout;

	nlmsg_end(skb, nlh);
	return 0;

errout:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

/* callback and param */
struct sctp_comm_param {
	struct sk_buff *skb;
	struct netlink_callback *cb;
	const struct inet_diag_req_v2 *r;
	const struct nlmsghdr *nlh;
	bool net_admin;
};

static size_t inet_assoc_attr_size(struct sctp_association *asoc)
{
	int addrlen = sizeof(struct sockaddr_storage);
	int addrcnt = 0;
	struct sctp_sockaddr_entry *laddr;

	list_for_each_entry_rcu(laddr, &asoc->base.bind_addr.address_list,
				list)
		addrcnt++;

	return	  nla_total_size(sizeof(struct sctp_info))
		+ nla_total_size(addrlen * asoc->peer.transport_count)
		+ nla_total_size(addrlen * addrcnt)
		+ nla_total_size(sizeof(struct inet_diag_msg))
		+ inet_diag_msg_attrs_size()
		+ nla_total_size(sizeof(struct inet_diag_meminfo))
		+ 64;
}

static int sctp_tsp_dump_one(struct sctp_transport *tsp, void *p)
{
	struct sctp_association *assoc = tsp->asoc;
	struct sock *sk = tsp->asoc->base.sk;
	struct sctp_comm_param *commp = p;
	struct sk_buff *in_skb = commp->skb;
	const struct inet_diag_req_v2 *req = commp->r;
	const struct nlmsghdr *nlh = commp->nlh;
	struct net *net = sock_net(in_skb->sk);
	struct sk_buff *rep;
	int err;

	err = sock_diag_check_cookie(sk, req->id.idiag_cookie);
	if (err)
		goto out;

	err = -ENOMEM;
	rep = nlmsg_new(inet_assoc_attr_size(assoc), GFP_KERNEL);
	if (!rep)
		goto out;

	lock_sock(sk);
	if (sk != assoc->base.sk) {
		release_sock(sk);
		sk = assoc->base.sk;
		lock_sock(sk);
	}
	err = inet_sctp_diag_fill(sk, assoc, rep, req,
				  sk_user_ns(NETLINK_CB(in_skb).sk),
				  NETLINK_CB(in_skb).portid,
				  nlh->nlmsg_seq, 0, nlh,
				  commp->net_admin);
	release_sock(sk);
	if (err < 0) {
		WARN_ON(err == -EMSGSIZE);
		kfree_skb(rep);
		goto out;
	}

	err = netlink_unicast(net->diag_nlsk, rep, NETLINK_CB(in_skb).portid,
			      MSG_DONTWAIT);
	if (err > 0)
		err = 0;
out:
	return err;
}

static int sctp_sock_dump(struct sctp_transport *tsp, void *p)
{
	struct sctp_endpoint *ep = tsp->asoc->ep;
	struct sctp_comm_param *commp = p;
	struct sock *sk = ep->base.sk;
	struct sk_buff *skb = commp->skb;
	struct netlink_callback *cb = commp->cb;
	const struct inet_diag_req_v2 *r = commp->r;
	struct sctp_association *assoc;
	int err = 0;

	lock_sock(sk);
	list_for_each_entry(assoc, &ep->asocs, asocs) {
		if (cb->args[4] < cb->args[1])
			goto next;

		if (r->id.idiag_sport != htons(assoc->base.bind_addr.port) &&
		    r->id.idiag_sport)
			goto next;
		if (r->id.idiag_dport != htons(assoc->peer.port) &&
		    r->id.idiag_dport)
			goto next;

		if (!cb->args[3] &&
		    inet_sctp_diag_fill(sk, NULL, skb, r,
					sk_user_ns(NETLINK_CB(cb->skb).sk),
					NETLINK_CB(cb->skb).portid,
					cb->nlh->nlmsg_seq,
					NLM_F_MULTI, cb->nlh,
					commp->net_admin) < 0) {
			err = 1;
			goto release;
		}
		cb->args[3] = 1;

		if (inet_sctp_diag_fill(sk, assoc, skb, r,
					sk_user_ns(NETLINK_CB(cb->skb).sk),
					NETLINK_CB(cb->skb).portid,
					cb->nlh->nlmsg_seq, 0, cb->nlh,
					commp->net_admin) < 0) {
			err = 1;
			goto release;
		}
next:
		cb->args[4]++;
	}
	cb->args[1] = 0;
	cb->args[3] = 0;
	cb->args[4] = 0;
release:
	release_sock(sk);
	return err;
}

static int sctp_sock_filter(struct sctp_transport *tsp, void *p)
{
	struct sctp_endpoint *ep = tsp->asoc->ep;
	struct sctp_comm_param *commp = p;
	struct sock *sk = ep->base.sk;
	const struct inet_diag_req_v2 *r = commp->r;
	struct sctp_association *assoc =
		list_entry(ep->asocs.next, struct sctp_association, asocs);

	/* find the ep only once through the transports by this condition */
	if (tsp->asoc != assoc)
		return 0;

	if (r->sdiag_family != AF_UNSPEC && sk->sk_family != r->sdiag_family)
		return 0;

	return 1;
}

static int sctp_ep_dump(struct sctp_endpoint *ep, void *p)
{
	struct sctp_comm_param *commp = p;
	struct sock *sk = ep->base.sk;
	struct sk_buff *skb = commp->skb;
	struct netlink_callback *cb = commp->cb;
	const struct inet_diag_req_v2 *r = commp->r;
	struct net *net = sock_net(skb->sk);
	struct inet_sock *inet = inet_sk(sk);
	int err = 0;

	if (!net_eq(sock_net(sk), net))
		goto out;

	if (cb->args[4] < cb->args[1])
		goto next;

	if (!(r->idiag_states & TCPF_LISTEN) && !list_empty(&ep->asocs))
		goto next;

	if (r->sdiag_family != AF_UNSPEC &&
	    sk->sk_family != r->sdiag_family)
		goto next;

	if (r->id.idiag_sport != inet->inet_sport &&
	    r->id.idiag_sport)
		goto next;

	if (r->id.idiag_dport != inet->inet_dport &&
	    r->id.idiag_dport)
		goto next;

	if (inet_sctp_diag_fill(sk, NULL, skb, r,
				sk_user_ns(NETLINK_CB(cb->skb).sk),
				NETLINK_CB(cb->skb).portid,
				cb->nlh->nlmsg_seq, NLM_F_MULTI,
				cb->nlh, commp->net_admin) < 0) {
		err = 2;
		goto out;
	}
next:
	cb->args[4]++;
out:
	return err;
}

/* define the functions for sctp_diag_handler*/
static void sctp_diag_get_info(struct sock *sk, struct inet_diag_msg *r,
			       void *info)
{
	struct sctp_infox *infox = (struct sctp_infox *)info;

	if (infox->asoc) {
		r->idiag_rqueue = atomic_read(&infox->asoc->rmem_alloc);
		r->idiag_wqueue = infox->asoc->sndbuf_used;
	} else {
		r->idiag_rqueue = READ_ONCE(sk->sk_ack_backlog);
		r->idiag_wqueue = READ_ONCE(sk->sk_max_ack_backlog);
	}
	if (infox->sctpinfo)
		sctp_get_sctp_info(sk, infox->asoc, infox->sctpinfo);
}

static int sctp_diag_dump_one(struct netlink_callback *cb,
			      const struct inet_diag_req_v2 *req)
{
	struct sk_buff *in_skb = cb->skb;
	struct net *net = sock_net(in_skb->sk);
	const struct nlmsghdr *nlh = cb->nlh;
	union sctp_addr laddr, paddr;
	struct sctp_comm_param commp = {
		.skb = in_skb,
		.r = req,
		.nlh = nlh,
		.net_admin = netlink_net_capable(in_skb, CAP_NET_ADMIN),
	};

	if (req->sdiag_family == AF_INET) {
		laddr.v4.sin_port = req->id.idiag_sport;
		laddr.v4.sin_addr.s_addr = req->id.idiag_src[0];
		laddr.v4.sin_family = AF_INET;

		paddr.v4.sin_port = req->id.idiag_dport;
		paddr.v4.sin_addr.s_addr = req->id.idiag_dst[0];
		paddr.v4.sin_family = AF_INET;
	} else {
		laddr.v6.sin6_port = req->id.idiag_sport;
		memcpy(&laddr.v6.sin6_addr, req->id.idiag_src,
		       sizeof(laddr.v6.sin6_addr));
		laddr.v6.sin6_family = AF_INET6;

		paddr.v6.sin6_port = req->id.idiag_dport;
		memcpy(&paddr.v6.sin6_addr, req->id.idiag_dst,
		       sizeof(paddr.v6.sin6_addr));
		paddr.v6.sin6_family = AF_INET6;
	}

	return sctp_transport_lookup_process(sctp_tsp_dump_one,
					     net, &laddr, &paddr, &commp);
}

static void sctp_diag_dump(struct sk_buff *skb, struct netlink_callback *cb,
			   const struct inet_diag_req_v2 *r)
{
	u32 idiag_states = r->idiag_states;
	struct net *net = sock_net(skb->sk);
	struct sctp_comm_param commp = {
		.skb = skb,
		.cb = cb,
		.r = r,
		.net_admin = netlink_net_capable(cb->skb, CAP_NET_ADMIN),
	};
	int pos = cb->args[2];

	/* eps hashtable dumps
	 * args:
	 * 0 : if it will traversal listen sock
	 * 1 : to record the sock pos of this time's traversal
	 * 4 : to work as a temporary variable to traversal list
	 */
	if (cb->args[0] == 0) {
		if (!(idiag_states & TCPF_LISTEN))
			goto skip;
		if (sctp_for_each_endpoint(sctp_ep_dump, &commp))
			goto done;
skip:
		cb->args[0] = 1;
		cb->args[1] = 0;
		cb->args[4] = 0;
	}

	/* asocs by transport hashtable dump
	 * args:
	 * 1 : to record the assoc pos of this time's traversal
	 * 2 : to record the transport pos of this time's traversal
	 * 3 : to mark if we have dumped the ep info of the current asoc
	 * 4 : to work as a temporary variable to traversal list
	 * 5 : to save the sk we get from travelsing the tsp list.
	 */
	if (!(idiag_states & ~(TCPF_LISTEN | TCPF_CLOSE)))
		goto done;

	sctp_for_each_transport(sctp_sock_filter, sctp_sock_dump,
				net, &pos, &commp);
	cb->args[2] = pos;

done:
	cb->args[1] = cb->args[4];
	cb->args[4] = 0;
}

static const struct inet_diag_handler sctp_diag_handler = {
	.dump		 = sctp_diag_dump,
	.dump_one	 = sctp_diag_dump_one,
	.idiag_get_info  = sctp_diag_get_info,
	.idiag_type	 = IPPROTO_SCTP,
	.idiag_info_size = sizeof(struct sctp_info),
};

static int __init sctp_diag_init(void)
{
	return inet_diag_register(&sctp_diag_handler);
}

static void __exit sctp_diag_exit(void)
{
	inet_diag_unregister(&sctp_diag_handler);
}

module_init(sctp_diag_init);
module_exit(sctp_diag_exit);
MODULE_LICENSE("GPL");
MODULE_ALIAS_NET_PF_PROTO_TYPE(PF_NETLINK, NETLINK_SOCK_DIAG, 2-132);
