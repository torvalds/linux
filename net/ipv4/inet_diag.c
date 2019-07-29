/*
 * inet_diag.c	Module for monitoring INET transport protocols sockets.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/cache.h>
#include <linux/init.h>
#include <linux/time.h>

#include <net/icmp.h>
#include <net/tcp.h>
#include <net/ipv6.h>
#include <net/inet_common.h>
#include <net/inet_connection_sock.h>
#include <net/inet_hashtables.h>
#include <net/inet_timewait_sock.h>
#include <net/inet6_hashtables.h>
#include <net/netlink.h>

#include <linux/inet.h>
#include <linux/stddef.h>

#include <linux/inet_diag.h>
#include <linux/sock_diag.h>

static const struct inet_diag_handler **inet_diag_table;

struct inet_diag_entry {
	const __be32 *saddr;
	const __be32 *daddr;
	u16 sport;
	u16 dport;
	u16 family;
	u16 userlocks;
	u32 ifindex;
	u32 mark;
};

static DEFINE_MUTEX(inet_diag_table_mutex);

static const struct inet_diag_handler *inet_diag_lock_handler(int proto)
{
	if (!inet_diag_table[proto])
		sock_load_diag_module(AF_INET, proto);

	mutex_lock(&inet_diag_table_mutex);
	if (!inet_diag_table[proto])
		return ERR_PTR(-ENOENT);

	return inet_diag_table[proto];
}

static void inet_diag_unlock_handler(const struct inet_diag_handler *handler)
{
	mutex_unlock(&inet_diag_table_mutex);
}

void inet_diag_msg_common_fill(struct inet_diag_msg *r, struct sock *sk)
{
	r->idiag_family = sk->sk_family;

	r->id.idiag_sport = htons(sk->sk_num);
	r->id.idiag_dport = sk->sk_dport;
	r->id.idiag_if = sk->sk_bound_dev_if;
	sock_diag_save_cookie(sk, r->id.idiag_cookie);

#if IS_ENABLED(CONFIG_IPV6)
	if (sk->sk_family == AF_INET6) {
		*(struct in6_addr *)r->id.idiag_src = sk->sk_v6_rcv_saddr;
		*(struct in6_addr *)r->id.idiag_dst = sk->sk_v6_daddr;
	} else
#endif
	{
	memset(&r->id.idiag_src, 0, sizeof(r->id.idiag_src));
	memset(&r->id.idiag_dst, 0, sizeof(r->id.idiag_dst));

	r->id.idiag_src[0] = sk->sk_rcv_saddr;
	r->id.idiag_dst[0] = sk->sk_daddr;
	}
}
EXPORT_SYMBOL_GPL(inet_diag_msg_common_fill);

static size_t inet_sk_attr_size(struct sock *sk,
				const struct inet_diag_req_v2 *req,
				bool net_admin)
{
	const struct inet_diag_handler *handler;
	size_t aux = 0;

	handler = inet_diag_table[req->sdiag_protocol];
	if (handler && handler->idiag_get_aux_size)
		aux = handler->idiag_get_aux_size(sk, net_admin);

	return	  nla_total_size(sizeof(struct tcp_info))
		+ nla_total_size(1) /* INET_DIAG_SHUTDOWN */
		+ nla_total_size(1) /* INET_DIAG_TOS */
		+ nla_total_size(1) /* INET_DIAG_TCLASS */
		+ nla_total_size(4) /* INET_DIAG_MARK */
		+ nla_total_size(sizeof(struct inet_diag_meminfo))
		+ nla_total_size(sizeof(struct inet_diag_msg))
		+ nla_total_size(SK_MEMINFO_VARS * sizeof(u32))
		+ nla_total_size(TCP_CA_NAME_MAX)
		+ nla_total_size(sizeof(struct tcpvegas_info))
		+ aux
		+ 64;
}

int inet_diag_msg_attrs_fill(struct sock *sk, struct sk_buff *skb,
			     struct inet_diag_msg *r, int ext,
			     struct user_namespace *user_ns,
			     bool net_admin)
{
	const struct inet_sock *inet = inet_sk(sk);

	if (nla_put_u8(skb, INET_DIAG_SHUTDOWN, sk->sk_shutdown))
		goto errout;

	/* IPv6 dual-stack sockets use inet->tos for IPv4 connections,
	 * hence this needs to be included regardless of socket family.
	 */
	if (ext & (1 << (INET_DIAG_TOS - 1)))
		if (nla_put_u8(skb, INET_DIAG_TOS, inet->tos) < 0)
			goto errout;

#if IS_ENABLED(CONFIG_IPV6)
	if (r->idiag_family == AF_INET6) {
		if (ext & (1 << (INET_DIAG_TCLASS - 1)))
			if (nla_put_u8(skb, INET_DIAG_TCLASS,
				       inet6_sk(sk)->tclass) < 0)
				goto errout;

		if (((1 << sk->sk_state) & (TCPF_LISTEN | TCPF_CLOSE)) &&
		    nla_put_u8(skb, INET_DIAG_SKV6ONLY, ipv6_only_sock(sk)))
			goto errout;
	}
#endif

	if (net_admin && nla_put_u32(skb, INET_DIAG_MARK, sk->sk_mark))
		goto errout;

	r->idiag_uid = from_kuid_munged(user_ns, sock_i_uid(sk));
	r->idiag_inode = sock_i_ino(sk);

	return 0;
errout:
	return 1;
}
EXPORT_SYMBOL_GPL(inet_diag_msg_attrs_fill);

