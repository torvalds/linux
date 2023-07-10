// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	UDP over IPv6
 *	Linux INET6 implementation
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>
 *
 *	Based on linux/ipv4/udp.c
 *
 *	Fixes:
 *	Hideaki YOSHIFUJI	:	sin6_scope_id support
 *	YOSHIFUJI Hideaki @USAGI and:	Support IPV6_V6ONLY socket option, which
 *	Alexey Kuznetsov		allow both IPv4 and IPv6 sockets to bind
 *					a single port at the same time.
 *      Kazunori MIYAZAWA @USAGI:       change process style to use ip6_append_data
 *      YOSHIFUJI Hideaki @USAGI:	convert /proc/net/udp6 to seq_file.
 */

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/in6.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/ipv6.h>
#include <linux/icmpv6.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/indirect_call_wrapper.h>

#include <net/addrconf.h>
#include <net/ndisc.h>
#include <net/protocol.h>
#include <net/transp_v6.h>
#include <net/ip6_route.h>
#include <net/raw.h>
#include <net/tcp_states.h>
#include <net/ip6_checksum.h>
#include <net/ip6_tunnel.h>
#include <net/xfrm.h>
#include <net/inet_hashtables.h>
#include <net/inet6_hashtables.h>
#include <net/busy_poll.h>
#include <net/sock_reuseport.h>

#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <trace/events/skb.h>
#include "udp_impl.h"

static void udpv6_destruct_sock(struct sock *sk)
{
	udp_destruct_common(sk);
	inet6_sock_destruct(sk);
}

int udpv6_init_sock(struct sock *sk)
{
	skb_queue_head_init(&udp_sk(sk)->reader_queue);
	sk->sk_destruct = udpv6_destruct_sock;
	return 0;
}

static u32 udp6_ehashfn(const struct net *net,
			const struct in6_addr *laddr,
			const u16 lport,
			const struct in6_addr *faddr,
			const __be16 fport)
{
	static u32 udp6_ehash_secret __read_mostly;
	static u32 udp_ipv6_hash_secret __read_mostly;

	u32 lhash, fhash;

	net_get_random_once(&udp6_ehash_secret,
			    sizeof(udp6_ehash_secret));
	net_get_random_once(&udp_ipv6_hash_secret,
			    sizeof(udp_ipv6_hash_secret));

	lhash = (__force u32)laddr->s6_addr32[3];
	fhash = __ipv6_addr_jhash(faddr, udp_ipv6_hash_secret);

	return __inet6_ehashfn(lhash, lport, fhash, fport,
			       udp_ipv6_hash_secret + net_hash_mix(net));
}

int udp_v6_get_port(struct sock *sk, unsigned short snum)
{
	unsigned int hash2_nulladdr =
		ipv6_portaddr_hash(sock_net(sk), &in6addr_any, snum);
	unsigned int hash2_partial =
		ipv6_portaddr_hash(sock_net(sk), &sk->sk_v6_rcv_saddr, 0);

	/* precompute partial secondary hash */
	udp_sk(sk)->udp_portaddr_hash = hash2_partial;
	return udp_lib_get_port(sk, snum, hash2_nulladdr);
}

void udp_v6_rehash(struct sock *sk)
{
	u16 new_hash = ipv6_portaddr_hash(sock_net(sk),
					  &sk->sk_v6_rcv_saddr,
					  inet_sk(sk)->inet_num);

	udp_lib_rehash(sk, new_hash);
}

static int compute_score(struct sock *sk, struct net *net,
			 const struct in6_addr *saddr, __be16 sport,
			 const struct in6_addr *daddr, unsigned short hnum,
			 int dif, int sdif)
{
	int score;
	struct inet_sock *inet;
	bool dev_match;

	if (!net_eq(sock_net(sk), net) ||
	    udp_sk(sk)->udp_port_hash != hnum ||
	    sk->sk_family != PF_INET6)
		return -1;

	if (!ipv6_addr_equal(&sk->sk_v6_rcv_saddr, daddr))
		return -1;

	score = 0;
	inet = inet_sk(sk);

	if (inet->inet_dport) {
		if (inet->inet_dport != sport)
			return -1;
		score++;
	}

	if (!ipv6_addr_any(&sk->sk_v6_daddr)) {
		if (!ipv6_addr_equal(&sk->sk_v6_daddr, saddr))
			return -1;
		score++;
	}

	dev_match = udp_sk_bound_dev_eq(net, sk->sk_bound_dev_if, dif, sdif);
	if (!dev_match)
		return -1;
	if (sk->sk_bound_dev_if)
		score++;

	if (READ_ONCE(sk->sk_incoming_cpu) == raw_smp_processor_id())
		score++;

	return score;
}

static struct sock *lookup_reuseport(struct net *net, struct sock *sk,
				     struct sk_buff *skb,
				     const struct in6_addr *saddr,
				     __be16 sport,
				     const struct in6_addr *daddr,
				     unsigned int hnum)
{
	struct sock *reuse_sk = NULL;
	u32 hash;

	if (sk->sk_reuseport && sk->sk_state != TCP_ESTABLISHED) {
		hash = udp6_ehashfn(net, daddr, hnum, saddr, sport);
		reuse_sk = reuseport_select_sock(sk, hash, skb,
						 sizeof(struct udphdr));
	}
	return reuse_sk;
}

/* called with rcu_read_lock() */
static struct sock *udp6_lib_lookup2(struct net *net,
		const struct in6_addr *saddr, __be16 sport,
		const struct in6_addr *daddr, unsigned int hnum,
		int dif, int sdif, struct udp_hslot *hslot2,
		struct sk_buff *skb)
{
	struct sock *sk, *result;
	int score, badness;

	result = NULL;
	badness = -1;
	udp_portaddr_for_each_entry_rcu(sk, &hslot2->head) {
		score = compute_score(sk, net, saddr, sport,
				      daddr, hnum, dif, sdif);
		if (score > badness) {
			result = lookup_reuseport(net, sk, skb,
						  saddr, sport, daddr, hnum);
			/* Fall back to scoring if group has connections */
			if (result && !reuseport_has_conns(sk))
				return result;

			result = result ? : sk;
			badness = score;
		}
	}
	return result;
}

static inline struct sock *udp6_lookup_run_bpf(struct net *net,
					       struct udp_table *udptable,
					       struct sk_buff *skb,
					       const struct in6_addr *saddr,
					       __be16 sport,
					       const struct in6_addr *daddr,
					       u16 hnum)
{
	struct sock *sk, *reuse_sk;
	bool no_reuseport;

	if (udptable != &udp_table)
		return NULL; /* only UDP is supported */

	no_reuseport = bpf_sk_lookup_run_v6(net, IPPROTO_UDP,
					    saddr, sport, daddr, hnum, &sk);
	if (no_reuseport || IS_ERR_OR_NULL(sk))
		return sk;

	reuse_sk = lookup_reuseport(net, sk, skb, saddr, sport, daddr, hnum);
	if (reuse_sk)
		sk = reuse_sk;
	return sk;
}

/* rcu_read_lock() must be held */
struct sock *__udp6_lib_lookup(struct net *net,
			       const struct in6_addr *saddr, __be16 sport,
			       const struct in6_addr *daddr, __be16 dport,
			       int dif, int sdif, struct udp_table *udptable,
			       struct sk_buff *skb)
{
	unsigned short hnum = ntohs(dport);
	unsigned int hash2, slot2;
	struct udp_hslot *hslot2;
	struct sock *result, *sk;

	hash2 = ipv6_portaddr_hash(net, daddr, hnum);
	slot2 = hash2 & udptable->mask;
	hslot2 = &udptable->hash2[slot2];

