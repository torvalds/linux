/* SCTP kernel implementation
 * (C) Copyright IBM Corp. 2001, 2004
 * Copyright (c) 1999-2000 Cisco, Inc.
 * Copyright (c) 1999-2001 Motorola, Inc.
 * Copyright (c) 2001 Intel Corp.
 * Copyright (c) 2001 Nokia, Inc.
 * Copyright (c) 2001 La Monte H.P. Yarroll
 *
 * This file is part of the SCTP kernel implementation
 *
 * Initialization/cleanup for SCTP protocol support.
 *
 * This SCTP implementation is free software;
 * you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This SCTP implementation is distributed in the hope that it
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 *                 ************************
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU CC; see the file COPYING.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * Please send any bug reports or fixes you make to the
 * email address(es):
 *    lksctp developers <linux-sctp@vger.kernel.org>
 *
 * Written or modified by:
 *    La Monte H.P. Yarroll <piggy@acm.org>
 *    Karl Knutson <karl@athena.chicago.il.us>
 *    Jon Grimm <jgrimm@us.ibm.com>
 *    Sridhar Samudrala <sri@us.ibm.com>
 *    Daisy Chang <daisyc@us.ibm.com>
 *    Ardelle Fan <ardelle.fan@intel.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/seq_file.h>
#include <linux/bootmem.h>
#include <linux/highmem.h>
#include <linux/swap.h>
#include <linux/slab.h>
#include <net/net_namespace.h>
#include <net/protocol.h>
#include <net/ip.h>
#include <net/ipv6.h>
#include <net/route.h>
#include <net/sctp/sctp.h>
#include <net/addrconf.h>
#include <net/inet_common.h>
#include <net/inet_ecn.h>

#define MAX_SCTP_PORT_HASH_ENTRIES (64 * 1024)

/* Global data structures. */
struct sctp_globals sctp_globals __read_mostly;

struct idr sctp_assocs_id;
DEFINE_SPINLOCK(sctp_assocs_id_lock);

static struct sctp_pf *sctp_pf_inet6_specific;
static struct sctp_pf *sctp_pf_inet_specific;
static struct sctp_af *sctp_af_v4_specific;
static struct sctp_af *sctp_af_v6_specific;

struct kmem_cache *sctp_chunk_cachep __read_mostly;
struct kmem_cache *sctp_bucket_cachep __read_mostly;

long sysctl_sctp_mem[3];
int sysctl_sctp_rmem[3];
int sysctl_sctp_wmem[3];

/* Private helper to extract ipv4 address and stash them in
 * the protocol structure.
 */
static void sctp_v4_copy_addrlist(struct list_head *addrlist,
				  struct net_device *dev)
{
	struct in_device *in_dev;
	struct in_ifaddr *ifa;
	struct sctp_sockaddr_entry *addr;

	rcu_read_lock();
	if ((in_dev = __in_dev_get_rcu(dev)) == NULL) {
		rcu_read_unlock();
		return;
	}

	for (ifa = in_dev->ifa_list; ifa; ifa = ifa->ifa_next) {
		/* Add the address to the local list.  */
		addr = kzalloc(sizeof(*addr), GFP_ATOMIC);
		if (addr) {
			addr->a.v4.sin_family = AF_INET;
			addr->a.v4.sin_port = 0;
			addr->a.v4.sin_addr.s_addr = ifa->ifa_local;
			addr->valid = 1;
			INIT_LIST_HEAD(&addr->list);
			list_add_tail(&addr->list, addrlist);
		}
	}

	rcu_read_unlock();
}

/* Extract our IP addresses from the system and stash them in the
 * protocol structure.
 */
static void sctp_get_local_addr_list(struct net *net)
{
	struct net_device *dev;
	struct list_head *pos;
	struct sctp_af *af;

	rcu_read_lock();
	for_each_netdev_rcu(net, dev) {
		list_for_each(pos, &sctp_address_families) {
			af = list_entry(pos, struct sctp_af, list);
			af->copy_addrlist(&net->sctp.local_addr_list, dev);
		}
	}
	rcu_read_unlock();
}

/* Free the existing local addresses.  */
static void sctp_free_local_addr_list(struct net *net)
{
	struct sctp_sockaddr_entry *addr;
	struct list_head *pos, *temp;

	list_for_each_safe(pos, temp, &net->sctp.local_addr_list) {
		addr = list_entry(pos, struct sctp_sockaddr_entry, list);
		list_del(pos);
		kfree(addr);
	}
}

/* Copy the local addresses which are valid for 'scope' into 'bp'.  */
int sctp_copy_local_addr_list(struct net *net, struct sctp_bind_addr *bp,
			      enum sctp_scope scope, gfp_t gfp, int copy_flags)
{
	struct sctp_sockaddr_entry *addr;
	union sctp_addr laddr;
	int error = 0;

	rcu_read_lock();
	list_for_each_entry_rcu(addr, &net->sctp.local_addr_list, list) {
		if (!addr->valid)
			continue;
		if (!sctp_in_scope(net, &addr->a, scope))
			continue;

		/* Now that the address is in scope, check to see if
		 * the address type is really supported by the local
		 * sock as well as the remote peer.
		 */
		if (addr->a.sa.sa_family == AF_INET &&
		    !(copy_flags & SCTP_ADDR4_PEERSUPP))
			continue;
		if (addr->a.sa.sa_family == AF_INET6 &&
		    (!(copy_flags & SCTP_ADDR6_ALLOWED) ||
		     !(copy_flags & SCTP_ADDR6_PEERSUPP)))
			continue;

		laddr = addr->a;
		/* also works for setting ipv6 address port */
		laddr.v4.sin_port = htons(bp->port);
		if (sctp_bind_addr_state(bp, &laddr) != -1)
			continue;

		error = sctp_add_bind_addr(bp, &addr->a, sizeof(addr->a),
					   SCTP_ADDR_SRC, GFP_ATOMIC);
		if (error)
			break;
	}

	rcu_read_unlock();
	return error;
}

/* Copy over any ip options */
static void sctp_v4_copy_ip_options(struct sock *sk, struct sock *newsk)
{
	struct inet_sock *newinet, *inet = inet_sk(sk);
	struct ip_options_rcu *inet_opt, *newopt = NULL;

	newinet = inet_sk(newsk);

	rcu_read_lock();
	inet_opt = rcu_dereference(inet->inet_opt);
	if (inet_opt) {
		newopt = sock_kmalloc(newsk, sizeof(*inet_opt) +
				      inet_opt->opt.optlen, GFP_ATOMIC);
		if (newopt)
			memcpy(newopt, inet_opt, sizeof(*inet_opt) +
			       inet_opt->opt.optlen);
		else
			pr_err("%s: Failed to copy ip options\n", __func__);
	}
	RCU_INIT_POINTER(newinet->inet_opt, newopt);
	rcu_read_unlock();
}

/* Account for the IP options */
static int sctp_v4_ip_options_len(struct sock *sk)
{
	struct inet_sock *inet = inet_sk(sk);
	struct ip_options_rcu *inet_opt;
	int len = 0;

	rcu_read_lock();
	inet_opt = rcu_dereference(inet->inet_opt);
	if (inet_opt)
		len = inet_opt->opt.optlen;

	rcu_read_unlock();
	return len;
}

