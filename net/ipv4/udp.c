/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		The User Datagram Protocol (UDP).
 *
 * Version:	$Id: udp.c,v 1.102 2002/02/01 22:01:04 davem Exp $
 *
 * Authors:	Ross Biro
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Arnt Gulbrandsen, <agulbra@nvg.unit.no>
 *		Alan Cox, <Alan.Cox@linux.org>
 *		Hirokazu Takahashi, <taka@valinux.co.jp>
 *
 * Fixes:
 *		Alan Cox	:	verify_area() calls
 *		Alan Cox	: 	stopped close while in use off icmp
 *					messages. Not a fix but a botch that
 *					for udp at least is 'valid'.
 *		Alan Cox	:	Fixed icmp handling properly
 *		Alan Cox	: 	Correct error for oversized datagrams
 *		Alan Cox	:	Tidied select() semantics. 
 *		Alan Cox	:	udp_err() fixed properly, also now 
 *					select and read wake correctly on errors
 *		Alan Cox	:	udp_send verify_area moved to avoid mem leak
 *		Alan Cox	:	UDP can count its memory
 *		Alan Cox	:	send to an unknown connection causes
 *					an ECONNREFUSED off the icmp, but
 *					does NOT close.
 *		Alan Cox	:	Switched to new sk_buff handlers. No more backlog!
 *		Alan Cox	:	Using generic datagram code. Even smaller and the PEEK
 *					bug no longer crashes it.
 *		Fred Van Kempen	: 	Net2e support for sk->broadcast.
 *		Alan Cox	:	Uses skb_free_datagram
 *		Alan Cox	:	Added get/set sockopt support.
 *		Alan Cox	:	Broadcasting without option set returns EACCES.
 *		Alan Cox	:	No wakeup calls. Instead we now use the callbacks.
 *		Alan Cox	:	Use ip_tos and ip_ttl
 *		Alan Cox	:	SNMP Mibs
 *		Alan Cox	:	MSG_DONTROUTE, and 0.0.0.0 support.
 *		Matt Dillon	:	UDP length checks.
 *		Alan Cox	:	Smarter af_inet used properly.
 *		Alan Cox	:	Use new kernel side addressing.
 *		Alan Cox	:	Incorrect return on truncated datagram receive.
 *	Arnt Gulbrandsen 	:	New udp_send and stuff
 *		Alan Cox	:	Cache last socket
 *		Alan Cox	:	Route cache
 *		Jon Peatfield	:	Minor efficiency fix to sendto().
 *		Mike Shaver	:	RFC1122 checks.
 *		Alan Cox	:	Nonblocking error fix.
 *	Willy Konynenberg	:	Transparent proxying support.
 *		Mike McLagan	:	Routing by source
 *		David S. Miller	:	New socket lookup architecture.
 *					Last socket cache retained as it
 *					does have a high hit rate.
 *		Olaf Kirch	:	Don't linearise iovec on sendmsg.
 *		Andi Kleen	:	Some cleanups, cache destination entry
 *					for connect. 
 *	Vitaly E. Lavrov	:	Transparent proxy revived after year coma.
 *		Melvin Smith	:	Check msg_name not msg_namelen in sendto(),
 *					return ENOTCONN for unconnected sockets (POSIX)
 *		Janos Farkas	:	don't deliver multi/broadcasts to a different
 *					bound-to-device socket
 *	Hirokazu Takahashi	:	HW checksumming for outgoing UDP
 *					datagrams.
 *	Hirokazu Takahashi	:	sendfile() on UDP works now.
 *		Arnaldo C. Melo :	convert /proc/net/udp to seq_file
 *	YOSHIFUJI Hideaki @USAGI and:	Support IPV6_V6ONLY socket option, which
 *	Alexey Kuznetsov:		allow both IPv4 and IPv6 sockets to bind
 *					a single port at the same time.
 *	Derek Atkins <derek@ihtfp.com>: Add Encapulation Support
 *
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
 
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/ioctls.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/module.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/igmp.h>
#include <linux/in.h>
#include <linux/errno.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/config.h>
#include <linux/inet.h>
#include <linux/ipv6.h>
#include <linux/netdevice.h>
#include <net/snmp.h>
#include <net/ip.h>
#include <net/tcp_states.h>
#include <net/protocol.h>
#include <linux/skbuff.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <net/sock.h>
#include <net/udp.h>
#include <net/icmp.h>
#include <net/route.h>
#include <net/inet_common.h>
#include <net/checksum.h>
#include <net/xfrm.h>

/*
 *	Snmp MIB for the UDP layer
 */

DEFINE_SNMP_STAT(struct udp_mib, udp_statistics) __read_mostly;

struct hlist_head udp_hash[UDP_HTABLE_SIZE];
DEFINE_RWLOCK(udp_hash_lock);

/* Shared by v4/v6 udp. */
int udp_port_rover;

static int udp_v4_get_port(struct sock *sk, unsigned short snum)
{
	struct hlist_node *node;
	struct sock *sk2;
	struct inet_sock *inet = inet_sk(sk);

	write_lock_bh(&udp_hash_lock);
	if (snum == 0) {
		int best_size_so_far, best, result, i;

		if (udp_port_rover > sysctl_local_port_range[1] ||
		    udp_port_rover < sysctl_local_port_range[0])
			udp_port_rover = sysctl_local_port_range[0];
		best_size_so_far = 32767;
		best = result = udp_port_rover;
		for (i = 0; i < UDP_HTABLE_SIZE; i++, result++) {
			struct hlist_head *list;
			int size;

			list = &udp_hash[result & (UDP_HTABLE_SIZE - 1)];
			if (hlist_empty(list)) {
				if (result > sysctl_local_port_range[1])
					result = sysctl_local_port_range[0] +
						((result - sysctl_local_port_range[0]) &
						 (UDP_HTABLE_SIZE - 1));
				goto gotit;
			}
			size = 0;
			sk_for_each(sk2, node, list)
				if (++size >= best_size_so_far)
					goto next;
			best_size_so_far = size;
			best = result;
		next:;
		}
		result = best;
		for(i = 0; i < (1 << 16) / UDP_HTABLE_SIZE; i++, result += UDP_HTABLE_SIZE) {
			if (result > sysctl_local_port_range[1])
				result = sysctl_local_port_range[0]
					+ ((result - sysctl_local_port_range[0]) &
					   (UDP_HTABLE_SIZE - 1));
			if (!udp_lport_inuse(result))
				break;
		}
		if (i >= (1 << 16) / UDP_HTABLE_SIZE)
			goto fail;
gotit:
		udp_port_rover = snum = result;
	} else {
		sk_for_each(sk2, node,
			    &udp_hash[snum & (UDP_HTABLE_SIZE - 1)]) {
			struct inet_sock *inet2 = inet_sk(sk2);

			if (inet2->num == snum &&
			    sk2 != sk &&
			    !ipv6_only_sock(sk2) &&
			    (!sk2->sk_bound_dev_if ||
			     !sk->sk_bound_dev_if ||
			     sk2->sk_bound_dev_if == sk->sk_bound_dev_if) &&
			    (!inet2->rcv_saddr ||
			     !inet->rcv_saddr ||
			     inet2->rcv_saddr == inet->rcv_saddr) &&
			    (!sk2->sk_reuse || !sk->sk_reuse))
				goto fail;
		}
	}
	inet->num = snum;
	if (sk_unhashed(sk)) {
		struct hlist_head *h = &udp_hash[snum & (UDP_HTABLE_SIZE - 1)];

		sk_add_node(sk, h);
		sock_prot_inc_use(sk->sk_prot);
	}
	write_unlock_bh(&udp_hash_lock);
	return 0;

fail:
	write_unlock_bh(&udp_hash_lock);
	return 1;
}