	/* Lookup connected or non-wildcard sockets */
	result = udp6_lib_lookup2(net, saddr, sport,
				  daddr, hnum, dif, sdif,
				  hslot2, skb);
	if (!IS_ERR_OR_NULL(result) && result->sk_state == TCP_ESTABLISHED)
		goto done;

	/* Lookup redirect from BPF */
	if (static_branch_unlikely(&bpf_sk_lookup_enabled)) {
		sk = udp6_lookup_run_bpf(net, udptable, skb,
					 saddr, sport, daddr, hnum);
		if (sk) {
			result = sk;
			goto done;
		}
	}

	/* Got non-wildcard socket or error on first lookup */
	if (result)
		goto done;

	/* Lookup wildcard sockets */
	hash2 = ipv6_portaddr_hash(net, &in6addr_any, hnum);
	slot2 = hash2 & udptable->mask;
	hslot2 = &udptable->hash2[slot2];

	result = udp6_lib_lookup2(net, saddr, sport,
				  &in6addr_any, hnum, dif, sdif,
				  hslot2, skb);
done:
	if (IS_ERR(result))
		return NULL;
	return result;
}
EXPORT_SYMBOL_GPL(__udp6_lib_lookup);

static struct sock *__udp6_lib_lookup_skb(struct sk_buff *skb,
					  __be16 sport, __be16 dport,
					  struct udp_table *udptable)
{
	const struct ipv6hdr *iph = ipv6_hdr(skb);

	return __udp6_lib_lookup(dev_net(skb->dev), &iph->saddr, sport,
				 &iph->daddr, dport, inet6_iif(skb),
				 inet6_sdif(skb), udptable, skb);
}

struct sock *udp6_lib_lookup_skb(struct sk_buff *skb,
				 __be16 sport, __be16 dport)
{
	const struct ipv6hdr *iph = ipv6_hdr(skb);

	return __udp6_lib_lookup(dev_net(skb->dev), &iph->saddr, sport,
				 &iph->daddr, dport, inet6_iif(skb),
				 inet6_sdif(skb), &udp_table, NULL);
}
EXPORT_SYMBOL_GPL(udp6_lib_lookup_skb);

/* Must be called under rcu_read_lock().
 * Does increment socket refcount.
 */
#if IS_ENABLED(CONFIG_NF_TPROXY_IPV6) || IS_ENABLED(CONFIG_NF_SOCKET_IPV6)
struct sock *udp6_lib_lookup(struct net *net, const struct in6_addr *saddr, __be16 sport,
			     const struct in6_addr *daddr, __be16 dport, int dif)
{
	struct sock *sk;

	sk =  __udp6_lib_lookup(net, saddr, sport, daddr, dport,
				dif, 0, &udp_table, NULL);
	if (sk && !refcount_inc_not_zero(&sk->sk_refcnt))
		sk = NULL;
	return sk;
}
EXPORT_SYMBOL_GPL(udp6_lib_lookup);
#endif

/* do not use the scratch area len for jumbogram: their length execeeds the
 * scratch area space; note that the IP6CB flags is still in the first
 * cacheline, so checking for jumbograms is cheap
 */
static int udp6_skb_len(struct sk_buff *skb)
{
	return unlikely(inet6_is_jumbogram(skb)) ? skb->len : udp_skb_len(skb);
}

/*
 *	This should be easy, if there is something there we
 *	return it, otherwise we block.
 */

int udpv6_recvmsg(struct sock *sk, struct msghdr *msg, size_t len,
		  int noblock, int flags, int *addr_len)
{
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct inet_sock *inet = inet_sk(sk);
	struct sk_buff *skb;
	unsigned int ulen, copied;
	int off, err, peeking = flags & MSG_PEEK;
	int is_udplite = IS_UDPLITE(sk);
	struct udp_mib __percpu *mib;
	bool checksum_valid = false;
	int is_udp4;

	if (flags & MSG_ERRQUEUE)
		return ipv6_recv_error(sk, msg, len, addr_len);

	if (np->rxpmtu && np->rxopt.bits.rxpmtu)
		return ipv6_recv_rxpmtu(sk, msg, len, addr_len);

try_again:
	off = sk_peek_offset(sk, flags);
	skb = __skb_recv_udp(sk, flags, noblock, &off, &err);
	if (!skb)
		return err;

	ulen = udp6_skb_len(skb);
	copied = len;
	if (copied > ulen - off)
		copied = ulen - off;
	else if (copied < ulen)
		msg->msg_flags |= MSG_TRUNC;

	is_udp4 = (skb->protocol == htons(ETH_P_IP));
	mib = __UDPX_MIB(sk, is_udp4);

	/*
	 * If checksum is needed at all, try to do it while copying the
	 * data.  If the data is truncated, or if we only want a partial
	 * coverage checksum (UDP-Lite), do it before the copy.
	 */

	if (copied < ulen || peeking ||
	    (is_udplite && UDP_SKB_CB(skb)->partial_cov)) {
		checksum_valid = udp_skb_csum_unnecessary(skb) ||
				!__udp_lib_checksum_complete(skb);
		if (!checksum_valid)
			goto csum_copy_err;
	}

	if (checksum_valid || udp_skb_csum_unnecessary(skb)) {
		if (udp_skb_is_linear(skb))
			err = copy_linear_skb(skb, copied, off, &msg->msg_iter);
		else
			err = skb_copy_datagram_msg(skb, off, msg, copied);
	} else {
		err = skb_copy_and_csum_datagram_msg(skb, off, msg);
		if (err == -EINVAL)
			goto csum_copy_err;
	}
	if (unlikely(err)) {
		if (!peeking) {
			atomic_inc(&sk->sk_drops);
			SNMP_INC_STATS(mib, UDP_MIB_INERRORS);
		}
		kfree_skb(skb);
		return err;
	}
	if (!peeking)
		SNMP_INC_STATS(mib, UDP_MIB_INDATAGRAMS);

	sock_recv_ts_and_drops(msg, sk, skb);

	/* Copy the address. */
	if (msg->msg_name) {
		DECLARE_SOCKADDR(struct sockaddr_in6 *, sin6, msg->msg_name);
		sin6->sin6_family = AF_INET6;
		sin6->sin6_port = udp_hdr(skb)->source;
		sin6->sin6_flowinfo = 0;

		if (is_udp4) {
			ipv6_addr_set_v4mapped(ip_hdr(skb)->saddr,
					       &sin6->sin6_addr);
			sin6->sin6_scope_id = 0;
		} else {
			sin6->sin6_addr = ipv6_hdr(skb)->saddr;
			sin6->sin6_scope_id =
				ipv6_iface_scope_id(&sin6->sin6_addr,
						    inet6_iif(skb));
		}
		*addr_len = sizeof(*sin6);

		if (cgroup_bpf_enabled)
			BPF_CGROUP_RUN_PROG_UDP6_RECVMSG_LOCK(sk,
						(struct sockaddr *)sin6);
	}

	if (udp_sk(sk)->gro_enabled)
		udp_cmsg_recv(msg, sk, skb);

	if (np->rxopt.all)
		ip6_datagram_recv_common_ctl(sk, msg, skb);

	if (is_udp4) {
		if (inet->cmsg_flags)
			ip_cmsg_recv_offset(msg, sk, skb,
					    sizeof(struct udphdr), off);
	} else {
		if (np->rxopt.all)
			ip6_datagram_recv_specific_ctl(sk, msg, skb);
	}

	err = copied;
	if (flags & MSG_TRUNC)
		err = ulen;

	skb_consume_udp(sk, skb, peeking ? -err : err);
	return err;

csum_copy_err:
	if (!__sk_queue_drop_skb(sk, &udp_sk(sk)->reader_queue, skb, flags,
				 udp_skb_destructor)) {
		SNMP_INC_STATS(mib, UDP_MIB_CSUMERRORS);
		SNMP_INC_STATS(mib, UDP_MIB_INERRORS);
	}
	kfree_skb(skb);

	/* starting over for a new packet, but check if we need to yield */
	cond_resched();
	msg->msg_flags &= ~MSG_TRUNC;
	goto try_again;
}