/* Initialize a sctp_addr from in incoming skb.  */
static void sctp_v4_from_skb(union sctp_addr *addr, struct sk_buff *skb,
			     int is_saddr)
{
	/* Always called on head skb, so this is safe */
	struct sctphdr *sh = sctp_hdr(skb);
	struct sockaddr_in *sa = &addr->v4;

	addr->v4.sin_family = AF_INET;

	if (is_saddr) {
		sa->sin_port = sh->source;
		sa->sin_addr.s_addr = ip_hdr(skb)->saddr;
	} else {
		sa->sin_port = sh->dest;
		sa->sin_addr.s_addr = ip_hdr(skb)->daddr;
	}
}

/* Initialize an sctp_addr from a socket. */
static void sctp_v4_from_sk(union sctp_addr *addr, struct sock *sk)
{
	addr->v4.sin_family = AF_INET;
	addr->v4.sin_port = 0;
	addr->v4.sin_addr.s_addr = inet_sk(sk)->inet_rcv_saddr;
}

/* Initialize sk->sk_rcv_saddr from sctp_addr. */
static void sctp_v4_to_sk_saddr(union sctp_addr *addr, struct sock *sk)
{
	inet_sk(sk)->inet_rcv_saddr = addr->v4.sin_addr.s_addr;
}

/* Initialize sk->sk_daddr from sctp_addr. */
static void sctp_v4_to_sk_daddr(union sctp_addr *addr, struct sock *sk)
{
	inet_sk(sk)->inet_daddr = addr->v4.sin_addr.s_addr;
}

/* Initialize a sctp_addr from an address parameter. */
static void sctp_v4_from_addr_param(union sctp_addr *addr,
				    union sctp_addr_param *param,
				    __be16 port, int iif)
{
	addr->v4.sin_family = AF_INET;
	addr->v4.sin_port = port;
	addr->v4.sin_addr.s_addr = param->v4.addr.s_addr;
}

/* Initialize an address parameter from a sctp_addr and return the length
 * of the address parameter.
 */
static int sctp_v4_to_addr_param(const union sctp_addr *addr,
				 union sctp_addr_param *param)
{
	int length = sizeof(struct sctp_ipv4addr_param);

	param->v4.param_hdr.type = SCTP_PARAM_IPV4_ADDRESS;
	param->v4.param_hdr.length = htons(length);
	param->v4.addr.s_addr = addr->v4.sin_addr.s_addr;

	return length;
}

/* Initialize a sctp_addr from a dst_entry. */
static void sctp_v4_dst_saddr(union sctp_addr *saddr, struct flowi4 *fl4,
			      __be16 port)
{
	saddr->v4.sin_family = AF_INET;
	saddr->v4.sin_port = port;
	saddr->v4.sin_addr.s_addr = fl4->saddr;
}

/* Compare two addresses exactly. */
static int sctp_v4_cmp_addr(const union sctp_addr *addr1,
			    const union sctp_addr *addr2)
{
	if (addr1->sa.sa_family != addr2->sa.sa_family)
		return 0;
	if (addr1->v4.sin_port != addr2->v4.sin_port)
		return 0;
	if (addr1->v4.sin_addr.s_addr != addr2->v4.sin_addr.s_addr)
		return 0;

	return 1;
}

/* Initialize addr struct to INADDR_ANY. */
static void sctp_v4_inaddr_any(union sctp_addr *addr, __be16 port)
{
	addr->v4.sin_family = AF_INET;
	addr->v4.sin_addr.s_addr = htonl(INADDR_ANY);
	addr->v4.sin_port = port;
}

/* Is this a wildcard address? */
static int sctp_v4_is_any(const union sctp_addr *addr)
{
	return htonl(INADDR_ANY) == addr->v4.sin_addr.s_addr;
}

/* This function checks if the address is a valid address to be used for
 * SCTP binding.
 *
 * Output:
 * Return 0 - If the address is a non-unicast or an illegal address.
 * Return 1 - If the address is a unicast.
 */
static int sctp_v4_addr_valid(union sctp_addr *addr,
			      struct sctp_sock *sp,
			      const struct sk_buff *skb)
{
	/* IPv4 addresses not allowed */
	if (sp && ipv6_only_sock(sctp_opt2sk(sp)))
		return 0;

	/* Is this a non-unicast address or a unusable SCTP address? */
	if (IS_IPV4_UNUSABLE_ADDRESS(addr->v4.sin_addr.s_addr))
		return 0;

	/* Is this a broadcast address? */
	if (skb && skb_rtable(skb)->rt_flags & RTCF_BROADCAST)
		return 0;

	return 1;
}

/* Should this be available for binding?   */
static int sctp_v4_available(union sctp_addr *addr, struct sctp_sock *sp)
{
	struct net *net = sock_net(&sp->inet.sk);
	int ret = inet_addr_type(net, addr->v4.sin_addr.s_addr);


	if (addr->v4.sin_addr.s_addr != htonl(INADDR_ANY) &&
	   ret != RTN_LOCAL &&
	   !sp->inet.freebind &&
	   !net->ipv4.sysctl_ip_nonlocal_bind)
		return 0;

	if (ipv6_only_sock(sctp_opt2sk(sp)))
		return 0;

	return 1;
}

/* Checking the loopback, private and other address scopes as defined in
 * RFC 1918.   The IPv4 scoping is based on the draft for SCTP IPv4
 * scoping <draft-stewart-tsvwg-sctp-ipv4-00.txt>.
 *
 * Level 0 - unusable SCTP addresses
 * Level 1 - loopback address
 * Level 2 - link-local addresses
 * Level 3 - private addresses.
 * Level 4 - global addresses
 * For INIT and INIT-ACK address list, let L be the level of
 * of requested destination address, sender and receiver
 * SHOULD include all of its addresses with level greater
 * than or equal to L.
 *
 * IPv4 scoping can be controlled through sysctl option
 * net.sctp.addr_scope_policy
 */
static enum sctp_scope sctp_v4_scope(union sctp_addr *addr)
{
	enum sctp_scope retval;

	/* Check for unusable SCTP addresses. */
	if (IS_IPV4_UNUSABLE_ADDRESS(addr->v4.sin_addr.s_addr)) {
		retval =  SCTP_SCOPE_UNUSABLE;
	} else if (ipv4_is_loopback(addr->v4.sin_addr.s_addr)) {
		retval = SCTP_SCOPE_LOOPBACK;
	} else if (ipv4_is_linklocal_169(addr->v4.sin_addr.s_addr)) {
		retval = SCTP_SCOPE_LINK;
	} else if (ipv4_is_private_10(addr->v4.sin_addr.s_addr) ||
		   ipv4_is_private_172(addr->v4.sin_addr.s_addr) ||
		   ipv4_is_private_192(addr->v4.sin_addr.s_addr)) {
		retval = SCTP_SCOPE_PRIVATE;
	} else {
		retval = SCTP_SCOPE_GLOBAL;
	}

	return retval;
}

/* Returns a valid dst cache entry for the given source and destination ip
 * addresses. If an association is passed, trys to get a dst entry with a
 * source address that matches an address in the bind address list.
 */