int inet_sk_diag_fill(struct sock *sk, struct inet_connection_sock *icsk,
		      struct sk_buff *skb, const struct inet_diag_req_v2 *req,
		      struct user_namespace *user_ns,
		      u32 portid, u32 seq, u16 nlmsg_flags,
		      const struct nlmsghdr *unlh,
		      bool net_admin)
{
	const struct tcp_congestion_ops *ca_ops;
	const struct inet_diag_handler *handler;
	int ext = req->idiag_ext;
	struct inet_diag_msg *r;
	struct nlmsghdr  *nlh;
	struct nlattr *attr;
	void *info = NULL;

	handler = inet_diag_table[req->sdiag_protocol];
	BUG_ON(!handler);

	nlh = nlmsg_put(skb, portid, seq, unlh->nlmsg_type, sizeof(*r),
			nlmsg_flags);
	if (!nlh)
		return -EMSGSIZE;

	r = nlmsg_data(nlh);
	BUG_ON(!sk_fullsock(sk));

	inet_diag_msg_common_fill(r, sk);
	r->idiag_state = sk->sk_state;
	r->idiag_timer = 0;
	r->idiag_retrans = 0;

	if (inet_diag_msg_attrs_fill(sk, skb, r, ext, user_ns, net_admin))
		goto errout;

	if (ext & (1 << (INET_DIAG_MEMINFO - 1))) {
		struct inet_diag_meminfo minfo = {
			.idiag_rmem = sk_rmem_alloc_get(sk),
			.idiag_wmem = sk->sk_wmem_queued,
			.idiag_fmem = sk->sk_forward_alloc,
			.idiag_tmem = sk_wmem_alloc_get(sk),
		};

		if (nla_put(skb, INET_DIAG_MEMINFO, sizeof(minfo), &minfo) < 0)
			goto errout;
	}

	if (ext & (1 << (INET_DIAG_SKMEMINFO - 1)))
		if (sock_diag_put_meminfo(sk, skb, INET_DIAG_SKMEMINFO))
			goto errout;

	/*
	 * RAW sockets might have user-defined protocols assigned,
	 * so report the one supplied on socket creation.
	 */
	if (sk->sk_type == SOCK_RAW) {
		if (nla_put_u8(skb, INET_DIAG_PROTOCOL, sk->sk_protocol))
			goto errout;
	}

	if (!icsk) {
		handler->idiag_get_info(sk, r, NULL);
		goto out;
	}

	if (icsk->icsk_pending == ICSK_TIME_RETRANS ||
	    icsk->icsk_pending == ICSK_TIME_REO_TIMEOUT ||
	    icsk->icsk_pending == ICSK_TIME_LOSS_PROBE) {
		r->idiag_timer = 1;
		r->idiag_retrans = icsk->icsk_retransmits;
		r->idiag_expires =
			jiffies_to_msecs(icsk->icsk_timeout - jiffies);
	} else if (icsk->icsk_pending == ICSK_TIME_PROBE0) {
		r->idiag_timer = 4;
		r->idiag_retrans = icsk->icsk_probes_out;
		r->idiag_expires =
			jiffies_to_msecs(icsk->icsk_timeout - jiffies);
	} else if (timer_pending(&sk->sk_timer)) {
		r->idiag_timer = 2;
		r->idiag_retrans = icsk->icsk_probes_out;
		r->idiag_expires =
			jiffies_to_msecs(sk->sk_timer.expires - jiffies);
	} else {
		r->idiag_timer = 0;
		r->idiag_expires = 0;
	}

	if ((ext & (1 << (INET_DIAG_INFO - 1))) && handler->idiag_info_size) {
		attr = nla_reserve_64bit(skb, INET_DIAG_INFO,
					 handler->idiag_info_size,
					 INET_DIAG_PAD);
		if (!attr)
			goto errout;

		info = nla_data(attr);
	}

	if (ext & (1 << (INET_DIAG_CONG - 1))) {
		int err = 0;

		rcu_read_lock();
		ca_ops = READ_ONCE(icsk->icsk_ca_ops);
		if (ca_ops)
			err = nla_put_string(skb, INET_DIAG_CONG, ca_ops->name);
		rcu_read_unlock();
		if (err < 0)
			goto errout;
	}

	handler->idiag_get_info(sk, r, info);

	if (ext & (1 << (INET_DIAG_INFO - 1)) && handler->idiag_get_aux)
		if (handler->idiag_get_aux(sk, net_admin, skb) < 0)
			goto errout;

	if (sk->sk_state < TCP_TIME_WAIT) {
		union tcp_cc_info info;
		size_t sz = 0;
		int attr;

		rcu_read_lock();
		ca_ops = READ_ONCE(icsk->icsk_ca_ops);
		if (ca_ops && ca_ops->get_info)
			sz = ca_ops->get_info(sk, ext, &attr, &info);
		rcu_read_unlock();
		if (sz && nla_put(skb, attr, sz, &info) < 0)
			goto errout;
	}

	if (ext & (1 << (INET_DIAG_CLASS_ID - 1))) {
		u32 classid = 0;

#ifdef CONFIG_SOCK_CGROUP_DATA
		classid = sock_cgroup_classid(&sk->sk_cgrp_data);
#endif

		if (nla_put_u32(skb, INET_DIAG_CLASS_ID, classid))
			goto errout;
	}

out:
	nlmsg_end(skb, nlh);
	return 0;

errout:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}
EXPORT_SYMBOL_GPL(inet_sk_diag_fill);