DEFINE_STATIC_KEY_FALSE(udpv6_encap_needed_key);
void udpv6_encap_enable(void)
{
	static_branch_inc(&udpv6_encap_needed_key);
}
EXPORT_SYMBOL(udpv6_encap_enable);

/* Handler for tunnels with arbitrary destination ports: no socket lookup, go
 * through error handlers in encapsulations looking for a match.
 */
static int __udp6_lib_err_encap_no_sk(struct sk_buff *skb,
				      struct inet6_skb_parm *opt,
				      u8 type, u8 code, int offset, __be32 info)
{
	int i;

	for (i = 0; i < MAX_IPTUN_ENCAP_OPS; i++) {
		int (*handler)(struct sk_buff *skb, struct inet6_skb_parm *opt,
			       u8 type, u8 code, int offset, __be32 info);
		const struct ip6_tnl_encap_ops *encap;

		encap = rcu_dereference(ip6tun_encaps[i]);
		if (!encap)
			continue;
		handler = encap->err_handler;
		if (handler && !handler(skb, opt, type, code, offset, info))
			return 0;
	}

	return -ENOENT;
}

/* Try to match ICMP errors to UDP tunnels by looking up a socket without
 * reversing source and destination port: this will match tunnels that force the
 * same destination port on both endpoints (e.g. VXLAN, GENEVE). Note that
 * lwtunnels might actually break this assumption by being configured with
 * different destination ports on endpoints, in this case we won't be able to
 * trace ICMP messages back to them.
 *
 * If this doesn't match any socket, probe tunnels with arbitrary destination
 * ports (e.g. FoU, GUE): there, the receiving socket is useless, as the port
 * we've sent packets to won't necessarily match the local destination port.
 *
 * Then ask the tunnel implementation to match the error against a valid
 * association.
 *
 * Return an error if we can't find a match, the socket if we need further
 * processing, zero otherwise.
 */
static struct sock *__udp6_lib_err_encap(struct net *net,
					 const struct ipv6hdr *hdr, int offset,
					 struct udphdr *uh,
					 struct udp_table *udptable,
					 struct sk_buff *skb,
					 struct inet6_skb_parm *opt,
					 u8 type, u8 code, __be32 info)
{
	int network_offset, transport_offset;
	struct sock *sk;

	network_offset = skb_network_offset(skb);
	transport_offset = skb_transport_offset(skb);

	/* Network header needs to point to the outer IPv6 header inside ICMP */
	skb_reset_network_header(skb);

	/* Transport header needs to point to the UDP header */
	skb_set_transport_header(skb, offset);

	sk = __udp6_lib_lookup(net, &hdr->daddr, uh->source,
			       &hdr->saddr, uh->dest,
			       inet6_iif(skb), 0, udptable, skb);
	if (sk) {
		int (*lookup)(struct sock *sk, struct sk_buff *skb);
		struct udp_sock *up = udp_sk(sk);

		lookup = READ_ONCE(up->encap_err_lookup);
		if (!lookup || lookup(sk, skb))
			sk = NULL;
	}

	if (!sk) {
		sk = ERR_PTR(__udp6_lib_err_encap_no_sk(skb, opt, type, code,
							offset, info));
	}

	skb_set_transport_header(skb, transport_offset);
	skb_set_network_header(skb, network_offset);

	return sk;
}

int __udp6_lib_err(struct sk_buff *skb, struct inet6_skb_parm *opt,
		   u8 type, u8 code, int offset, __be32 info,
		   struct udp_table *udptable)
{
	struct ipv6_pinfo *np;
	const struct ipv6hdr *hdr = (const struct ipv6hdr *)skb->data;
	const struct in6_addr *saddr = &hdr->saddr;
	const struct in6_addr *daddr = &hdr->daddr;
	struct udphdr *uh = (struct udphdr *)(skb->data+offset);
	bool tunnel = false;
	struct sock *sk;
	int harderr;
	int err;
	struct net *net = dev_net(skb->dev);

	sk = __udp6_lib_lookup(net, daddr, uh->dest, saddr, uh->source,
			       inet6_iif(skb), inet6_sdif(skb), udptable, NULL);
	if (!sk) {
		/* No socket for error: try tunnels before discarding */
		sk = ERR_PTR(-ENOENT);
		if (static_branch_unlikely(&udpv6_encap_needed_key)) {
			sk = __udp6_lib_err_encap(net, hdr, offset, uh,
						  udptable, skb,
						  opt, type, code, info);
			if (!sk)
				return 0;
		}

		if (IS_ERR(sk)) {
			__ICMP6_INC_STATS(net, __in6_dev_get(skb->dev),
					  ICMP6_MIB_INERRORS);
			return PTR_ERR(sk);
		}

		tunnel = true;
	}

	harderr = icmpv6_err_convert(type, code, &err);
	np = inet6_sk(sk);

	if (type == ICMPV6_PKT_TOOBIG) {
		if (!ip6_sk_accept_pmtu(sk))
			goto out;
		ip6_sk_update_pmtu(skb, sk, info);
		if (np->pmtudisc != IPV6_PMTUDISC_DONT)
			harderr = 1;
	}
	if (type == NDISC_REDIRECT) {
		if (tunnel) {
			ip6_redirect(skb, sock_net(sk), inet6_iif(skb),
				     sk->sk_mark, sk->sk_uid);
		} else {
			ip6_sk_redirect(skb, sk);
		}
		goto out;
	}

	/* Tunnels don't have an application socket: don't pass errors back */
	if (tunnel)
		goto out;

	if (!np->recverr) {
		if (!harderr || sk->sk_state != TCP_ESTABLISHED)
			goto out;
	} else {
		ipv6_icmp_error(sk, skb, err, uh->dest, ntohl(info), (u8 *)(uh+1));
	}

	sk->sk_err = err;
	sk->sk_error_report(sk);
out:
	return 0;
}

static int __udpv6_queue_rcv_skb(struct sock *sk, struct sk_buff *skb)
{
	int rc;

	if (!ipv6_addr_any(&sk->sk_v6_daddr)) {
		sock_rps_save_rxhash(sk, skb);
		sk_mark_napi_id(sk, skb);
		sk_incoming_cpu_update(sk);
	} else {
		sk_mark_napi_id_once(sk, skb);
	}

	rc = __udp_enqueue_schedule_skb(sk, skb);
	if (rc < 0) {
		int is_udplite = IS_UDPLITE(sk);

		/* Note that an ENOMEM error is charged twice */
		if (rc == -ENOMEM)
			UDP6_INC_STATS(sock_net(sk),
					 UDP_MIB_RCVBUFERRORS, is_udplite);
		UDP6_INC_STATS(sock_net(sk), UDP_MIB_INERRORS, is_udplite);
		kfree_skb(skb);
		return -1;
	}

	return 0;
}

static __inline__ int udpv6_err(struct sk_buff *skb,
				struct inet6_skb_parm *opt, u8 type,
				u8 code, int offset, __be32 info)
{
	return __udp6_lib_err(skb, opt, type, code, offset, info, &udp_table);
}