static void sctp_v4_get_dst(struct sctp_transport *t, union sctp_addr *saddr,
				struct flowi *fl, struct sock *sk)
{
	struct sctp_association *asoc = t->asoc;
	struct rtable *rt;
	struct flowi4 *fl4 = &fl->u.ip4;
	struct sctp_bind_addr *bp;
	struct sctp_sockaddr_entry *laddr;
	struct dst_entry *dst = NULL;
	union sctp_addr *daddr = &t->ipaddr;
	union sctp_addr dst_saddr;

	memset(fl4, 0x0, sizeof(struct flowi4));
	fl4->daddr  = daddr->v4.sin_addr.s_addr;
	fl4->fl4_dport = daddr->v4.sin_port;
	fl4->flowi4_proto = IPPROTO_SCTP;
	if (asoc) {
		fl4->flowi4_tos = RT_CONN_FLAGS(asoc->base.sk);
		fl4->flowi4_oif = asoc->base.sk->sk_bound_dev_if;
		fl4->fl4_sport = htons(asoc->base.bind_addr.port);
	}
	if (saddr) {
		fl4->saddr = saddr->v4.sin_addr.s_addr;
		fl4->fl4_sport = saddr->v4.sin_port;
	}

	pr_debug("%s: dst:%pI4, src:%pI4 - ", __func__, &fl4->daddr,
		 &fl4->saddr);

	rt = ip_route_output_key(sock_net(sk), fl4);
	if (!IS_ERR(rt))
		dst = &rt->dst;

	/* If there is no association or if a source address is passed, no
	 * more validation is required.
	 */
	if (!asoc || saddr)
		goto out;

	bp = &asoc->base.bind_addr;

	if (dst) {
		/* Walk through the bind address list and look for a bind
		 * address that matches the source address of the returned dst.
		 */
		sctp_v4_dst_saddr(&dst_saddr, fl4, htons(bp->port));
		rcu_read_lock();
		list_for_each_entry_rcu(laddr, &bp->address_list, list) {
			if (!laddr->valid || (laddr->state == SCTP_ADDR_DEL) ||
			    (laddr->state != SCTP_ADDR_SRC &&
			    !asoc->src_out_of_asoc_ok))
				continue;
			if (sctp_v4_cmp_addr(&dst_saddr, &laddr->a))
				goto out_unlock;
		}
		rcu_read_unlock();

		/* None of the bound addresses match the source address of the
		 * dst. So release it.
		 */
		dst_release(dst);
		dst = NULL;
	}

	/* Walk through the bind address list and try to get a dst that
	 * matches a bind address as the source address.
	 */
	rcu_read_lock();
	list_for_each_entry_rcu(laddr, &bp->address_list, list) {
		struct net_device *odev;

		if (!laddr->valid)
			continue;
		if (laddr->state != SCTP_ADDR_SRC ||
		    AF_INET != laddr->a.sa.sa_family)
			continue;

		fl4->fl4_sport = laddr->a.v4.sin_port;
		flowi4_update_output(fl4,
				     asoc->base.sk->sk_bound_dev_if,
				     RT_CONN_FLAGS(asoc->base.sk),
				     daddr->v4.sin_addr.s_addr,
				     laddr->a.v4.sin_addr.s_addr);

		rt = ip_route_output_key(sock_net(sk), fl4);
		if (IS_ERR(rt))
			continue;

		/* Ensure the src address belongs to the output
		 * interface.
		 */
		odev = __ip_dev_find(sock_net(sk), laddr->a.v4.sin_addr.s_addr,
				     false);
		if (!odev || odev->ifindex != fl4->flowi4_oif) {
			if (!dst)
				dst = &rt->dst;
			else
				dst_release(&rt->dst);
			continue;
		}

		dst_release(dst);
		dst = &rt->dst;
		break;
	}

out_unlock:
	rcu_read_unlock();
out:
	t->dst = dst;
	if (dst)
		pr_debug("rt_dst:%pI4, rt_src:%pI4\n",
			 &fl4->daddr, &fl4->saddr);
	else
		pr_debug("no route\n");
}

/* For v4, the source address is cached in the route entry(dst). So no need
 * to cache it separately and hence this is an empty routine.
 */
static void sctp_v4_get_saddr(struct sctp_sock *sk,
			      struct sctp_transport *t,
			      struct flowi *fl)
{
	union sctp_addr *saddr = &t->saddr;
	struct rtable *rt = (struct rtable *)t->dst;

	if (rt) {
		saddr->v4.sin_family = AF_INET;
		saddr->v4.sin_addr.s_addr = fl->u.ip4.saddr;
	}
}

/* What interface did this skb arrive on? */
static int sctp_v4_skb_iif(const struct sk_buff *skb)
{
	return inet_iif(skb);
}

/* Was this packet marked by Explicit Congestion Notification? */
static int sctp_v4_is_ce(const struct sk_buff *skb)
{
	return INET_ECN_is_ce(ip_hdr(skb)->tos);
}

/* Create and initialize a new sk for the socket returned by accept(). */
static struct sock *sctp_v4_create_accept_sk(struct sock *sk,
					     struct sctp_association *asoc,
					     bool kern)
{
	struct sock *newsk = sk_alloc(sock_net(sk), PF_INET, GFP_KERNEL,
			sk->sk_prot, kern);
	struct inet_sock *newinet;

	if (!newsk)
		goto out;

	sock_init_data(NULL, newsk);

	sctp_copy_sock(newsk, sk, asoc);
	sock_reset_flag(newsk, SOCK_ZAPPED);

	sctp_v4_copy_ip_options(sk, newsk);

	newinet = inet_sk(newsk);

	newinet->inet_daddr = asoc->peer.primary_addr.v4.sin_addr.s_addr;

	sk_refcnt_debug_inc(newsk);

	if (newsk->sk_prot->init(newsk)) {
		sk_common_release(newsk);
		newsk = NULL;
	}

out:
	return newsk;
}

static int sctp_v4_addr_to_user(struct sctp_sock *sp, union sctp_addr *addr)
{
	/* No address mapping for V4 sockets */
	return sizeof(struct sockaddr_in);
}

/* Dump the v4 addr to the seq file. */
static void sctp_v4_seq_dump_addr(struct seq_file *seq, union sctp_addr *addr)
{
	seq_printf(seq, "%pI4 ", &addr->v4.sin_addr);
}

static void sctp_v4_ecn_capable(struct sock *sk)
{
	INET_ECN_xmit(sk);
}

