// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		RAW - implementation of IP "raw" sockets.
 *
 * Authors:	Ross Biro
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 * Fixes:
 *		Alan Cox	:	verify_area() fixed up
 *		Alan Cox	:	ICMP error handling
 *		Alan Cox	:	EMSGSIZE if you send too big a packet
 *		Alan Cox	: 	Now uses generic datagrams and shared
 *					skbuff library. No more peek crashes,
 *					no more backlogs
 *		Alan Cox	:	Checks sk->broadcast.
 *		Alan Cox	:	Uses skb_free_datagram/skb_copy_datagram
 *		Alan Cox	:	Raw passes ip options too
 *		Alan Cox	:	Setsocketopt added
 *		Alan Cox	:	Fixed error return for broadcasts
 *		Alan Cox	:	Removed wake_up calls
 *		Alan Cox	:	Use ttl/tos
 *		Alan Cox	:	Cleaned up old debugging
 *		Alan Cox	:	Use new kernel side addresses
 *	Arnt Gulbrandsen	:	Fixed MSG_DONTROUTE in raw sockets.
 *		Alan Cox	:	BSD style RAW socket demultiplexing.
 *		Alan Cox	:	Beginnings of mrouted support.
 *		Alan Cox	:	Added IP_HDRINCL option.
 *		Alan Cox	:	Skip broadcast check if BSDism set.
 *		David S. Miller	:	New socket lookup architecture.
 */

#include <linux/types.h>
#include <linux/atomic.h>
#include <asm/byteorder.h>
#include <asm/current.h>
#include <linux/uaccess.h>
#include <asm/ioctls.h>
#include <linux/stddef.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/spinlock.h>
#include <linux/sockios.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/mroute.h>
#include <linux/netdevice.h>
#include <linux/in_route.h>
#include <linux/route.h>
#include <linux/skbuff.h>
#include <linux/igmp.h>
#include <net/net_namespace.h>
#include <net/dst.h>
#include <net/sock.h>
#include <linux/ip.h>
#include <linux/net.h>
#include <net/ip.h>
#include <net/icmp.h>
#include <net/udp.h>
#include <net/raw.h>
#include <net/snmp.h>
#include <net/tcp_states.h>
#include <net/inet_common.h>
#include <net/checksum.h>
#include <net/xfrm.h>
#include <linux/rtnetlink.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/compat.h>
#include <linux/uio.h>

struct raw_frag_vec {
	struct msghdr *msg;
	union {
		struct icmphdr icmph;
		char c[1];
	} hdr;
	int hlen;
};

struct raw_hashinfo raw_v4_hashinfo;
EXPORT_SYMBOL_GPL(raw_v4_hashinfo);

int raw_hash_sk(struct sock *sk)
{
	struct raw_hashinfo *h = sk->sk_prot->h.raw_hash;
	struct hlist_head *hlist;

	hlist = &h->ht[raw_hashfunc(sock_net(sk), inet_sk(sk)->inet_num)];

	spin_lock(&h->lock);
	sk_add_node_rcu(sk, hlist);
	sock_set_flag(sk, SOCK_RCU_FREE);
	spin_unlock(&h->lock);
	sock_prot_inuse_add(sock_net(sk), sk->sk_prot, 1);

	return 0;
}
EXPORT_SYMBOL_GPL(raw_hash_sk);

void raw_unhash_sk(struct sock *sk)
{
	struct raw_hashinfo *h = sk->sk_prot->h.raw_hash;

	spin_lock(&h->lock);
	if (sk_del_node_init_rcu(sk))
		sock_prot_inuse_add(sock_net(sk), sk->sk_prot, -1);
	spin_unlock(&h->lock);
}
EXPORT_SYMBOL_GPL(raw_unhash_sk);

bool raw_v4_match(struct net *net, const struct sock *sk, unsigned short num,
		  __be32 raddr, __be32 laddr, int dif, int sdif)
{
	const struct inet_sock *inet = inet_sk(sk);

	if (net_eq(sock_net(sk), net) && inet->inet_num == num	&&
	    !(inet->inet_daddr && inet->inet_daddr != raddr) 	&&
	    !(inet->inet_rcv_saddr && inet->inet_rcv_saddr != laddr) &&
	    raw_sk_bound_dev_eq(net, sk->sk_bound_dev_if, dif, sdif))
		return true;
	return false;
}
EXPORT_SYMBOL_GPL(raw_v4_match);

/*
 *	0 - deliver
 *	1 - block
 */