static int inet_csk_diag_fill(struct sock *sk,
			      struct sk_buff *skb,
			      const struct inet_diag_req_v2 *req,
			      struct user_namespace *user_ns,
			      u32 portid, u32 seq, u16 nlmsg_flags,
			      const struct nlmsghdr *unlh,
			      bool net_admin)
{
	return inet_sk_diag_fill(sk, inet_csk(sk), skb, req, user_ns,
				 portid, seq, nlmsg_flags, unlh, net_admin);
}

static int inet_twsk_diag_fill(struct sock *sk,
			       struct sk_buff *skb,
			       u32 portid, u32 seq, u16 nlmsg_flags,
			       const struct nlmsghdr *unlh)
{
	struct inet_timewait_sock *tw = inet_twsk(sk);
	struct inet_diag_msg *r;
	struct nlmsghdr *nlh;
	long tmo;

	nlh = nlmsg_put(skb, portid, seq, unlh->nlmsg_type, sizeof(*r),
			nlmsg_flags);
	if (!nlh)
		return -EMSGSIZE;

	r = nlmsg_data(nlh);
	BUG_ON(tw->tw_state != TCP_TIME_WAIT);

	tmo = tw->tw_timer.expires - jiffies;
	if (tmo < 0)
		tmo = 0;

	inet_diag_msg_common_fill(r, sk);
	r->idiag_retrans      = 0;

	r->idiag_state	      = tw->tw_substate;
	r->idiag_timer	      = 3;
	r->idiag_expires      = jiffies_to_msecs(tmo);
	r->idiag_rqueue	      = 0;
	r->idiag_wqueue	      = 0;
	r->idiag_uid	      = 0;
	r->idiag_inode	      = 0;

	nlmsg_end(skb, nlh);
	return 0;
}

static int inet_req_diag_fill(struct sock *sk, struct sk_buff *skb,
			      u32 portid, u32 seq, u16 nlmsg_flags,
			      const struct nlmsghdr *unlh, bool net_admin)
{
	struct request_sock *reqsk = inet_reqsk(sk);
	struct inet_diag_msg *r;
	struct nlmsghdr *nlh;
	long tmo;

	nlh = nlmsg_put(skb, portid, seq, unlh->nlmsg_type, sizeof(*r),
			nlmsg_flags);
	if (!nlh)
		return -EMSGSIZE;

	r = nlmsg_data(nlh);
	inet_diag_msg_common_fill(r, sk);
	r->idiag_state = TCP_SYN_RECV;
	r->idiag_timer = 1;
	r->idiag_retrans = reqsk->num_retrans;

	BUILD_BUG_ON(offsetof(struct inet_request_sock, ir_cookie) !=
		     offsetof(struct sock, sk_cookie));

	tmo = inet_reqsk(sk)->rsk_timer.expires - jiffies;
	r->idiag_expires = (tmo >= 0) ? jiffies_to_msecs(tmo) : 0;
	r->idiag_rqueue	= 0;
	r->idiag_wqueue	= 0;
	r->idiag_uid	= 0;
	r->idiag_inode	= 0;

	if (net_admin && nla_put_u32(skb, INET_DIAG_MARK,
				     inet_rsk(reqsk)->ir_mark))
		return -EMSGSIZE;

	nlmsg_end(skb, nlh);
	return 0;
}

static int sk_diag_fill(struct sock *sk, struct sk_buff *skb,
			const struct inet_diag_req_v2 *r,
			struct user_namespace *user_ns,
			u32 portid, u32 seq, u16 nlmsg_flags,
			const struct nlmsghdr *unlh, bool net_admin)
{
	if (sk->sk_state == TCP_TIME_WAIT)
		return inet_twsk_diag_fill(sk, skb, portid, seq,
					   nlmsg_flags, unlh);

	if (sk->sk_state == TCP_NEW_SYN_RECV)
		return inet_req_diag_fill(sk, skb, portid, seq,
					  nlmsg_flags, unlh, net_admin);

	return inet_csk_diag_fill(sk, skb, r, user_ns, portid, seq,
				  nlmsg_flags, unlh, net_admin);
}

struct sock *inet_diag_find_one_icsk(struct net *net,
				     struct inet_hashinfo *hashinfo,
				     const struct inet_diag_req_v2 *req)
{
	struct sock *sk;

	rcu_read_lock();
	if (req->sdiag_family == AF_INET)
		sk = inet_lookup(net, hashinfo, NULL, 0, req->id.idiag_dst[0],
				 req->id.idiag_dport, req->id.idiag_src[0],
				 req->id.idiag_sport, req->id.idiag_if);
#if IS_ENABLED(CONFIG_IPV6)
	else if (req->sdiag_family == AF_INET6) {
		if (ipv6_addr_v4mapped((struct in6_addr *)req->id.idiag_dst) &&
		    ipv6_addr_v4mapped((struct in6_addr *)req->id.idiag_src))
			sk = inet_lookup(net, hashinfo, NULL, 0, req->id.idiag_dst[3],
					 req->id.idiag_dport, req->id.idiag_src[3],
					 req->id.idiag_sport, req->id.idiag_if);
		else
			sk = inet6_lookup(net, hashinfo, NULL, 0,
					  (struct in6_addr *)req->id.idiag_dst,
					  req->id.idiag_dport,
					  (struct in6_addr *)req->id.idiag_src,
					  req->id.idiag_sport,
					  req->id.idiag_if);
	}
#endif
	else {
		rcu_read_unlock();
		return ERR_PTR(-EINVAL);
	}
	rcu_read_unlock();
	if (!sk)
		return ERR_PTR(-ENOENT);

	if (sock_diag_check_cookie(sk, req->id.idiag_cookie)) {
		sock_gen_put(sk);
		return ERR_PTR(-ENOENT);
	}

	return sk;
}
EXPORT_SYMBOL_GPL(inet_diag_find_one_icsk);