static int udpv6_queue_rcv_one_skb(struct sock *sk, struct sk_buff *skb)
{
	struct udp_sock *up = udp_sk(sk);
	int is_udplite = IS_UDPLITE(sk);

	if (!xfrm6_policy_check(sk, XFRM_POLICY_IN, skb))
		goto drop;

	if (static_branch_unlikely(&udpv6_encap_needed_key) && up->encap_type) {
		int (*encap_rcv)(struct sock *sk, struct sk_buff *skb);

		/*
		 * This is an encapsulation socket so pass the skb to
		 * the socket's udp_encap_rcv() hook. Otherwise, just
		 * fall through and pass this up the UDP socket.
		 * up->encap_rcv() returns the following value:
		 * =0 if skb was successfully passed to the encap
		 *    handler or was discarded by it.
		 * >0 if skb should be passed on to UDP.
		 * <0 if skb should be resubmitted as proto -N
		 */

		/* if we're overly short, let UDP handle it */
		encap_rcv = READ_ONCE(up->encap_rcv);
		if (encap_rcv) {
			int ret;

			/* Verify checksum before giving to encap */
			if (udp_lib_checksum_complete(skb))
				goto csum_error;

			ret = encap_rcv(sk, skb);
			if (ret <= 0) {
				__UDP_INC_STATS(sock_net(sk),
						UDP_MIB_INDATAGRAMS,
						is_udplite);
				return -ret;
			}
		}

		/* FALLTHROUGH -- it's a UDP Packet */
	}

	/*
	 * UDP-Lite specific tests, ignored on UDP sockets (see net/ipv4/udp.c).
	 */
	if ((up->pcflag & UDPLITE_RECV_CC)  &&  UDP_SKB_CB(skb)->partial_cov) {

		if (up->pcrlen == 0) {          /* full coverage was set  */
			net_dbg_ratelimited("UDPLITE6: partial coverage %d while full coverage %d requested\n",
					    UDP_SKB_CB(skb)->cscov, skb->len);
			goto drop;
		}
		if (UDP_SKB_CB(skb)->cscov  <  up->pcrlen) {
			net_dbg_ratelimited("UDPLITE6: coverage %d too small, need min %d\n",
					    UDP_SKB_CB(skb)->cscov, up->pcrlen);
			goto drop;
		}
	}

	prefetch(&sk->sk_rmem_alloc);
	if (rcu_access_pointer(sk->sk_filter) &&
	    udp_lib_checksum_complete(skb))
		goto csum_error;

	if (sk_filter_trim_cap(sk, skb, sizeof(struct udphdr)))
		goto drop;

	udp_csum_pull_header(skb);

	skb_dst_drop(skb);

	return __udpv6_queue_rcv_skb(sk, skb);

csum_error:
	__UDP6_INC_STATS(sock_net(sk), UDP_MIB_CSUMERRORS, is_udplite);
drop:
	__UDP6_INC_STATS(sock_net(sk), UDP_MIB_INERRORS, is_udplite);
	atomic_inc(&sk->sk_drops);
	kfree_skb(skb);
	return -1;
}

static int udpv6_queue_rcv_skb(struct sock *sk, struct sk_buff *skb)
{
	struct sk_buff *next, *segs;
	int ret;

	if (likely(!udp_unexpected_gso(sk, skb)))
		return udpv6_queue_rcv_one_skb(sk, skb);

	__skb_push(skb, -skb_mac_offset(skb));
	segs = udp_rcv_segment(sk, skb, false);
	skb_list_walk_safe(segs, skb, next) {
		__skb_pull(skb, skb_transport_offset(skb));

		ret = udpv6_queue_rcv_one_skb(sk, skb);
		if (ret > 0)
			ip6_protocol_deliver_rcu(dev_net(skb->dev), skb, ret,
						 true);
	}
	return 0;
}

static bool __udp_v6_is_mcast_sock(struct net *net, struct sock *sk,
				   __be16 loc_port, const struct in6_addr *loc_addr,
				   __be16 rmt_port, const struct in6_addr *rmt_addr,
				   int dif, int sdif, unsigned short hnum)
{
	struct inet_sock *inet = inet_sk(sk);

	if (!net_eq(sock_net(sk), net))
		return false;

	if (udp_sk(sk)->udp_port_hash != hnum ||
	    sk->sk_family != PF_INET6 ||
	    (inet->inet_dport && inet->inet_dport != rmt_port) ||
	    (!ipv6_addr_any(&sk->sk_v6_daddr) &&
		    !ipv6_addr_equal(&sk->sk_v6_daddr, rmt_addr)) ||
	    !udp_sk_bound_dev_eq(net, sk->sk_bound_dev_if, dif, sdif) ||
	    (!ipv6_addr_any(&sk->sk_v6_rcv_saddr) &&
		    !ipv6_addr_equal(&sk->sk_v6_rcv_saddr, loc_addr)))
		return false;
	if (!inet6_mc_check(sk, loc_addr, rmt_addr))
		return false;
	return true;
}

static void udp6_csum_zero_error(struct sk_buff *skb)
{
	/* RFC 2460 section 8.1 says that we SHOULD log
	 * this error. Well, it is reasonable.
	 */
	net_dbg_ratelimited("IPv6: udp checksum is 0 for [%pI6c]:%u->[%pI6c]:%u\n",
			    &ipv6_hdr(skb)->saddr, ntohs(udp_hdr(skb)->source),
			    &ipv6_hdr(skb)->daddr, ntohs(udp_hdr(skb)->dest));
}

/*
 * Note: called only from the BH handler context,
 * so we don't need to lock the hashes.
 */
static int __udp6_lib_mcast_deliver(struct net *net, struct sk_buff *skb,
		const struct in6_addr *saddr, const struct in6_addr *daddr,
		struct udp_table *udptable, int proto)
{
	struct sock *sk, *first = NULL;
	const struct udphdr *uh = udp_hdr(skb);
	unsigned short hnum = ntohs(uh->dest);
	struct udp_hslot *hslot = udp_hashslot(udptable, net, hnum);
	unsigned int offset = offsetof(typeof(*sk), sk_node);
	unsigned int hash2 = 0, hash2_any = 0, use_hash2 = (hslot->count > 10);
	int dif = inet6_iif(skb);
	int sdif = inet6_sdif(skb);
	struct hlist_node *node;
	struct sk_buff *nskb;

	if (use_hash2) {
		hash2_any = ipv6_portaddr_hash(net, &in6addr_any, hnum) &
			    udptable->mask;
		hash2 = ipv6_portaddr_hash(net, daddr, hnum) & udptable->mask;
start_lookup:
		hslot = &udptable->hash2[hash2];
		offset = offsetof(typeof(*sk), __sk_common.skc_portaddr_node);
	}

	sk_for_each_entry_offset_rcu(sk, node, &hslot->head, offset) {
		if (!__udp_v6_is_mcast_sock(net, sk, uh->dest, daddr,
					    uh->source, saddr, dif, sdif,
					    hnum))
			continue;
		/* If zero checksum and no_check is not on for
		 * the socket then skip it.
		 */
		if (!uh->check && !udp_sk(sk)->no_check6_rx)
			continue;
		if (!first) {
			first = sk;
			continue;
		}
		nskb = skb_clone(skb, GFP_ATOMIC);
		if (unlikely(!nskb)) {
			atomic_inc(&sk->sk_drops);
			__UDP6_INC_STATS(net, UDP_MIB_RCVBUFERRORS,
					 IS_UDPLITE(sk));
			__UDP6_INC_STATS(net, UDP_MIB_INERRORS,
					 IS_UDPLITE(sk));
			continue;
		}

		if (udpv6_queue_rcv_skb(sk, nskb) > 0)
			consume_skb(nskb);
	}

	/* Also lookup *:port if we are using hash2 and haven't done so yet. */
	if (use_hash2 && hash2 != hash2_any) {
		hash2 = hash2_any;
		goto start_lookup;
	}

	if (first) {
		if (udpv6_queue_rcv_skb(first, skb) > 0)
			consume_skb(skb);
	} else {
		kfree_skb(skb);
		__UDP6_INC_STATS(net, UDP_MIB_IGNOREDMULTI,
				 proto == IPPROTO_UDPLITE);
	}
	return 0;
}