static int icmp_filter(const struct sock *sk, const struct sk_buff *skb)
{
	struct icmphdr _hdr;
	const struct icmphdr *hdr;

	hdr = skb_header_pointer(skb, skb_transport_offset(skb),
				 sizeof(_hdr), &_hdr);
	if (!hdr)
		return 1;

	if (hdr->type < 32) {
		__u32 data = raw_sk(sk)->filter.data;

		return ((1U << hdr->type) & data) != 0;
	}

	/* Do not block unknown ICMP types */
	return 0;
}

/* IP input processing comes here for RAW socket delivery.
 * Caller owns SKB, so we must make clones.
 *
 * RFC 1122: SHOULD pass TOS value up to the transport layer.
 * -> It does. And not only TOS, but all IP header.
 */
static int raw_v4_input(struct net *net, struct sk_buff *skb,
			const struct iphdr *iph, int hash)
{
	int sdif = inet_sdif(skb);
	struct hlist_head *hlist;
	int dif = inet_iif(skb);
	int delivered = 0;
	struct sock *sk;

	hlist = &raw_v4_hashinfo.ht[hash];
	rcu_read_lock();
	sk_for_each_rcu(sk, hlist) {
		if (!raw_v4_match(net, sk, iph->protocol,
				  iph->saddr, iph->daddr, dif, sdif))
			continue;

		if (atomic_read(&sk->sk_rmem_alloc) >=
		    READ_ONCE(sk->sk_rcvbuf)) {
			atomic_inc(&sk->sk_drops);
			continue;
		}

		delivered = 1;
		if ((iph->protocol != IPPROTO_ICMP || !icmp_filter(sk, skb)) &&
		    ip_mc_sf_allow(sk, iph->daddr, iph->saddr,
				   skb->dev->ifindex, sdif)) {
			struct sk_buff *clone = skb_clone(skb, GFP_ATOMIC);

			/* Not releasing hash table! */
			if (clone)
				raw_rcv(sk, clone);
		}
	}
	rcu_read_unlock();
	return delivered;
}

int raw_local_deliver(struct sk_buff *skb, int protocol)
{
	struct net *net = dev_net(skb->dev);

	return raw_v4_input(net, skb, ip_hdr(skb),
			    raw_hashfunc(net, protocol));
}

static void raw_err(struct sock *sk, struct sk_buff *skb, u32 info)
{
	struct inet_sock *inet = inet_sk(sk);
	const int type = icmp_hdr(skb)->type;
	const int code = icmp_hdr(skb)->code;
	int harderr = 0;
	bool recverr;
	int err = 0;

	if (type == ICMP_DEST_UNREACH && code == ICMP_FRAG_NEEDED)
		ipv4_sk_update_pmtu(skb, sk, info);
	else if (type == ICMP_REDIRECT) {
		ipv4_sk_redirect(skb, sk);
		return;
	}

	/* Report error on raw socket, if:
	   1. User requested ip_recverr.
	   2. Socket is connected (otherwise the error indication
	      is useless without ip_recverr and error is hard.
	 */
	recverr = inet_test_bit(RECVERR, sk);
	if (!recverr && sk->sk_state != TCP_ESTABLISHED)
		return;

	switch (type) {
	default:
	case ICMP_TIME_EXCEEDED:
		err = EHOSTUNREACH;
		break;
	case ICMP_SOURCE_QUENCH:
		return;
	case ICMP_PARAMETERPROB:
		err = EPROTO;
		harderr = 1;
		break;
	case ICMP_DEST_UNREACH:
		err = EHOSTUNREACH;
		if (code > NR_ICMP_UNREACH)
			break;
		if (code == ICMP_FRAG_NEEDED) {
			harderr = READ_ONCE(inet->pmtudisc) != IP_PMTUDISC_DONT;
			err = EMSGSIZE;
		} else {
			err = icmp_err_convert[code].errno;
			harderr = icmp_err_convert[code].fatal;
		}
	}

	if (recverr) {
		const struct iphdr *iph = (const struct iphdr *)skb->data;
		u8 *payload = skb->data + (iph->ihl << 2);

		if (inet_test_bit(HDRINCL, sk))
			payload = skb->data;
		ip_icmp_error(sk, skb, err, 0, info, payload);
	}

	if (recverr || harderr) {
		sk->sk_err = err;
		sk_error_report(sk);
	}
}