int inet_diag_dump_one_icsk(struct inet_hashinfo *hashinfo,
			    struct sk_buff *in_skb,
			    const struct nlmsghdr *nlh,
			    const struct inet_diag_req_v2 *req)
{
	bool net_admin = netlink_net_capable(in_skb, CAP_NET_ADMIN);
	struct net *net = sock_net(in_skb->sk);
	struct sk_buff *rep;
	struct sock *sk;
	int err;

	sk = inet_diag_find_one_icsk(net, hashinfo, req);
	if (IS_ERR(sk))
		return PTR_ERR(sk);

	rep = nlmsg_new(inet_sk_attr_size(sk, req, net_admin), GFP_KERNEL);
	if (!rep) {
		err = -ENOMEM;
		goto out;
	}

	err = sk_diag_fill(sk, rep, req,
			   sk_user_ns(NETLINK_CB(in_skb).sk),
			   NETLINK_CB(in_skb).portid,
			   nlh->nlmsg_seq, 0, nlh, net_admin);
	if (err < 0) {
		WARN_ON(err == -EMSGSIZE);
		nlmsg_free(rep);
		goto out;
	}
	err = netlink_unicast(net->diag_nlsk, rep, NETLINK_CB(in_skb).portid,
			      MSG_DONTWAIT);
	if (err > 0)
		err = 0;

out:
	if (sk)
		sock_gen_put(sk);

	return err;
}
EXPORT_SYMBOL_GPL(inet_diag_dump_one_icsk);

static int inet_diag_cmd_exact(int cmd, struct sk_buff *in_skb,
			       const struct nlmsghdr *nlh,
			       const struct inet_diag_req_v2 *req)
{
	const struct inet_diag_handler *handler;
	int err;

	handler = inet_diag_lock_handler(req->sdiag_protocol);
	if (IS_ERR(handler))
		err = PTR_ERR(handler);
	else if (cmd == SOCK_DIAG_BY_FAMILY)
		err = handler->dump_one(in_skb, nlh, req);
	else if (cmd == SOCK_DESTROY && handler->destroy)
		err = handler->destroy(in_skb, req);
	else
		err = -EOPNOTSUPP;
	inet_diag_unlock_handler(handler);

	return err;
}

static int bitstring_match(const __be32 *a1, const __be32 *a2, int bits)
{
	int words = bits >> 5;

	bits &= 0x1f;

	if (words) {
		if (memcmp(a1, a2, words << 2))
			return 0;
	}
	if (bits) {
		__be32 w1, w2;
		__be32 mask;

		w1 = a1[words];
		w2 = a2[words];

		mask = htonl((0xffffffff) << (32 - bits));

		if ((w1 ^ w2) & mask)
			return 0;
	}

	return 1;
}

static int inet_diag_bc_run(const struct nlattr *_bc,
			    const struct inet_diag_entry *entry)
{
	const void *bc = nla_data(_bc);
	int len = nla_len(_bc);

	while (len > 0) {
		int yes = 1;
		const struct inet_diag_bc_op *op = bc;

		switch (op->code) {
		case INET_DIAG_BC_NOP:
			break;
		case INET_DIAG_BC_JMP:
			yes = 0;
			break;
		case INET_DIAG_BC_S_EQ:
			yes = entry->sport == op[1].no;
			break;
		case INET_DIAG_BC_S_GE:
			yes = entry->sport >= op[1].no;
			break;
		case INET_DIAG_BC_S_LE:
			yes = entry->sport <= op[1].no;
			break;
		case INET_DIAG_BC_D_EQ:
			yes = entry->dport == op[1].no;
			break;
		case INET_DIAG_BC_D_GE:
			yes = entry->dport >= op[1].no;
			break;
		case INET_DIAG_BC_D_LE:
			yes = entry->dport <= op[1].no;
			break;
		case INET_DIAG_BC_AUTO:
			yes = !(entry->userlocks & SOCK_BINDPORT_LOCK);
			break;
		case INET_DIAG_BC_S_COND:
		case INET_DIAG_BC_D_COND: {
			const struct inet_diag_hostcond *cond;
			const __be32 *addr;

			cond = (const struct inet_diag_hostcond *)(op + 1);
			if (cond->port != -1 &&
			    cond->port != (op->code == INET_DIAG_BC_S_COND ?
					     entry->sport : entry->dport)) {
				yes = 0;
				break;
			}

			if (op->code == INET_DIAG_BC_S_COND)
				addr = entry->saddr;
			else
				addr = entry->daddr;

			if (cond->family != AF_UNSPEC &&
			    cond->family != entry->family) {
				if (entry->family == AF_INET6 &&
				    cond->family == AF_INET) {
					if (addr[0] == 0 && addr[1] == 0 &&
					    addr[2] == htonl(0xffff) &&
					    bitstring_match(addr + 3,
							    cond->addr,
							    cond->prefix_len))
						break;
				}
				yes = 0;
				break;
			}

			if (cond->prefix_len == 0)
				break;
			if (bitstring_match(addr, cond->addr,
					    cond->prefix_len))
				break;
			yes = 0;
			break;
		}
		case INET_DIAG_BC_DEV_COND: {
			u32 ifindex;

			ifindex = *((const u32 *)(op + 1));
			if (ifindex != entry->ifindex)
				yes = 0;
			break;
		}
		case INET_DIAG_BC_MARK_COND: {
			struct inet_diag_markcond *cond;

			cond = (struct inet_diag_markcond *)(op + 1);
			if ((entry->mark & cond->mask) != cond->mark)
				yes = 0;
			break;
		}
		}

		if (yes) {
			len -= op->yes;
			bc += op->yes;
		} else {
			len -= op->no;
			bc += op->no;
		}
	}
	return len == 0;
}