static void udp6_sk_rx_dst_set(struct sock *sk, struct dst_entry *dst)
{
	if (udp_sk_rx_dst_set(sk, dst)) {
		const struct rt6_info *rt = (const struct rt6_info *)dst;

		inet6_sk(sk)->rx_dst_cookie = rt6_get_cookie(rt);
	}
}

/* wrapper for udp_queue_rcv_skb tacking care of csum conversion and
 * return code conversion for ip layer consumption
 */
static int udp6_unicast_rcv_skb(struct sock *sk, struct sk_buff *skb,
				struct udphdr *uh)
{
	int ret;

	if (inet_get_convert_csum(sk) && uh->check && !IS_UDPLITE(sk))
		skb_checksum_try_convert(skb, IPPROTO_UDP, ip6_compute_pseudo);

	ret = udpv6_queue_rcv_skb(sk, skb);

	/* a return value > 0 means to resubmit the input */
	if (ret > 0)
		return ret;
	return 0;
}

int __udp6_lib_rcv(struct sk_buff *skb, struct udp_table *udptable,
		   int proto)
{
	const struct in6_addr *saddr, *daddr;
	struct net *net = dev_net(skb->dev);
	struct udphdr *uh;
	struct sock *sk;
	bool refcounted;
	u32 ulen = 0;

	if (!pskb_may_pull(skb, sizeof(struct udphdr)))
		goto discard;

	saddr = &ipv6_hdr(skb)->saddr;
	daddr = &ipv6_hdr(skb)->daddr;
	uh = udp_hdr(skb);

	ulen = ntohs(uh->len);
	if (ulen > skb->len)
		goto short_packet;

	if (proto == IPPROTO_UDP) {
		/* UDP validates ulen. */

		/* Check for jumbo payload */
		if (ulen == 0)
			ulen = skb->len;

		if (ulen < sizeof(*uh))
			goto short_packet;

		if (ulen < skb->len) {
			if (pskb_trim_rcsum(skb, ulen))
				goto short_packet;
			saddr = &ipv6_hdr(skb)->saddr;
			daddr = &ipv6_hdr(skb)->daddr;
			uh = udp_hdr(skb);
		}
	}

	if (udp6_csum_init(skb, uh, proto))
		goto csum_error;

	/* Check if the socket is already available, e.g. due to early demux */
	sk = skb_steal_sock(skb, &refcounted);
	if (sk) {
		struct dst_entry *dst = skb_dst(skb);
		int ret;

		if (unlikely(rcu_dereference(sk->sk_rx_dst) != dst))
			udp6_sk_rx_dst_set(sk, dst);

		if (!uh->check && !udp_sk(sk)->no_check6_rx) {
			if (refcounted)
				sock_put(sk);
			goto report_csum_error;
		}

		ret = udp6_unicast_rcv_skb(sk, skb, uh);
		if (refcounted)
			sock_put(sk);
		return ret;
	}

	/*
	 *	Multicast receive code
	 */
	if (ipv6_addr_is_multicast(daddr))
		return __udp6_lib_mcast_deliver(net, skb,
				saddr, daddr, udptable, proto);

	/* Unicast */
	sk = __udp6_lib_lookup_skb(skb, uh->source, uh->dest, udptable);
	if (sk) {
		if (!uh->check && !udp_sk(sk)->no_check6_rx)
			goto report_csum_error;
		return udp6_unicast_rcv_skb(sk, skb, uh);
	}

	if (!uh->check)
		goto report_csum_error;

	if (!xfrm6_policy_check(NULL, XFRM_POLICY_IN, skb))
		goto discard;

	if (udp_lib_checksum_complete(skb))
		goto csum_error;

	__UDP6_INC_STATS(net, UDP_MIB_NOPORTS, proto == IPPROTO_UDPLITE);
	icmpv6_send(skb, ICMPV6_DEST_UNREACH, ICMPV6_PORT_UNREACH, 0);

	kfree_skb(skb);
	return 0;

short_packet:
	net_dbg_ratelimited("UDP%sv6: short packet: From [%pI6c]:%u %d/%d to [%pI6c]:%u\n",
			    proto == IPPROTO_UDPLITE ? "-Lite" : "",
			    saddr, ntohs(uh->source),
			    ulen, skb->len,
			    daddr, ntohs(uh->dest));
	goto discard;

report_csum_error:
	udp6_csum_zero_error(skb);
csum_error:
	__UDP6_INC_STATS(net, UDP_MIB_CSUMERRORS, proto == IPPROTO_UDPLITE);
discard:
	__UDP6_INC_STATS(net, UDP_MIB_INERRORS, proto == IPPROTO_UDPLITE);
	kfree_skb(skb);
	return 0;
}


static struct sock *__udp6_lib_demux_lookup(struct net *net,
			__be16 loc_port, const struct in6_addr *loc_addr,
			__be16 rmt_port, const struct in6_addr *rmt_addr,
			int dif, int sdif)
{
	unsigned short hnum = ntohs(loc_port);
	unsigned int hash2 = ipv6_portaddr_hash(net, loc_addr, hnum);
	unsigned int slot2 = hash2 & udp_table.mask;
	struct udp_hslot *hslot2 = &udp_table.hash2[slot2];
	const __portpair ports = INET_COMBINED_PORTS(rmt_port, hnum);
	struct sock *sk;

	udp_portaddr_for_each_entry_rcu(sk, &hslot2->head) {
		if (sk->sk_state == TCP_ESTABLISHED &&
		    inet6_match(net, sk, rmt_addr, loc_addr, ports, dif, sdif))
			return sk;
		/* Only check first socket in chain */
		break;
	}
	return NULL;
}

void udp_v6_early_demux(struct sk_buff *skb)
{
	struct net *net = dev_net(skb->dev);
	const struct udphdr *uh;
	struct sock *sk;
	struct dst_entry *dst;
	int dif = skb->dev->ifindex;
	int sdif = inet6_sdif(skb);

	if (!pskb_may_pull(skb, skb_transport_offset(skb) +
	    sizeof(struct udphdr)))
		return;

	uh = udp_hdr(skb);

	if (skb->pkt_type == PACKET_HOST)
		sk = __udp6_lib_demux_lookup(net, uh->dest,
					     &ipv6_hdr(skb)->daddr,
					     uh->source, &ipv6_hdr(skb)->saddr,
					     dif, sdif);
	else
		return;

	if (!sk || !refcount_inc_not_zero(&sk->sk_refcnt))
		return;

	skb->sk = sk;
	skb->destructor = sock_efree;
	dst = rcu_dereference(sk->sk_rx_dst);

	if (dst)
		dst = dst_check(dst, inet6_sk(sk)->rx_dst_cookie);
	if (dst) {
		/* set noref for now.
		 * any place which wants to hold dst has to call
		 * dst_hold_safe()
		 */
		skb_dst_set_noref(skb, dst);
	}
}

INDIRECT_CALLABLE_SCOPE int udpv6_rcv(struct sk_buff *skb)
{
	return __udp6_lib_rcv(skb, &udp_table, IPPROTO_UDP);
}

/*
 * Throw away all pending data and cancel the corking. Socket is locked.
 */
static void udp_v6_flush_pending_frames(struct sock *sk)
{
	struct udp_sock *up = udp_sk(sk);

	if (up->pending == AF_INET)
		udp_flush_pending_frames(sk);
	else if (up->pending) {
		up->len = 0;
		up->pending = 0;
		ip6_flush_pending_frames(sk);
	}
}