void raw_icmp_error(struct sk_buff *skb, int protocol, u32 info)
{
	struct net *net = dev_net(skb->dev);
	int dif = skb->dev->ifindex;
	int sdif = inet_sdif(skb);
	struct hlist_head *hlist;
	const struct iphdr *iph;
	struct sock *sk;
	int hash;

	hash = raw_hashfunc(net, protocol);
	hlist = &raw_v4_hashinfo.ht[hash];

	rcu_read_lock();
	sk_for_each_rcu(sk, hlist) {
		iph = (const struct iphdr *)skb->data;
		if (!raw_v4_match(net, sk, iph->protocol,
				  iph->daddr, iph->saddr, dif, sdif))
			continue;
		raw_err(sk, skb, info);
	}
	rcu_read_unlock();
}

static int raw_rcv_skb(struct sock *sk, struct sk_buff *skb)
{
	enum skb_drop_reason reason;

	/* Charge it to the socket. */

	ipv4_pktinfo_prepare(sk, skb, true);
	if (sock_queue_rcv_skb_reason(sk, skb, &reason) < 0) {
		sk_skb_reason_drop(sk, skb, reason);
		return NET_RX_DROP;
	}

	return NET_RX_SUCCESS;
}

int raw_rcv(struct sock *sk, struct sk_buff *skb)
{
	if (!xfrm4_policy_check(sk, XFRM_POLICY_IN, skb)) {
		atomic_inc(&sk->sk_drops);
		sk_skb_reason_drop(sk, skb, SKB_DROP_REASON_XFRM_POLICY);
		return NET_RX_DROP;
	}
	nf_reset_ct(skb);

	skb_push(skb, -skb_network_offset(skb));

	raw_rcv_skb(sk, skb);
	return 0;
}

static int raw_send_hdrinc(struct sock *sk, struct flowi4 *fl4,
			   struct msghdr *msg, size_t length,
			   struct rtable **rtp, unsigned int flags,
			   const struct sockcm_cookie *sockc)
{
	struct inet_sock *inet = inet_sk(sk);
	struct net *net = sock_net(sk);
	struct iphdr *iph;
	struct sk_buff *skb;
	unsigned int iphlen;
	int err;
	struct rtable *rt = *rtp;
	int hlen, tlen;

	if (length > rt->dst.dev->mtu) {
		ip_local_error(sk, EMSGSIZE, fl4->daddr, inet->inet_dport,
			       rt->dst.dev->mtu);
		return -EMSGSIZE;
	}
	if (length < sizeof(struct iphdr))
		return -EINVAL;

	if (flags&MSG_PROBE)
		goto out;

	hlen = LL_RESERVED_SPACE(rt->dst.dev);
	tlen = rt->dst.dev->needed_tailroom;
	skb = sock_alloc_send_skb(sk,
				  length + hlen + tlen + 15,
				  flags & MSG_DONTWAIT, &err);
	if (!skb)
		goto error;
	skb_reserve(skb, hlen);

	skb->protocol = htons(ETH_P_IP);
	skb->priority = sockc->priority;
	skb->mark = sockc->mark;
	skb_set_delivery_type_by_clockid(skb, sockc->transmit_time, sk->sk_clockid);
	skb_dst_set(skb, &rt->dst);
	*rtp = NULL;

	skb_reset_network_header(skb);
	iph = ip_hdr(skb);
	skb_put(skb, length);

	skb->ip_summed = CHECKSUM_NONE;

	skb_setup_tx_timestamp(skb, sockc);

	if (flags & MSG_CONFIRM)
		skb_set_dst_pending_confirm(skb, 1);

	skb->transport_header = skb->network_header;
	err = -EFAULT;
	if (memcpy_from_msg(iph, msg, length))
		goto error_free;

	iphlen = iph->ihl * 4;

	/*
	 * We don't want to modify the ip header, but we do need to
	 * be sure that it won't cause problems later along the network
	 * stack.  Specifically we want to make sure that iph->ihl is a
	 * sane value.  If ihl points beyond the length of the buffer passed
	 * in, reject the frame as invalid
	 */
	err = -EINVAL;
	if (iphlen > length)
		goto error_free;

	if (iphlen >= sizeof(*iph)) {
		if (!iph->saddr)
			iph->saddr = fl4->saddr;
		iph->check   = 0;
		iph->tot_len = htons(length);
		if (!iph->id)
			ip_select_ident(net, skb, NULL);

		iph->check = ip_fast_csum((unsigned char *)iph, iph->ihl);
		skb->transport_header += iphlen;
		if (iph->protocol == IPPROTO_ICMP &&
		    length >= iphlen + sizeof(struct icmphdr))
			icmp_out_count(net, ((struct icmphdr *)
				skb_transport_header(skb))->type);
	}

	err = NF_HOOK(NFPROTO_IPV4, NF_INET_LOCAL_OUT,
		      net, sk, skb, NULL, rt->dst.dev,
		      dst_output);
	if (err > 0)
		err = net_xmit_errno(err);
	if (err)
		goto error;
out:
	return 0;

error_free:
	kfree_skb(skb);
error:
	IP_INC_STATS(net, IPSTATS_MIB_OUTDISCARDS);
	if (err == -ENOBUFS && !inet_test_bit(RECVERR, sk))
		err = 0;
	return err;
}