static void udp_v4_hash(struct sock *sk)
{
	BUG();
}

static void udp_v4_unhash(struct sock *sk)
{
	write_lock_bh(&udp_hash_lock);
	if (sk_del_node_init(sk)) {
		inet_sk(sk)->num = 0;
		sock_prot_dec_use(sk->sk_prot);
	}
	write_unlock_bh(&udp_hash_lock);
}

/* UDP is nearly always wildcards out the wazoo, it makes no sense to try
 * harder than this. -DaveM
 */
static struct sock *udp_v4_lookup_longway(u32 saddr, u16 sport,
					  u32 daddr, u16 dport, int dif)
{
	struct sock *sk, *result = NULL;
	struct hlist_node *node;
	unsigned short hnum = ntohs(dport);
	int badness = -1;

	sk_for_each(sk, node, &udp_hash[hnum & (UDP_HTABLE_SIZE - 1)]) {
		struct inet_sock *inet = inet_sk(sk);

		if (inet->num == hnum && !ipv6_only_sock(sk)) {
			int score = (sk->sk_family == PF_INET ? 1 : 0);
			if (inet->rcv_saddr) {
				if (inet->rcv_saddr != daddr)
					continue;
				score+=2;
			}
			if (inet->daddr) {
				if (inet->daddr != saddr)
					continue;
				score+=2;
			}
			if (inet->dport) {
				if (inet->dport != sport)
					continue;
				score+=2;
			}
			if (sk->sk_bound_dev_if) {
				if (sk->sk_bound_dev_if != dif)
					continue;
				score+=2;
			}
			if(score == 9) {
				result = sk;
				break;
			} else if(score > badness) {
				result = sk;
				badness = score;
			}
		}
	}
	return result;
}

static __inline__ struct sock *udp_v4_lookup(u32 saddr, u16 sport,
					     u32 daddr, u16 dport, int dif)
{
	struct sock *sk;

	read_lock(&udp_hash_lock);
	sk = udp_v4_lookup_longway(saddr, sport, daddr, dport, dif);
	if (sk)
		sock_hold(sk);
	read_unlock(&udp_hash_lock);
	return sk;
}

static inline struct sock *udp_v4_mcast_next(struct sock *sk,
					     u16 loc_port, u32 loc_addr,
					     u16 rmt_port, u32 rmt_addr,
					     int dif)
{
	struct hlist_node *node;
	struct sock *s = sk;
	unsigned short hnum = ntohs(loc_port);

	sk_for_each_from(s, node) {
		struct inet_sock *inet = inet_sk(s);

		if (inet->num != hnum					||
		    (inet->daddr && inet->daddr != rmt_addr)		||
		    (inet->dport != rmt_port && inet->dport)		||
		    (inet->rcv_saddr && inet->rcv_saddr != loc_addr)	||
		    ipv6_only_sock(s)					||
		    (s->sk_bound_dev_if && s->sk_bound_dev_if != dif))
			continue;
		if (!ip_mc_sf_allow(s, loc_addr, rmt_addr, dif))
			continue;
		goto found;
  	}
	s = NULL;
found:
  	return s;
}

/*
 * This routine is called by the ICMP module when it gets some
 * sort of error condition.  If err < 0 then the socket should
 * be closed and the error returned to the user.  If err > 0
 * it's just the icmp type << 8 | icmp code.  
 * Header points to the ip header of the error packet. We move
 * on past this. Then (as it used to claim before adjustment)
 * header points to the first 8 bytes of the udp header.  We need
 * to find the appropriate port.
 */

void udp_err(struct sk_buff *skb, u32 info)
{
	struct inet_sock *inet;
	struct iphdr *iph = (struct iphdr*)skb->data;
	struct udphdr *uh = (struct udphdr*)(skb->data+(iph->ihl<<2));
	int type = skb->h.icmph->type;
	int code = skb->h.icmph->code;
	struct sock *sk;
	int harderr;
	int err;

	sk = udp_v4_lookup(iph->daddr, uh->dest, iph->saddr, uh->source, skb->dev->ifindex);
	if (sk == NULL) {
		ICMP_INC_STATS_BH(ICMP_MIB_INERRORS);
    	  	return;	/* No socket for error */
	}

	err = 0;
	harderr = 0;
	inet = inet_sk(sk);

	switch (type) {
	default:
	case ICMP_TIME_EXCEEDED:
		err = EHOSTUNREACH;
		break;
	case ICMP_SOURCE_QUENCH:
		goto out;
	case ICMP_PARAMETERPROB:
		err = EPROTO;
		harderr = 1;
		break;
	case ICMP_DEST_UNREACH:
		if (code == ICMP_FRAG_NEEDED) { /* Path MTU discovery */
			if (inet->pmtudisc != IP_PMTUDISC_DONT) {
				err = EMSGSIZE;
				harderr = 1;
				break;
			}
			goto out;
		}
		err = EHOSTUNREACH;
		if (code <= NR_ICMP_UNREACH) {
			harderr = icmp_err_convert[code].fatal;
			err = icmp_err_convert[code].errno;
		}
		break;
	}

	/*
	 *      RFC1122: OK.  Passes ICMP errors back to application, as per 
	 *	4.1.3.3.
	 */
	if (!inet->recverr) {
		if (!harderr || sk->sk_state != TCP_ESTABLISHED)
			goto out;
	} else {
		ip_icmp_error(sk, skb, err, uh->dest, info, (u8*)(uh+1));
	}
	sk->sk_err = err;
	sk->sk_error_report(sk);
out:
	sock_put(sk);
}