static int udpv6_pre_connect(struct sock *sk, struct sockaddr *uaddr,
			     int addr_len)
{
	if (addr_len < offsetofend(struct sockaddr, sa_family))
		return -EINVAL;
	/* The following checks are replicated from __ip6_datagram_connect()
	 * and intended to prevent BPF program called below from accessing
	 * bytes that are out of the bound specified by user in addr_len.
	 */
	if (uaddr->sa_family == AF_INET) {
		if (__ipv6_only_sock(sk))
			return -EAFNOSUPPORT;
		return udp_pre_connect(sk, uaddr, addr_len);
	}

	if (addr_len < SIN6_LEN_RFC2133)
		return -EINVAL;

	return BPF_CGROUP_RUN_PROG_INET6_CONNECT_LOCK(sk, uaddr);
}

/**
 *	udp6_hwcsum_outgoing  -  handle outgoing HW checksumming
 *	@sk:	socket we are sending on
 *	@skb:	sk_buff containing the filled-in UDP header
 *		(checksum field must be zeroed out)
 *	@saddr: source address
 *	@daddr: destination address
 *	@len:	length of packet
 */
static void udp6_hwcsum_outgoing(struct sock *sk, struct sk_buff *skb,
				 const struct in6_addr *saddr,
				 const struct in6_addr *daddr, int len)
{
	unsigned int offset;
	struct udphdr *uh = udp_hdr(skb);
	struct sk_buff *frags = skb_shinfo(skb)->frag_list;
	__wsum csum = 0;

	if (!frags) {
		/* Only one fragment on the socket.  */
		skb->csum_start = skb_transport_header(skb) - skb->head;
		skb->csum_offset = offsetof(struct udphdr, check);
		uh->check = ~csum_ipv6_magic(saddr, daddr, len, IPPROTO_UDP, 0);
	} else {
		/*
		 * HW-checksum won't work as there are two or more
		 * fragments on the socket so that all csums of sk_buffs
		 * should be together
		 */
		offset = skb_transport_offset(skb);
		skb->csum = skb_checksum(skb, offset, skb->len - offset, 0);
		csum = skb->csum;

		skb->ip_summed = CHECKSUM_NONE;

		do {
			csum = csum_add(csum, frags->csum);
		} while ((frags = frags->next));

		uh->check = csum_ipv6_magic(saddr, daddr, len, IPPROTO_UDP,
					    csum);
		if (uh->check == 0)
			uh->check = CSUM_MANGLED_0;
	}
}

/*
 *	Sending
 */

static int udp_v6_send_skb(struct sk_buff *skb, struct flowi6 *fl6,
			   struct inet_cork *cork)
{
	struct sock *sk = skb->sk;
	struct udphdr *uh;
	int err = 0;
	int is_udplite = IS_UDPLITE(sk);
	__wsum csum = 0;
	int offset = skb_transport_offset(skb);
	int len = skb->len - offset;
	int datalen = len - sizeof(*uh);

	/*
	 * Create a UDP header
	 */
	uh = udp_hdr(skb);
	uh->source = fl6->fl6_sport;
	uh->dest = fl6->fl6_dport;
	uh->len = htons(len);
	uh->check = 0;

	if (cork->gso_size) {
		const int hlen = skb_network_header_len(skb) +
				 sizeof(struct udphdr);

		if (hlen + cork->gso_size > cork->fragsize) {
			kfree_skb(skb);
			return -EINVAL;
		}
		if (datalen > cork->gso_size * UDP_MAX_SEGMENTS) {
			kfree_skb(skb);
			return -EINVAL;
		}
		if (udp_sk(sk)->no_check6_tx) {
			kfree_skb(skb);
			return -EINVAL;
		}
		if (skb->ip_summed != CHECKSUM_PARTIAL || is_udplite ||
		    dst_xfrm(skb_dst(skb))) {
			kfree_skb(skb);
			return -EIO;
		}

		if (datalen > cork->gso_size) {
			skb_shinfo(skb)->gso_size = cork->gso_size;
			skb_shinfo(skb)->gso_type = SKB_GSO_UDP_L4;
			skb_shinfo(skb)->gso_segs = DIV_ROUND_UP(datalen,
								 cork->gso_size);
		}
		goto csum_partial;
	}

	if (is_udplite)
		csum = udplite_csum(skb);
	else if (udp_sk(sk)->no_check6_tx) {   /* UDP csum disabled */
		skb->ip_summed = CHECKSUM_NONE;
		goto send;
	} else if (skb->ip_summed == CHECKSUM_PARTIAL) { /* UDP hardware csum */
csum_partial:
		udp6_hwcsum_outgoing(sk, skb, &fl6->saddr, &fl6->daddr, len);
		goto send;
	} else
		csum = udp_csum(skb);

	/* add protocol-dependent pseudo-header */
	uh->check = csum_ipv6_magic(&fl6->saddr, &fl6->daddr,
				    len, fl6->flowi6_proto, csum);
	if (uh->check == 0)
		uh->check = CSUM_MANGLED_0;

send:
	err = ip6_send_skb(skb);
	if (err) {
		if (err == -ENOBUFS && !inet6_sk(sk)->recverr) {
			UDP6_INC_STATS(sock_net(sk),
				       UDP_MIB_SNDBUFERRORS, is_udplite);
			err = 0;
		}
	} else {
		UDP6_INC_STATS(sock_net(sk),
			       UDP_MIB_OUTDATAGRAMS, is_udplite);
	}
	return err;
}

static int udp_v6_push_pending_frames(struct sock *sk)
{
	struct sk_buff *skb;
	struct udp_sock  *up = udp_sk(sk);
	struct flowi6 fl6;
	int err = 0;

	if (up->pending == AF_INET)
		return udp_push_pending_frames(sk);

	/* ip6_finish_skb will release the cork, so make a copy of
	 * fl6 here.
	 */
	fl6 = inet_sk(sk)->cork.fl.u.ip6;

	skb = ip6_finish_skb(sk);
	if (!skb)
		goto out;

	err = udp_v6_send_skb(skb, &fl6, &inet_sk(sk)->cork.base);

out:
	up->len = 0;
	up->pending = 0;
	return err;
}