static int raw_probe_proto_opt(struct raw_frag_vec *rfv, struct flowi4 *fl4)
{
	int err;

	if (fl4->flowi4_proto != IPPROTO_ICMP)
		return 0;

	/* We only need the first two bytes. */
	rfv->hlen = 2;

	err = memcpy_from_msg(rfv->hdr.c, rfv->msg, rfv->hlen);
	if (err)
		return err;

	fl4->fl4_icmp_type = rfv->hdr.icmph.type;
	fl4->fl4_icmp_code = rfv->hdr.icmph.code;

	return 0;
}

static int raw_getfrag(void *from, char *to, int offset, int len, int odd,
		       struct sk_buff *skb)
{
	struct raw_frag_vec *rfv = from;

	if (offset < rfv->hlen) {
		int copy = min(rfv->hlen - offset, len);

		if (skb->ip_summed == CHECKSUM_PARTIAL)
			memcpy(to, rfv->hdr.c + offset, copy);
		else
			skb->csum = csum_block_add(
				skb->csum,
				csum_partial_copy_nocheck(rfv->hdr.c + offset,
							  to, copy),
				odd);

		odd = 0;
		offset += copy;
		to += copy;
		len -= copy;

		if (!len)
			return 0;
	}

	offset -= rfv->hlen;

	return ip_generic_getfrag(rfv->msg, to, offset, len, odd, skb);
}

