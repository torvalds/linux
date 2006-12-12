/*
 * inet_diag.c	Module for monitoring INET transport protocols sockets.
 *
 * Version:	$Id: inet_diag.c,v 1.3 2002/02/01 22:01:04 davem Exp $
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/random.h>
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

#include <linux/inet.h>
#include <linux/stddef.h>

#include <linux/inet_diag.h>

static const struct inet_diag_handler **inet_diag_table;

struct inet_diag_entry {
	__be32 *saddr;
	__be32 *daddr;
	u16 sport;
	u16 dport;
	u16 family;
	u16 userlocks;
};

static struct sock *idiagnl;

#define INET_DIAG_PUT(skb, attrtype, attrlen) \
	RTA_DATA(__RTA_PUT(skb, attrtype, attrlen))

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
	unsigned char	 *b = skb->tail;
	const struct inet_diag_handler *handler;

	handler = inet_diag_table[unlh->nlmsg_type];
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

	r->id.idiag_sport = inet->sport;
	r->id.idiag_dport = inet->dport;
	r->id.idiag_src[0] = inet->rcv_saddr;
	r->id.idiag_dst[0] = inet->daddr;

#if defined(CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
	if (r->idiag_family == AF_INET6) {
		struct ipv6_pinfo *np = inet6_sk(sk);

		ipv6_addr_copy((struct in6_addr *)r->id.idiag_src,
			       &np->rcv_saddr);
		ipv6_addr_copy((struct in6_addr *)r->id.idiag_dst,
			       &np->daddr);
	}
#endif

#define EXPIRES_IN_MS(tmo)  ((tmo - jiffies) * 1000 + HZ - 1) / HZ

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
		minfo->idiag_rmem = atomic_read(&sk->sk_rmem_alloc);
		minfo->idiag_wmem = sk->sk_wmem_queued;
		minfo->idiag_fmem = sk->sk_forward_alloc;
		minfo->idiag_tmem = atomic_read(&sk->sk_wmem_alloc);
	}

	handler->idiag_get_info(sk, r, info);

	if (sk->sk_state < TCP_TIME_WAIT &&
	    icsk->icsk_ca_ops && icsk->icsk_ca_ops->get_info)
		icsk->icsk_ca_ops->get_info(sk, ext, skb);

	nlh->nlmsg_len = skb->tail - b;
	return skb->len;

rtattr_failure:
nlmsg_failure:
	skb_trim(skb, b - skb->data);
	return -1;
}

static int inet_twsk_diag_fill(struct inet_timewait_sock *tw,
			       struct sk_buff *skb, int ext, u32 pid,
			       u32 seq, u16 nlmsg_flags,
			       const struct nlmsghdr *unlh)
{
	long tmo;
	struct inet_diag_msg *r;
	const unsigned char *previous_tail = skb->tail;
	struct nlmsghdr *nlh = NLMSG_PUT(skb, pid, seq,
					 unlh->nlmsg_type, sizeof(*r));

	r = NLMSG_DATA(nlh);
	BUG_ON(tw->tw_state != TCP_TIME_WAIT);

	nlh->nlmsg_flags = nlmsg_flags;

	tmo = tw->tw_ttd - jiffies;
	if (tmo < 0)
		tmo = 0;

	r->idiag_family	      = tw->tw_family;
	r->idiag_state	      = tw->tw_state;
	r->idiag_timer	      = 0;
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
	r->idiag_expires      = (tmo * 1000 + HZ - 1) / HZ;
	r->idiag_rqueue	      = 0;
	r->idiag_wqueue	      = 0;
	r->idiag_uid	      = 0;
	r->idiag_inode	      = 0;
#if defined(CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
	if (tw->tw_family == AF_INET6) {
		const struct inet6_timewait_sock *tw6 =
						inet6_twsk((struct sock *)tw);

		ipv6_addr_copy((struct in6_addr *)r->id.idiag_src,
			       &tw6->tw_v6_rcv_saddr);
		ipv6_addr_copy((struct in6_addr *)r->id.idiag_dst,
			       &tw6->tw_v6_daddr);
	}
#endif
	nlh->nlmsg_len = skb->tail - previous_tail;
	return skb->len;
nlmsg_failure:
	skb_trim(skb, previous_tail - skb->data);
	return -1;
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
			       const struct nlmsghdr *nlh)
{
	int err;
	struct sock *sk;
	struct inet_diag_req *req = NLMSG_DATA(nlh);
	struct sk_buff *rep;
	struct inet_hashinfo *hashinfo;
	const struct inet_diag_handler *handler;

	handler = inet_diag_table[nlh->nlmsg_type];
	BUG_ON(handler == NULL);
	hashinfo = handler->idiag_hashinfo;

	if (req->idiag_family == AF_INET) {
		sk = inet_lookup(hashinfo, req->id.idiag_dst[0],
				 req->id.idiag_dport, req->id.idiag_src[0],
				 req->id.idiag_sport, req->id.idiag_if);
	}
#if defined(CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
	else if (req->idiag_family == AF_INET6) {
		sk = inet6_lookup(hashinfo,
				  (struct in6_addr *)req->id.idiag_dst,
				  req->id.idiag_dport,
				  (struct in6_addr *)req->id.idiag_src,
				  req->id.idiag_sport,
				  req->id.idiag_if);
	}
#endif
	else {
		return -EINVAL;
	}

	if (sk == NULL)
		return -ENOENT;

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

	if (sk_diag_fill(sk, rep, req->idiag_ext,
			 NETLINK_CB(in_skb).pid,
			 nlh->nlmsg_seq, 0, nlh) <= 0)
		BUG();

	err = netlink_unicast(idiagnl, rep, NETLINK_CB(in_skb).pid,
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
			yes = entry->dport <= op[1].no;
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
	return (len == 0);
}

static int valid_cc(const void *bc, int len, int cc)
{
	while (len >= 0) {
		const struct inet_diag_bc_op *op = bc;

		if (cc > len)
			return 0;
		if (cc == len)
			return 1;
		if (op->yes < 4)
			return 0;
		len -= op->yes;
		bc  += op->yes;
	}
	return 0;
}

static int inet_diag_bc_audit(const void *bytecode, int bytecode_len)
{
	const unsigned char *bc = bytecode;
	int  len = bytecode_len;

	while (len > 0) {
		struct inet_diag_bc_op *op = (struct inet_diag_bc_op *)bc;

//printk("BC: %d %d %d {%d} / %d\n", op->code, op->yes, op->no, op[1].no, len);
		switch (op->code) {
		case INET_DIAG_BC_AUTO:
		case INET_DIAG_BC_S_COND:
		case INET_DIAG_BC_D_COND:
		case INET_DIAG_BC_S_GE:
		case INET_DIAG_BC_S_LE:
		case INET_DIAG_BC_D_GE:
		case INET_DIAG_BC_D_LE:
			if (op->yes < 4 || op->yes > len + 4)
				return -EINVAL;
		case INET_DIAG_BC_JMP:
			if (op->no < 4 || op->no > len + 4)
				return -EINVAL;
			if (op->no < len &&
			    !valid_cc(bytecode, bytecode_len, len - op->no))
				return -EINVAL;
			break;
		case INET_DIAG_BC_NOP:
			if (op->yes < 4 || op->yes > len + 4)
				return -EINVAL;
			break;
		default:
			return -EINVAL;
		}
		bc  += op->yes;
		len -= op->yes;
	}
	return len == 0 ? 0 : -EINVAL;
}

static int inet_csk_diag_dump(struct sock *sk,
			      struct sk_buff *skb,
			      struct netlink_callback *cb)
{
	struct inet_diag_req *r = NLMSG_DATA(cb->nlh);

	if (cb->nlh->nlmsg_len > 4 + NLMSG_SPACE(sizeof(*r))) {
		struct inet_diag_entry entry;
		struct rtattr *bc = (struct rtattr *)(r + 1);
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
			entry.saddr = &inet->rcv_saddr;
			entry.daddr = &inet->daddr;
		}
		entry.sport = inet->num;
		entry.dport = ntohs(inet->dport);
		entry.userlocks = sk->sk_userlocks;

		if (!inet_diag_bc_run(RTA_DATA(bc), RTA_PAYLOAD(bc), &entry))
			return 0;
	}

	return inet_csk_diag_fill(sk, skb, r->idiag_ext,
				  NETLINK_CB(cb->skb).pid,
				  cb->nlh->nlmsg_seq, NLM_F_MULTI, cb->nlh);
}

static int inet_twsk_diag_dump(struct inet_timewait_sock *tw,
			       struct sk_buff *skb,
			       struct netlink_callback *cb)
{
	struct inet_diag_req *r = NLMSG_DATA(cb->nlh);

	if (cb->nlh->nlmsg_len > 4 + NLMSG_SPACE(sizeof(*r))) {
		struct inet_diag_entry entry;
		struct rtattr *bc = (struct rtattr *)(r + 1);

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

		if (!inet_diag_bc_run(RTA_DATA(bc), RTA_PAYLOAD(bc), &entry))
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
	unsigned char *b = skb->tail;
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

	r->id.idiag_sport = inet->sport;
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
		ipv6_addr_copy((struct in6_addr *)r->id.idiag_src,
			       &inet6_rsk(req)->loc_addr);
		ipv6_addr_copy((struct in6_addr *)r->id.idiag_dst,
			       &inet6_rsk(req)->rmt_addr);
	}
#endif
	nlh->nlmsg_len = skb->tail - b;

	return skb->len;

nlmsg_failure:
	skb_trim(skb, b - skb->data);
	return -1;
}

static int inet_diag_dump_reqs(struct sk_buff *skb, struct sock *sk,
			       struct netlink_callback *cb)
{
	struct inet_diag_entry entry;
	struct inet_diag_req *r = NLMSG_DATA(cb->nlh);
	struct inet_connection_sock *icsk = inet_csk(sk);
	struct listen_sock *lopt;
	struct rtattr *bc = NULL;
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

	if (cb->nlh->nlmsg_len > 4 + NLMSG_SPACE(sizeof(*r))) {
		bc = (struct rtattr *)(r + 1);
		entry.sport = inet->num;
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

				if (!inet_diag_bc_run(RTA_DATA(bc),
						    RTA_PAYLOAD(bc), &entry))
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
	struct inet_diag_req *r = NLMSG_DATA(cb->nlh);
	const struct inet_diag_handler *handler;
	struct inet_hashinfo *hashinfo;

	handler = inet_diag_table[cb->nlh->nlmsg_type];
	BUG_ON(handler == NULL);
	hashinfo = handler->idiag_hashinfo;

	s_i = cb->args[1];
	s_num = num = cb->args[2];

	if (cb->args[0] == 0) {
		if (!(r->idiag_states & (TCPF_LISTEN | TCPF_SYN_RECV)))
			goto skip_listen_ht;

		inet_listen_lock(hashinfo);
		for (i = s_i; i < INET_LHTABLE_SIZE; i++) {
			struct sock *sk;
			struct hlist_node *node;

			num = 0;
			sk_for_each(sk, node, &hashinfo->listening_hash[i]) {
				struct inet_sock *inet = inet_sk(sk);

				if (num < s_num) {
					num++;
					continue;
				}

				if (r->id.idiag_sport != inet->sport &&
				    r->id.idiag_sport)
					goto next_listen;

				if (!(r->idiag_states & TCPF_LISTEN) ||
				    r->id.idiag_dport ||
				    cb->args[3] > 0)
					goto syn_recv;

				if (inet_csk_diag_dump(sk, skb, cb) < 0) {
					inet_listen_unlock(hashinfo);
					goto done;
				}

syn_recv:
				if (!(r->idiag_states & TCPF_SYN_RECV))
					goto next_listen;

				if (inet_diag_dump_reqs(skb, sk, cb) < 0) {
					inet_listen_unlock(hashinfo);
					goto done;
				}

next_listen:
				cb->args[3] = 0;
				cb->args[4] = 0;
				++num;
			}

			s_num = 0;
			cb->args[3] = 0;
			cb->args[4] = 0;
		}
		inet_listen_unlock(hashinfo);
skip_listen_ht:
		cb->args[0] = 1;
		s_i = num = s_num = 0;
	}

	if (!(r->idiag_states & ~(TCPF_LISTEN | TCPF_SYN_RECV)))
		return skb->len;

	for (i = s_i; i < hashinfo->ehash_size; i++) {
		struct inet_ehash_bucket *head = &hashinfo->ehash[i];
		struct sock *sk;
		struct hlist_node *node;

		if (i > s_i)
			s_num = 0;

		read_lock_bh(&head->lock);
		num = 0;
		sk_for_each(sk, node, &head->chain) {
			struct inet_sock *inet = inet_sk(sk);

			if (num < s_num)
				goto next_normal;
			if (!(r->idiag_states & (1 << sk->sk_state)))
				goto next_normal;
			if (r->id.idiag_sport != inet->sport &&
			    r->id.idiag_sport)
				goto next_normal;
			if (r->id.idiag_dport != inet->dport &&
			    r->id.idiag_dport)
				goto next_normal;
			if (inet_csk_diag_dump(sk, skb, cb) < 0) {
				read_unlock_bh(&head->lock);
				goto done;
			}
next_normal:
			++num;
		}

		if (r->idiag_states & TCPF_TIME_WAIT) {
			struct inet_timewait_sock *tw;

			inet_twsk_for_each(tw, node,
				    &hashinfo->ehash[i + hashinfo->ehash_size].chain) {

				if (num < s_num)
					goto next_dying;
				if (r->id.idiag_sport != tw->tw_sport &&
				    r->id.idiag_sport)
					goto next_dying;
				if (r->id.idiag_dport != tw->tw_dport &&
				    r->id.idiag_dport)
					goto next_dying;
				if (inet_twsk_diag_dump(tw, skb, cb) < 0) {
					read_unlock_bh(&head->lock);
					goto done;
				}
next_dying:
				++num;
			}
		}
		read_unlock_bh(&head->lock);
	}

done:
	cb->args[1] = i;
	cb->args[2] = num;
	return skb->len;
}

static inline int inet_diag_rcv_msg(struct sk_buff *skb, struct nlmsghdr *nlh)
{
	if (!(nlh->nlmsg_flags&NLM_F_REQUEST))
		return 0;

	if (nlh->nlmsg_type >= INET_DIAG_GETSOCK_MAX)
		goto err_inval;

	if (inet_diag_table[nlh->nlmsg_type] == NULL)
		return -ENOENT;

	if (NLMSG_LENGTH(sizeof(struct inet_diag_req)) > skb->len)
		goto err_inval;

	if (nlh->nlmsg_flags&NLM_F_DUMP) {
		if (nlh->nlmsg_len >
		    (4 + NLMSG_SPACE(sizeof(struct inet_diag_req)))) {
			struct rtattr *rta = (void *)(NLMSG_DATA(nlh) +
						 sizeof(struct inet_diag_req));
			if (rta->rta_type != INET_DIAG_REQ_BYTECODE ||
			    rta->rta_len < 8 ||
			    rta->rta_len >
			    (nlh->nlmsg_len -
			     NLMSG_SPACE(sizeof(struct inet_diag_req))))
				goto err_inval;
			if (inet_diag_bc_audit(RTA_DATA(rta), RTA_PAYLOAD(rta)))
				goto err_inval;
		}
		return netlink_dump_start(idiagnl, skb, nlh,
					  inet_diag_dump, NULL);
	} else
		return inet_diag_get_exact(skb, nlh);

err_inval:
	return -EINVAL;
}


static inline void inet_diag_rcv_skb(struct sk_buff *skb)
{
	if (skb->len >= NLMSG_SPACE(0)) {
		int err;
		struct nlmsghdr *nlh = (struct nlmsghdr *)skb->data;

		if (nlh->nlmsg_len < sizeof(*nlh) ||
		    skb->len < nlh->nlmsg_len)
			return;
		err = inet_diag_rcv_msg(skb, nlh);
		if (err || nlh->nlmsg_flags & NLM_F_ACK)
			netlink_ack(skb, nlh, err);
	}
}

static void inet_diag_rcv(struct sock *sk, int len)
{
	struct sk_buff *skb;
	unsigned int qlen = skb_queue_len(&sk->sk_receive_queue);

	while (qlen-- && (skb = skb_dequeue(&sk->sk_receive_queue))) {
		inet_diag_rcv_skb(skb);
		kfree_skb(skb);
	}
}

static DEFINE_SPINLOCK(inet_diag_register_lock);

int inet_diag_register(const struct inet_diag_handler *h)
{
	const __u16 type = h->idiag_type;
	int err = -EINVAL;

	if (type >= INET_DIAG_GETSOCK_MAX)
		goto out;

	spin_lock(&inet_diag_register_lock);
	err = -EEXIST;
	if (inet_diag_table[type] == NULL) {
		inet_diag_table[type] = h;
		err = 0;
	}
	spin_unlock(&inet_diag_register_lock);
out:
	return err;
}
EXPORT_SYMBOL_GPL(inet_diag_register);

void inet_diag_unregister(const struct inet_diag_handler *h)
{
	const __u16 type = h->idiag_type;

	if (type >= INET_DIAG_GETSOCK_MAX)
		return;

	spin_lock(&inet_diag_register_lock);
	inet_diag_table[type] = NULL;
	spin_unlock(&inet_diag_register_lock);

	synchronize_rcu();
}
EXPORT_SYMBOL_GPL(inet_diag_unregister);

static int __init inet_diag_init(void)
{
	const int inet_diag_table_size = (INET_DIAG_GETSOCK_MAX *
					  sizeof(struct inet_diag_handler *));
	int err = -ENOMEM;

	inet_diag_table = kzalloc(inet_diag_table_size, GFP_KERNEL);
	if (!inet_diag_table)
		goto out;

	idiagnl = netlink_kernel_create(NETLINK_INET_DIAG, 0, inet_diag_rcv,
					THIS_MODULE);
	if (idiagnl == NULL)
		goto out_free_table;
	err = 0;
out:
	return err;
out_free_table:
	kfree(inet_diag_table);
	goto out;
}

static void __exit inet_diag_exit(void)
{
	sock_release(idiagnl->sk_socket);
	kfree(inet_diag_table);
}

module_init(inet_diag_init);
module_exit(inet_diag_exit);
MODULE_LICENSE("GPL");
