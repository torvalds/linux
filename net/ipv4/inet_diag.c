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
	__be32 *saddr;
	__be32 *daddr;
	u16 sport;
	u16 dport;
	u16 family;
	u16 userlocks;
};

static struct sock *sdiagnl;

#define INET_DIAG_PUT(skb, attrtype, attrlen) \
	RTA_DATA(__RTA_PUT(skb, attrtype, attrlen))

static inline int inet_diag_type2proto(int type)
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

static DEFINE_MUTEX(inet_diag_table_mutex);

static const struct inet_diag_handler *inet_diag_lock_handler(int proto)
{
	if (!inet_diag_table[proto])
		request_module("net-pf-%d-proto-%d-type-%d", PF_NETLINK,
			       NETLINK_SOCK_DIAG, proto);

	mutex_lock(&inet_diag_table_mutex);
	if (!inet_diag_table[proto])
		return ERR_PTR(-ENOENT);

	return inet_diag_table[proto];
}

static inline void inet_diag_unlock_handler(
	const struct inet_diag_handler *handler)
{
	mutex_unlock(&inet_diag_table_mutex);
}

static int inet_csk_diag_fill(struct sock *sk,
			      struct sk_buff *skb,
			      int ext, u32 pid, u32 seq, u16 nlmsg_flags,
			      const struct nlmsghdr *unlh)
{
	const struct inet_sock *inet = inet_sk(sk);
	const struct inet_connection_sock *icsk = inet_csk(sk);
	struct inet_diag_msg *r;
	struct nlmsghdr  *nlh;
	void *info = NULL;
	struct inet_diag_meminfo  *minfo = NULL;
	unsigned char	 *b = skb_tail_pointer(skb);
	const struct inet_diag_handler *handler;

	handler = inet_diag_table[inet_diag_type2proto(unlh->nlmsg_type)];
	BUG_ON(handler == NULL);

	nlh = NLMSG_PUT(skb, pid, seq, unlh->nlmsg_type, sizeof(*r));
	nlh->nlmsg_flags = nlmsg_flags;

	r = NLMSG_DATA(nlh);
	BUG_ON(sk->sk_state == TCP_TIME_WAIT);

	if (ext & (1 << (INET_DIAG_MEMINFO - 1)))
		minfo = INET_DIAG_PUT(skb, INET_DIAG_MEMINFO, sizeof(*minfo));

	if (ext & (1 << (INET_DIAG_INFO - 1)))
		info = INET_DIAG_PUT(skb, INET_DIAG_INFO,
				     handler->idiag_info_size);

	if ((ext & (1 << (INET_DIAG_CONG - 1))) && icsk->icsk_ca_ops) {
		const size_t len = strlen(icsk->icsk_ca_ops->name);

		strcpy(INET_DIAG_PUT(skb, INET_DIAG_CONG, len + 1),
		       icsk->icsk_ca_ops->name);
	}

	r->idiag_family = sk->sk_family;
	r->idiag_state = sk->sk_state;
	r->idiag_timer = 0;
	r->idiag_retrans = 0;

	r->id.idiag_if = sk->sk_bound_dev_if;
	r->id.idiag_cookie[0] = (u32)(unsigned long)sk;
	r->id.idiag_cookie[1] = (u32)(((unsigned long)sk >> 31) >> 1);

	r->id.idiag_sport = inet->inet_sport;
	r->id.idiag_dport = inet->inet_dport;
	r->id.idiag_src[0] = inet->inet_rcv_saddr;
	r->id.idiag_dst[0] = inet->inet_daddr;

	/* IPv6 dual-stack sockets use inet->tos for IPv4 connections,
	 * hence this needs to be included regardless of socket family.
	 */
	if (ext & (1 << (INET_DIAG_TOS - 1)))
		RTA_PUT_U8(skb, INET_DIAG_TOS, inet->tos);

#if defined(CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
	if (r->idiag_family == AF_INET6) {
		const struct ipv6_pinfo *np = inet6_sk(sk);

		*(struct in6_addr *)r->id.idiag_src = np->rcv_saddr;
		*(struct in6_addr *)r->id.idiag_dst = np->daddr;
		if (ext & (1 << (INET_DIAG_TCLASS - 1)))
			RTA_PUT_U8(skb, INET_DIAG_TCLASS, np->tclass);
	}
#endif

#define EXPIRES_IN_MS(tmo)  DIV_ROUND_UP((tmo - jiffies) * 1000, HZ)

	if (icsk->icsk_pending == ICSK_TIME_RETRANS) {
		r->idiag_timer = 1;
		r->idiag_retrans = icsk->icsk_retransmits;
		r->idiag_expires = EXPIRES_IN_MS(icsk->icsk_timeout);
	} else if (icsk->icsk_pending == ICSK_TIME_PROBE0) {
		r->idiag_timer = 4;
		r->idiag_retrans = icsk->icsk_probes_out;
		r->idiag_expires = EXPIRES_IN_MS(icsk->icsk_timeout);
	} else if (timer_pending(&sk->sk_timer)) {
		r->idiag_timer = 2;
		r->idiag_retrans = icsk->icsk_probes_out;
		r->idiag_expires = EXPIRES_IN_MS(sk->sk_timer.expires);
	} else {
		r->idiag_timer = 0;
		r->idiag_expires = 0;
	}
#undef EXPIRES_IN_MS

	r->idiag_uid = sock_i_uid(sk);
	r->idiag_inode = sock_i_ino(sk);

	if (minfo) {
		minfo->idiag_rmem = sk_rmem_alloc_get(sk);
		minfo->idiag_wmem = sk->sk_wmem_queued;
		minfo->idiag_fmem = sk->sk_forward_alloc;
		minfo->idiag_tmem = sk_wmem_alloc_get(sk);
	}

	handler->idiag_get_info(sk, r, info);

	if (sk->sk_state < TCP_TIME_WAIT &&
	    icsk->icsk_ca_ops && icsk->icsk_ca_ops->get_info)
		icsk->icsk_ca_ops->get_info(sk, ext, skb);

	nlh->nlmsg_len = skb_tail_pointer(skb) - b;
	return skb->len;

rtattr_failure:
nlmsg_failure:
	nlmsg_trim(skb, b);
	return -EMSGSIZE;
}