/*
 * Throw away all pending data and cancel the corking. Socket is locked.
 */
static void udp_flush_pending_frames(struct sock *sk)
{
	struct udp_sock *up = udp_sk(sk);

	if (up->pending) {
		up->len = 0;
		up->pending = 0;
		ip_flush_pending_frames(sk);
	}
}

/*
 * Push out all pending data as one UDP datagram. Socket is locked.
 */
static int udp_push_pending_frames(struct sock *sk, struct udp_sock *up)
{
	struct inet_sock *inet = inet_sk(sk);
	struct flowi *fl = &inet->cork.fl;
	struct sk_buff *skb;
	struct udphdr *uh;
	int err = 0;

	/* Grab the skbuff where UDP header space exists. */
	if ((skb = skb_peek(&sk->sk_write_queue)) == NULL)
		goto out;

	/*
	 * Create a UDP header
	 */
	uh = skb->h.uh;
	uh->source = fl->fl_ip_sport;
	uh->dest = fl->fl_ip_dport;
	uh->len = htons(up->len);
	uh->check = 0;

	if (sk->sk_no_check == UDP_CSUM_NOXMIT) {
		skb->ip_summed = CHECKSUM_NONE;
		goto send;
	}

	if (skb_queue_len(&sk->sk_write_queue) == 1) {
		/*
		 * Only one fragment on the socket.
		 */
		if (skb->ip_summed == CHECKSUM_HW) {
			skb->csum = offsetof(struct udphdr, check);
			uh->check = ~csum_tcpudp_magic(fl->fl4_src, fl->fl4_dst,
					up->len, IPPROTO_UDP, 0);
		} else {
			skb->csum = csum_partial((char *)uh,
					sizeof(struct udphdr), skb->csum);
			uh->check = csum_tcpudp_magic(fl->fl4_src, fl->fl4_dst,
					up->len, IPPROTO_UDP, skb->csum);
			if (uh->check == 0)
				uh->check = -1;
		}
	} else {
		unsigned int csum = 0;
		/*
		 * HW-checksum won't work as there are two or more 
		 * fragments on the socket so that all csums of sk_buffs
		 * should be together.
		 */
		if (skb->ip_summed == CHECKSUM_HW) {
			int offset = (unsigned char *)uh - skb->data;
			skb->csum = skb_checksum(skb, offset, skb->len - offset, 0);

			skb->ip_summed = CHECKSUM_NONE;
		} else {
			skb->csum = csum_partial((char *)uh,
					sizeof(struct udphdr), skb->csum);
		}

		skb_queue_walk(&sk->sk_write_queue, skb) {
			csum = csum_add(csum, skb->csum);
		}
		uh->check = csum_tcpudp_magic(fl->fl4_src, fl->fl4_dst,
				up->len, IPPROTO_UDP, csum);
		if (uh->check == 0)
			uh->check = -1;
	}
send:
	err = ip_push_pending_frames(sk);
out:
	up->len = 0;
	up->pending = 0;
	return err;
}


static unsigned short udp_check(struct udphdr *uh, int len, unsigned long saddr, unsigned long daddr, unsigned long base)
{
	return(csum_tcpudp_magic(saddr, daddr, len, IPPROTO_UDP, base));
}