/* This helper is available for all sockets (ESTABLISH, TIMEWAIT, SYN_RECV)
 */
static void entry_fill_addrs(struct inet_diag_entry *entry,
			     const struct sock *sk)
{
#if IS_ENABLED(CONFIG_IPV6)
	if (sk->sk_family == AF_INET6) {
		entry->saddr = sk->sk_v6_rcv_saddr.s6_addr32;
		entry->daddr = sk->sk_v6_daddr.s6_addr32;
	} else
#endif
	{
		entry->saddr = &sk->sk_rcv_saddr;
		entry->daddr = &sk->sk_daddr;
	}
}

int inet_diag_bc_sk(const struct nlattr *bc, struct sock *sk)
{
	struct inet_sock *inet = inet_sk(sk);
	struct inet_diag_entry entry;

	if (!bc)
		return 1;

	entry.family = sk->sk_family;
	entry_fill_addrs(&entry, sk);
	entry.sport = inet->inet_num;
	entry.dport = ntohs(inet->inet_dport);
	entry.ifindex = sk->sk_bound_dev_if;
	entry.userlocks = sk_fullsock(sk) ? sk->sk_userlocks : 0;
	if (sk_fullsock(sk))
		entry.mark = sk->sk_mark;
	else if (sk->sk_state == TCP_NEW_SYN_RECV)
		entry.mark = inet_rsk(inet_reqsk(sk))->ir_mark;
	else
		entry.mark = 0;

	return inet_diag_bc_run(bc, &entry);
}
EXPORT_SYMBOL_GPL(inet_diag_bc_sk);

static int valid_cc(const void *bc, int len, int cc)
{
	while (len >= 0) {
		const struct inet_diag_bc_op *op = bc;

		if (cc > len)
			return 0;
		if (cc == len)
			return 1;
		if (op->yes < 4 || op->yes & 3)
			return 0;
		len -= op->yes;
		bc  += op->yes;
	}
	return 0;
}

/* data is u32 ifindex */
static bool valid_devcond(const struct inet_diag_bc_op *op, int len,
			  int *min_len)
{
	/* Check ifindex space. */
	*min_len += sizeof(u32);
	if (len < *min_len)
		return false;

	return true;
}
/* Validate an inet_diag_hostcond. */
static bool valid_hostcond(const struct inet_diag_bc_op *op, int len,
			   int *min_len)
{
	struct inet_diag_hostcond *cond;
	int addr_len;

	/* Check hostcond space. */
	*min_len += sizeof(struct inet_diag_hostcond);
	if (len < *min_len)
		return false;
	cond = (struct inet_diag_hostcond *)(op + 1);

	/* Check address family and address length. */
	switch (cond->family) {
	case AF_UNSPEC:
		addr_len = 0;
		break;
	case AF_INET:
		addr_len = sizeof(struct in_addr);
		break;
	case AF_INET6:
		addr_len = sizeof(struct in6_addr);
		break;
	default:
		return false;
	}
	*min_len += addr_len;
	if (len < *min_len)
		return false;

	/* Check prefix length (in bits) vs address length (in bytes). */
	if (cond->prefix_len > 8 * addr_len)
		return false;

	return true;
}

/* Validate a port comparison operator. */
static bool valid_port_comparison(const struct inet_diag_bc_op *op,
				  int len, int *min_len)
{
	/* Port comparisons put the port in a follow-on inet_diag_bc_op. */
	*min_len += sizeof(struct inet_diag_bc_op);
	if (len < *min_len)
		return false;
	return true;
}

static bool valid_markcond(const struct inet_diag_bc_op *op, int len,
			   int *min_len)
{
	*min_len += sizeof(struct inet_diag_markcond);
	return len >= *min_len;
}

static int inet_diag_bc_audit(const struct nlattr *attr,
			      const struct sk_buff *skb)
{
	bool net_admin = netlink_net_capable(skb, CAP_NET_ADMIN);
	const void *bytecode, *bc;
	int bytecode_len, len;

	if (!attr || nla_len(attr) < sizeof(struct inet_diag_bc_op))
		return -EINVAL;

	bytecode = bc = nla_data(attr);
	len = bytecode_len = nla_len(attr);

	while (len > 0) {
		int min_len = sizeof(struct inet_diag_bc_op);
		const struct inet_diag_bc_op *op = bc;

		switch (op->code) {
		case INET_DIAG_BC_S_COND:
		case INET_DIAG_BC_D_COND:
			if (!valid_hostcond(bc, len, &min_len))
				return -EINVAL;
			break;
		case INET_DIAG_BC_DEV_COND:
			if (!valid_devcond(bc, len, &min_len))
				return -EINVAL;
			break;
		case INET_DIAG_BC_S_EQ:
		case INET_DIAG_BC_S_GE:
		case INET_DIAG_BC_S_LE:
		case INET_DIAG_BC_D_EQ:
		case INET_DIAG_BC_D_GE:
		case INET_DIAG_BC_D_LE:
			if (!valid_port_comparison(bc, len, &min_len))
				return -EINVAL;
			break;
		case INET_DIAG_BC_MARK_COND:
			if (!net_admin)
				return -EPERM;
			if (!valid_markcond(bc, len, &min_len))
				return -EINVAL;
			break;
		case INET_DIAG_BC_AUTO:
		case INET_DIAG_BC_JMP:
		case INET_DIAG_BC_NOP:
			break;
		default:
			return -EINVAL;
		}

		if (op->code != INET_DIAG_BC_NOP) {
			if (op->no < min_len || op->no > len + 4 || op->no & 3)
				return -EINVAL;
			if (op->no < len &&
			    !valid_cc(bytecode, bytecode_len, len - op->no))
				return -EINVAL;
		}

		if (op->yes < min_len || op->yes > len + 4 || op->yes & 3)
			return -EINVAL;
		bc  += op->yes;
		len -= op->yes;
	}
	return len == 0 ? 0 : -EINVAL;
}