static int inet_twsk_diag_fill(struct inet_timewait_sock *tw,
			       struct sk_buff *skb, int ext, u32 pid,
			       u32 seq, u16 nlmsg_flags,
			       const struct nlmsghdr *unlh)
{
	long tmo;
	struct inet_diag_msg *r;
	const unsigned char *previous_tail = skb_tail_pointer(skb);
	struct nlmsghdr *nlh = NLMSG_PUT(skb, pid, seq,
					 unlh->nlmsg_type, sizeof(*r));

	r = NLMSG_DATA(nlh);
	BUG_ON(tw->tw_state != TCP_TIME_WAIT);

	nlh->nlmsg_flags = nlmsg_flags;

	tmo = tw->tw_ttd - jiffies;
	if (tmo < 0)
		tmo = 0;

	r->idiag_family	      = tw->tw_family;
	r->idiag_retrans      = 0;
	r->id.idiag_if	      = tw->tw_bound_dev_if;
	r->id.idiag_cookie[0] = (u32)(unsigned long)tw;
	r->id.idiag_cookie[1] = (u32)(((unsigned long)tw >> 31) >> 1);
	r->id.idiag_sport     = tw->tw_sport;
	r->id.idiag_dport     = tw->tw_dport;
	r->id.idiag_src[0]    = tw->tw_rcv_saddr;
	r->id.idiag_dst[0]    = tw->tw_daddr;
	r->idiag_state	      = tw->tw_substate;
	r->idiag_timer	      = 3;
	r->idiag_expires      = DIV_ROUND_UP(tmo * 1000, HZ);
	r->idiag_rqueue	      = 0;
	r->idiag_wqueue	      = 0;
	r->idiag_uid	      = 0;
	r->idiag_inode	      = 0;
#if defined(CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
	if (tw->tw_family == AF_INET6) {
		const struct inet6_timewait_sock *tw6 =
						inet6_twsk((struct sock *)tw);

		*(struct in6_addr *)r->id.idiag_src = tw6->tw_v6_rcv_saddr;
		*(struct in6_addr *)r->id.idiag_dst = tw6->tw_v6_daddr;
	}
#endif
	nlh->nlmsg_len = skb_tail_pointer(skb) - previous_tail;
	return skb->len;
nlmsg_failure:
	nlmsg_trim(skb, previous_tail);
	return -EMSGSIZE;
}