int udp_sendmsg(struct kiocb *iocb, struct sock *sk, struct msghdr *msg,
		size_t len)
{
	struct inet_sock *inet = inet_sk(sk);
	struct udp_sock *up = udp_sk(sk);
	int ulen = len;
	struct ipcm_cookie ipc;
	struct rtable *rt = NULL;
	int free = 0;
	int connected = 0;
	u32 daddr, faddr, saddr;
	u16 dport;
	u8  tos;
	int err;
	int corkreq = up->corkflag || msg->msg_flags&MSG_MORE;

	if (len > 0xFFFF)
		return -EMSGSIZE;

	/* 
	 *	Check the flags.
	 */

	if (msg->msg_flags&MSG_OOB)	/* Mirror BSD error message compatibility */
		return -EOPNOTSUPP;

	ipc.opt = NULL;

	if (up->pending) {
		/*
		 * There are pending frames.
	 	 * The socket lock must be held while it's corked.
		 */
		lock_sock(sk);
		if (likely(up->pending)) {
			if (unlikely(up->pending != AF_INET)) {
				release_sock(sk);
				return -EINVAL;
			}
 			goto do_append_data;
		}
		release_sock(sk);
	}
	ulen += sizeof(struct udphdr);

	/*
	 *	Get and verify the address. 
	 */
	if (msg->msg_name) {
		struct sockaddr_in * usin = (struct sockaddr_in*)msg->msg_name;
		if (msg->msg_namelen < sizeof(*usin))
			return -EINVAL;
		if (usin->sin_family != AF_INET) {
			if (usin->sin_family != AF_UNSPEC)
				return -EAFNOSUPPORT;
		}

		daddr = usin->sin_addr.s_addr;
		dport = usin->sin_port;
		if (dport == 0)
			return -EINVAL;
	} else {
		if (sk->sk_state != TCP_ESTABLISHED)
			return -EDESTADDRREQ;
		daddr = inet->daddr;
		dport = inet->dport;
		/* Open fast path for connected socket.
		   Route will not be used, if at least one option is set.
		 */
		connected = 1;
  	}
	ipc.addr = inet->saddr;

	ipc.oif = sk->sk_bound_dev_if;
	if (msg->msg_controllen) {
		err = ip_cmsg_send(msg, &ipc);
		if (err)
			return err;
		if (ipc.opt)
			free = 1;
		connected = 0;
	}
	if (!ipc.opt)
		ipc.opt = inet->opt;

	saddr = ipc.addr;
	ipc.addr = faddr = daddr;

	if (ipc.opt && ipc.opt->srr) {
		if (!daddr)
			return -EINVAL;
		faddr = ipc.opt->faddr;
		connected = 0;
	}
	tos = RT_TOS(inet->tos);
	if (sock_flag(sk, SOCK_LOCALROUTE) ||
	    (msg->msg_flags & MSG_DONTROUTE) || 
	    (ipc.opt && ipc.opt->is_strictroute)) {
		tos |= RTO_ONLINK;
		connected = 0;
	}

	if (MULTICAST(daddr)) {
		if (!ipc.oif)
			ipc.oif = inet->mc_index;
		if (!saddr)
			saddr = inet->mc_addr;
		connected = 0;
	}

	if (connected)
		rt = (struct rtable*)sk_dst_check(sk, 0);

	if (rt == NULL) {
		struct flowi fl = { .oif = ipc.oif,
				    .nl_u = { .ip4_u =
					      { .daddr = faddr,
						.saddr = saddr,
						.tos = tos } },
				    .proto = IPPROTO_UDP,
				    .uli_u = { .ports =
					       { .sport = inet->sport,
						 .dport = dport } } };
		err = ip_route_output_flow(&rt, &fl, sk, !(msg->msg_flags&MSG_DONTWAIT));
		if (err)
			goto out;

		err = -EACCES;
		if ((rt->rt_flags & RTCF_BROADCAST) &&
		    !sock_flag(sk, SOCK_BROADCAST))
			goto out;
		if (connected)
			sk_dst_set(sk, dst_clone(&rt->u.dst));
	}

	if (msg->msg_flags&MSG_CONFIRM)
		goto do_confirm;
back_from_confirm:

	saddr = rt->rt_src;
	if (!ipc.addr)
		daddr = ipc.addr = rt->rt_dst;

	lock_sock(sk);
	if (unlikely(up->pending)) {
		/* The socket is already corked while preparing it. */
		/* ... which is an evident application bug. --ANK */
		release_sock(sk);

		LIMIT_NETDEBUG(KERN_DEBUG "udp cork app bug 2\n");
		err = -EINVAL;
		goto out;
	}
	/*
	 *	Now cork the socket to pend data.
	 */
	inet->cork.fl.fl4_dst = daddr;
	inet->cork.fl.fl_ip_dport = dport;
	inet->cork.fl.fl4_src = saddr;
	inet->cork.fl.fl_ip_sport = inet->sport;
	up->pending = AF_INET;

do_append_data:
	up->len += ulen;
	err = ip_append_data(sk, ip_generic_getfrag, msg->msg_iov, ulen, 
			sizeof(struct udphdr), &ipc, rt, 
			corkreq ? msg->msg_flags|MSG_MORE : msg->msg_flags);
	if (err)
		udp_flush_pending_frames(sk);
	else if (!corkreq)
		err = udp_push_pending_frames(sk, up);
	release_sock(sk);

out:
	ip_rt_put(rt);
	if (free)
		kfree(ipc.opt);
	if (!err) {
		UDP_INC_STATS_USER(UDP_MIB_OUTDATAGRAMS);
		return len;
	}
	return err;

do_confirm:
	dst_confirm(&rt->u.dst);
	if (!(msg->msg_flags&MSG_PROBE) || len)
		goto back_from_confirm;
	err = 0;
	goto out;
}

static int udp_sendpage(struct sock *sk, struct page *page, int offset,
			size_t size, int flags)
{
	struct udp_sock *up = udp_sk(sk);
	int ret;

	if (!up->pending) {
		struct msghdr msg = {	.msg_flags = flags|MSG_MORE };

		/* Call udp_sendmsg to specify destination address which
		 * sendpage interface can't pass.
		 * This will succeed only when the socket is connected.
		 */
		ret = udp_sendmsg(NULL, sk, &msg, 0);
		if (ret < 0)
			return ret;
	}

	lock_sock(sk);

	if (unlikely(!up->pending)) {
		release_sock(sk);

		LIMIT_NETDEBUG(KERN_DEBUG "udp cork app bug 3\n");
		return -EINVAL;
	}

	ret = ip_append_page(sk, page, offset, size, flags);
	if (ret == -EOPNOTSUPP) {
		release_sock(sk);
		return sock_no_sendpage(sk->sk_socket, page, offset,
					size, flags);
	}
	if (ret < 0) {
		udp_flush_pending_frames(sk);
		goto out;
	}

	up->len += size;
	if (!(up->corkflag || (flags&MSG_MORE)))
		ret = udp_push_pending_frames(sk, up);
	if (!ret)
		ret = size;
out:
	release_sock(sk);
	return ret;
}

/*
 *	IOCTL requests applicable to the UDP protocol
 */
 
int udp_ioctl(struct sock *sk, int cmd, unsigned long arg)
{
	switch(cmd) 
	{
		case SIOCOUTQ:
		{
			int amount = atomic_read(&sk->sk_wmem_alloc);
			return put_user(amount, (int __user *)arg);
		}

		case SIOCINQ:
		{
			struct sk_buff *skb;
			unsigned long amount;

			amount = 0;
			spin_lock_bh(&sk->sk_receive_queue.lock);
			skb = skb_peek(&sk->sk_receive_queue);
			if (skb != NULL) {
				/*
				 * We will only return the amount
				 * of this packet since that is all
				 * that will be read.
				 */
				amount = skb->len - sizeof(struct udphdr);
			}
			spin_unlock_bh(&sk->sk_receive_queue.lock);
			return put_user(amount, (int __user *)arg);
		}

		default:
			return -ENOIOCTLCMD;
	}
	return(0);
}

static __inline__ int __udp_checksum_complete(struct sk_buff *skb)
{
	return __skb_checksum_complete(skb);
}

static __inline__ int udp_checksum_complete(struct sk_buff *skb)
{
	return skb->ip_summed != CHECKSUM_UNNECESSARY &&
		__udp_checksum_complete(skb);
}

/*
 * 	This should be easy, if there is something there we
 * 	return it, otherwise we block.
 */