static int inet_csk_diag_dump(struct sock *sk,
			      struct sk_buff *skb,
			      struct netlink_callback *cb,
			      const struct inet_diag_req_v2 *r,
			      const struct nlattr *bc,
			      bool net_admin)
{
	if (!inet_diag_bc_sk(bc, sk))
		return 0;

	return inet_csk_diag_fill(sk, skb, r,
				  sk_user_ns(NETLINK_CB(cb->skb).sk),
				  NETLINK_CB(cb->skb).portid,
				  cb->nlh->nlmsg_seq, NLM_F_MULTI, cb->nlh,
				  net_admin);
}

static void twsk_build_assert(void)
{
	BUILD_BUG_ON(offsetof(struct inet_timewait_sock, tw_family) !=
		     offsetof(struct sock, sk_family));

	BUILD_BUG_ON(offsetof(struct inet_timewait_sock, tw_num) !=
		     offsetof(struct inet_sock, inet_num));

	BUILD_BUG_ON(offsetof(struct inet_timewait_sock, tw_dport) !=
		     offsetof(struct inet_sock, inet_dport));

	BUILD_BUG_ON(offsetof(struct inet_timewait_sock, tw_rcv_saddr) !=
		     offsetof(struct inet_sock, inet_rcv_saddr));

	BUILD_BUG_ON(offsetof(struct inet_timewait_sock, tw_daddr) !=
		     offsetof(struct inet_sock, inet_daddr));

#if IS_ENABLED(CONFIG_IPV6)
	BUILD_BUG_ON(offsetof(struct inet_timewait_sock, tw_v6_rcv_saddr) !=
		     offsetof(struct sock, sk_v6_rcv_saddr));

	BUILD_BUG_ON(offsetof(struct inet_timewait_sock, tw_v6_daddr) !=
		     offsetof(struct sock, sk_v6_daddr));
#endif
}

void inet_diag_dump_icsk(struct inet_hashinfo *hashinfo, struct sk_buff *skb,
			 struct netlink_callback *cb,
			 const struct inet_diag_req_v2 *r, struct nlattr *bc)
{
	bool net_admin = netlink_net_capable(cb->skb, CAP_NET_ADMIN);
	struct net *net = sock_net(skb->sk);
	u32 idiag_states = r->idiag_states;
	int i, num, s_i, s_num;
	struct sock *sk;

	if (idiag_states & TCPF_SYN_RECV)
		idiag_states |= TCPF_NEW_SYN_RECV;
	s_i = cb->args[1];
	s_num = num = cb->args[2];

	if (cb->args[0] == 0) {
		if (!(idiag_states & TCPF_LISTEN) || r->id.idiag_dport)
			goto skip_listen_ht;

		for (i = s_i; i < INET_LHTABLE_SIZE; i++) {
			struct inet_listen_hashbucket *ilb;

			num = 0;
			ilb = &hashinfo->listening_hash[i];
			spin_lock(&ilb->lock);
			sk_for_each(sk, &ilb->head) {
				struct inet_sock *inet = inet_sk(sk);

				if (!net_eq(sock_net(sk), net))
					continue;

				if (num < s_num) {
					num++;
					continue;
				}

				if (r->sdiag_family != AF_UNSPEC &&
				    sk->sk_family != r->sdiag_family)
					goto next_listen;

				if (r->id.idiag_sport != inet->inet_sport &&
				    r->id.idiag_sport)
					goto next_listen;

				if (inet_csk_diag_dump(sk, skb, cb, r,
						       bc, net_admin) < 0) {
					spin_unlock(&ilb->lock);
					goto done;
				}

next_listen:
				++num;
			}
			spin_unlock(&ilb->lock);

			s_num = 0;
		}
skip_listen_ht:
		cb->args[0] = 1;
		s_i = num = s_num = 0;
	}

	if (!(idiag_states & ~TCPF_LISTEN))
		goto out;

#define SKARR_SZ 16
	for (i = s_i; i <= hashinfo->ehash_mask; i++) {
		struct inet_ehash_bucket *head = &hashinfo->ehash[i];
		spinlock_t *lock = inet_ehash_lockp(hashinfo, i);
		struct hlist_nulls_node *node;
		struct sock *sk_arr[SKARR_SZ];
		int num_arr[SKARR_SZ];
		int idx, accum, res;

		if (hlist_nulls_empty(&head->chain))
			continue;

		if (i > s_i)
			s_num = 0;

next_chunk:
		num = 0;
		accum = 0;
		spin_lock_bh(lock);
		sk_nulls_for_each(sk, node, &head->chain) {
			int state;

			if (!net_eq(sock_net(sk), net))
				continue;
			if (num < s_num)
				goto next_normal;
			state = (sk->sk_state == TCP_TIME_WAIT) ?
				inet_twsk(sk)->tw_substate : sk->sk_state;
			if (!(idiag_states & (1 << state)))
				goto next_normal;
			if (r->sdiag_family != AF_UNSPEC &&
			    sk->sk_family != r->sdiag_family)
				goto next_normal;
			if (r->id.idiag_sport != htons(sk->sk_num) &&
			    r->id.idiag_sport)
				goto next_normal;
			if (r->id.idiag_dport != sk->sk_dport &&
			    r->id.idiag_dport)
				goto next_normal;
			twsk_build_assert();

			if (!inet_diag_bc_sk(bc, sk))
				goto next_normal;

			if (!refcount_inc_not_zero(&sk->sk_refcnt))
				goto next_normal;

			num_arr[accum] = num;
			sk_arr[accum] = sk;
			if (++accum == SKARR_SZ)
				break;
next_normal:
			++num;
		}
		spin_unlock_bh(lock);
		res = 0;
		for (idx = 0; idx < accum; idx++) {
			if (res >= 0) {
				res = sk_diag_fill(sk_arr[idx], skb, r,
					   sk_user_ns(NETLINK_CB(cb->skb).sk),
					   NETLINK_CB(cb->skb).portid,
					   cb->nlh->nlmsg_seq, NLM_F_MULTI,
					   cb->nlh, net_admin);
				if (res < 0)
					num = num_arr[idx];
			}
			sock_gen_put(sk_arr[idx]);
		}
		if (res < 0)
			break;
		cond_resched();
		if (accum == SKARR_SZ) {
			s_num = num + 1;
			goto next_chunk;
		}
	}

done:
	cb->args[1] = i;
	cb->args[2] = num;
out:
	;
}
EXPORT_SYMBOL_GPL(inet_diag_dump_icsk);