int udpv6_sendmsg(struct sock *sk, struct msghdr *msg, size_t len)
{
	struct ipv6_txoptions opt_space;
	struct udp_sock *up = udp_sk(sk);
	struct inet_sock *inet = inet_sk(sk);
	struct ipv6_pinfo *np = inet6_sk(sk);
	DECLARE_SOCKADDR(struct sockaddr_in6 *, sin6, msg->msg_name);
	struct in6_addr *daddr, *final_p, final;
	struct ipv6_txoptions *opt = NULL;
	struct ipv6_txoptions *opt_to_free = NULL;
	struct ip6_flowlabel *flowlabel = NULL;
	struct flowi6 fl6;
	struct dst_entry *dst;
	struct ipcm6_cookie ipc6;
	int addr_len = msg->msg_namelen;
	bool connected = false;
	int ulen = len;
	int corkreq = READ_ONCE(up->corkflag) || msg->msg_flags&MSG_MORE;
	int err;
	int is_udplite = IS_UDPLITE(sk);
	int (*getfrag)(void *, char *, int, int, int, struct sk_buff *);

	ipcm6_init(&ipc6);
	ipc6.gso_size = READ_ONCE(up->gso_size);
	ipc6.sockc.tsflags = sk->sk_tsflags;
	ipc6.sockc.mark = sk->sk_mark;

	/* destination address check */
	if (sin6) {
		if (addr_len < offsetof(struct sockaddr, sa_data))
			return -EINVAL;

		switch (sin6->sin6_family) {
		case AF_INET6:
			if (addr_len < SIN6_LEN_RFC2133)
				return -EINVAL;
			daddr = &sin6->sin6_addr;
			if (ipv6_addr_any(daddr) &&
			    ipv6_addr_v4mapped(&np->saddr))
				ipv6_addr_set_v4mapped(htonl(INADDR_LOOPBACK),
						       daddr);
			break;
		case AF_INET:
			goto do_udp_sendmsg;
		case AF_UNSPEC:
			msg->msg_name = sin6 = NULL;
			msg->msg_namelen = addr_len = 0;
			daddr = NULL;
			break;
		default:
			return -EINVAL;
		}
	} else if (!up->pending) {
		if (sk->sk_state != TCP_ESTABLISHED)
			return -EDESTADDRREQ;
		daddr = &sk->sk_v6_daddr;
	} else
		daddr = NULL;

	if (daddr) {
		if (ipv6_addr_v4mapped(daddr)) {
			struct sockaddr_in sin;
			sin.sin_family = AF_INET;
			sin.sin_port = sin6 ? sin6->sin6_port : inet->inet_dport;
			sin.sin_addr.s_addr = daddr->s6_addr32[3];
			msg->msg_name = &sin;
			msg->msg_namelen = sizeof(sin);
do_udp_sendmsg:
			err = __ipv6_only_sock(sk) ?
				-ENETUNREACH : udp_sendmsg(sk, msg, len);
			msg->msg_name = sin6;
			msg->msg_namelen = addr_len;
			return err;
		}
	}

	if (up->pending == AF_INET)
		return udp_sendmsg(sk, msg, len);

	/* Rough check on arithmetic overflow,
	   better check is made in ip6_append_data().
	   */
	if (len > INT_MAX - sizeof(struct udphdr))
		return -EMSGSIZE;

	getfrag  =  is_udplite ?  udplite_getfrag : ip_generic_getfrag;
	if (up->pending) {
		/*
		 * There are pending frames.
		 * The socket lock must be held while it's corked.
		 */
		lock_sock(sk);
		if (likely(up->pending)) {
			if (unlikely(up->pending != AF_INET6)) {
				release_sock(sk);
				return -EAFNOSUPPORT;
			}
			dst = NULL;
			goto do_append_data;
		}
		release_sock(sk);
	}
	ulen += sizeof(struct udphdr);

	memset(&fl6, 0, sizeof(fl6));

	if (sin6) {
		if (sin6->sin6_port == 0)
			return -EINVAL;

		fl6.fl6_dport = sin6->sin6_port;
		daddr = &sin6->sin6_addr;

		if (np->sndflow) {
			fl6.flowlabel = sin6->sin6_flowinfo&IPV6_FLOWINFO_MASK;
			if (fl6.flowlabel&IPV6_FLOWLABEL_MASK) {
				flowlabel = fl6_sock_lookup(sk, fl6.flowlabel);
				if (IS_ERR(flowlabel))
					return -EINVAL;
			}
		}

		/*
		 * Otherwise it will be difficult to maintain
		 * sk->sk_dst_cache.
		 */
		if (sk->sk_state == TCP_ESTABLISHED &&
		    ipv6_addr_equal(daddr, &sk->sk_v6_daddr))
			daddr = &sk->sk_v6_daddr;

		if (addr_len >= sizeof(struct sockaddr_in6) &&
		    sin6->sin6_scope_id &&
		    __ipv6_addr_needs_scope_id(__ipv6_addr_type(daddr)))
			fl6.flowi6_oif = sin6->sin6_scope_id;
	} else {
		if (sk->sk_state != TCP_ESTABLISHED)
			return -EDESTADDRREQ;

		fl6.fl6_dport = inet->inet_dport;
		daddr = &sk->sk_v6_daddr;
		fl6.flowlabel = np->flow_label;
		connected = true;
	}

	if (!fl6.flowi6_oif)
		fl6.flowi6_oif = sk->sk_bound_dev_if;

	if (!fl6.flowi6_oif)
		fl6.flowi6_oif = np->sticky_pktinfo.ipi6_ifindex;

	fl6.flowi6_uid = sk->sk_uid;

	if (msg->msg_controllen) {
		opt = &opt_space;
		memset(opt, 0, sizeof(struct ipv6_txoptions));
		opt->tot_len = sizeof(*opt);
		ipc6.opt = opt;

		err = udp_cmsg_send(sk, msg, &ipc6.gso_size);
		if (err > 0)
			err = ip6_datagram_send_ctl(sock_net(sk), sk, msg, &fl6,
						    &ipc6);
		if (err < 0) {
			fl6_sock_release(flowlabel);
			return err;
		}
		if ((fl6.flowlabel&IPV6_FLOWLABEL_MASK) && !flowlabel) {
			flowlabel = fl6_sock_lookup(sk, fl6.flowlabel);
			if (IS_ERR(flowlabel))
				return -EINVAL;
		}
		if (!(opt->opt_nflen|opt->opt_flen))
			opt = NULL;
		connected = false;
	}
	if (!opt) {
		opt = txopt_get(np);
		opt_to_free = opt;
	}
	if (flowlabel)
		opt = fl6_merge_options(&opt_space, flowlabel, opt);
	opt = ipv6_fixup_options(&opt_space, opt);
	ipc6.opt = opt;

	fl6.flowi6_proto = sk->sk_protocol;
	fl6.flowi6_mark = ipc6.sockc.mark;
	fl6.daddr = *daddr;
	if (ipv6_addr_any(&fl6.saddr) && !ipv6_addr_any(&np->saddr))
		fl6.saddr = np->saddr;
	fl6.fl6_sport = inet->inet_sport;

	if (cgroup_bpf_enabled && !connected) {
		err = BPF_CGROUP_RUN_PROG_UDP6_SENDMSG_LOCK(sk,
					   (struct sockaddr *)sin6, &fl6.saddr);
		if (err)
			goto out_no_dst;
		if (sin6) {
			if (ipv6_addr_v4mapped(&sin6->sin6_addr)) {
				/* BPF program rewrote IPv6-only by IPv4-mapped
				 * IPv6. It's currently unsupported.
				 */
				err = -ENOTSUPP;
				goto out_no_dst;
			}
			if (sin6->sin6_port == 0) {
				/* BPF program set invalid port. Reject it. */
				err = -EINVAL;
				goto out_no_dst;
			}
			fl6.fl6_dport = sin6->sin6_port;
			fl6.daddr = sin6->sin6_addr;
		}
	}

	if (ipv6_addr_any(&fl6.daddr))
		fl6.daddr.s6_addr[15] = 0x1; /* :: means loopback (BSD'ism) */

	final_p = fl6_update_dst(&fl6, opt, &final);
	if (final_p)
		connected = false;

	if (!fl6.flowi6_oif && ipv6_addr_is_multicast(&fl6.daddr)) {
		fl6.flowi6_oif = np->mcast_oif;
		connected = false;
	} else if (!fl6.flowi6_oif)
		fl6.flowi6_oif = np->ucast_oif;

	security_sk_classify_flow(sk, flowi6_to_flowi_common(&fl6));

	if (ipc6.tclass < 0)
		ipc6.tclass = np->tclass;

	fl6.flowlabel = ip6_make_flowinfo(ipc6.tclass, fl6.flowlabel);

	dst = ip6_sk_dst_lookup_flow(sk, &fl6, final_p, connected);
	if (IS_ERR(dst)) {
		err = PTR_ERR(dst);
		dst = NULL;
		goto out;
	}

	if (ipc6.hlimit < 0)
		ipc6.hlimit = ip6_sk_dst_hoplimit(np, &fl6, dst);

	if (msg->msg_flags&MSG_CONFIRM)
		goto do_confirm;
back_from_confirm:

	/* Lockless fast path for the non-corking case */
	if (!corkreq) {
		struct inet_cork_full cork;
		struct sk_buff *skb;

		skb = ip6_make_skb(sk, getfrag, msg, ulen,
				   sizeof(struct udphdr), &ipc6,
				   &fl6, (struct rt6_info *)dst,
				   msg->msg_flags, &cork);
		err = PTR_ERR(skb);
		if (!IS_ERR_OR_NULL(skb))
			err = udp_v6_send_skb(skb, &fl6, &cork.base);
		goto out;
	}

	lock_sock(sk);
	if (unlikely(up->pending)) {
		/* The socket is already corked while preparing it. */
		/* ... which is an evident application bug. --ANK */
		release_sock(sk);

		net_dbg_ratelimited("udp cork app bug 2\n");
		err = -EINVAL;
		goto out;
	}

	up->pending = AF_INET6;

do_append_data:
	if (ipc6.dontfrag < 0)
		ipc6.dontfrag = np->dontfrag;
	up->len += ulen;
	err = ip6_append_data(sk, getfrag, msg, ulen, sizeof(struct udphdr),
			      &ipc6, &fl6, (struct rt6_info *)dst,
			      corkreq ? msg->msg_flags|MSG_MORE : msg->msg_flags);
	if (err)
		udp_v6_flush_pending_frames(sk);
	else if (!corkreq)
		err = udp_v6_push_pending_frames(sk);
	else if (unlikely(skb_queue_empty(&sk->sk_write_queue)))
		up->pending = 0;

	if (err > 0)
		err = np->recverr ? net_xmit_errno(err) : 0;
	release_sock(sk);

out:
	dst_release(dst);
out_no_dst:
	fl6_sock_release(flowlabel);
	txopt_put(opt_to_free);
	if (!err)
		return len;
	/*
	 * ENOBUFS = no kernel mem, SOCK_NOSPACE = no sndbuf space.  Reporting
	 * ENOBUFS might not be good (it's not tunable per se), but otherwise
	 * we don't have a good statistic (IpOutDiscards but it can be too many
	 * things).  We could add another new stat but at least for now that
	 * seems like overkill.
	 */
	if (err == -ENOBUFS || test_bit(SOCK_NOSPACE, &sk->sk_socket->flags)) {
		UDP6_INC_STATS(sock_net(sk),
			       UDP_MIB_SNDBUFERRORS, is_udplite);
	}
	return err;

do_confirm:
	if (msg->msg_flags & MSG_PROBE)
		dst_confirm_neigh(dst, &fl6.daddr);
	if (!(msg->msg_flags&MSG_PROBE) || len)
		goto back_from_confirm;
	err = 0;
	goto out;
}