static int udp_recvmsg(struct kiocb *iocb, struct sock *sk, struct msghdr *msg,
		       size_t len, int noblock, int flags, int *addr_len)
{
	struct inet_sock *inet = inet_sk(sk);
  	struct sockaddr_in *sin = (struct sockaddr_in *)msg->msg_name;
  	struct sk_buff *skb;
  	int copied, err;

	/*
	 *	Check any passed addresses
	 */
	if (addr_len)
		*addr_len=sizeof(*sin);

	if (flags & MSG_ERRQUEUE)
		return ip_recv_error(sk, msg, len);

try_again:
	skb = skb_recv_datagram(sk, flags, noblock, &err);
	if (!skb)
		goto out;
  
  	copied = skb->len - sizeof(struct udphdr);
	if (copied > len) {
		copied = len;
		msg->msg_flags |= MSG_TRUNC;
	}

	if (skb->ip_summed==CHECKSUM_UNNECESSARY) {
		err = skb_copy_datagram_iovec(skb, sizeof(struct udphdr), msg->msg_iov,
					      copied);
	} else if (msg->msg_flags&MSG_TRUNC) {
		if (__udp_checksum_complete(skb))
			goto csum_copy_err;
		err = skb_copy_datagram_iovec(skb, sizeof(struct udphdr), msg->msg_iov,
					      copied);
	} else {
		err = skb_copy_and_csum_datagram_iovec(skb, sizeof(struct udphdr), msg->msg_iov);

		if (err == -EINVAL)
			goto csum_copy_err;
	}

	if (err)
		goto out_free;

	sock_recv_timestamp(msg, sk, skb);

	/* Copy the address. */
	if (sin)
	{
		sin->sin_family = AF_INET;
		sin->sin_port = skb->h.uh->source;
		sin->sin_addr.s_addr = skb->nh.iph->saddr;
		memset(sin->sin_zero, 0, sizeof(sin->sin_zero));
  	}
	if (inet->cmsg_flags)
		ip_cmsg_recv(msg, skb);

	err = copied;
	if (flags & MSG_TRUNC)
		err = skb->len - sizeof(struct udphdr);
  
out_free:
  	skb_free_datagram(sk, skb);
out:
  	return err;

csum_copy_err:
	UDP_INC_STATS_BH(UDP_MIB_INERRORS);

	skb_kill_datagram(sk, skb, flags);

	if (noblock)
		return -EAGAIN;	
	goto try_again;
}


int udp_disconnect(struct sock *sk, int flags)
{
	struct inet_sock *inet = inet_sk(sk);
	/*
	 *	1003.1g - break association.
	 */
	 
	sk->sk_state = TCP_CLOSE;
	inet->daddr = 0;
	inet->dport = 0;
	sk->sk_bound_dev_if = 0;
	if (!(sk->sk_userlocks & SOCK_BINDADDR_LOCK))
		inet_reset_saddr(sk);

	if (!(sk->sk_userlocks & SOCK_BINDPORT_LOCK)) {
		sk->sk_prot->unhash(sk);
		inet->sport = 0;
	}
	sk_dst_reset(sk);
	return 0;
}

static void udp_close(struct sock *sk, long timeout)
{
	sk_common_release(sk);
}

/* return:
 * 	1  if the the UDP system should process it
 *	0  if we should drop this packet
 * 	-1 if it should get processed by xfrm4_rcv_encap
 */
static int udp_encap_rcv(struct sock * sk, struct sk_buff *skb)
{
#ifndef CONFIG_XFRM
	return 1; 
#else
	struct udp_sock *up = udp_sk(sk);
  	struct udphdr *uh = skb->h.uh;
	struct iphdr *iph;
	int iphlen, len;
  
	__u8 *udpdata = (__u8 *)uh + sizeof(struct udphdr);
	__u32 *udpdata32 = (__u32 *)udpdata;
	__u16 encap_type = up->encap_type;

	/* if we're overly short, let UDP handle it */
	if (udpdata > skb->tail)
		return 1;

	/* if this is not encapsulated socket, then just return now */
	if (!encap_type)
		return 1;

	len = skb->tail - udpdata;

	switch (encap_type) {
	default:
	case UDP_ENCAP_ESPINUDP:
		/* Check if this is a keepalive packet.  If so, eat it. */
		if (len == 1 && udpdata[0] == 0xff) {
			return 0;
		} else if (len > sizeof(struct ip_esp_hdr) && udpdata32[0] != 0 ) {
			/* ESP Packet without Non-ESP header */
			len = sizeof(struct udphdr);
		} else
			/* Must be an IKE packet.. pass it through */
			return 1;
		break;
	case UDP_ENCAP_ESPINUDP_NON_IKE:
		/* Check if this is a keepalive packet.  If so, eat it. */
		if (len == 1 && udpdata[0] == 0xff) {
			return 0;
		} else if (len > 2 * sizeof(u32) + sizeof(struct ip_esp_hdr) &&
			   udpdata32[0] == 0 && udpdata32[1] == 0) {
			
			/* ESP Packet with Non-IKE marker */
			len = sizeof(struct udphdr) + 2 * sizeof(u32);
		} else
			/* Must be an IKE packet.. pass it through */
			return 1;
		break;
	}

	/* At this point we are sure that this is an ESPinUDP packet,
	 * so we need to remove 'len' bytes from the packet (the UDP
	 * header and optional ESP marker bytes) and then modify the
	 * protocol to ESP, and then call into the transform receiver.
	 */
	if (skb_cloned(skb) && pskb_expand_head(skb, 0, 0, GFP_ATOMIC))
		return 0;

	/* Now we can update and verify the packet length... */
	iph = skb->nh.iph;
	iphlen = iph->ihl << 2;
	iph->tot_len = htons(ntohs(iph->tot_len) - len);
	if (skb->len < iphlen + len) {
		/* packet is too small!?! */
		return 0;
	}

	/* pull the data buffer up to the ESP header and set the
	 * transport header to point to ESP.  Keep UDP on the stack
	 * for later.
	 */
	skb->h.raw = skb_pull(skb, len);

	/* modify the protocol (it's ESP!) */
	iph->protocol = IPPROTO_ESP;

	/* and let the caller know to send this into the ESP processor... */
	return -1;
#endif
}

/* returns:
 *  -1: error
 *   0: success
 *  >0: "udp encap" protocol resubmission
 *
 * Note that in the success and error cases, the skb is assumed to
 * have either been requeued or freed.
 */
