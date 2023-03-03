// SPDX-License-Identifier: GPL-2.0-only
#include <net/netfilter/nf_tproxy.h>
#include <linux/module.h>
#include <net/inet6_hashtables.h>
#include <net/addrconf.h>
#include <net/udp.h>
#include <net/tcp.h>

const struct in6_addr *
nf_tproxy_laddr6(struct sk_buff *skb, const struct in6_addr *user_laddr,
	      const struct in6_addr *daddr)
{
	struct inet6_dev *indev;
	struct inet6_ifaddr *ifa;
	struct in6_addr *laddr;

	if (!ipv6_addr_any(user_laddr))
		return user_laddr;
	laddr = NULL;

	indev = __in6_dev_get(skb->dev);
	if (indev) {
		read_lock_bh(&indev->lock);
		list_for_each_entry(ifa, &indev->addr_list, if_list) {
			if (ifa->flags & (IFA_F_TENTATIVE | IFA_F_DEPRECATED))
				continue;

			laddr = &ifa->addr;
			break;
		}
		read_unlock_bh(&indev->lock);
	}

	return laddr ? laddr : daddr;
}
EXPORT_SYMBOL_GPL(nf_tproxy_laddr6);

struct sock *
nf_tproxy_handle_time_wait6(struct sk_buff *skb, int tproto, int thoff,
			 struct net *net,
			 const struct in6_addr *laddr,
			 const __be16 lport,
			 struct sock *sk)
{
	const struct ipv6hdr *iph = ipv6_hdr(skb);
	struct tcphdr _hdr, *hp;

	hp = skb_header_pointer(skb, thoff, sizeof(_hdr), &_hdr);
	if (hp == NULL) {
		inet_twsk_put(inet_twsk(sk));
		return NULL;
	}

	if (hp->syn && !hp->rst && !hp->ack && !hp->fin) {
		/* SYN to a TIME_WAIT socket, we'd rather redirect it
		 * to a listener socket if there's one */
		struct sock *sk2;

		sk2 = nf_tproxy_get_sock_v6(net, skb, thoff, tproto,
					    &iph->saddr,
					    nf_tproxy_laddr6(skb, laddr, &iph->daddr),
					    hp->source,
					    lport ? lport : hp->dest,
					    skb->dev, NF_TPROXY_LOOKUP_LISTENER);
		if (sk2) {
			nf_tproxy_twsk_deschedule_put(inet_twsk(sk));
			sk = sk2;
		}
	}

	return sk;
}
EXPORT_SYMBOL_GPL(nf_tproxy_handle_time_wait6);

struct sock *
nf_tproxy_get_sock_v6(struct net *net, struct sk_buff *skb, int thoff,
		      const u8 protocol,
		      const struct in6_addr *saddr, const struct in6_addr *daddr,
		      const __be16 sport, const __be16 dport,
		      const struct net_device *in,
		      const enum nf_tproxy_lookup_t lookup_type)
{
	struct sock *sk;

	switch (protocol) {
	case IPPROTO_TCP: {
		struct tcphdr _hdr, *hp;

		hp = skb_header_pointer(skb, thoff,
					sizeof(struct tcphdr), &_hdr);
		if (hp == NULL)
			return NULL;

		switch (lookup_type) {
		case NF_TPROXY_LOOKUP_LISTENER:
			sk = inet6_lookup_listener(net, &tcp_hashinfo, skb,
						   thoff + __tcp_hdrlen(hp),
						   saddr, sport,
						   daddr, ntohs(dport),
						   in->ifindex, 0);

			if (sk && !refcount_inc_not_zero(&sk->sk_refcnt))
				sk = NULL;
			/* NOTE: we return listeners even if bound to
			 * 0.0.0.0, those are filtered out in
			 * xt_socket, since xt_TPROXY needs 0 bound
			 * listeners too
			 */
			break;
		case NF_TPROXY_LOOKUP_ESTABLISHED:
			sk = __inet6_lookup_established(net, &tcp_hashinfo,
							saddr, sport, daddr, ntohs(dport),
							in->ifindex, 0);
			break;
		default:
			BUG();
		}
		break;
		}
	case IPPROTO_UDP:
		sk = udp6_lib_lookup(net, saddr, sport, daddr, dport,
				     in->ifindex);
		if (sk) {
			int connected = (sk->sk_state == TCP_ESTABLISHED);
			int wildcard = ipv6_addr_any(&sk->sk_v6_rcv_saddr);

			/* NOTE: we return listeners even if bound to
			 * 0.0.0.0, those are filtered out in
			 * xt_socket, since xt_TPROXY needs 0 bound
			 * listeners too
			 */
			if ((lookup_type == NF_TPROXY_LOOKUP_ESTABLISHED && (!connected || wildcard)) ||
			    (lookup_type == NF_TPROXY_LOOKUP_LISTENER && connected)) {
				sock_put(sk);
				sk = NULL;
			}
		}
		break;
	default:
		WARN_ON(1);
		sk = NULL;
	}

	pr_debug("tproxy socket lookup: proto %u %pI6:%u -> %pI6:%u, lookup type: %d, sock %p\n",
		 protocol, saddr, ntohs(sport), daddr, ntohs(dport), lookup_type, sk);

	return sk;
}
EXPORT_SYMBOL_GPL(nf_tproxy_get_sock_v6);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Balazs Scheidler, Krisztian Kovacs");
MODULE_DESCRIPTION("Netfilter IPv6 transparent proxy support");