static int __inet_diag_dump(struct sk_buff *skb, struct netlink_callback *cb,
			    const struct inet_diag_req_v2 *r,
			    struct nlattr *bc)
{
	const struct inet_diag_handler *handler;
	int err = 0;

	handler = inet_diag_lock_handler(r->sdiag_protocol);
	if (!IS_ERR(handler))
		handler->dump(skb, cb, r, bc);
	else
		err = PTR_ERR(handler);
	inet_diag_unlock_handler(handler);

	return err ? : skb->len;
}

static int inet_diag_dump(struct sk_buff *skb, struct netlink_callback *cb)
{
	int hdrlen = sizeof(struct inet_diag_req_v2);
	struct nlattr *bc = NULL;

	if (nlmsg_attrlen(cb->nlh, hdrlen))
		bc = nlmsg_find_attr(cb->nlh, hdrlen, INET_DIAG_REQ_BYTECODE);

	return __inet_diag_dump(skb, cb, nlmsg_data(cb->nlh), bc);
}

static int inet_diag_type2proto(int type)
{
	switch (type) {
	case TCPDIAG_GETSOCK:
		return IPPROTO_TCP;
	case DCCPDIAG_GETSOCK:
		return IPPROTO_DCCP;
	default:
		return 0;
	}
}

static int inet_diag_dump_compat(struct sk_buff *skb,
				 struct netlink_callback *cb)
{
	struct inet_diag_req *rc = nlmsg_data(cb->nlh);
	int hdrlen = sizeof(struct inet_diag_req);
	struct inet_diag_req_v2 req;
	struct nlattr *bc = NULL;

	req.sdiag_family = AF_UNSPEC; /* compatibility */
	req.sdiag_protocol = inet_diag_type2proto(cb->nlh->nlmsg_type);
	req.idiag_ext = rc->idiag_ext;
	req.idiag_states = rc->idiag_states;
	req.id = rc->id;

	if (nlmsg_attrlen(cb->nlh, hdrlen))
		bc = nlmsg_find_attr(cb->nlh, hdrlen, INET_DIAG_REQ_BYTECODE);

	return __inet_diag_dump(skb, cb, &req, bc);
}

static int inet_diag_get_exact_compat(struct sk_buff *in_skb,
				      const struct nlmsghdr *nlh)
{
	struct inet_diag_req *rc = nlmsg_data(nlh);
	struct inet_diag_req_v2 req;

	req.sdiag_family = rc->idiag_family;
	req.sdiag_protocol = inet_diag_type2proto(nlh->nlmsg_type);
	req.idiag_ext = rc->idiag_ext;
	req.idiag_states = rc->idiag_states;
	req.id = rc->id;

	return inet_diag_cmd_exact(SOCK_DIAG_BY_FAMILY, in_skb, nlh, &req);
}

static int inet_diag_rcv_msg_compat(struct sk_buff *skb, struct nlmsghdr *nlh)
{
	int hdrlen = sizeof(struct inet_diag_req);
	struct net *net = sock_net(skb->sk);

	if (nlh->nlmsg_type >= INET_DIAG_GETSOCK_MAX ||
	    nlmsg_len(nlh) < hdrlen)
		return -EINVAL;

	if (nlh->nlmsg_flags & NLM_F_DUMP) {
		if (nlmsg_attrlen(nlh, hdrlen)) {
			struct nlattr *attr;
			int err;

			attr = nlmsg_find_attr(nlh, hdrlen,
					       INET_DIAG_REQ_BYTECODE);
			err = inet_diag_bc_audit(attr, skb);
			if (err)
				return err;
		}
		{
			struct netlink_dump_control c = {
				.dump = inet_diag_dump_compat,
			};
			return netlink_dump_start(net->diag_nlsk, skb, nlh, &c);
		}
	}

	return inet_diag_get_exact_compat(skb, nlh);
}