static int udp_queue_rcv_skb(struct sock * sk, struct sk_buff *skb)
{
	struct udp_sock *up = udp_sk(sk);

	/*
	 *	Charge it to the socket, dropping if the queue is full.
	 */
	if (!xfrm4_policy_check(sk, XFRM_POLICY_IN, skb)) {
		kfree_skb(skb);
		return -1;
	}
	nf_reset(skb);

	if (up->encap_type) {
		/*
		 * This is an encapsulation socket, so let's see if this is
		 * an encapsulated packet.
		 * If it's a keepalive packet, then just eat it.
		 * If it's an encapsulateed packet, then pass it to the
		 * IPsec xfrm input and return the response
		 * appropriately.  Otherwise, just fall through and
		 * pass this up the UDP socket.
		 */
		int ret;

		ret = udp_encap_rcv(sk, skb);
		if (ret == 0) {
			/* Eat the packet .. */
			kfree_skb(skb);
			return 0;
		}
		if (ret < 0) {
			/* process the ESP packet */
			ret = xfrm4_rcv_encap(skb, up->encap_type);
			UDP_INC_STATS_BH(UDP_MIB_INDATAGRAMS);
			return -ret;
		}
		/* FALLTHROUGH -- it's a UDP Packet */
	}

	if (sk->sk_filter && skb->ip_summed != CHECKSUM_UNNECESSARY) {
		if (__udp_checksum_complete(skb)) {
			UDP_INC_STATS_BH(UDP_MIB_INERRORS);
			kfree_skb(skb);
			return -1;
		}
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	}

	if (sock_queue_rcv_skb(sk,skb)<0) {
		UDP_INC_STATS_BH(UDP_MIB_INERRORS);
		kfree_skb(skb);
		return -1;
	}
	UDP_INC_STATS_BH(UDP_MIB_INDATAGRAMS);
	return 0;
}

/*
 *	Multicasts and broadcasts go to each listener.
 *
 *	Note: called only from the BH handler context,
 *	so we don't need to lock the hashes.
 */
static int udp_v4_mcast_deliver(struct sk_buff *skb, struct udphdr *uh,
				 u32 saddr, u32 daddr)
{
	struct sock *sk;
	int dif;

	read_lock(&udp_hash_lock);
	sk = sk_head(&udp_hash[ntohs(uh->dest) & (UDP_HTABLE_SIZE - 1)]);
	dif = skb->dev->ifindex;
	sk = udp_v4_mcast_next(sk, uh->dest, daddr, uh->source, saddr, dif);
	if (sk) {
		struct sock *sknext = NULL;

		do {
			struct sk_buff *skb1 = skb;

			sknext = udp_v4_mcast_next(sk_next(sk), uh->dest, daddr,
						   uh->source, saddr, dif);
			if(sknext)
				skb1 = skb_clone(skb, GFP_ATOMIC);

			if(skb1) {
				int ret = udp_queue_rcv_skb(sk, skb1);
				if (ret > 0)
					/* we should probably re-process instead
					 * of dropping packets here. */
					kfree_skb(skb1);
			}
			sk = sknext;
		} while(sknext);
	} else
		kfree_skb(skb);
	read_unlock(&udp_hash_lock);
	return 0;
}

/* Initialize UDP checksum. If exited with zero value (success),
 * CHECKSUM_UNNECESSARY means, that no more checks are required.
 * Otherwise, csum completion requires chacksumming packet body,
 * including udp header and folding it to skb->csum.
 */
static void udp_checksum_init(struct sk_buff *skb, struct udphdr *uh,
			     unsigned short ulen, u32 saddr, u32 daddr)
{
	if (uh->check == 0) {
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	} else if (skb->ip_summed == CHECKSUM_HW) {
		if (!udp_check(uh, ulen, saddr, daddr, skb->csum))
			skb->ip_summed = CHECKSUM_UNNECESSARY;
	}
	if (skb->ip_summed != CHECKSUM_UNNECESSARY)
		skb->csum = csum_tcpudp_nofold(saddr, daddr, ulen, IPPROTO_UDP, 0);
	/* Probably, we should checksum udp header (it should be in cache
	 * in any case) and data in tiny packets (< rx copybreak).
	 */
}

/*
 *	All we need to do is get the socket, and then do a checksum. 
 */
 
int udp_rcv(struct sk_buff *skb)
{
  	struct sock *sk;
  	struct udphdr *uh;
	unsigned short ulen;
	struct rtable *rt = (struct rtable*)skb->dst;
	u32 saddr = skb->nh.iph->saddr;
	u32 daddr = skb->nh.iph->daddr;
	int len = skb->len;

	/*
	 *	Validate the packet and the UDP length.
	 */
	if (!pskb_may_pull(skb, sizeof(struct udphdr)))
		goto no_header;

	uh = skb->h.uh;

	ulen = ntohs(uh->len);

	if (ulen > len || ulen < sizeof(*uh))
		goto short_packet;

	if (pskb_trim_rcsum(skb, ulen))
		goto short_packet;

	udp_checksum_init(skb, uh, ulen, saddr, daddr);

	if(rt->rt_flags & (RTCF_BROADCAST|RTCF_MULTICAST))
		return udp_v4_mcast_deliver(skb, uh, saddr, daddr);

	sk = udp_v4_lookup(saddr, uh->source, daddr, uh->dest, skb->dev->ifindex);

	if (sk != NULL) {
		int ret = udp_queue_rcv_skb(sk, skb);
		sock_put(sk);

		/* a return value > 0 means to resubmit the input, but
		 * it it wants the return to be -protocol, or 0
		 */
		if (ret > 0)
			return -ret;
		return 0;
	}

	if (!xfrm4_policy_check(NULL, XFRM_POLICY_IN, skb))
		goto drop;
	nf_reset(skb);

	/* No socket. Drop packet silently, if checksum is wrong */
	if (udp_checksum_complete(skb))
		goto csum_error;

	UDP_INC_STATS_BH(UDP_MIB_NOPORTS);
	icmp_send(skb, ICMP_DEST_UNREACH, ICMP_PORT_UNREACH, 0);

	/*
	 * Hmm.  We got an UDP packet to a port to which we
	 * don't wanna listen.  Ignore it.
	 */
	kfree_skb(skb);
	return(0);

short_packet:
	LIMIT_NETDEBUG(KERN_DEBUG "UDP: short packet: From %u.%u.%u.%u:%u %d/%d to %u.%u.%u.%u:%u\n",
		       NIPQUAD(saddr),
		       ntohs(uh->source),
		       ulen,
		       len,
		       NIPQUAD(daddr),
		       ntohs(uh->dest));
no_header:
	UDP_INC_STATS_BH(UDP_MIB_INERRORS);
	kfree_skb(skb);
	return(0);

csum_error:
	/* 
	 * RFC1122: OK.  Discards the bad packet silently (as far as 
	 * the network is concerned, anyway) as per 4.1.3.4 (MUST). 
	 */
	LIMIT_NETDEBUG(KERN_DEBUG "UDP: bad checksum. From %d.%d.%d.%d:%d to %d.%d.%d.%d:%d ulen %d\n",
		       NIPQUAD(saddr),
		       ntohs(uh->source),
		       NIPQUAD(daddr),
		       ntohs(uh->dest),
		       ulen);
drop:
	UDP_INC_STATS_BH(UDP_MIB_INERRORS);
	kfree_skb(skb);
	return(0);
}