static int raw_sendmsg(struct sock *sk, struct msghdr *msg, size_t len)
{
	struct inet_sock *inet = inet_sk(sk);
	struct net *net = sock_net(sk);
	struct ipcm_cookie ipc;
	struct rtable *rt = NULL;
	struct flowi4 fl4;
	u8 tos, scope;
	int free = 0;
	__be32 daddr;
	__be32 saddr;
	int uc_index, err;
	struct ip_options_data opt_copy;
	struct raw_frag_vec rfv;
	int hdrincl;

	err = -EMSGSIZE;
	if (len > 0xFFFF)
		goto out;

	hdrincl = inet_test_bit(HDRINCL, sk);

	/*
	 *	Check the flags.
	 */

	err = -EOPNOTSUPP;
	if (msg->msg_flags & MSG_OOB)	/* Mirror BSD error message */
		goto out;               /* compatibility */

	/*
	 *	Get and verify the address.
	 */

	if (msg->msg_namelen) {
		DECLARE_SOCKADDR(struct sockaddr_in *, usin, msg->msg_name);
		err = -EINVAL;
		if (msg->msg_namelen < sizeof(*usin))
			goto out;
		if (usin->sin_family != AF_INET) {
			pr_info_once("%s: %s forgot to set AF_INET. Fix it!\n",
				     __func__, current->comm);
			err = -EAFNOSUPPORT;
			if (usin->sin_family)
				goto out;
		}
		daddr = usin->sin_addr.s_addr;
		/* ANK: I did not forget to get protocol from port field.
		 * I just do not know, who uses this weirdness.
		 * IP_HDRINCL is much more convenient.
		 */
	} else {
		err = -EDESTADDRREQ;
		if (sk->sk_state != TCP_ESTABLISHED)
			goto out;
		daddr = inet->inet_daddr;
	}

	ipcm_init_sk(&ipc, inet);
	/* Keep backward compat */
	if (hdrincl)
		ipc.protocol = IPPROTO_RAW;

	if (msg->msg_controllen) {
		err = ip_cmsg_send(sk, msg, &ipc, false);
		if (unlikely(err)) {
			kfree(ipc.opt);
			goto out;
		}
		if (ipc.opt)
			free = 1;
	}

	saddr = ipc.addr;
	ipc.addr = daddr;

	if (!ipc.opt) {
		struct ip_options_rcu *inet_opt;

		rcu_read_lock();
		inet_opt = rcu_dereference(inet->inet_opt);
		if (inet_opt) {
			memcpy(&opt_copy, inet_opt,
			       sizeof(*inet_opt) + inet_opt->opt.optlen);
			ipc.opt = &opt_copy.opt;
		}
		rcu_read_unlock();
	}

	if (ipc.opt) {
		err = -EINVAL;
		/* Linux does not mangle headers on raw sockets,
		 * so that IP options + IP_HDRINCL is non-sense.
		 */
		if (hdrincl)
			goto done;
		if (ipc.opt->opt.srr) {
			if (!daddr)
				goto done;
			daddr = ipc.opt->opt.faddr;
		}
	}
	tos = get_rttos(&ipc, inet);
	scope = ip_sendmsg_scope(inet, &ipc, msg);

	uc_index = READ_ONCE(inet->uc_index);
	if (ipv4_is_multicast(daddr)) {
		if (!ipc.oif || netif_index_is_l3_master(sock_net(sk), ipc.oif))
			ipc.oif = READ_ONCE(inet->mc_index);
		if (!saddr)
			saddr = READ_ONCE(inet->mc_addr);
	} else if (!ipc.oif) {
		ipc.oif = uc_index;
	} else if (ipv4_is_lbcast(daddr) && uc_index) {
		/* oif is set, packet is to local broadcast
		 * and uc_index is set. oif is most likely set
		 * by sk_bound_dev_if. If uc_index != oif check if the
		 * oif is an L3 master and uc_index is an L3 slave.
		 * If so, we want to allow the send using the uc_index.
		 */
		if (ipc.oif != uc_index &&
		    ipc.oif == l3mdev_master_ifindex_by_index(sock_net(sk),
							      uc_index)) {
			ipc.oif = uc_index;
		}
	}

	flowi4_init_output(&fl4, ipc.oif, ipc.sockc.mark, tos, scope,
			   hdrincl ? ipc.protocol : sk->sk_protocol,
			   inet_sk_flowi_flags(sk) |
			    (hdrincl ? FLOWI_FLAG_KNOWN_NH : 0),
			   daddr, saddr, 0, 0, sk->sk_uid);

	fl4.fl4_icmp_type = 0;
	fl4.fl4_icmp_code = 0;

	if (!hdrincl) {
		rfv.msg = msg;
		rfv.hlen = 0;

		err = raw_probe_proto_opt(&rfv, &fl4);
		if (err)
			goto done;
	}

	security_sk_classify_flow(sk, flowi4_to_flowi_common(&fl4));
	rt = ip_route_output_flow(net, &fl4, sk);
	if (IS_ERR(rt)) {
		err = PTR_ERR(rt);
		rt = NULL;
		goto done;
	}

	err = -EACCES;
	if (rt->rt_flags & RTCF_BROADCAST && !sock_flag(sk, SOCK_BROADCAST))
		goto done;

	if (msg->msg_flags & MSG_CONFIRM)
		goto do_confirm;
back_from_confirm:

	if (hdrincl)
		err = raw_send_hdrinc(sk, &fl4, msg, len,
				      &rt, msg->msg_flags, &ipc.sockc);

	 else {
		if (!ipc.addr)
			ipc.addr = fl4.daddr;
		lock_sock(sk);
		err = ip_append_data(sk, &fl4, raw_getfrag,
				     &rfv, len, 0,
				     &ipc, &rt, msg->msg_flags);
		if (err)
			ip_flush_pending_frames(sk);
		else if (!(msg->msg_flags & MSG_MORE)) {
			err = ip_push_pending_frames(sk, &fl4);
			if (err == -ENOBUFS && !inet_test_bit(RECVERR, sk))
				err = 0;
		}
		release_sock(sk);
	}
done:
	if (free)
		kfree(ipc.opt);
	ip_rt_put(rt);

out:
	if (err < 0)
		return err;
	return len;

do_confirm:
	if (msg->msg_flags & MSG_PROBE)
		dst_confirm_neigh(&rt->dst, &fl4.daddr);
	if (!(msg->msg_flags & MSG_PROBE) || len)
		goto back_from_confirm;
	err = 0;
	goto done;
}

static void raw_close(struct sock *sk, long timeout)
{
	/*
	 * Raw sockets may have direct kernel references. Kill them.
	 */
	ip_ra_control(sk, 0, NULL);

	sk_common_release(sk);
}

static void raw_destroy(struct sock *sk)
{
	lock_sock(sk);
	ip_flush_pending_frames(sk);
	release_sock(sk);
}