static void sctp_addr_wq_timeout_handler(struct timer_list *t)
{
	struct net *net = from_timer(net, t, sctp.addr_wq_timer);
	struct sctp_sockaddr_entry *addrw, *temp;
	struct sctp_sock *sp;

	spin_lock_bh(&net->sctp.addr_wq_lock);

	list_for_each_entry_safe(addrw, temp, &net->sctp.addr_waitq, list) {
		pr_debug("%s: the first ent in wq:%p is addr:%pISc for cmd:%d at "
			 "entry:%p\n", __func__, &net->sctp.addr_waitq, &addrw->a.sa,
			 addrw->state, addrw);

#if IS_ENABLED(CONFIG_IPV6)
		/* Now we send an ASCONF for each association */
		/* Note. we currently don't handle link local IPv6 addressees */
		if (addrw->a.sa.sa_family == AF_INET6) {
			struct in6_addr *in6;

			if (ipv6_addr_type(&addrw->a.v6.sin6_addr) &
			    IPV6_ADDR_LINKLOCAL)
				goto free_next;

			in6 = (struct in6_addr *)&addrw->a.v6.sin6_addr;
			if (ipv6_chk_addr(net, in6, NULL, 0) == 0 &&
			    addrw->state == SCTP_ADDR_NEW) {
				unsigned long timeo_val;

				pr_debug("%s: this is on DAD, trying %d sec "
					 "later\n", __func__,
					 SCTP_ADDRESS_TICK_DELAY);

				timeo_val = jiffies;
				timeo_val += msecs_to_jiffies(SCTP_ADDRESS_TICK_DELAY);
				mod_timer(&net->sctp.addr_wq_timer, timeo_val);
				break;
			}
		}
#endif
		list_for_each_entry(sp, &net->sctp.auto_asconf_splist, auto_asconf_list) {
			struct sock *sk;

			sk = sctp_opt2sk(sp);
			/* ignore bound-specific endpoints */
			if (!sctp_is_ep_boundall(sk))
				continue;
			bh_lock_sock(sk);
			if (sctp_asconf_mgmt(sp, addrw) < 0)
				pr_debug("%s: sctp_asconf_mgmt failed\n", __func__);
			bh_unlock_sock(sk);
		}
#if IS_ENABLED(CONFIG_IPV6)
free_next:
#endif
		list_del(&addrw->list);
		kfree(addrw);
	}
	spin_unlock_bh(&net->sctp.addr_wq_lock);
}

static void sctp_free_addr_wq(struct net *net)
{
	struct sctp_sockaddr_entry *addrw;
	struct sctp_sockaddr_entry *temp;

	spin_lock_bh(&net->sctp.addr_wq_lock);
	del_timer(&net->sctp.addr_wq_timer);
	list_for_each_entry_safe(addrw, temp, &net->sctp.addr_waitq, list) {
		list_del(&addrw->list);
		kfree(addrw);
	}
	spin_unlock_bh(&net->sctp.addr_wq_lock);
}

/* lookup the entry for the same address in the addr_waitq
 * sctp_addr_wq MUST be locked
 */
static struct sctp_sockaddr_entry *sctp_addr_wq_lookup(struct net *net,
					struct sctp_sockaddr_entry *addr)
{
	struct sctp_sockaddr_entry *addrw;

	list_for_each_entry(addrw, &net->sctp.addr_waitq, list) {
		if (addrw->a.sa.sa_family != addr->a.sa.sa_family)
			continue;
		if (addrw->a.sa.sa_family == AF_INET) {
			if (addrw->a.v4.sin_addr.s_addr ==
			    addr->a.v4.sin_addr.s_addr)
				return addrw;
		} else if (addrw->a.sa.sa_family == AF_INET6) {
			if (ipv6_addr_equal(&addrw->a.v6.sin6_addr,
			    &addr->a.v6.sin6_addr))
				return addrw;
		}
	}
	return NULL;
}

void sctp_addr_wq_mgmt(struct net *net, struct sctp_sockaddr_entry *addr, int cmd)
{
	struct sctp_sockaddr_entry *addrw;
	unsigned long timeo_val;

	/* first, we check if an opposite message already exist in the queue.
	 * If we found such message, it is removed.
	 * This operation is a bit stupid, but the DHCP client attaches the
	 * new address after a couple of addition and deletion of that address
	 */

	spin_lock_bh(&net->sctp.addr_wq_lock);
	/* Offsets existing events in addr_wq */
	addrw = sctp_addr_wq_lookup(net, addr);
	if (addrw) {
		if (addrw->state != cmd) {
			pr_debug("%s: offsets existing entry for %d, addr:%pISc "
				 "in wq:%p\n", __func__, addrw->state, &addrw->a.sa,
				 &net->sctp.addr_waitq);

			list_del(&addrw->list);
			kfree(addrw);
		}
		spin_unlock_bh(&net->sctp.addr_wq_lock);
		return;
	}

	/* OK, we have to add the new address to the wait queue */
	addrw = kmemdup(addr, sizeof(struct sctp_sockaddr_entry), GFP_ATOMIC);
	if (addrw == NULL) {
		spin_unlock_bh(&net->sctp.addr_wq_lock);
		return;
	}
	addrw->state = cmd;
	list_add_tail(&addrw->list, &net->sctp.addr_waitq);

	pr_debug("%s: add new entry for cmd:%d, addr:%pISc in wq:%p\n",
		 __func__, addrw->state, &addrw->a.sa, &net->sctp.addr_waitq);

	if (!timer_pending(&net->sctp.addr_wq_timer)) {
		timeo_val = jiffies;
		timeo_val += msecs_to_jiffies(SCTP_ADDRESS_TICK_DELAY);
		mod_timer(&net->sctp.addr_wq_timer, timeo_val);
	}
	spin_unlock_bh(&net->sctp.addr_wq_lock);
}

/* Event handler for inet address addition/deletion events.
 * The sctp_local_addr_list needs to be protocted by a spin lock since
 * multiple notifiers (say IPv4 and IPv6) may be running at the same
 * time and thus corrupt the list.
 * The reader side is protected with RCU.
 */
static int sctp_inetaddr_event(struct notifier_block *this, unsigned long ev,
			       void *ptr)
{
	struct in_ifaddr *ifa = (struct in_ifaddr *)ptr;
	struct sctp_sockaddr_entry *addr = NULL;
	struct sctp_sockaddr_entry *temp;
	struct net *net = dev_net(ifa->ifa_dev->dev);
	int found = 0;

	switch (ev) {
	case NETDEV_UP:
		addr = kmalloc(sizeof(struct sctp_sockaddr_entry), GFP_ATOMIC);
		if (addr) {
			addr->a.v4.sin_family = AF_INET;
			addr->a.v4.sin_port = 0;
			addr->a.v4.sin_addr.s_addr = ifa->ifa_local;
			addr->valid = 1;
			spin_lock_bh(&net->sctp.local_addr_lock);
			list_add_tail_rcu(&addr->list, &net->sctp.local_addr_list);
			sctp_addr_wq_mgmt(net, addr, SCTP_ADDR_NEW);
			spin_unlock_bh(&net->sctp.local_addr_lock);
		}
		break;
	case NETDEV_DOWN:
		spin_lock_bh(&net->sctp.local_addr_lock);
		list_for_each_entry_safe(addr, temp,
					&net->sctp.local_addr_list, list) {
			if (addr->a.sa.sa_family == AF_INET &&
					addr->a.v4.sin_addr.s_addr ==
					ifa->ifa_local) {
				sctp_addr_wq_mgmt(net, addr, SCTP_ADDR_DEL);
				found = 1;
				addr->valid = 0;
				list_del_rcu(&addr->list);
				break;
			}
		}
		spin_unlock_bh(&net->sctp.local_addr_lock);
		if (found)
			kfree_rcu(addr, rcu);
		break;
	}

	return NOTIFY_DONE;
}