static int sk_diag_fill(struct sock *sk, struct sk_buff *skb,
			int ext, u32 pid, u32 seq, u16 nlmsg_flags,
			const struct nlmsghdr *unlh)
{
	if (sk->sk_state == TCP_TIME_WAIT)
		return inet_twsk_diag_fill((struct inet_timewait_sock *)sk,
					   skb, ext, pid, seq, nlmsg_flags,
					   unlh);
	return inet_csk_diag_fill(sk, skb, ext, pid, seq, nlmsg_flags, unlh);
}

static int inet_diag_get_exact(struct sk_buff *in_skb,
			       const struct nlmsghdr *nlh,
			       struct inet_diag_req *req)
{
	int err;
	struct sock *sk;
	struct sk_buff *rep;
	struct inet_hashinfo *hashinfo;
	const struct inet_diag_handler *handler;

	handler = inet_diag_lock_handler(req->sdiag_protocol);
	if (IS_ERR(handler)) {
		err = PTR_ERR(handler);
		goto unlock;
	}

	hashinfo = handler->idiag_hashinfo;
	err = -EINVAL;

	if (req->sdiag_family == AF_INET) {
		sk = inet_lookup(&init_net, hashinfo, req->id.idiag_dst[0],
				 req->id.idiag_dport, req->id.idiag_src[0],
				 req->id.idiag_sport, req->id.idiag_if);
	}
#if defined(CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
	else if (req->sdiag_family == AF_INET6) {
		sk = inet6_lookup(&init_net, hashinfo,
				  (struct in6_addr *)req->id.idiag_dst,
				  req->id.idiag_dport,
				  (struct in6_addr *)req->id.idiag_src,
				  req->id.idiag_sport,
				  req->id.idiag_if);
	}
#endif
	else {
		goto unlock;
	}

	err = -ENOENT;
	if (sk == NULL)
		goto unlock;

	err = -ESTALE;
	if ((req->id.idiag_cookie[0] != INET_DIAG_NOCOOKIE ||
	     req->id.idiag_cookie[1] != INET_DIAG_NOCOOKIE) &&
	    ((u32)(unsigned long)sk != req->id.idiag_cookie[0] ||
	     (u32)((((unsigned long)sk) >> 31) >> 1) != req->id.idiag_cookie[1]))
		goto out;

	err = -ENOMEM;
	rep = alloc_skb(NLMSG_SPACE((sizeof(struct inet_diag_msg) +
				     sizeof(struct inet_diag_meminfo) +
				     handler->idiag_info_size + 64)),
			GFP_KERNEL);
	if (!rep)
		goto out;