void udpv6_destroy_sock(struct sock *sk)
{
	struct udp_sock *up = udp_sk(sk);
	lock_sock(sk);

	/* protects from races with udp_abort() */
	sock_set_flag(sk, SOCK_DEAD);
	udp_v6_flush_pending_frames(sk);
	release_sock(sk);

	if (static_branch_unlikely(&udpv6_encap_needed_key)) {
		if (up->encap_type) {
			void (*encap_destroy)(struct sock *sk);
			encap_destroy = READ_ONCE(up->encap_destroy);
			if (encap_destroy)
				encap_destroy(sk);
		}
		if (up->encap_enabled) {
			static_branch_dec(&udpv6_encap_needed_key);
			udp_encap_disable();
		}
	}
}

/*
 *	Socket option code for UDP
 */
int udpv6_setsockopt(struct sock *sk, int level, int optname, sockptr_t optval,
		     unsigned int optlen)
{
	if (level == SOL_UDP  ||  level == SOL_UDPLITE)
		return udp_lib_setsockopt(sk, level, optname,
					  optval, optlen,
					  udp_v6_push_pending_frames);
	return ipv6_setsockopt(sk, level, optname, optval, optlen);
}

int udpv6_getsockopt(struct sock *sk, int level, int optname,
		     char __user *optval, int __user *optlen)
{
	if (level == SOL_UDP  ||  level == SOL_UDPLITE)
		return udp_lib_getsockopt(sk, level, optname, optval, optlen);
	return ipv6_getsockopt(sk, level, optname, optval, optlen);
}

static const struct inet6_protocol udpv6_protocol = {
	.handler	=	udpv6_rcv,
	.err_handler	=	udpv6_err,
	.flags		=	INET6_PROTO_NOPOLICY|INET6_PROTO_FINAL,
};

/* ------------------------------------------------------------------------ */
#ifdef CONFIG_PROC_FS
int udp6_seq_show(struct seq_file *seq, void *v)
{
	if (v == SEQ_START_TOKEN) {
		seq_puts(seq, IPV6_SEQ_DGRAM_HEADER);
	} else {
		int bucket = ((struct udp_iter_state *)seq->private)->bucket;
		struct inet_sock *inet = inet_sk(v);
		__u16 srcp = ntohs(inet->inet_sport);
		__u16 destp = ntohs(inet->inet_dport);
		__ip6_dgram_sock_seq_show(seq, v, srcp, destp,
					  udp_rqueue_get(v), bucket);
	}
	return 0;
}

const struct seq_operations udp6_seq_ops = {
	.start		= udp_seq_start,
	.next		= udp_seq_next,
	.stop		= udp_seq_stop,
	.show		= udp6_seq_show,
};
EXPORT_SYMBOL(udp6_seq_ops);

static struct udp_seq_afinfo udp6_seq_afinfo = {
	.family		= AF_INET6,
	.udp_table	= &udp_table,
};

int __net_init udp6_proc_init(struct net *net)
{
	if (!proc_create_net_data("udp6", 0444, net->proc_net, &udp6_seq_ops,
			sizeof(struct udp_iter_state), &udp6_seq_afinfo))
		return -ENOMEM;
	return 0;
}

void udp6_proc_exit(struct net *net)
{
	remove_proc_entry("udp6", net->proc_net);
}
#endif /* CONFIG_PROC_FS */

/* ------------------------------------------------------------------------ */

struct proto udpv6_prot = {
	.name			= "UDPv6",
	.owner			= THIS_MODULE,
	.close			= udp_lib_close,
	.pre_connect		= udpv6_pre_connect,
	.connect		= ip6_datagram_connect,
	.disconnect		= udp_disconnect,
	.ioctl			= udp_ioctl,
	.init			= udpv6_init_sock,
	.destroy		= udpv6_destroy_sock,
	.setsockopt		= udpv6_setsockopt,
	.getsockopt		= udpv6_getsockopt,
	.sendmsg		= udpv6_sendmsg,
	.recvmsg		= udpv6_recvmsg,
	.release_cb		= ip6_datagram_release_cb,
	.hash			= udp_lib_hash,
	.unhash			= udp_lib_unhash,
	.rehash			= udp_v6_rehash,
	.get_port		= udp_v6_get_port,
	.memory_allocated	= &udp_memory_allocated,
	.sysctl_mem		= sysctl_udp_mem,
	.sysctl_wmem_offset     = offsetof(struct net, ipv4.sysctl_udp_wmem_min),
	.sysctl_rmem_offset     = offsetof(struct net, ipv4.sysctl_udp_rmem_min),
	.obj_size		= sizeof(struct udp6_sock),
	.h.udp_table		= &udp_table,
	.diag_destroy		= udp_abort,
};

static struct inet_protosw udpv6_protosw = {
	.type =      SOCK_DGRAM,
	.protocol =  IPPROTO_UDP,
	.prot =      &udpv6_prot,
	.ops =       &inet6_dgram_ops,
	.flags =     INET_PROTOSW_PERMANENT,
};

int __init udpv6_init(void)
{
	int ret;

	ret = inet6_add_protocol(&udpv6_protocol, IPPROTO_UDP);
	if (ret)
		goto out;

	ret = inet6_register_protosw(&udpv6_protosw);
	if (ret)
		goto out_udpv6_protocol;
out:
	return ret;

out_udpv6_protocol:
	inet6_del_protocol(&udpv6_protocol, IPPROTO_UDP);
	goto out;
}

void udpv6_exit(void)
{
	inet6_unregister_protosw(&udpv6_protosw);
	inet6_del_protocol(&udpv6_protocol, IPPROTO_UDP);
}