/*
 * Initialize the control inode/socket with a control endpoint data
 * structure.  This endpoint is reserved exclusively for the OOTB processing.
 */
static int sctp_ctl_sock_init(struct net *net)
{
	int err;
	sa_family_t family = PF_INET;

	if (sctp_get_pf_specific(PF_INET6))
		family = PF_INET6;

	err = inet_ctl_sock_create(&net->sctp.ctl_sock, family,
				   SOCK_SEQPACKET, IPPROTO_SCTP, net);

	/* If IPv6 socket could not be created, try the IPv4 socket */
	if (err < 0 && family == PF_INET6)
		err = inet_ctl_sock_create(&net->sctp.ctl_sock, AF_INET,
					   SOCK_SEQPACKET, IPPROTO_SCTP,
					   net);

	if (err < 0) {
		pr_err("Failed to create the SCTP control socket\n");
		return err;
	}
	return 0;
}

/* Register address family specific functions. */
int sctp_register_af(struct sctp_af *af)
{
	switch (af->sa_family) {
	case AF_INET:
		if (sctp_af_v4_specific)
			return 0;
		sctp_af_v4_specific = af;
		break;
	case AF_INET6:
		if (sctp_af_v6_specific)
			return 0;
		sctp_af_v6_specific = af;
		break;
	default:
		return 0;
	}

	INIT_LIST_HEAD(&af->list);
	list_add_tail(&af->list, &sctp_address_families);
	return 1;
}

/* Get the table of functions for manipulating a particular address
 * family.
 */
struct sctp_af *sctp_get_af_specific(sa_family_t family)
{
	switch (family) {
	case AF_INET:
		return sctp_af_v4_specific;
	case AF_INET6:
		return sctp_af_v6_specific;
	default:
		return NULL;
	}
}

/* Common code to initialize a AF_INET msg_name. */
static void sctp_inet_msgname(char *msgname, int *addr_len)
{
	struct sockaddr_in *sin;

	sin = (struct sockaddr_in *)msgname;
	*addr_len = sizeof(struct sockaddr_in);
	sin->sin_family = AF_INET;
	memset(sin->sin_zero, 0, sizeof(sin->sin_zero));
}

/* Copy the primary address of the peer primary address as the msg_name. */
static void sctp_inet_event_msgname(struct sctp_ulpevent *event, char *msgname,
				    int *addr_len)
{
	struct sockaddr_in *sin, *sinfrom;

	if (msgname) {
		struct sctp_association *asoc;

		asoc = event->asoc;
		sctp_inet_msgname(msgname, addr_len);
		sin = (struct sockaddr_in *)msgname;
		sinfrom = &asoc->peer.primary_addr.v4;
		sin->sin_port = htons(asoc->peer.port);
		sin->sin_addr.s_addr = sinfrom->sin_addr.s_addr;
	}
}

/* Initialize and copy out a msgname from an inbound skb. */
static void sctp_inet_skb_msgname(struct sk_buff *skb, char *msgname, int *len)
{
	if (msgname) {
		struct sctphdr *sh = sctp_hdr(skb);
		struct sockaddr_in *sin = (struct sockaddr_in *)msgname;

		sctp_inet_msgname(msgname, len);
		sin->sin_port = sh->source;
		sin->sin_addr.s_addr = ip_hdr(skb)->saddr;
	}
}

/* Do we support this AF? */
static int sctp_inet_af_supported(sa_family_t family, struct sctp_sock *sp)
{
	/* PF_INET only supports AF_INET addresses. */
	return AF_INET == family;
}

/* Address matching with wildcards allowed. */
static int sctp_inet_cmp_addr(const union sctp_addr *addr1,
			      const union sctp_addr *addr2,
			      struct sctp_sock *opt)
{
	/* PF_INET only supports AF_INET addresses. */
	if (addr1->sa.sa_family != addr2->sa.sa_family)
		return 0;
	if (htonl(INADDR_ANY) == addr1->v4.sin_addr.s_addr ||
	    htonl(INADDR_ANY) == addr2->v4.sin_addr.s_addr)
		return 1;
	if (addr1->v4.sin_addr.s_addr == addr2->v4.sin_addr.s_addr)
		return 1;

	return 0;
}

/* Verify that provided sockaddr looks bindable.  Common verification has
 * already been taken care of.
 */
static int sctp_inet_bind_verify(struct sctp_sock *opt, union sctp_addr *addr)
{
	return sctp_v4_available(addr, opt);
}

/* Verify that sockaddr looks sendable.  Common verification has already
 * been taken care of.
 */
static int sctp_inet_send_verify(struct sctp_sock *opt, union sctp_addr *addr)
{
	return 1;
}

/* Fill in Supported Address Type information for INIT and INIT-ACK
 * chunks.  Returns number of addresses supported.
 */
static int sctp_inet_supported_addrs(const struct sctp_sock *opt,
				     __be16 *types)
{
	types[0] = SCTP_PARAM_IPV4_ADDRESS;
	return 1;
}

/* Wrapper routine that calls the ip transmit routine. */
static inline int sctp_v4_xmit(struct sk_buff *skb,
			       struct sctp_transport *transport)
{
	struct inet_sock *inet = inet_sk(skb->sk);

	pr_debug("%s: skb:%p, len:%d, src:%pI4, dst:%pI4\n", __func__, skb,
		 skb->len, &transport->fl.u.ip4.saddr, &transport->fl.u.ip4.daddr);

	inet->pmtudisc = transport->param_flags & SPP_PMTUD_ENABLE ?
			 IP_PMTUDISC_DO : IP_PMTUDISC_DONT;

	SCTP_INC_STATS(sock_net(&inet->sk), SCTP_MIB_OUTSCTPPACKS);

	return ip_queue_xmit(&inet->sk, skb, &transport->fl);
}

static struct sctp_af sctp_af_inet;

static struct sctp_pf sctp_pf_inet = {
	.event_msgname = sctp_inet_event_msgname,
	.skb_msgname   = sctp_inet_skb_msgname,
	.af_supported  = sctp_inet_af_supported,
	.cmp_addr      = sctp_inet_cmp_addr,
	.bind_verify   = sctp_inet_bind_verify,
	.send_verify   = sctp_inet_send_verify,
	.supported_addrs = sctp_inet_supported_addrs,
	.create_accept_sk = sctp_v4_create_accept_sk,
	.addr_to_user  = sctp_v4_addr_to_user,
	.to_sk_saddr   = sctp_v4_to_sk_saddr,
	.to_sk_daddr   = sctp_v4_to_sk_daddr,
	.copy_ip_options = sctp_v4_copy_ip_options,
	.af            = &sctp_af_inet
};

/* Notifier for inetaddr addition/deletion events.  */
static struct notifier_block sctp_inetaddr_notifier = {
	.notifier_call = sctp_inetaddr_event,
};