static int udp_destroy_sock(struct sock *sk)
{
	lock_sock(sk);
	udp_flush_pending_frames(sk);
	release_sock(sk);
	return 0;
}

/*
 *	Socket option code for UDP
 */
static int do_udp_setsockopt(struct sock *sk, int level, int optname,
			  char __user *optval, int optlen)
{
	struct udp_sock *up = udp_sk(sk);
	int val;
	int err = 0;

	if(optlen<sizeof(int))
		return -EINVAL;

	if (get_user(val, (int __user *)optval))
		return -EFAULT;

	switch(optname) {
	case UDP_CORK:
		if (val != 0) {
			up->corkflag = 1;
		} else {
			up->corkflag = 0;
			lock_sock(sk);
			udp_push_pending_frames(sk, up);
			release_sock(sk);
		}
		break;
		
	case UDP_ENCAP:
		switch (val) {
		case 0:
		case UDP_ENCAP_ESPINUDP:
		case UDP_ENCAP_ESPINUDP_NON_IKE:
			up->encap_type = val;
			break;
		default:
			err = -ENOPROTOOPT;
			break;
		}
		break;

	default:
		err = -ENOPROTOOPT;
		break;
	};

	return err;
}

static int udp_setsockopt(struct sock *sk, int level, int optname,
			  char __user *optval, int optlen)
{
	if (level != SOL_UDP)
		return ip_setsockopt(sk, level, optname, optval, optlen);
	return do_udp_setsockopt(sk, level, optname, optval, optlen);
}

#ifdef CONFIG_COMPAT
static int compat_udp_setsockopt(struct sock *sk, int level, int optname,
				 char __user *optval, int optlen)
{
	if (level != SOL_UDP)
		return compat_ip_setsockopt(sk, level, optname, optval, optlen);
	return do_udp_setsockopt(sk, level, optname, optval, optlen);
}
#endif

static int do_udp_getsockopt(struct sock *sk, int level, int optname,
			  char __user *optval, int __user *optlen)
{
	struct udp_sock *up = udp_sk(sk);
	int val, len;

	if(get_user(len,optlen))
		return -EFAULT;

	len = min_t(unsigned int, len, sizeof(int));
	
	if(len < 0)
		return -EINVAL;

	switch(optname) {
	case UDP_CORK:
		val = up->corkflag;
		break;

	case UDP_ENCAP:
		val = up->encap_type;
		break;

	default:
		return -ENOPROTOOPT;
	};

  	if(put_user(len, optlen))
  		return -EFAULT;
	if(copy_to_user(optval, &val,len))
		return -EFAULT;
  	return 0;
}

static int udp_getsockopt(struct sock *sk, int level, int optname,
			  char __user *optval, int __user *optlen)
{
	if (level != SOL_UDP)
		return ip_getsockopt(sk, level, optname, optval, optlen);
	return do_udp_getsockopt(sk, level, optname, optval, optlen);
}

#ifdef CONFIG_COMPAT
static int compat_udp_getsockopt(struct sock *sk, int level, int optname,
				 char __user *optval, int __user *optlen)
{
	if (level != SOL_UDP)
		return compat_ip_getsockopt(sk, level, optname, optval, optlen);
	return do_udp_getsockopt(sk, level, optname, optval, optlen);
}
#endif
/**
 * 	udp_poll - wait for a UDP event.
 *	@file - file struct
 *	@sock - socket
 *	@wait - poll table
 *
 *	This is same as datagram poll, except for the special case of 
 *	blocking sockets. If application is using a blocking fd
 *	and a packet with checksum error is in the queue;
 *	then it could get return from select indicating data available
 *	but then block when reading it. Add special case code
 *	to work around these arguably broken applications.
 */
unsigned int udp_poll(struct file *file, struct socket *sock, poll_table *wait)
{
	unsigned int mask = datagram_poll(file, sock, wait);
	struct sock *sk = sock->sk;
	
	/* Check for false positives due to checksum errors */
	if ( (mask & POLLRDNORM) &&
	     !(file->f_flags & O_NONBLOCK) &&
	     !(sk->sk_shutdown & RCV_SHUTDOWN)){
		struct sk_buff_head *rcvq = &sk->sk_receive_queue;
		struct sk_buff *skb;

		spin_lock_bh(&rcvq->lock);
		while ((skb = skb_peek(rcvq)) != NULL) {
			if (udp_checksum_complete(skb)) {
				UDP_INC_STATS_BH(UDP_MIB_INERRORS);
				__skb_unlink(skb, rcvq);
				kfree_skb(skb);
			} else {
				skb->ip_summed = CHECKSUM_UNNECESSARY;
				break;
			}
		}
		spin_unlock_bh(&rcvq->lock);

		/* nothing to see, move along */
		if (skb == NULL)
			mask &= ~(POLLIN | POLLRDNORM);
	}

	return mask;
	
}

struct proto udp_prot = {
 	.name		   = "UDP",
	.owner		   = THIS_MODULE,
	.close		   = udp_close,
	.connect	   = ip4_datagram_connect,
	.disconnect	   = udp_disconnect,
	.ioctl		   = udp_ioctl,
	.destroy	   = udp_destroy_sock,
	.setsockopt	   = udp_setsockopt,
	.getsockopt	   = udp_getsockopt,
	.sendmsg	   = udp_sendmsg,
	.recvmsg	   = udp_recvmsg,
	.sendpage	   = udp_sendpage,
	.backlog_rcv	   = udp_queue_rcv_skb,
	.hash		   = udp_v4_hash,
	.unhash		   = udp_v4_unhash,
	.get_port	   = udp_v4_get_port,
	.obj_size	   = sizeof(struct udp_sock),
#ifdef CONFIG_COMPAT
	.compat_setsockopt = compat_udp_setsockopt,
	.compat_getsockopt = compat_udp_getsockopt,
#endif
};