static int inet_diag_handler_cmd(struct sk_buff *skb, struct nlmsghdr *h)
{
	int hdrlen = sizeof(struct inet_diag_req_v2);
	struct net *net = sock_net(skb->sk);

	if (nlmsg_len(h) < hdrlen)
		return -EINVAL;

	if (h->nlmsg_type == SOCK_DIAG_BY_FAMILY &&
	    h->nlmsg_flags & NLM_F_DUMP) {
		if (nlmsg_attrlen(h, hdrlen)) {
			struct nlattr *attr;
			int err;

			attr = nlmsg_find_attr(h, hdrlen,
					       INET_DIAG_REQ_BYTECODE);
			err = inet_diag_bc_audit(attr, skb);
			if (err)
				return err;
		}
		{
			struct netlink_dump_control c = {
				.dump = inet_diag_dump,
			};
			return netlink_dump_start(net->diag_nlsk, skb, h, &c);
		}
	}

	return inet_diag_cmd_exact(h->nlmsg_type, skb, h, nlmsg_data(h));
}

static
int inet_diag_handler_get_info(struct sk_buff *skb, struct sock *sk)
{
	const struct inet_diag_handler *handler;
	struct nlmsghdr *nlh;
	struct nlattr *attr;
	struct inet_diag_msg *r;
	void *info = NULL;
	int err = 0;

	nlh = nlmsg_put(skb, 0, 0, SOCK_DIAG_BY_FAMILY, sizeof(*r), 0);
	if (!nlh)
		return -ENOMEM;

	r = nlmsg_data(nlh);
	memset(r, 0, sizeof(*r));
	inet_diag_msg_common_fill(r, sk);
	if (sk->sk_type == SOCK_DGRAM || sk->sk_type == SOCK_STREAM)
		r->id.idiag_sport = inet_sk(sk)->inet_sport;
	r->idiag_state = sk->sk_state;

	if ((err = nla_put_u8(skb, INET_DIAG_PROTOCOL, sk->sk_protocol))) {
		nlmsg_cancel(skb, nlh);
		return err;
	}

	handler = inet_diag_lock_handler(sk->sk_protocol);
	if (IS_ERR(handler)) {
		inet_diag_unlock_handler(handler);
		nlmsg_cancel(skb, nlh);
		return PTR_ERR(handler);
	}

	attr = handler->idiag_info_size
		? nla_reserve_64bit(skb, INET_DIAG_INFO,
				    handler->idiag_info_size,
				    INET_DIAG_PAD)
		: NULL;
	if (attr)
		info = nla_data(attr);

	handler->idiag_get_info(sk, r, info);
	inet_diag_unlock_handler(handler);

	nlmsg_end(skb, nlh);
	return 0;
}

static const struct sock_diag_handler inet_diag_handler = {
	.family = AF_INET,
	.dump = inet_diag_handler_cmd,
	.get_info = inet_diag_handler_get_info,
	.destroy = inet_diag_handler_cmd,
};

static const struct sock_diag_handler inet6_diag_handler = {
	.family = AF_INET6,
	.dump = inet_diag_handler_cmd,
	.get_info = inet_diag_handler_get_info,
	.destroy = inet_diag_handler_cmd,
};

int inet_diag_register(const struct inet_diag_handler *h)
{
	const __u16 type = h->idiag_type;
	int err = -EINVAL;

	if (type >= IPPROTO_MAX)
		goto out;

	mutex_lock(&inet_diag_table_mutex);
	err = -EEXIST;
	if (!inet_diag_table[type]) {
		inet_diag_table[type] = h;
		err = 0;
	}
	mutex_unlock(&inet_diag_table_mutex);
out:
	return err;
}
EXPORT_SYMBOL_GPL(inet_diag_register);

void inet_diag_unregister(const struct inet_diag_handler *h)
{
	const __u16 type = h->idiag_type;

	if (type >= IPPROTO_MAX)
		return;

	mutex_lock(&inet_diag_table_mutex);
	inet_diag_table[type] = NULL;
	mutex_unlock(&inet_diag_table_mutex);
}
EXPORT_SYMBOL_GPL(inet_diag_unregister);

static int __init inet_diag_init(void)
{
	const int inet_diag_table_size = (IPPROTO_MAX *
					  sizeof(struct inet_diag_handler *));
	int err = -ENOMEM;

	inet_diag_table = kzalloc(inet_diag_table_size, GFP_KERNEL);
	if (!inet_diag_table)
		goto out;

	err = sock_diag_register(&inet_diag_handler);
	if (err)
		goto out_free_nl;

	err = sock_diag_register(&inet6_diag_handler);
	if (err)
		goto out_free_inet;

	sock_diag_register_inet_compat(inet_diag_rcv_msg_compat);
out:
	return err;

out_free_inet:
	sock_diag_unregister(&inet_diag_handler);
out_free_nl:
	kfree(inet_diag_table);
	goto out;
}

static void __exit inet_diag_exit(void)
{
	sock_diag_unregister(&inet6_diag_handler);
	sock_diag_unregister(&inet_diag_handler);
	sock_diag_unregister_inet_compat(inet_diag_rcv_msg_compat);
	kfree(inet_diag_table);
}

module_init(inet_diag_init);
module_exit(inet_diag_exit);
MODULE_LICENSE("GPL");
MODULE_ALIAS_NET_PF_PROTO_TYPE(PF_NETLINK, NETLINK_SOCK_DIAG, 2 /* AF_INET */);
MODULE_ALIAS_NET_PF_PROTO_TYPE(PF_NETLINK, NETLINK_SOCK_DIAG, 10 /* AF_INET6 */);