/* This gets rid of all the nasties in af_inet. -DaveM */
static int raw_bind(struct sock *sk, struct sockaddr *uaddr, int addr_len)
{
	struct inet_sock *inet = inet_sk(sk);
	struct sockaddr_in *addr = (struct sockaddr_in *) uaddr;
	struct net *net = sock_net(sk);
	u32 tb_id = RT_TABLE_LOCAL;
	int ret = -EINVAL;
	int chk_addr_ret;

	lock_sock(sk);
	if (sk->sk_state != TCP_CLOSE || addr_len < sizeof(struct sockaddr_in))
		goto out;

	if (sk->sk_bound_dev_if)
		tb_id = l3mdev_fib_table_by_index(net,
						  sk->sk_bound_dev_if) ? : tb_id;

	chk_addr_ret = inet_addr_type_table(net, addr->sin_addr.s_addr, tb_id);

	ret = -EADDRNOTAVAIL;
	if (!inet_addr_valid_or_nonlocal(net, inet, addr->sin_addr.s_addr,
					 chk_addr_ret))
		goto out;

	inet->inet_rcv_saddr = inet->inet_saddr = addr->sin_addr.s_addr;
	if (chk_addr_ret == RTN_MULTICAST || chk_addr_ret == RTN_BROADCAST)
		inet->inet_saddr = 0;  /* Use device */
	sk_dst_reset(sk);
	ret = 0;
out:
	release_sock(sk);
	return ret;
}

/*
 *	This should be easy, if there is something there
 *	we return it, otherwise we block.
 */

static int raw_recvmsg(struct sock *sk, struct msghdr *msg, size_t len,
		       int flags, int *addr_len)
{
	struct inet_sock *inet = inet_sk(sk);
	size_t copied = 0;
	int err = -EOPNOTSUPP;
	DECLARE_SOCKADDR(struct sockaddr_in *, sin, msg->msg_name);
	struct sk_buff *skb;

	if (flags & MSG_OOB)
		goto out;

	if (flags & MSG_ERRQUEUE) {
		err = ip_recv_error(sk, msg, len, addr_len);
		goto out;
	}

	skb = skb_recv_datagram(sk, flags, &err);
	if (!skb)
		goto out;

	copied = skb->len;
	if (len < copied) {
		msg->msg_flags |= MSG_TRUNC;
		copied = len;
	}

	err = skb_copy_datagram_msg(skb, 0, msg, copied);
	if (err)
		goto done;

	sock_recv_cmsgs(msg, sk, skb);

	/* Copy the address. */
	if (sin) {
		sin->sin_family = AF_INET;
		sin->sin_addr.s_addr = ip_hdr(skb)->saddr;
		sin->sin_port = 0;
		memset(&sin->sin_zero, 0, sizeof(sin->sin_zero));
		*addr_len = sizeof(*sin);
	}
	if (inet_cmsg_flags(inet))
		ip_cmsg_recv(msg, skb);
	if (flags & MSG_TRUNC)
		copied = skb->len;
done:
	skb_free_datagram(sk, skb);
out:
	if (err)
		return err;
	return copied;
}

static int raw_sk_init(struct sock *sk)
{
	struct raw_sock *rp = raw_sk(sk);

	if (inet_sk(sk)->inet_num == IPPROTO_ICMP)
		memset(&rp->filter, 0, sizeof(rp->filter));
	return 0;
}

static int raw_seticmpfilter(struct sock *sk, sockptr_t optval, int optlen)
{
	if (optlen > sizeof(struct icmp_filter))
		optlen = sizeof(struct icmp_filter);
	if (copy_from_sockptr(&raw_sk(sk)->filter, optval, optlen))
		return -EFAULT;
	return 0;
}

static int raw_geticmpfilter(struct sock *sk, char __user *optval, int __user *optlen)
{
	int len, ret = -EFAULT;

	if (get_user(len, optlen))
		goto out;
	ret = -EINVAL;
	if (len < 0)
		goto out;
	if (len > sizeof(struct icmp_filter))
		len = sizeof(struct icmp_filter);
	ret = -EFAULT;
	if (put_user(len, optlen) ||
	    copy_to_user(optval, &raw_sk(sk)->filter, len))
		goto out;
	ret = 0;
out:	return ret;
}

static int do_raw_setsockopt(struct sock *sk, int optname,
			     sockptr_t optval, unsigned int optlen)
{
	if (optname == ICMP_FILTER) {
		if (inet_sk(sk)->inet_num != IPPROTO_ICMP)
			return -EOPNOTSUPP;
		else
			return raw_seticmpfilter(sk, optval, optlen);
	}
	return -ENOPROTOOPT;
}