/* Socket operations.  */
static const struct proto_ops inet_seqpacket_ops = {
	.family		   = PF_INET,
	.owner		   = THIS_MODULE,
	.release	   = inet_release,	/* Needs to be wrapped... */
	.bind		   = inet_bind,
	.connect	   = sctp_inet_connect,
	.socketpair	   = sock_no_socketpair,
	.accept		   = inet_accept,
	.getname	   = inet_getname,	/* Semantics are different.  */
	.poll_mask	   = sctp_poll_mask,
	.ioctl		   = inet_ioctl,
	.listen		   = sctp_inet_listen,
	.shutdown	   = inet_shutdown,	/* Looks harmless.  */
	.setsockopt	   = sock_common_setsockopt, /* IP_SOL IP_OPTION is a problem */
	.getsockopt	   = sock_common_getsockopt,
	.sendmsg	   = inet_sendmsg,
	.recvmsg	   = inet_recvmsg,
	.mmap		   = sock_no_mmap,
	.sendpage	   = sock_no_sendpage,
#ifdef CONFIG_COMPAT
	.compat_setsockopt = compat_sock_common_setsockopt,
	.compat_getsockopt = compat_sock_common_getsockopt,
#endif
};

/* Registration with AF_INET family.  */
static struct inet_protosw sctp_seqpacket_protosw = {
	.type       = SOCK_SEQPACKET,
	.protocol   = IPPROTO_SCTP,
	.prot       = &sctp_prot,
	.ops        = &inet_seqpacket_ops,
	.flags      = SCTP_PROTOSW_FLAG
};
static struct inet_protosw sctp_stream_protosw = {
	.type       = SOCK_STREAM,
	.protocol   = IPPROTO_SCTP,
	.prot       = &sctp_prot,
	.ops        = &inet_seqpacket_ops,
	.flags      = SCTP_PROTOSW_FLAG
};

/* Register with IP layer.  */
static const struct net_protocol sctp_protocol = {
	.handler     = sctp_rcv,
	.err_handler = sctp_v4_err,
	.no_policy   = 1,
	.netns_ok    = 1,
	.icmp_strict_tag_validation = 1,
};

/* IPv4 address related functions.  */
static struct sctp_af sctp_af_inet = {
	.sa_family	   = AF_INET,
	.sctp_xmit	   = sctp_v4_xmit,
	.setsockopt	   = ip_setsockopt,
	.getsockopt	   = ip_getsockopt,
	.get_dst	   = sctp_v4_get_dst,
	.get_saddr	   = sctp_v4_get_saddr,
	.copy_addrlist	   = sctp_v4_copy_addrlist,
	.from_skb	   = sctp_v4_from_skb,
	.from_sk	   = sctp_v4_from_sk,
	.from_addr_param   = sctp_v4_from_addr_param,
	.to_addr_param	   = sctp_v4_to_addr_param,
	.cmp_addr	   = sctp_v4_cmp_addr,
	.addr_valid	   = sctp_v4_addr_valid,
	.inaddr_any	   = sctp_v4_inaddr_any,
	.is_any		   = sctp_v4_is_any,
	.available	   = sctp_v4_available,
	.scope		   = sctp_v4_scope,
	.skb_iif	   = sctp_v4_skb_iif,
	.is_ce		   = sctp_v4_is_ce,
	.seq_dump_addr	   = sctp_v4_seq_dump_addr,
	.ecn_capable	   = sctp_v4_ecn_capable,
	.net_header_len	   = sizeof(struct iphdr),
	.sockaddr_len	   = sizeof(struct sockaddr_in),
	.ip_options_len	   = sctp_v4_ip_options_len,
#ifdef CONFIG_COMPAT
	.compat_setsockopt = compat_ip_setsockopt,
	.compat_getsockopt = compat_ip_getsockopt,
#endif
};

struct sctp_pf *sctp_get_pf_specific(sa_family_t family)
{
	switch (family) {
	case PF_INET:
		return sctp_pf_inet_specific;
	case PF_INET6:
		return sctp_pf_inet6_specific;
	default:
		return NULL;
	}
}

/* Register the PF specific function table.  */
int sctp_register_pf(struct sctp_pf *pf, sa_family_t family)
{
	switch (family) {
	case PF_INET:
		if (sctp_pf_inet_specific)
			return 0;
		sctp_pf_inet_specific = pf;
		break;
	case PF_INET6:
		if (sctp_pf_inet6_specific)
			return 0;
		sctp_pf_inet6_specific = pf;
		break;
	default:
		return 0;
	}
	return 1;
}

static inline int init_sctp_mibs(struct net *net)
{
	net->sctp.sctp_statistics = alloc_percpu(struct sctp_mib);
	if (!net->sctp.sctp_statistics)
		return -ENOMEM;
	return 0;
}

static inline void cleanup_sctp_mibs(struct net *net)
{
	free_percpu(net->sctp.sctp_statistics);
}

static void sctp_v4_pf_init(void)
{
	/* Initialize the SCTP specific PF functions. */
	sctp_register_pf(&sctp_pf_inet, PF_INET);
	sctp_register_af(&sctp_af_inet);
}

static void sctp_v4_pf_exit(void)
{
	list_del(&sctp_af_inet.list);
}

static int sctp_v4_protosw_init(void)
{
	int rc;

	rc = proto_register(&sctp_prot, 1);
	if (rc)
		return rc;

	/* Register SCTP(UDP and TCP style) with socket layer.  */
	inet_register_protosw(&sctp_seqpacket_protosw);
	inet_register_protosw(&sctp_stream_protosw);

	return 0;
}

static void sctp_v4_protosw_exit(void)
{
	inet_unregister_protosw(&sctp_stream_protosw);
	inet_unregister_protosw(&sctp_seqpacket_protosw);
	proto_unregister(&sctp_prot);
}

static int sctp_v4_add_protocol(void)
{
	/* Register notifier for inet address additions/deletions. */
	register_inetaddr_notifier(&sctp_inetaddr_notifier);

	/* Register SCTP with inet layer.  */
	if (inet_add_protocol(&sctp_protocol, IPPROTO_SCTP) < 0)
		return -EAGAIN;

	return 0;
}

static void sctp_v4_del_protocol(void)
{
	inet_del_protocol(&sctp_protocol, IPPROTO_SCTP);
	unregister_inetaddr_notifier(&sctp_inetaddr_notifier);
}