/* ------------------------------------------------------------------------ */
#ifdef CONFIG_PROC_FS

static struct sock *udp_get_first(struct seq_file *seq)
{
	struct sock *sk;
	struct udp_iter_state *state = seq->private;

	for (state->bucket = 0; state->bucket < UDP_HTABLE_SIZE; ++state->bucket) {
		struct hlist_node *node;
		sk_for_each(sk, node, &udp_hash[state->bucket]) {
			if (sk->sk_family == state->family)
				goto found;
		}
	}
	sk = NULL;
found:
	return sk;
}

static struct sock *udp_get_next(struct seq_file *seq, struct sock *sk)
{
	struct udp_iter_state *state = seq->private;

	do {
		sk = sk_next(sk);
try_again:
		;
	} while (sk && sk->sk_family != state->family);

	if (!sk && ++state->bucket < UDP_HTABLE_SIZE) {
		sk = sk_head(&udp_hash[state->bucket]);
		goto try_again;
	}
	return sk;
}

static struct sock *udp_get_idx(struct seq_file *seq, loff_t pos)
{
	struct sock *sk = udp_get_first(seq);

	if (sk)
		while(pos && (sk = udp_get_next(seq, sk)) != NULL)
			--pos;
	return pos ? NULL : sk;
}

static void *udp_seq_start(struct seq_file *seq, loff_t *pos)
{
	read_lock(&udp_hash_lock);
	return *pos ? udp_get_idx(seq, *pos-1) : (void *)1;
}

static void *udp_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct sock *sk;

	if (v == (void *)1)
		sk = udp_get_idx(seq, 0);
	else
		sk = udp_get_next(seq, v);

	++*pos;
	return sk;
}

static void udp_seq_stop(struct seq_file *seq, void *v)
{
	read_unlock(&udp_hash_lock);
}

static int udp_seq_open(struct inode *inode, struct file *file)
{
	struct udp_seq_afinfo *afinfo = PDE(inode)->data;
	struct seq_file *seq;
	int rc = -ENOMEM;
	struct udp_iter_state *s = kmalloc(sizeof(*s), GFP_KERNEL);

	if (!s)
		goto out;
	memset(s, 0, sizeof(*s));
	s->family		= afinfo->family;
	s->seq_ops.start	= udp_seq_start;
	s->seq_ops.next		= udp_seq_next;
	s->seq_ops.show		= afinfo->seq_show;
	s->seq_ops.stop		= udp_seq_stop;

	rc = seq_open(file, &s->seq_ops);
	if (rc)
		goto out_kfree;

	seq	     = file->private_data;
	seq->private = s;
out:
	return rc;
out_kfree:
	kfree(s);
	goto out;
}

/* ------------------------------------------------------------------------ */
int udp_proc_register(struct udp_seq_afinfo *afinfo)
{
	struct proc_dir_entry *p;
	int rc = 0;

	if (!afinfo)
		return -EINVAL;
	afinfo->seq_fops->owner		= afinfo->owner;
	afinfo->seq_fops->open		= udp_seq_open;
	afinfo->seq_fops->read		= seq_read;
	afinfo->seq_fops->llseek	= seq_lseek;
	afinfo->seq_fops->release	= seq_release_private;

	p = proc_net_fops_create(afinfo->name, S_IRUGO, afinfo->seq_fops);
	if (p)
		p->data = afinfo;
	else
		rc = -ENOMEM;
	return rc;
}

void udp_proc_unregister(struct udp_seq_afinfo *afinfo)
{
	if (!afinfo)
		return;
	proc_net_remove(afinfo->name);
	memset(afinfo->seq_fops, 0, sizeof(*afinfo->seq_fops));
}

/* ------------------------------------------------------------------------ */
static void udp4_format_sock(struct sock *sp, char *tmpbuf, int bucket)
{
	struct inet_sock *inet = inet_sk(sp);
	unsigned int dest = inet->daddr;
	unsigned int src  = inet->rcv_saddr;
	__u16 destp	  = ntohs(inet->dport);
	__u16 srcp	  = ntohs(inet->sport);

	sprintf(tmpbuf, "%4d: %08X:%04X %08X:%04X"
		" %02X %08X:%08X %02X:%08lX %08X %5d %8d %lu %d %p",
		bucket, src, srcp, dest, destp, sp->sk_state, 
		atomic_read(&sp->sk_wmem_alloc),
		atomic_read(&sp->sk_rmem_alloc),
		0, 0L, 0, sock_i_uid(sp), 0, sock_i_ino(sp),
		atomic_read(&sp->sk_refcnt), sp);
}

static int udp4_seq_show(struct seq_file *seq, void *v)
{
	if (v == SEQ_START_TOKEN)
		seq_printf(seq, "%-127s\n",
			   "  sl  local_address rem_address   st tx_queue "
			   "rx_queue tr tm->when retrnsmt   uid  timeout "
			   "inode");
	else {
		char tmpbuf[129];
		struct udp_iter_state *state = seq->private;

		udp4_format_sock(v, tmpbuf, state->bucket);
		seq_printf(seq, "%-127s\n", tmpbuf);
	}
	return 0;
}

/* ------------------------------------------------------------------------ */
static struct file_operations udp4_seq_fops;
static struct udp_seq_afinfo udp4_seq_afinfo = {
	.owner		= THIS_MODULE,
	.name		= "udp",
	.family		= AF_INET,
	.seq_show	= udp4_seq_show,
	.seq_fops	= &udp4_seq_fops,
};

int __init udp4_proc_init(void)
{
	return udp_proc_register(&udp4_seq_afinfo);
}

void udp4_proc_exit(void)
{
	udp_proc_unregister(&udp4_seq_afinfo);
}
#endif /* CONFIG_PROC_FS */

EXPORT_SYMBOL(udp_disconnect);
EXPORT_SYMBOL(udp_hash);
EXPORT_SYMBOL(udp_hash_lock);
EXPORT_SYMBOL(udp_ioctl);
EXPORT_SYMBOL(udp_port_rover);
EXPORT_SYMBOL(udp_prot);
EXPORT_SYMBOL(udp_sendmsg);
EXPORT_SYMBOL(udp_poll);

#ifdef CONFIG_PROC_FS
EXPORT_SYMBOL(udp_proc_register);
EXPORT_SYMBOL(udp_proc_unregister);
#endif