static int raw_setsockopt(struct sock *sk, int level, int optname,
			  sockptr_t optval, unsigned int optlen)
{
	if (level != SOL_RAW)
		return ip_setsockopt(sk, level, optname, optval, optlen);
	return do_raw_setsockopt(sk, optname, optval, optlen);
}

static int do_raw_getsockopt(struct sock *sk, int optname,
			     char __user *optval, int __user *optlen)
{
	if (optname == ICMP_FILTER) {
		if (inet_sk(sk)->inet_num != IPPROTO_ICMP)
			return -EOPNOTSUPP;
		else
			return raw_geticmpfilter(sk, optval, optlen);
	}
	return -ENOPROTOOPT;
}

static int raw_getsockopt(struct sock *sk, int level, int optname,
			  char __user *optval, int __user *optlen)
{
	if (level != SOL_RAW)
		return ip_getsockopt(sk, level, optname, optval, optlen);
	return do_raw_getsockopt(sk, optname, optval, optlen);
}

static int raw_ioctl(struct sock *sk, int cmd, int *karg)
{
	switch (cmd) {
	case SIOCOUTQ: {
		*karg = sk_wmem_alloc_get(sk);
		return 0;
	}
	case SIOCINQ: {
		struct sk_buff *skb;

		spin_lock_bh(&sk->sk_receive_queue.lock);
		skb = skb_peek(&sk->sk_receive_queue);
		if (skb)
			*karg = skb->len;
		else
			*karg = 0;
		spin_unlock_bh(&sk->sk_receive_queue.lock);
		return 0;
	}

	default:
#ifdef CONFIG_IP_MROUTE
		return ipmr_ioctl(sk, cmd, karg);
#else
		return -ENOIOCTLCMD;
#endif
	}
}

#ifdef CONFIG_COMPAT
static int compat_raw_ioctl(struct sock *sk, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case SIOCOUTQ:
	case SIOCINQ:
		return -ENOIOCTLCMD;
	default:
#ifdef CONFIG_IP_MROUTE
		return ipmr_compat_ioctl(sk, cmd, compat_ptr(arg));
#else
		return -ENOIOCTLCMD;
#endif
	}
}
#endif

int raw_abort(struct sock *sk, int err)
{
	lock_sock(sk);

	sk->sk_err = err;
	sk_error_report(sk);
	__udp_disconnect(sk, 0);

	release_sock(sk);

	return 0;
}
EXPORT_SYMBOL_GPL(raw_abort);

struct proto raw_prot = {
	.name		   = "RAW",
	.owner		   = THIS_MODULE,
	.close		   = raw_close,
	.destroy	   = raw_destroy,
	.connect	   = ip4_datagram_connect,
	.disconnect	   = __udp_disconnect,
	.ioctl		   = raw_ioctl,
	.init		   = raw_sk_init,
	.setsockopt	   = raw_setsockopt,
	.getsockopt	   = raw_getsockopt,
	.sendmsg	   = raw_sendmsg,
	.recvmsg	   = raw_recvmsg,
	.bind		   = raw_bind,
	.backlog_rcv	   = raw_rcv_skb,
	.release_cb	   = ip4_datagram_release_cb,
	.hash		   = raw_hash_sk,
	.unhash		   = raw_unhash_sk,
	.obj_size	   = sizeof(struct raw_sock),
	.useroffset	   = offsetof(struct raw_sock, filter),
	.usersize	   = sizeof_field(struct raw_sock, filter),
	.h.raw_hash	   = &raw_v4_hashinfo,
#ifdef CONFIG_COMPAT
	.compat_ioctl	   = compat_raw_ioctl,
#endif
	.diag_destroy	   = raw_abort,
};

#ifdef CONFIG_PROC_FS
static struct sock *raw_get_first(struct seq_file *seq, int bucket)
{
	struct raw_hashinfo *h = pde_data(file_inode(seq->file));
	struct raw_iter_state *state = raw_seq_private(seq);
	struct hlist_head *hlist;
	struct sock *sk;

	for (state->bucket = bucket; state->bucket < RAW_HTABLE_SIZE;
			++state->bucket) {
		hlist = &h->ht[state->bucket];
		sk_for_each(sk, hlist) {
			if (sock_net(sk) == seq_file_net(seq))
				return sk;
		}
	}
	return NULL;
}