static int __net_init sctp_defaults_init(struct net *net)
{
	int status;

	/*
	 * 14. Suggested SCTP Protocol Parameter Values
	 */
	/* The following protocol parameters are RECOMMENDED:  */
	/* RTO.Initial              - 3  seconds */
	net->sctp.rto_initial			= SCTP_RTO_INITIAL;
	/* RTO.Min                  - 1  second */
	net->sctp.rto_min	 		= SCTP_RTO_MIN;
	/* RTO.Max                 -  60 seconds */
	net->sctp.rto_max 			= SCTP_RTO_MAX;
	/* RTO.Alpha                - 1/8 */
	net->sctp.rto_alpha			= SCTP_RTO_ALPHA;
	/* RTO.Beta                 - 1/4 */
	net->sctp.rto_beta			= SCTP_RTO_BETA;

	/* Valid.Cookie.Life        - 60  seconds */
	net->sctp.valid_cookie_life		= SCTP_DEFAULT_COOKIE_LIFE;

	/* Whether Cookie Preservative is enabled(1) or not(0) */
	net->sctp.cookie_preserve_enable 	= 1;

	/* Default sctp sockets to use md5 as their hmac alg */
#if defined (CONFIG_SCTP_DEFAULT_COOKIE_HMAC_MD5)
	net->sctp.sctp_hmac_alg			= "md5";
#elif defined (CONFIG_SCTP_DEFAULT_COOKIE_HMAC_SHA1)
	net->sctp.sctp_hmac_alg			= "sha1";
#else
	net->sctp.sctp_hmac_alg			= NULL;
#endif

	/* Max.Burst		    - 4 */
	net->sctp.max_burst			= SCTP_DEFAULT_MAX_BURST;

	/* Enable pf state by default */
	net->sctp.pf_enable = 1;

	/* Association.Max.Retrans  - 10 attempts
	 * Path.Max.Retrans         - 5  attempts (per destination address)
	 * Max.Init.Retransmits     - 8  attempts
	 */
	net->sctp.max_retrans_association	= 10;
	net->sctp.max_retrans_path		= 5;
	net->sctp.max_retrans_init		= 8;

	/* Sendbuffer growth	    - do per-socket accounting */
	net->sctp.sndbuf_policy			= 0;

	/* Rcvbuffer growth	    - do per-socket accounting */
	net->sctp.rcvbuf_policy			= 0;

	/* HB.interval              - 30 seconds */
	net->sctp.hb_interval			= SCTP_DEFAULT_TIMEOUT_HEARTBEAT;

	/* delayed SACK timeout */
	net->sctp.sack_timeout			= SCTP_DEFAULT_TIMEOUT_SACK;

	/* Disable ADDIP by default. */
	net->sctp.addip_enable = 0;
	net->sctp.addip_noauth = 0;
	net->sctp.default_auto_asconf = 0;

	/* Enable PR-SCTP by default. */
	net->sctp.prsctp_enable = 1;

	/* Disable RECONF by default. */
	net->sctp.reconf_enable = 0;

	/* Disable AUTH by default. */
	net->sctp.auth_enable = 0;

	/* Set SCOPE policy to enabled */
	net->sctp.scope_policy = SCTP_SCOPE_POLICY_ENABLE;

	/* Set the default rwnd update threshold */
	net->sctp.rwnd_upd_shift = SCTP_DEFAULT_RWND_SHIFT;

	/* Initialize maximum autoclose timeout. */
	net->sctp.max_autoclose		= INT_MAX / HZ;

	status = sctp_sysctl_net_register(net);
	if (status)
		goto err_sysctl_register;

	/* Allocate and initialise sctp mibs.  */
	status = init_sctp_mibs(net);
	if (status)
		goto err_init_mibs;

#ifdef CONFIG_PROC_FS
	/* Initialize proc fs directory.  */
	status = sctp_proc_init(net);
	if (status)
		goto err_init_proc;
#endif

	sctp_dbg_objcnt_init(net);

	/* Initialize the local address list. */
	INIT_LIST_HEAD(&net->sctp.local_addr_list);
	spin_lock_init(&net->sctp.local_addr_lock);
	sctp_get_local_addr_list(net);

	/* Initialize the address event list */
	INIT_LIST_HEAD(&net->sctp.addr_waitq);
	INIT_LIST_HEAD(&net->sctp.auto_asconf_splist);
	spin_lock_init(&net->sctp.addr_wq_lock);
	net->sctp.addr_wq_timer.expires = 0;
	timer_setup(&net->sctp.addr_wq_timer, sctp_addr_wq_timeout_handler, 0);

	return 0;

#ifdef CONFIG_PROC_FS
err_init_proc:
	cleanup_sctp_mibs(net);
#endif
err_init_mibs:
	sctp_sysctl_net_unregister(net);
err_sysctl_register:
	return status;
}

static void __net_exit sctp_defaults_exit(struct net *net)
{
	/* Free the local address list */
	sctp_free_addr_wq(net);
	sctp_free_local_addr_list(net);

#ifdef CONFIG_PROC_FS
	remove_proc_subtree("sctp", net->proc_net);
	net->sctp.proc_net_sctp = NULL;
#endif
	cleanup_sctp_mibs(net);
	sctp_sysctl_net_unregister(net);
}

static struct pernet_operations sctp_defaults_ops = {
	.init = sctp_defaults_init,
	.exit = sctp_defaults_exit,
};

static int __net_init sctp_ctrlsock_init(struct net *net)
{
	int status;

	/* Initialize the control inode/socket for handling OOTB packets.  */
	status = sctp_ctl_sock_init(net);
	if (status)
		pr_err("Failed to initialize the SCTP control sock\n");

	return status;
}

static void __net_init sctp_ctrlsock_exit(struct net *net)
{
	/* Free the control endpoint.  */
	inet_ctl_sock_destroy(net->sctp.ctl_sock);
}

static struct pernet_operations sctp_ctrlsock_ops = {
	.init = sctp_ctrlsock_init,
	.exit = sctp_ctrlsock_exit,
};