	err = sk_diag_fill(sk, rep, req->idiag_ext,
			   NETLINK_CB(in_skb).pid,
			   nlh->nlmsg_seq, 0, nlh);
	if (err < 0) {
		WARN_ON(err == -EMSGSIZE);
		kfree_skb(rep);
		goto out;
	}
	err = netlink_unicast(sdiagnl, rep, NETLINK_CB(in_skb).pid,
			      MSG_DONTWAIT);
	if (err > 0)
		err = 0;

out:
	if (sk) {
		if (sk->sk_state == TCP_TIME_WAIT)
			inet_twsk_put((struct inet_timewait_sock *)sk);
		else
			sock_put(sk);
	}
unlock:
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


static int inet_diag_bc_run(const void *bc, int len,
			    const struct inet_diag_entry *entry)
{
	while (len > 0) {
		int yes = 1;
		const struct inet_diag_bc_op *op = bc;

		switch (op->code) {
		case INET_DIAG_BC_NOP:
			break;
		case INET_DIAG_BC_JMP:
			yes = 0;
			break;
		case INET_DIAG_BC_S_GE:
			yes = entry->sport >= op[1].no;
			break;
		case INET_DIAG_BC_S_LE:
			yes = entry->sport <= op[1].no;
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
			struct inet_diag_hostcond *cond;
			__be32 *addr;

			cond = (struct inet_diag_hostcond *)(op + 1);
			if (cond->port != -1 &&
			    cond->port != (op->code == INET_DIAG_BC_S_COND ?
					     entry->sport : entry->dport)) {
				yes = 0;
				break;
			}

			if (cond->prefix_len == 0)
				break;

			if (op->code == INET_DIAG_BC_S_COND)
				addr = entry->saddr;
			else
				addr = entry->daddr;

			if (bitstring_match(addr, cond->addr,
					    cond->prefix_len))
				break;
			if (entry->family == AF_INET6 &&
			    cond->family == AF_INET) {
				if (addr[0] == 0 && addr[1] == 0 &&
				    addr[2] == htonl(0xffff) &&
				    bitstring_match(addr + 3, cond->addr,
						    cond->prefix_len))
					break;
			}
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

static int inet_diag_bc_audit(const void *bytecode, int bytecode_len)
{
	const void *bc = bytecode;
	int  len = bytecode_len;

	while (len > 0) {
		const struct inet_diag_bc_op *op = bc;

//printk("BC: %d %d %d {%d} / %d\n", op->code, op->yes, op->no, op[1].no, len);
		switch (op->code) {
		case INET_DIAG_BC_AUTO:
		case INET_DIAG_BC_S_COND:
		case INET_DIAG_BC_D_COND:
		case INET_DIAG_BC_S_GE:
		case INET_DIAG_BC_S_LE:
		case INET_DIAG_BC_D_GE:
		case INET_DIAG_BC_D_LE:
		case INET_DIAG_BC_JMP:
			if (op->no < 4 || op->no > len + 4 || op->no & 3)
				return -EINVAL;
			if (op->no < len &&
			    !valid_cc(bytecode, bytecode_len, len - op->no))
				return -EINVAL;
			break;
		case INET_DIAG_BC_NOP:
			break;
		default:
			return -EINVAL;
		}
		if (op->yes < 4 || op->yes > len + 4 || op->yes & 3)
			return -EINVAL;
		bc  += op->yes;
		len -= op->yes;
	}
	return len == 0 ? 0 : -EINVAL;
}

static int inet_csk_diag_dump(struct sock *sk,
			      struct sk_buff *skb,
			      struct netlink_callback *cb,
			      const struct nlattr *bc)
{
	struct inet_diag_req_compat *r = NLMSG_DATA(cb->nlh);

	if (bc != NULL) {
		struct inet_diag_entry entry;
		struct inet_sock *inet = inet_sk(sk);

		entry.family = sk->sk_family;
#if defined(CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
		if (entry.family == AF_INET6) {
			struct ipv6_pinfo *np = inet6_sk(sk);

			entry.saddr = np->rcv_saddr.s6_addr32;
			entry.daddr = np->daddr.s6_addr32;
		} else
#endif
		{
			entry.saddr = &inet->inet_rcv_saddr;
			entry.daddr = &inet->inet_daddr;
		}
		entry.sport = inet->inet_num;
		entry.dport = ntohs(inet->inet_dport);
		entry.userlocks = sk->sk_userlocks;

		if (!inet_diag_bc_run(nla_data(bc), nla_len(bc), &entry))
			return 0;
	}

	return inet_csk_diag_fill(sk, skb, r->idiag_ext,
				  NETLINK_CB(cb->skb).pid,
				  cb->nlh->nlmsg_seq, NLM_F_MULTI, cb->nlh);
}

static int inet_twsk_diag_dump(struct inet_timewait_sock *tw,
			       struct sk_buff *skb,
			       struct netlink_callback *cb,
			       const struct nlattr *bc)
{
	struct inet_diag_req_compat *r = NLMSG_DATA(cb->nlh);

	if (bc != NULL) {
		struct inet_diag_entry entry;

		entry.family = tw->tw_family;
#if defined(CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
		if (tw->tw_family == AF_INET6) {
			struct inet6_timewait_sock *tw6 =
						inet6_twsk((struct sock *)tw);
			entry.saddr = tw6->tw_v6_rcv_saddr.s6_addr32;
			entry.daddr = tw6->tw_v6_daddr.s6_addr32;
		} else
#endif
		{
			entry.saddr = &tw->tw_rcv_saddr;
			entry.daddr = &tw->tw_daddr;
		}
		entry.sport = tw->tw_num;
		entry.dport = ntohs(tw->tw_dport);
		entry.userlocks = 0;

		if (!inet_diag_bc_run(nla_data(bc), nla_len(bc), &entry))
			return 0;
	}

	return inet_twsk_diag_fill(tw, skb, r->idiag_ext,
				   NETLINK_CB(cb->skb).pid,
				   cb->nlh->nlmsg_seq, NLM_F_MULTI, cb->nlh);
}

static int inet_diag_fill_req(struct sk_buff *skb, struct sock *sk,
			      struct request_sock *req, u32 pid, u32 seq,
			      const struct nlmsghdr *unlh)
{
	const struct inet_request_sock *ireq = inet_rsk(req);
	struct inet_sock *inet = inet_sk(sk);
	unsigned char *b = skb_tail_pointer(skb);
	struct inet_diag_msg *r;
	struct nlmsghdr *nlh;
	long tmo;

	nlh = NLMSG_PUT(skb, pid, seq, unlh->nlmsg_type, sizeof(*r));
	nlh->nlmsg_flags = NLM_F_MULTI;
	r = NLMSG_DATA(nlh);

	r->idiag_family = sk->sk_family;
	r->idiag_state = TCP_SYN_RECV;
	r->idiag_timer = 1;
	r->idiag_retrans = req->retrans;

	r->id.idiag_if = sk->sk_bound_dev_if;
	r->id.idiag_cookie[0] = (u32)(unsigned long)req;
	r->id.idiag_cookie[1] = (u32)(((unsigned long)req >> 31) >> 1);

	tmo = req->expires - jiffies;
	if (tmo < 0)
		tmo = 0;

	r->id.idiag_sport = inet->inet_sport;
	r->id.idiag_dport = ireq->rmt_port;
	r->id.idiag_src[0] = ireq->loc_addr;
	r->id.idiag_dst[0] = ireq->rmt_addr;
	r->idiag_expires = jiffies_to_msecs(tmo);
	r->idiag_rqueue = 0;
	r->idiag_wqueue = 0;
	r->idiag_uid = sock_i_uid(sk);
	r->idiag_inode = 0;
#if defined(CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
	if (r->idiag_family == AF_INET6) {
		*(struct in6_addr *)r->id.idiag_src = inet6_rsk(req)->loc_addr;
		*(struct in6_addr *)r->id.idiag_dst = inet6_rsk(req)->rmt_addr;
	}
#endif
	nlh->nlmsg_len = skb_tail_pointer(skb) - b;

	return skb->len;

nlmsg_failure:
	nlmsg_trim(skb, b);
	return -1;
}

static int inet_diag_dump_reqs(struct sk_buff *skb, struct sock *sk,
			       struct netlink_callback *cb,
			       const struct nlattr *bc)
{
	struct inet_diag_entry entry;
	struct inet_diag_req_compat *r = NLMSG_DATA(cb->nlh);
	struct inet_connection_sock *icsk = inet_csk(sk);
	struct listen_sock *lopt;
	struct inet_sock *inet = inet_sk(sk);
	int j, s_j;
	int reqnum, s_reqnum;
	int err = 0;

	s_j = cb->args[3];
	s_reqnum = cb->args[4];

	if (s_j > 0)
		s_j--;

	entry.family = sk->sk_family;

	read_lock_bh(&icsk->icsk_accept_queue.syn_wait_lock);

	lopt = icsk->icsk_accept_queue.listen_opt;
	if (!lopt || !lopt->qlen)
		goto out;

	if (bc != NULL) {
		entry.sport = inet->inet_num;
		entry.userlocks = sk->sk_userlocks;
	}

	for (j = s_j; j < lopt->nr_table_entries; j++) {
		struct request_sock *req, *head = lopt->syn_table[j];

		reqnum = 0;
		for (req = head; req; reqnum++, req = req->dl_next) {
			struct inet_request_sock *ireq = inet_rsk(req);

			if (reqnum < s_reqnum)
				continue;
			if (r->id.idiag_dport != ireq->rmt_port &&
			    r->id.idiag_dport)
				continue;

			if (bc) {
				entry.saddr =
#if defined(CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
					(entry.family == AF_INET6) ?
					inet6_rsk(req)->loc_addr.s6_addr32 :
#endif
					&ireq->loc_addr;
				entry.daddr =
#if defined(CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
					(entry.family == AF_INET6) ?
					inet6_rsk(req)->rmt_addr.s6_addr32 :
#endif
					&ireq->rmt_addr;
				entry.dport = ntohs(ireq->rmt_port);

				if (!inet_diag_bc_run(nla_data(bc),
						      nla_len(bc), &entry))
					continue;
			}

			err = inet_diag_fill_req(skb, sk, req,
					       NETLINK_CB(cb->skb).pid,
					       cb->nlh->nlmsg_seq, cb->nlh);
			if (err < 0) {
				cb->args[3] = j + 1;
				cb->args[4] = reqnum;
				goto out;
			}
		}

		s_reqnum = 0;
	}

out:
	read_unlock_bh(&icsk->icsk_accept_queue.syn_wait_lock);

	return err;
}

static int inet_diag_dump(struct sk_buff *skb, struct netlink_callback *cb)
{
	int i, num;
	int s_i, s_num;
	struct inet_diag_req_compat *r = NLMSG_DATA(cb->nlh);
	const struct inet_diag_handler *handler;
	struct inet_hashinfo *hashinfo;
	const struct nlattr *bc = NULL;

	if (nlmsg_attrlen(cb->nlh, sizeof(struct inet_diag_req_compat)))
		bc = nlmsg_find_attr(cb->nlh, sizeof(*r), INET_DIAG_REQ_BYTECODE);

	handler = inet_diag_lock_handler(inet_diag_type2proto(cb->nlh->nlmsg_type));
	if (IS_ERR(handler))
		goto unlock;

	hashinfo = handler->idiag_hashinfo;

	s_i = cb->args[1];
	s_num = num = cb->args[2];

	if (cb->args[0] == 0) {
		if (!(r->idiag_states & (TCPF_LISTEN | TCPF_SYN_RECV)))
			goto skip_listen_ht;

		for (i = s_i; i < INET_LHTABLE_SIZE; i++) {
			struct sock *sk;
			struct hlist_nulls_node *node;
			struct inet_listen_hashbucket *ilb;

			num = 0;
			ilb = &hashinfo->listening_hash[i];
			spin_lock_bh(&ilb->lock);
			sk_nulls_for_each(sk, node, &ilb->head) {
				struct inet_sock *inet = inet_sk(sk);

				if (num < s_num) {
					num++;
					continue;
				}

				if (r->id.idiag_sport != inet->inet_sport &&
				    r->id.idiag_sport)
					goto next_listen;

				if (!(r->idiag_states & TCPF_LISTEN) ||
				    r->id.idiag_dport ||
				    cb->args[3] > 0)
					goto syn_recv;

				if (inet_csk_diag_dump(sk, skb, cb, bc) < 0) {
					spin_unlock_bh(&ilb->lock);
					goto done;
				}

syn_recv:
				if (!(r->idiag_states & TCPF_SYN_RECV))
					goto next_listen;

				if (inet_diag_dump_reqs(skb, sk, cb, bc) < 0) {
					spin_unlock_bh(&ilb->lock);
					goto done;
				}

next_listen:
				cb->args[3] = 0;
				cb->args[4] = 0;
				++num;
			}
			spin_unlock_bh(&ilb->lock);

			s_num = 0;
			cb->args[3] = 0;
			cb->args[4] = 0;
		}
skip_listen_ht:
		cb->args[0] = 1;
		s_i = num = s_num = 0;
	}

	if (!(r->idiag_states & ~(TCPF_LISTEN | TCPF_SYN_RECV)))
		goto unlock;

	for (i = s_i; i <= hashinfo->ehash_mask; i++) {
		struct inet_ehash_bucket *head = &hashinfo->ehash[i];
		spinlock_t *lock = inet_ehash_lockp(hashinfo, i);
		struct sock *sk;
		struct hlist_nulls_node *node;

		num = 0;

		if (hlist_nulls_empty(&head->chain) &&
			hlist_nulls_empty(&head->twchain))
			continue;

		if (i > s_i)
			s_num = 0;

		spin_lock_bh(lock);
		sk_nulls_for_each(sk, node, &head->chain) {
			struct inet_sock *inet = inet_sk(sk);

			if (num < s_num)
				goto next_normal;
			if (!(r->idiag_states & (1 << sk->sk_state)))
				goto next_normal;
			if (r->id.idiag_sport != inet->inet_sport &&
			    r->id.idiag_sport)
				goto next_normal;
			if (r->id.idiag_dport != inet->inet_dport &&
			    r->id.idiag_dport)
				goto next_normal;
			if (inet_csk_diag_dump(sk, skb, cb, bc) < 0) {
				spin_unlock_bh(lock);
				goto done;
			}
next_normal:
			++num;
		}

		if (r->idiag_states & TCPF_TIME_WAIT) {
			struct inet_timewait_sock *tw;

			inet_twsk_for_each(tw, node,
				    &head->twchain) {

				if (num < s_num)
					goto next_dying;
				if (r->id.idiag_sport != tw->tw_sport &&
				    r->id.idiag_sport)
					goto next_dying;
				if (r->id.idiag_dport != tw->tw_dport &&
				    r->id.idiag_dport)
					goto next_dying;
				if (inet_twsk_diag_dump(tw, skb, cb, bc) < 0) {
					spin_unlock_bh(lock);
					goto done;
				}
next_dying:
				++num;
			}
		}
		spin_unlock_bh(lock);
	}

done:
	cb->args[1] = i;
	cb->args[2] = num;
unlock:
	inet_diag_unlock_handler(handler);
	return skb->len;
}

static int inet_diag_get_exact_compat(struct sk_buff *in_skb,
			       const struct nlmsghdr *nlh)
{
	struct inet_diag_req_compat *rc = NLMSG_DATA(nlh);
	struct inet_diag_req req;

	req.sdiag_family = rc->idiag_family;
	req.sdiag_protocol = inet_diag_type2proto(nlh->nlmsg_type);
	req.idiag_ext = rc->idiag_ext;
	req.idiag_states = rc->idiag_states;
	req.id = rc->id;

	return inet_diag_get_exact(in_skb, nlh, &req);
}

static int inet_diag_rcv_msg_compat(struct sk_buff *skb, struct nlmsghdr *nlh)
{
	int hdrlen = sizeof(struct inet_diag_req_compat);

	if (nlh->nlmsg_type >= INET_DIAG_GETSOCK_MAX ||
	    nlmsg_len(nlh) < hdrlen)
		return -EINVAL;

	if (nlh->nlmsg_flags & NLM_F_DUMP) {
		if (nlmsg_attrlen(nlh, hdrlen)) {
			struct nlattr *attr;

			attr = nlmsg_find_attr(nlh, hdrlen,
					       INET_DIAG_REQ_BYTECODE);
			if (attr == NULL ||
			    nla_len(attr) < sizeof(struct inet_diag_bc_op) ||
			    inet_diag_bc_audit(nla_data(attr), nla_len(attr)))
				return -EINVAL;
		}

		return netlink_dump_start(sdiagnl, skb, nlh,
					  inet_diag_dump, NULL, 0);
	}

	return inet_diag_get_exact_compat(skb, nlh);
}

static int inet_diag_handler_dump(struct sk_buff *skb, struct nlmsghdr *h)
{
	int hdrlen = sizeof(struct inet_diag_req);

	if (nlmsg_len(h) < hdrlen)
		return -EINVAL;

	if (h->nlmsg_flags & NLM_F_DUMP) {
		return -EAFNOSUPPORT;
	}

	return inet_diag_get_exact(skb, h, (struct inet_diag_req *)NLMSG_DATA(h));
}

static struct sock_diag_handler inet_diag_handler = {
	.family = AF_INET,
	.dump = inet_diag_handler_dump,
};

static struct sock_diag_handler inet6_diag_handler = {
	.family = AF_INET6,
	.dump = inet_diag_handler_dump,
};

static struct sock_diag_handler *sock_diag_handlers[AF_MAX];
static DEFINE_MUTEX(sock_diag_table_mutex);

int sock_diag_register(struct sock_diag_handler *hndl)
{
	int err = 0;

	if (hndl->family > AF_MAX)
		return -EINVAL;

	mutex_lock(&sock_diag_table_mutex);
	if (sock_diag_handlers[hndl->family])
		err = -EBUSY;
	else
		sock_diag_handlers[hndl->family] = hndl;
	mutex_unlock(&sock_diag_table_mutex);

	return err;
}

void sock_diag_unregister(struct sock_diag_handler *hnld)
{
	int family = hnld->family;

	if (family > AF_MAX)
		return;

	mutex_lock(&sock_diag_table_mutex);
	BUG_ON(sock_diag_handlers[family] != hnld);
	sock_diag_handlers[family] = NULL;
	mutex_unlock(&sock_diag_table_mutex);
}

static inline struct sock_diag_handler *sock_diag_lock_handler(int family)
{
	mutex_lock(&sock_diag_table_mutex);
	return sock_diag_handlers[family];
}

static inline void sock_diag_unlock_handler(struct sock_diag_handler *h)
{
	mutex_unlock(&sock_diag_table_mutex);
}

static int __sock_diag_rcv_msg(struct sk_buff *skb, struct nlmsghdr *nlh)
{
	int err;
	struct sock_diag_req *req = NLMSG_DATA(nlh);
	struct sock_diag_handler *hndl;

	if (nlmsg_len(nlh) < sizeof(*req))
		return -EINVAL;

	hndl = sock_diag_lock_handler(req->sdiag_family);
	if (hndl == NULL)
		err = -ENOENT;
	else
		err = hndl->dump(skb, nlh);
	sock_diag_unlock_handler(hndl);

	return err;
}

static int sock_diag_rcv_msg(struct sk_buff *skb, struct nlmsghdr *nlh)
{
	switch (nlh->nlmsg_type) {
	case TCPDIAG_GETSOCK:
	case DCCPDIAG_GETSOCK:
		return inet_diag_rcv_msg_compat(skb, nlh);
	case SOCK_DIAG_BY_FAMILY:
		return __sock_diag_rcv_msg(skb, nlh);
	default:
		return -EINVAL;
	}
}

static DEFINE_MUTEX(sock_diag_mutex);

static void sock_diag_rcv(struct sk_buff *skb)
{
	mutex_lock(&sock_diag_mutex);
	netlink_rcv_skb(skb, &sock_diag_rcv_msg);
	mutex_unlock(&sock_diag_mutex);
}

int inet_diag_register(const struct inet_diag_handler *h)
{
	const __u16 type = h->idiag_type;
	int err = -EINVAL;

	if (type >= IPPROTO_MAX)
		goto out;

	mutex_lock(&inet_diag_table_mutex);
	err = -EEXIST;
	if (inet_diag_table[type] == NULL) {
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

	sdiagnl = netlink_kernel_create(&init_net, NETLINK_SOCK_DIAG, 0,
					sock_diag_rcv, NULL, THIS_MODULE);
	if (sdiagnl == NULL)
		goto out_free_table;

	err = sock_diag_register(&inet_diag_handler);
	if (err)
		goto out_free_nl;

	err = sock_diag_register(&inet6_diag_handler);
	if (err)
		goto out_free_inet;

out:
	return err;

out_free_inet:
	sock_diag_unregister(&inet_diag_handler);
out_free_nl:
	netlink_kernel_release(sdiagnl);
out_free_table:
	kfree(inet_diag_table);
	goto out;
}

static void __exit inet_diag_exit(void)
{
	sock_diag_unregister(&inet6_diag_handler);
	sock_diag_unregister(&inet_diag_handler);
	netlink_kernel_release(sdiagnl);
	kfree(inet_diag_table);
}

module_init(inet_diag_init);
module_exit(inet_diag_exit);
MODULE_LICENSE("GPL");
MODULE_ALIAS_NET_PF_PROTO(PF_NETLINK, NETLINK_SOCK_DIAG);