static struct sock *raw_get_next(struct seq_file *seq, struct sock *sk)
{
	struct raw_iter_state *state = raw_seq_private(seq);

	do {
		sk = sk_next(sk);
	} while (sk && sock_net(sk) != seq_file_net(seq));

	if (!sk)
		return raw_get_first(seq, state->bucket + 1);
	return sk;
}

static struct sock *raw_get_idx(struct seq_file *seq, loff_t pos)
{
	struct sock *sk = raw_get_first(seq, 0);

	if (sk)
		while (pos && (sk = raw_get_next(seq, sk)) != NULL)
			--pos;
	return pos ? NULL : sk;
}

void *raw_seq_start(struct seq_file *seq, loff_t *pos)
	__acquires(&h->lock)
{
	struct raw_hashinfo *h = pde_data(file_inode(seq->file));

	spin_lock(&h->lock);

	return *pos ? raw_get_idx(seq, *pos - 1) : SEQ_START_TOKEN;
}
EXPORT_SYMBOL_GPL(raw_seq_start);

void *raw_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct sock *sk;

	if (v == SEQ_START_TOKEN)
		sk = raw_get_first(seq, 0);
	else
		sk = raw_get_next(seq, v);
	++*pos;
	return sk;
}
EXPORT_SYMBOL_GPL(raw_seq_next);

void raw_seq_stop(struct seq_file *seq, void *v)
	__releases(&h->lock)
{
	struct raw_hashinfo *h = pde_data(file_inode(seq->file));

	spin_unlock(&h->lock);
}
EXPORT_SYMBOL_GPL(raw_seq_stop);

static void raw_sock_seq_show(struct seq_file *seq, struct sock *sp, int i)
{
	struct inet_sock *inet = inet_sk(sp);
	__be32 dest = inet->inet_daddr,
	       src = inet->inet_rcv_saddr;
	__u16 destp = 0,
	      srcp  = inet->inet_num;

	seq_printf(seq, "%4d: %08X:%04X %08X:%04X"
		" %02X %08X:%08X %02X:%08lX %08X %5u %8d %lu %d %pK %u\n",
		i, src, srcp, dest, destp, sp->sk_state,
		sk_wmem_alloc_get(sp),
		sk_rmem_alloc_get(sp),
		0, 0L, 0,
		from_kuid_munged(seq_user_ns(seq), sock_i_uid(sp)),
		0, sock_i_ino(sp),
		refcount_read(&sp->sk_refcnt), sp, atomic_read(&sp->sk_drops));
}

static int raw_seq_show(struct seq_file *seq, void *v)
{
	if (v == SEQ_START_TOKEN)
		seq_printf(seq, "  sl  local_address rem_address   st tx_queue "
				"rx_queue tr tm->when retrnsmt   uid  timeout "
				"inode ref pointer drops\n");
	else
		raw_sock_seq_show(seq, v, raw_seq_private(seq)->bucket);
	return 0;
}

static const struct seq_operations raw_seq_ops = {
	.start = raw_seq_start,
	.next  = raw_seq_next,
	.stop  = raw_seq_stop,
	.show  = raw_seq_show,
};

static __net_init int raw_init_net(struct net *net)
{
	if (!proc_create_net_data("raw", 0444, net->proc_net, &raw_seq_ops,
			sizeof(struct raw_iter_state), &raw_v4_hashinfo))
		return -ENOMEM;

	return 0;
}

static __net_exit void raw_exit_net(struct net *net)
{
	remove_proc_entry("raw", net->proc_net);
}

static __net_initdata struct pernet_operations raw_net_ops = {
	.init = raw_init_net,
	.exit = raw_exit_net,
};

int __init raw_proc_init(void)
{

	return register_pernet_subsys(&raw_net_ops);
}

void __init raw_proc_exit(void)
{
	unregister_pernet_subsys(&raw_net_ops);
}
#endif /* CONFIG_PROC_FS */

static void raw_sysctl_init_net(struct net *net)
{
#ifdef CONFIG_NET_L3_MASTER_DEV
	net->ipv4.sysctl_raw_l3mdev_accept = 1;
#endif
}

static int __net_init raw_sysctl_init(struct net *net)
{
	raw_sysctl_init_net(net);
	return 0;
}

static struct pernet_operations __net_initdata raw_sysctl_ops = {
	.init	= raw_sysctl_init,
};

void __init raw_init(void)
{
	raw_sysctl_init_net(&init_net);
	if (register_pernet_subsys(&raw_sysctl_ops))
		panic("RAW: failed to init sysctl parameters.\n");
}