/* Initialize the universe into something sensible.  */
static __init int sctp_init(void)
{
	int i;
	int status = -EINVAL;
	unsigned long goal;
	unsigned long limit;
	int max_share;
	int order;
	int num_entries;
	int max_entry_order;

	sock_skb_cb_check_size(sizeof(struct sctp_ulpevent));

	/* Allocate bind_bucket and chunk caches. */
	status = -ENOBUFS;
	sctp_bucket_cachep = kmem_cache_create("sctp_bind_bucket",
					       sizeof(struct sctp_bind_bucket),
					       0, SLAB_HWCACHE_ALIGN,
					       NULL);
	if (!sctp_bucket_cachep)
		goto out;

	sctp_chunk_cachep = kmem_cache_create("sctp_chunk",
					       sizeof(struct sctp_chunk),
					       0, SLAB_HWCACHE_ALIGN,
					       NULL);
	if (!sctp_chunk_cachep)
		goto err_chunk_cachep;

	status = percpu_counter_init(&sctp_sockets_allocated, 0, GFP_KERNEL);
	if (status)
		goto err_percpu_counter_init;

	/* Implementation specific variables. */

	/* Initialize default stream count setup information. */
	sctp_max_instreams    		= SCTP_DEFAULT_INSTREAMS;
	sctp_max_outstreams   		= SCTP_DEFAULT_OUTSTREAMS;

	/* Initialize handle used for association ids. */
	idr_init(&sctp_assocs_id);

	limit = nr_free_buffer_pages() / 8;
	limit = max(limit, 128UL);
	sysctl_sctp_mem[0] = limit / 4 * 3;
	sysctl_sctp_mem[1] = limit;
	sysctl_sctp_mem[2] = sysctl_sctp_mem[0] * 2;

	/* Set per-socket limits to no more than 1/128 the pressure threshold*/
	limit = (sysctl_sctp_mem[1]) << (PAGE_SHIFT - 7);
	max_share = min(4UL*1024*1024, limit);

	sysctl_sctp_rmem[0] = SK_MEM_QUANTUM; /* give each asoc 1 page min */
	sysctl_sctp_rmem[1] = 1500 * SKB_TRUESIZE(1);
	sysctl_sctp_rmem[2] = max(sysctl_sctp_rmem[1], max_share);

	sysctl_sctp_wmem[0] = SK_MEM_QUANTUM;
	sysctl_sctp_wmem[1] = 16*1024;
	sysctl_sctp_wmem[2] = max(64*1024, max_share);

	/* Size and allocate the association hash table.
	 * The methodology is similar to that of the tcp hash tables.
	 * Though not identical.  Start by getting a goal size
	 */
	if (totalram_pages >= (128 * 1024))
		goal = totalram_pages >> (22 - PAGE_SHIFT);
	else
		goal = totalram_pages >> (24 - PAGE_SHIFT);

	/* Then compute the page order for said goal */
	order = get_order(goal);

	/* Now compute the required page order for the maximum sized table we
	 * want to create
	 */
	max_entry_order = get_order(MAX_SCTP_PORT_HASH_ENTRIES *
				    sizeof(struct sctp_bind_hashbucket));

	/* Limit the page order by that maximum hash table size */
	order = min(order, max_entry_order);

	/* Allocate and initialize the endpoint hash table.  */
	sctp_ep_hashsize = 64;
	sctp_ep_hashtable =
		kmalloc_array(64, sizeof(struct sctp_hashbucket), GFP_KERNEL);
	if (!sctp_ep_hashtable) {
		pr_err("Failed endpoint_hash alloc\n");
		status = -ENOMEM;
		goto err_ehash_alloc;
	}
	for (i = 0; i < sctp_ep_hashsize; i++) {
		rwlock_init(&sctp_ep_hashtable[i].lock);
		INIT_HLIST_HEAD(&sctp_ep_hashtable[i].chain);
	}

	/* Allocate and initialize the SCTP port hash table.
	 * Note that order is initalized to start at the max sized
	 * table we want to support.  If we can't get that many pages
	 * reduce the order and try again
	 */
	do {
		sctp_port_hashtable = (struct sctp_bind_hashbucket *)
			__get_free_pages(GFP_KERNEL | __GFP_NOWARN, order);
	} while (!sctp_port_hashtable && --order > 0);

	if (!sctp_port_hashtable) {
		pr_err("Failed bind hash alloc\n");
		status = -ENOMEM;
		goto err_bhash_alloc;
	}

	/* Now compute the number of entries that will fit in the
	 * port hash space we allocated
	 */
	num_entries = (1UL << order) * PAGE_SIZE /
		      sizeof(struct sctp_bind_hashbucket);

	/* And finish by rounding it down to the nearest power of two
	 * this wastes some memory of course, but its needed because
	 * the hash function operates based on the assumption that
	 * that the number of entries is a power of two
	 */
	sctp_port_hashsize = rounddown_pow_of_two(num_entries);

	for (i = 0; i < sctp_port_hashsize; i++) {
		spin_lock_init(&sctp_port_hashtable[i].lock);
		INIT_HLIST_HEAD(&sctp_port_hashtable[i].chain);
	}

	status = sctp_transport_hashtable_init();
	if (status)
		goto err_thash_alloc;

	pr_info("Hash tables configured (bind %d/%d)\n", sctp_port_hashsize,
		num_entries);

	sctp_sysctl_register();

	INIT_LIST_HEAD(&sctp_address_families);
	sctp_v4_pf_init();
	sctp_v6_pf_init();
	sctp_sched_ops_init();

	status = register_pernet_subsys(&sctp_defaults_ops);
	if (status)
		goto err_register_defaults;

	status = sctp_v4_protosw_init();
	if (status)
		goto err_protosw_init;

	status = sctp_v6_protosw_init();
	if (status)
		goto err_v6_protosw_init;

	status = register_pernet_subsys(&sctp_ctrlsock_ops);
	if (status)
		goto err_register_ctrlsock;

	status = sctp_v4_add_protocol();
	if (status)
		goto err_add_protocol;

	/* Register SCTP with inet6 layer.  */
	status = sctp_v6_add_protocol();
	if (status)
		goto err_v6_add_protocol;

	if (sctp_offload_init() < 0)
		pr_crit("%s: Cannot add SCTP protocol offload\n", __func__);

out:
	return status;
err_v6_add_protocol:
	sctp_v4_del_protocol();
err_add_protocol:
	unregister_pernet_subsys(&sctp_ctrlsock_ops);
err_register_ctrlsock:
	sctp_v6_protosw_exit();
err_v6_protosw_init:
	sctp_v4_protosw_exit();
err_protosw_init:
	unregister_pernet_subsys(&sctp_defaults_ops);
err_register_defaults:
	sctp_v4_pf_exit();
	sctp_v6_pf_exit();
	sctp_sysctl_unregister();
	free_pages((unsigned long)sctp_port_hashtable,
		   get_order(sctp_port_hashsize *
			     sizeof(struct sctp_bind_hashbucket)));
err_bhash_alloc:
	sctp_transport_hashtable_destroy();
err_thash_alloc:
	kfree(sctp_ep_hashtable);
err_ehash_alloc:
	percpu_counter_destroy(&sctp_sockets_allocated);
err_percpu_counter_init:
	kmem_cache_destroy(sctp_chunk_cachep);
err_chunk_cachep:
	kmem_cache_destroy(sctp_bucket_cachep);
	goto out;
}

/* Exit handler for the SCTP protocol.  */
static __exit void sctp_exit(void)
{
	/* BUG.  This should probably do something useful like clean
	 * up all the remaining associations and all that memory.
	 */

	/* Unregister with inet6/inet layers. */
	sctp_v6_del_protocol();
	sctp_v4_del_protocol();

	unregister_pernet_subsys(&sctp_ctrlsock_ops);

	/* Free protosw registrations */
	sctp_v6_protosw_exit();
	sctp_v4_protosw_exit();

	unregister_pernet_subsys(&sctp_defaults_ops);

	/* Unregister with socket layer. */
	sctp_v6_pf_exit();
	sctp_v4_pf_exit();

	sctp_sysctl_unregister();

	free_pages((unsigned long)sctp_port_hashtable,
		   get_order(sctp_port_hashsize *
			     sizeof(struct sctp_bind_hashbucket)));
	kfree(sctp_ep_hashtable);
	sctp_transport_hashtable_destroy();

	percpu_counter_destroy(&sctp_sockets_allocated);

	rcu_barrier(); /* Wait for completion of call_rcu()'s */

	kmem_cache_destroy(sctp_chunk_cachep);
	kmem_cache_destroy(sctp_bucket_cachep);
}

module_init(sctp_init);
module_exit(sctp_exit);

/*
 * __stringify doesn't likes enums, so use IPPROTO_SCTP value (132) directly.
 */
MODULE_ALIAS("net-pf-" __stringify(PF_INET) "-proto-132");
MODULE_ALIAS("net-pf-" __stringify(PF_INET6) "-proto-132");
MODULE_AUTHOR("Linux Kernel SCTP developers <linux-sctp@vger.kernel.org>");
MODULE_DESCRIPTION("Support for the SCTP protocol (RFC2960)");
module_param_named(no_checksums, sctp_checksum_disable, bool, 0644);
MODULE_PARM_DESC(no_checksums, "Disable checksums computing and verification");
MODULE_LICENSE("GPL");
