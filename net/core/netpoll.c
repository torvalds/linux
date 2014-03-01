/*
 * Common framework for low-level network console, dump, and debugger code
 *
 * Sep 8 2003  Matt Mackall <mpm@selenic.com>
 *
 * based on the netconsole code from:
 *
 * Copyright (C) 2001  Ingo Molnar <mingo@redhat.com>
 * Copyright (C) 2002  Red Hat, Inc.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/string.h>
#include <linux/if_arp.h>
#include <linux/inetdevice.h>
#include <linux/inet.h>
#include <linux/interrupt.h>
#include <linux/netpoll.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/rcupdate.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/if_vlan.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <net/addrconf.h>
#include <net/ndisc.h>
#include <net/ip6_checksum.h>
#include <asm/unaligned.h>
#include <trace/events/napi.h>

/*
 * We maintain a small pool of fully-sized skbs, to make sure the
 * message gets out even in extreme OOM situations.
 */

#define MAX_UDP_CHUNK 1460
#define MAX_SKBS 32

static struct sk_buff_head skb_pool;

static atomic_t trapped;

DEFINE_STATIC_SRCU(netpoll_srcu);

#define USEC_PER_POLL	50
#define NETPOLL_RX_ENABLED  1
#define NETPOLL_RX_DROP     2

#define MAX_SKB_SIZE							\
	(sizeof(struct ethhdr) +					\
	 sizeof(struct iphdr) +						\
	 sizeof(struct udphdr) +					\
	 MAX_UDP_CHUNK)

static void zap_completion_queue(void);
static void netpoll_neigh_reply(struct sk_buff *skb, struct netpoll_info *npinfo);
static void netpoll_async_cleanup(struct work_struct *work);

static unsigned int carrier_timeout = 4;
module_param(carrier_timeout, uint, 0644);

#define np_info(np, fmt, ...)				\
	pr_info("%s: " fmt, np->name, ##__VA_ARGS__)
#define np_err(np, fmt, ...)				\
	pr_err("%s: " fmt, np->name, ##__VA_ARGS__)
#define np_notice(np, fmt, ...)				\
	pr_notice("%s: " fmt, np->name, ##__VA_ARGS__)

static void queue_process(struct work_struct *work)
{
	struct netpoll_info *npinfo =
		container_of(work, struct netpoll_info, tx_work.work);
	struct sk_buff *skb;
	unsigned long flags;

	while ((skb = skb_dequeue(&npinfo->txq))) {
		struct net_device *dev = skb->dev;
		const struct net_device_ops *ops = dev->netdev_ops;
		struct netdev_queue *txq;

		if (!netif_device_present(dev) || !netif_running(dev)) {
			__kfree_skb(skb);
			continue;
		}

		txq = netdev_get_tx_queue(dev, skb_get_queue_mapping(skb));

		local_irq_save(flags);
		__netif_tx_lock(txq, smp_processor_id());
		if (netif_xmit_frozen_or_stopped(txq) ||
		    ops->ndo_start_xmit(skb, dev) != NETDEV_TX_OK) {
			skb_queue_head(&npinfo->txq, skb);
			__netif_tx_unlock(txq);
			local_irq_restore(flags);

			schedule_delayed_work(&npinfo->tx_work, HZ/10);
			return;
		}
		__netif_tx_unlock(txq);
		local_irq_restore(flags);
	}
}

static __sum16 checksum_udp(struct sk_buff *skb, struct udphdr *uh,
			    unsigned short ulen, __be32 saddr, __be32 daddr)
{
	__wsum psum;

	if (uh->check == 0 || skb_csum_unnecessary(skb))
		return 0;

	psum = csum_tcpudp_nofold(saddr, daddr, ulen, IPPROTO_UDP, 0);

	if (skb->ip_summed == CHECKSUM_COMPLETE &&
	    !csum_fold(csum_add(psum, skb->csum)))
		return 0;

	skb->csum = psum;

	return __skb_checksum_complete(skb);
}

/*
 * Check whether delayed processing was scheduled for our NIC. If so,
 * we attempt to grab the poll lock and use ->poll() to pump the card.
 * If this fails, either we've recursed in ->poll() or it's already
 * running on another CPU.
 *
 * Note: we don't mask interrupts with this lock because we're using
 * trylock here and interrupts are already disabled in the softirq
 * case. Further, we test the poll_owner to avoid recursion on UP
 * systems where the lock doesn't exist.
 *
 * In cases where there is bi-directional communications, reading only
 * one message at a time can lead to packets being dropped by the
 * network adapter, forcing superfluous retries and possibly timeouts.
 * Thus, we set our budget to greater than 1.
 */
static int poll_one_napi(struct netpoll_info *npinfo,
			 struct napi_struct *napi, int budget)
{
	int work;

	/* net_rx_action's ->poll() invocations and our's are
	 * synchronized by this test which is only made while
	 * holding the napi->poll_lock.
	 */
	if (!test_bit(NAPI_STATE_SCHED, &napi->state))
		return budget;

	npinfo->rx_flags |= NETPOLL_RX_DROP;
	atomic_inc(&trapped);
	set_bit(NAPI_STATE_NPSVC, &napi->state);

	work = napi->poll(napi, budget);
	trace_napi_poll(napi);

	clear_bit(NAPI_STATE_NPSVC, &napi->state);
	atomic_dec(&trapped);
	npinfo->rx_flags &= ~NETPOLL_RX_DROP;

	return budget - work;
}

static void poll_napi(struct net_device *dev)
{
	struct napi_struct *napi;
	int budget = 16;

	list_for_each_entry(napi, &dev->napi_list, dev_list) {
		if (napi->poll_owner != smp_processor_id() &&
		    spin_trylock(&napi->poll_lock)) {
			budget = poll_one_napi(rcu_dereference_bh(dev->npinfo),
					       napi, budget);
			spin_unlock(&napi->poll_lock);

			if (!budget)
				break;
		}
	}
}

static void service_neigh_queue(struct netpoll_info *npi)
{
	if (npi) {
		struct sk_buff *skb;

		while ((skb = skb_dequeue(&npi->neigh_tx)))
			netpoll_neigh_reply(skb, npi);
	}
}

static void netpoll_poll_dev(struct net_device *dev)
{
	const struct net_device_ops *ops;
	struct netpoll_info *ni = rcu_dereference_bh(dev->npinfo);

	/* Don't do any rx activity if the dev_lock mutex is held
	 * the dev_open/close paths use this to block netpoll activity
	 * while changing device state
	 */
	if (down_trylock(&ni->dev_lock))
		return;

	if (!netif_running(dev)) {
		up(&ni->dev_lock);
		return;
	}

	ops = dev->netdev_ops;
	if (!ops->ndo_poll_controller) {
		up(&ni->dev_lock);
		return;
	}

	/* Process pending work on NIC */
	ops->ndo_poll_controller(dev);

	poll_napi(dev);

	up(&ni->dev_lock);

	if (dev->flags & IFF_SLAVE) {
		if (ni) {
			struct net_device *bond_dev;
			struct sk_buff *skb;
			struct netpoll_info *bond_ni;

			bond_dev = netdev_master_upper_dev_get_rcu(dev);
			bond_ni = rcu_dereference_bh(bond_dev->npinfo);
			while ((skb = skb_dequeue(&ni->neigh_tx))) {
				skb->dev = bond_dev;
				skb_queue_tail(&bond_ni->neigh_tx, skb);
			}
		}
	}

	service_neigh_queue(ni);

	zap_completion_queue();
}

void netpoll_rx_disable(struct net_device *dev)
{
	struct netpoll_info *ni;
	int idx;
	might_sleep();
	idx = srcu_read_lock(&netpoll_srcu);
	ni = srcu_dereference(dev->npinfo, &netpoll_srcu);
	if (ni)
		down(&ni->dev_lock);
	srcu_read_unlock(&netpoll_srcu, idx);
}
EXPORT_SYMBOL(netpoll_rx_disable);

void netpoll_rx_enable(struct net_device *dev)
{
	struct netpoll_info *ni;
	rcu_read_lock();
	ni = rcu_dereference(dev->npinfo);
	if (ni)
		up(&ni->dev_lock);
	rcu_read_unlock();
}
EXPORT_SYMBOL(netpoll_rx_enable);

static void refill_skbs(void)
{
	struct sk_buff *skb;
	unsigned long flags;

	spin_lock_irqsave(&skb_pool.lock, flags);
	while (skb_pool.qlen < MAX_SKBS) {
		skb = alloc_skb(MAX_SKB_SIZE, GFP_ATOMIC);
		if (!skb)
			break;

		__skb_queue_tail(&skb_pool, skb);
	}
	spin_unlock_irqrestore(&skb_pool.lock, flags);
}

static void zap_completion_queue(void)
{
	unsigned long flags;
	struct softnet_data *sd = &get_cpu_var(softnet_data);

	if (sd->completion_queue) {
		struct sk_buff *clist;

		local_irq_save(flags);
		clist = sd->completion_queue;
		sd->completion_queue = NULL;
		local_irq_restore(flags);

		while (clist != NULL) {
			struct sk_buff *skb = clist;
			clist = clist->next;
			if (skb->destructor) {
				atomic_inc(&skb->users);
				dev_kfree_skb_any(skb); /* put this one back */
			} else {
				__kfree_skb(skb);
			}
		}
	}

	put_cpu_var(softnet_data);
}

static struct sk_buff *find_skb(struct netpoll *np, int len, int reserve)
{
	int count = 0;
	struct sk_buff *skb;

	zap_completion_queue();
	refill_skbs();
repeat:

	skb = alloc_skb(len, GFP_ATOMIC);
	if (!skb)
		skb = skb_dequeue(&skb_pool);

	if (!skb) {
		if (++count < 10) {
			netpoll_poll_dev(np->dev);
			goto repeat;
		}
		return NULL;
	}

	atomic_set(&skb->users, 1);
	skb_reserve(skb, reserve);
	return skb;
}

static int netpoll_owner_active(struct net_device *dev)
{
	struct napi_struct *napi;

	list_for_each_entry(napi, &dev->napi_list, dev_list) {
		if (napi->poll_owner == smp_processor_id())
			return 1;
	}
	return 0;
}

/* call with IRQ disabled */
void netpoll_send_skb_on_dev(struct netpoll *np, struct sk_buff *skb,
			     struct net_device *dev)
{
	int status = NETDEV_TX_BUSY;
	unsigned long tries;
	const struct net_device_ops *ops = dev->netdev_ops;
	/* It is up to the caller to keep npinfo alive. */
	struct netpoll_info *npinfo;

	WARN_ON_ONCE(!irqs_disabled());

	npinfo = rcu_dereference_bh(np->dev->npinfo);
	if (!npinfo || !netif_running(dev) || !netif_device_present(dev)) {
		__kfree_skb(skb);
		return;
	}

	/* don't get messages out of order, and no recursion */
	if (skb_queue_len(&npinfo->txq) == 0 && !netpoll_owner_active(dev)) {
		struct netdev_queue *txq;

		txq = netdev_pick_tx(dev, skb, NULL);

		/* try until next clock tick */
		for (tries = jiffies_to_usecs(1)/USEC_PER_POLL;
		     tries > 0; --tries) {
			if (__netif_tx_trylock(txq)) {
				if (!netif_xmit_stopped(txq)) {
					if (vlan_tx_tag_present(skb) &&
					    !vlan_hw_offload_capable(netif_skb_features(skb),
								     skb->vlan_proto)) {
						skb = __vlan_put_tag(skb, skb->vlan_proto, vlan_tx_tag_get(skb));
						if (unlikely(!skb)) {
							/* This is actually a packet drop, but we
							 * don't want the code at the end of this
							 * function to try and re-queue a NULL skb.
							 */
							status = NETDEV_TX_OK;
							goto unlock_txq;
						}
						skb->vlan_tci = 0;
					}

					status = ops->ndo_start_xmit(skb, dev);
					if (status == NETDEV_TX_OK)
						txq_trans_update(txq);
				}
			unlock_txq:
				__netif_tx_unlock(txq);

				if (status == NETDEV_TX_OK)
					break;

			}

			/* tickle device maybe there is some cleanup */
			netpoll_poll_dev(np->dev);

			udelay(USEC_PER_POLL);
		}

		WARN_ONCE(!irqs_disabled(),
			"netpoll_send_skb_on_dev(): %s enabled interrupts in poll (%pF)\n",
			dev->name, ops->ndo_start_xmit);

	}

	if (status != NETDEV_TX_OK) {
		skb_queue_tail(&npinfo->txq, skb);
		schedule_delayed_work(&npinfo->tx_work,0);
	}
}
EXPORT_SYMBOL(netpoll_send_skb_on_dev);

void netpoll_send_udp(struct netpoll *np, const char *msg, int len)
{
	int total_len, ip_len, udp_len;
	struct sk_buff *skb;
	struct udphdr *udph;
	struct iphdr *iph;
	struct ethhdr *eth;
	static atomic_t ip_ident;
	struct ipv6hdr *ip6h;

	udp_len = len + sizeof(*udph);
	if (np->ipv6)
		ip_len = udp_len + sizeof(*ip6h);
	else
		ip_len = udp_len + sizeof(*iph);

	total_len = ip_len + LL_RESERVED_SPACE(np->dev);

	skb = find_skb(np, total_len + np->dev->needed_tailroom,
		       total_len - len);
	if (!skb)
		return;

	skb_copy_to_linear_data(skb, msg, len);
	skb_put(skb, len);

	skb_push(skb, sizeof(*udph));
	skb_reset_transport_header(skb);
	udph = udp_hdr(skb);
	udph->source = htons(np->local_port);
	udph->dest = htons(np->remote_port);
	udph->len = htons(udp_len);

	if (np->ipv6) {
		udph->check = 0;
		udph->check = csum_ipv6_magic(&np->local_ip.in6,
					      &np->remote_ip.in6,
					      udp_len, IPPROTO_UDP,
					      csum_partial(udph, udp_len, 0));
		if (udph->check == 0)
			udph->check = CSUM_MANGLED_0;

		skb_push(skb, sizeof(*ip6h));
		skb_reset_network_header(skb);
		ip6h = ipv6_hdr(skb);

		/* ip6h->version = 6; ip6h->priority = 0; */
		put_unaligned(0x60, (unsigned char *)ip6h);
		ip6h->flow_lbl[0] = 0;
		ip6h->flow_lbl[1] = 0;
		ip6h->flow_lbl[2] = 0;

		ip6h->payload_len = htons(sizeof(struct udphdr) + len);
		ip6h->nexthdr = IPPROTO_UDP;
		ip6h->hop_limit = 32;
		ip6h->saddr = np->local_ip.in6;
		ip6h->daddr = np->remote_ip.in6;

		eth = (struct ethhdr *) skb_push(skb, ETH_HLEN);
		skb_reset_mac_header(skb);
		skb->protocol = eth->h_proto = htons(ETH_P_IPV6);
	} else {
		udph->check = 0;
		udph->check = csum_tcpudp_magic(np->local_ip.ip,
						np->remote_ip.ip,
						udp_len, IPPROTO_UDP,
						csum_partial(udph, udp_len, 0));
		if (udph->check == 0)
			udph->check = CSUM_MANGLED_0;

		skb_push(skb, sizeof(*iph));
		skb_reset_network_header(skb);
		iph = ip_hdr(skb);

		/* iph->version = 4; iph->ihl = 5; */
		put_unaligned(0x45, (unsigned char *)iph);
		iph->tos      = 0;
		put_unaligned(htons(ip_len), &(iph->tot_len));
		iph->id       = htons(atomic_inc_return(&ip_ident));
		iph->frag_off = 0;
		iph->ttl      = 64;
		iph->protocol = IPPROTO_UDP;
		iph->check    = 0;
		put_unaligned(np->local_ip.ip, &(iph->saddr));
		put_unaligned(np->remote_ip.ip, &(iph->daddr));
		iph->check    = ip_fast_csum((unsigned char *)iph, iph->ihl);

		eth = (struct ethhdr *) skb_push(skb, ETH_HLEN);
		skb_reset_mac_header(skb);
		skb->protocol = eth->h_proto = htons(ETH_P_IP);
	}

	ether_addr_copy(eth->h_source, np->dev->dev_addr);
	ether_addr_copy(eth->h_dest, np->remote_mac);

	skb->dev = np->dev;

	netpoll_send_skb(np, skb);
}
EXPORT_SYMBOL(netpoll_send_udp);

static void netpoll_neigh_reply(struct sk_buff *skb, struct netpoll_info *npinfo)
{
	int size, type = ARPOP_REPLY;
	__be32 sip, tip;
	unsigned char *sha;
	struct sk_buff *send_skb;
	struct netpoll *np, *tmp;
	unsigned long flags;
	int hlen, tlen;
	int hits = 0, proto;

	if (list_empty(&npinfo->rx_np))
		return;

	/* Before checking the packet, we do some early
	   inspection whether this is interesting at all */
	spin_lock_irqsave(&npinfo->rx_lock, flags);
	list_for_each_entry_safe(np, tmp, &npinfo->rx_np, rx) {
		if (np->dev == skb->dev)
			hits++;
	}
	spin_unlock_irqrestore(&npinfo->rx_lock, flags);

	/* No netpoll struct is using this dev */
	if (!hits)
		return;

	proto = ntohs(eth_hdr(skb)->h_proto);
	if (proto == ETH_P_ARP) {
		struct arphdr *arp;
		unsigned char *arp_ptr;
		/* No arp on this interface */
		if (skb->dev->flags & IFF_NOARP)
			return;

		if (!pskb_may_pull(skb, arp_hdr_len(skb->dev)))
			return;

		skb_reset_network_header(skb);
		skb_reset_transport_header(skb);
		arp = arp_hdr(skb);

		if ((arp->ar_hrd != htons(ARPHRD_ETHER) &&
		     arp->ar_hrd != htons(ARPHRD_IEEE802)) ||
		    arp->ar_pro != htons(ETH_P_IP) ||
		    arp->ar_op != htons(ARPOP_REQUEST))
			return;

		arp_ptr = (unsigned char *)(arp+1);
		/* save the location of the src hw addr */
		sha = arp_ptr;
		arp_ptr += skb->dev->addr_len;
		memcpy(&sip, arp_ptr, 4);
		arp_ptr += 4;
		/* If we actually cared about dst hw addr,
		   it would get copied here */
		arp_ptr += skb->dev->addr_len;
		memcpy(&tip, arp_ptr, 4);

		/* Should we ignore arp? */
		if (ipv4_is_loopback(tip) || ipv4_is_multicast(tip))
			return;

		size = arp_hdr_len(skb->dev);

		spin_lock_irqsave(&npinfo->rx_lock, flags);
		list_for_each_entry_safe(np, tmp, &npinfo->rx_np, rx) {
			if (tip != np->local_ip.ip)
				continue;

			hlen = LL_RESERVED_SPACE(np->dev);
			tlen = np->dev->needed_tailroom;
			send_skb = find_skb(np, size + hlen + tlen, hlen);
			if (!send_skb)
				continue;

			skb_reset_network_header(send_skb);
			arp = (struct arphdr *) skb_put(send_skb, size);
			send_skb->dev = skb->dev;
			send_skb->protocol = htons(ETH_P_ARP);

			/* Fill the device header for the ARP frame */
			if (dev_hard_header(send_skb, skb->dev, ETH_P_ARP,
					    sha, np->dev->dev_addr,
					    send_skb->len) < 0) {
				kfree_skb(send_skb);
				continue;
			}

			/*
			 * Fill out the arp protocol part.
			 *
			 * we only support ethernet device type,
			 * which (according to RFC 1390) should
			 * always equal 1 (Ethernet).
			 */

			arp->ar_hrd = htons(np->dev->type);
			arp->ar_pro = htons(ETH_P_IP);
			arp->ar_hln = np->dev->addr_len;
			arp->ar_pln = 4;
			arp->ar_op = htons(type);

			arp_ptr = (unsigned char *)(arp + 1);
			memcpy(arp_ptr, np->dev->dev_addr, np->dev->addr_len);
			arp_ptr += np->dev->addr_len;
			memcpy(arp_ptr, &tip, 4);
			arp_ptr += 4;
			memcpy(arp_ptr, sha, np->dev->addr_len);
			arp_ptr += np->dev->addr_len;
			memcpy(arp_ptr, &sip, 4);

			netpoll_send_skb(np, send_skb);

			/* If there are several rx_skb_hooks for the same
			 * address we're fine by sending a single reply
			 */
			break;
		}
		spin_unlock_irqrestore(&npinfo->rx_lock, flags);
	} else if( proto == ETH_P_IPV6) {
#if IS_ENABLED(CONFIG_IPV6)
		struct nd_msg *msg;
		u8 *lladdr = NULL;
		struct ipv6hdr *hdr;
		struct icmp6hdr *icmp6h;
		const struct in6_addr *saddr;
		const struct in6_addr *daddr;
		struct inet6_dev *in6_dev = NULL;
		struct in6_addr *target;

		in6_dev = in6_dev_get(skb->dev);
		if (!in6_dev || !in6_dev->cnf.accept_ra)
			return;

		if (!pskb_may_pull(skb, skb->len))
			return;

		msg = (struct nd_msg *)skb_transport_header(skb);

		__skb_push(skb, skb->data - skb_transport_header(skb));

		if (ipv6_hdr(skb)->hop_limit != 255)
			return;
		if (msg->icmph.icmp6_code != 0)
			return;
		if (msg->icmph.icmp6_type != NDISC_NEIGHBOUR_SOLICITATION)
			return;

		saddr = &ipv6_hdr(skb)->saddr;
		daddr = &ipv6_hdr(skb)->daddr;

		size = sizeof(struct icmp6hdr) + sizeof(struct in6_addr);

		spin_lock_irqsave(&npinfo->rx_lock, flags);
		list_for_each_entry_safe(np, tmp, &npinfo->rx_np, rx) {
			if (!ipv6_addr_equal(daddr, &np->local_ip.in6))
				continue;

			hlen = LL_RESERVED_SPACE(np->dev);
			tlen = np->dev->needed_tailroom;
			send_skb = find_skb(np, size + hlen + tlen, hlen);
			if (!send_skb)
				continue;

			send_skb->protocol = htons(ETH_P_IPV6);
			send_skb->dev = skb->dev;

			skb_reset_network_header(send_skb);
			hdr = (struct ipv6hdr *) skb_put(send_skb, sizeof(struct ipv6hdr));
			*(__be32*)hdr = htonl(0x60000000);
			hdr->payload_len = htons(size);
			hdr->nexthdr = IPPROTO_ICMPV6;
			hdr->hop_limit = 255;
			hdr->saddr = *saddr;
			hdr->daddr = *daddr;

			icmp6h = (struct icmp6hdr *) skb_put(send_skb, sizeof(struct icmp6hdr));
			icmp6h->icmp6_type = NDISC_NEIGHBOUR_ADVERTISEMENT;
			icmp6h->icmp6_router = 0;
			icmp6h->icmp6_solicited = 1;

			target = (struct in6_addr *) skb_put(send_skb, sizeof(struct in6_addr));
			*target = msg->target;
			icmp6h->icmp6_cksum = csum_ipv6_magic(saddr, daddr, size,
							      IPPROTO_ICMPV6,
							      csum_partial(icmp6h,
									   size, 0));

			if (dev_hard_header(send_skb, skb->dev, ETH_P_IPV6,
					    lladdr, np->dev->dev_addr,
					    send_skb->len) < 0) {
				kfree_skb(send_skb);
				continue;
			}

			netpoll_send_skb(np, send_skb);

			/* If there are several rx_skb_hooks for the same
			 * address, we're fine by sending a single reply
			 */
			break;
		}
		spin_unlock_irqrestore(&npinfo->rx_lock, flags);
#endif
	}
}

static bool pkt_is_ns(struct sk_buff *skb)
{
	struct nd_msg *msg;
	struct ipv6hdr *hdr;

	if (skb->protocol != htons(ETH_P_ARP))
		return false;
	if (!pskb_may_pull(skb, sizeof(struct ipv6hdr) + sizeof(struct nd_msg)))
		return false;

	msg = (struct nd_msg *)skb_transport_header(skb);
	__skb_push(skb, skb->data - skb_transport_header(skb));
	hdr = ipv6_hdr(skb);

	if (hdr->nexthdr != IPPROTO_ICMPV6)
		return false;
	if (hdr->hop_limit != 255)
		return false;
	if (msg->icmph.icmp6_code != 0)
		return false;
	if (msg->icmph.icmp6_type != NDISC_NEIGHBOUR_SOLICITATION)
		return false;

	return true;
}

int __netpoll_rx(struct sk_buff *skb, struct netpoll_info *npinfo)
{
	int proto, len, ulen, data_len;
	int hits = 0, offset;
	const struct iphdr *iph;
	struct udphdr *uh;
	struct netpoll *np, *tmp;
	uint16_t source;

	if (list_empty(&npinfo->rx_np))
		goto out;

	if (skb->dev->type != ARPHRD_ETHER)
		goto out;

	/* check if netpoll clients need ARP */
	if (skb->protocol == htons(ETH_P_ARP) && atomic_read(&trapped)) {
		skb_queue_tail(&npinfo->neigh_tx, skb);
		return 1;
	} else if (pkt_is_ns(skb) && atomic_read(&trapped)) {
		skb_queue_tail(&npinfo->neigh_tx, skb);
		return 1;
	}

	if (skb->protocol == cpu_to_be16(ETH_P_8021Q)) {
		skb = vlan_untag(skb);
		if (unlikely(!skb))
			goto out;
	}

	proto = ntohs(eth_hdr(skb)->h_proto);
	if (proto != ETH_P_IP && proto != ETH_P_IPV6)
		goto out;
	if (skb->pkt_type == PACKET_OTHERHOST)
		goto out;
	if (skb_shared(skb))
		goto out;

	if (proto == ETH_P_IP) {
		if (!pskb_may_pull(skb, sizeof(struct iphdr)))
			goto out;
		iph = (struct iphdr *)skb->data;
		if (iph->ihl < 5 || iph->version != 4)
			goto out;
		if (!pskb_may_pull(skb, iph->ihl*4))
			goto out;
		iph = (struct iphdr *)skb->data;
		if (ip_fast_csum((u8 *)iph, iph->ihl) != 0)
			goto out;

		len = ntohs(iph->tot_len);
		if (skb->len < len || len < iph->ihl*4)
			goto out;

		/*
		 * Our transport medium may have padded the buffer out.
		 * Now We trim to the true length of the frame.
		 */
		if (pskb_trim_rcsum(skb, len))
			goto out;

		iph = (struct iphdr *)skb->data;
		if (iph->protocol != IPPROTO_UDP)
			goto out;

		len -= iph->ihl*4;
		uh = (struct udphdr *)(((char *)iph) + iph->ihl*4);
		offset = (unsigned char *)(uh + 1) - skb->data;
		ulen = ntohs(uh->len);
		data_len = skb->len - offset;
		source = ntohs(uh->source);

		if (ulen != len)
			goto out;
		if (checksum_udp(skb, uh, ulen, iph->saddr, iph->daddr))
			goto out;
		list_for_each_entry_safe(np, tmp, &npinfo->rx_np, rx) {
			if (np->local_ip.ip && np->local_ip.ip != iph->daddr)
				continue;
			if (np->remote_ip.ip && np->remote_ip.ip != iph->saddr)
				continue;
			if (np->local_port && np->local_port != ntohs(uh->dest))
				continue;

			np->rx_skb_hook(np, source, skb, offset, data_len);
			hits++;
		}
	} else {
#if IS_ENABLED(CONFIG_IPV6)
		const struct ipv6hdr *ip6h;

		if (!pskb_may_pull(skb, sizeof(struct ipv6hdr)))
			goto out;
		ip6h = (struct ipv6hdr *)skb->data;
		if (ip6h->version != 6)
			goto out;
		len = ntohs(ip6h->payload_len);
		if (!len)
			goto out;
		if (len + sizeof(struct ipv6hdr) > skb->len)
			goto out;
		if (pskb_trim_rcsum(skb, len + sizeof(struct ipv6hdr)))
			goto out;
		ip6h = ipv6_hdr(skb);
		if (!pskb_may_pull(skb, sizeof(struct udphdr)))
			goto out;
		uh = udp_hdr(skb);
		offset = (unsigned char *)(uh + 1) - skb->data;
		ulen = ntohs(uh->len);
		data_len = skb->len - offset;
		source = ntohs(uh->source);
		if (ulen != skb->len)
			goto out;
		if (udp6_csum_init(skb, uh, IPPROTO_UDP))
			goto out;
		list_for_each_entry_safe(np, tmp, &npinfo->rx_np, rx) {
			if (!ipv6_addr_equal(&np->local_ip.in6, &ip6h->daddr))
				continue;
			if (!ipv6_addr_equal(&np->remote_ip.in6, &ip6h->saddr))
				continue;
			if (np->local_port && np->local_port != ntohs(uh->dest))
				continue;

			np->rx_skb_hook(np, source, skb, offset, data_len);
			hits++;
		}
#endif
	}

	if (!hits)
		goto out;

	kfree_skb(skb);
	return 1;

out:
	if (atomic_read(&trapped)) {
		kfree_skb(skb);
		return 1;
	}

	return 0;
}

void netpoll_print_options(struct netpoll *np)
{
	np_info(np, "local port %d\n", np->local_port);
	if (np->ipv6)
		np_info(np, "local IPv6 address %pI6c\n", &np->local_ip.in6);
	else
		np_info(np, "local IPv4 address %pI4\n", &np->local_ip.ip);
	np_info(np, "interface '%s'\n", np->dev_name);
	np_info(np, "remote port %d\n", np->remote_port);
	if (np->ipv6)
		np_info(np, "remote IPv6 address %pI6c\n", &np->remote_ip.in6);
	else
		np_info(np, "remote IPv4 address %pI4\n", &np->remote_ip.ip);
	np_info(np, "remote ethernet address %pM\n", np->remote_mac);
}
EXPORT_SYMBOL(netpoll_print_options);

static int netpoll_parse_ip_addr(const char *str, union inet_addr *addr)
{
	const char *end;

	if (!strchr(str, ':') &&
	    in4_pton(str, -1, (void *)addr, -1, &end) > 0) {
		if (!*end)
			return 0;
	}
	if (in6_pton(str, -1, addr->in6.s6_addr, -1, &end) > 0) {
#if IS_ENABLED(CONFIG_IPV6)
		if (!*end)
			return 1;
#else
		return -1;
#endif
	}
	return -1;
}

int netpoll_parse_options(struct netpoll *np, char *opt)
{
	char *cur=opt, *delim;
	int ipv6;
	bool ipversion_set = false;

	if (*cur != '@') {
		if ((delim = strchr(cur, '@')) == NULL)
			goto parse_failed;
		*delim = 0;
		if (kstrtou16(cur, 10, &np->local_port))
			goto parse_failed;
		cur = delim;
	}
	cur++;

	if (*cur != '/') {
		ipversion_set = true;
		if ((delim = strchr(cur, '/')) == NULL)
			goto parse_failed;
		*delim = 0;
		ipv6 = netpoll_parse_ip_addr(cur, &np->local_ip);
		if (ipv6 < 0)
			goto parse_failed;
		else
			np->ipv6 = (bool)ipv6;
		cur = delim;
	}
	cur++;

	if (*cur != ',') {
		/* parse out dev name */
		if ((delim = strchr(cur, ',')) == NULL)
			goto parse_failed;
		*delim = 0;
		strlcpy(np->dev_name, cur, sizeof(np->dev_name));
		cur = delim;
	}
	cur++;

	if (*cur != '@') {
		/* dst port */
		if ((delim = strchr(cur, '@')) == NULL)
			goto parse_failed;
		*delim = 0;
		if (*cur == ' ' || *cur == '\t')
			np_info(np, "warning: whitespace is not allowed\n");
		if (kstrtou16(cur, 10, &np->remote_port))
			goto parse_failed;
		cur = delim;
	}
	cur++;

	/* dst ip */
	if ((delim = strchr(cur, '/')) == NULL)
		goto parse_failed;
	*delim = 0;
	ipv6 = netpoll_parse_ip_addr(cur, &np->remote_ip);
	if (ipv6 < 0)
		goto parse_failed;
	else if (ipversion_set && np->ipv6 != (bool)ipv6)
		goto parse_failed;
	else
		np->ipv6 = (bool)ipv6;
	cur = delim + 1;

	if (*cur != 0) {
		/* MAC address */
		if (!mac_pton(cur, np->remote_mac))
			goto parse_failed;
	}

	netpoll_print_options(np);

	return 0;

 parse_failed:
	np_info(np, "couldn't parse config at '%s'!\n", cur);
	return -1;
}
EXPORT_SYMBOL(netpoll_parse_options);

int __netpoll_setup(struct netpoll *np, struct net_device *ndev, gfp_t gfp)
{
	struct netpoll_info *npinfo;
	const struct net_device_ops *ops;
	unsigned long flags;
	int err;

	np->dev = ndev;
	strlcpy(np->dev_name, ndev->name, IFNAMSIZ);
	INIT_WORK(&np->cleanup_work, netpoll_async_cleanup);

	if ((ndev->priv_flags & IFF_DISABLE_NETPOLL) ||
	    !ndev->netdev_ops->ndo_poll_controller) {
		np_err(np, "%s doesn't support polling, aborting\n",
		       np->dev_name);
		err = -ENOTSUPP;
		goto out;
	}

	if (!ndev->npinfo) {
		npinfo = kmalloc(sizeof(*npinfo), gfp);
		if (!npinfo) {
			err = -ENOMEM;
			goto out;
		}

		npinfo->rx_flags = 0;
		INIT_LIST_HEAD(&npinfo->rx_np);

		spin_lock_init(&npinfo->rx_lock);
		sema_init(&npinfo->dev_lock, 1);
		skb_queue_head_init(&npinfo->neigh_tx);
		skb_queue_head_init(&npinfo->txq);
		INIT_DELAYED_WORK(&npinfo->tx_work, queue_process);

		atomic_set(&npinfo->refcnt, 1);

		ops = np->dev->netdev_ops;
		if (ops->ndo_netpoll_setup) {
			err = ops->ndo_netpoll_setup(ndev, npinfo, gfp);
			if (err)
				goto free_npinfo;
		}
	} else {
		npinfo = rtnl_dereference(ndev->npinfo);
		atomic_inc(&npinfo->refcnt);
	}

	npinfo->netpoll = np;

	if (np->rx_skb_hook) {
		spin_lock_irqsave(&npinfo->rx_lock, flags);
		npinfo->rx_flags |= NETPOLL_RX_ENABLED;
		list_add_tail(&np->rx, &npinfo->rx_np);
		spin_unlock_irqrestore(&npinfo->rx_lock, flags);
	}

	/* last thing to do is link it to the net device structure */
	rcu_assign_pointer(ndev->npinfo, npinfo);

	return 0;

free_npinfo:
	kfree(npinfo);
out:
	return err;
}
EXPORT_SYMBOL_GPL(__netpoll_setup);

int netpoll_setup(struct netpoll *np)
{
	struct net_device *ndev = NULL;
	struct in_device *in_dev;
	int err;

	rtnl_lock();
	if (np->dev_name) {
		struct net *net = current->nsproxy->net_ns;
		ndev = __dev_get_by_name(net, np->dev_name);
	}
	if (!ndev) {
		np_err(np, "%s doesn't exist, aborting\n", np->dev_name);
		err = -ENODEV;
		goto unlock;
	}
	dev_hold(ndev);

	if (netdev_master_upper_dev_get(ndev)) {
		np_err(np, "%s is a slave device, aborting\n", np->dev_name);
		err = -EBUSY;
		goto put;
	}

	if (!netif_running(ndev)) {
		unsigned long atmost, atleast;

		np_info(np, "device %s not up yet, forcing it\n", np->dev_name);

		err = dev_open(ndev);

		if (err) {
			np_err(np, "failed to open %s\n", ndev->name);
			goto put;
		}

		rtnl_unlock();
		atleast = jiffies + HZ/10;
		atmost = jiffies + carrier_timeout * HZ;
		while (!netif_carrier_ok(ndev)) {
			if (time_after(jiffies, atmost)) {
				np_notice(np, "timeout waiting for carrier\n");
				break;
			}
			msleep(1);
		}

		/* If carrier appears to come up instantly, we don't
		 * trust it and pause so that we don't pump all our
		 * queued console messages into the bitbucket.
		 */

		if (time_before(jiffies, atleast)) {
			np_notice(np, "carrier detect appears untrustworthy, waiting 4 seconds\n");
			msleep(4000);
		}
		rtnl_lock();
	}

	if (!np->local_ip.ip) {
		if (!np->ipv6) {
			in_dev = __in_dev_get_rtnl(ndev);

			if (!in_dev || !in_dev->ifa_list) {
				np_err(np, "no IP address for %s, aborting\n",
				       np->dev_name);
				err = -EDESTADDRREQ;
				goto put;
			}

			np->local_ip.ip = in_dev->ifa_list->ifa_local;
			np_info(np, "local IP %pI4\n", &np->local_ip.ip);
		} else {
#if IS_ENABLED(CONFIG_IPV6)
			struct inet6_dev *idev;

			err = -EDESTADDRREQ;
			idev = __in6_dev_get(ndev);
			if (idev) {
				struct inet6_ifaddr *ifp;

				read_lock_bh(&idev->lock);
				list_for_each_entry(ifp, &idev->addr_list, if_list) {
					if (ipv6_addr_type(&ifp->addr) & IPV6_ADDR_LINKLOCAL)
						continue;
					np->local_ip.in6 = ifp->addr;
					err = 0;
					break;
				}
				read_unlock_bh(&idev->lock);
			}
			if (err) {
				np_err(np, "no IPv6 address for %s, aborting\n",
				       np->dev_name);
				goto put;
			} else
				np_info(np, "local IPv6 %pI6c\n", &np->local_ip.in6);
#else
			np_err(np, "IPv6 is not supported %s, aborting\n",
			       np->dev_name);
			err = -EINVAL;
			goto put;
#endif
		}
	}

	/* fill up the skb queue */
	refill_skbs();

	err = __netpoll_setup(np, ndev, GFP_KERNEL);
	if (err)
		goto put;

	rtnl_unlock();
	return 0;

put:
	dev_put(ndev);
unlock:
	rtnl_unlock();
	return err;
}
EXPORT_SYMBOL(netpoll_setup);

static int __init netpoll_init(void)
{
	skb_queue_head_init(&skb_pool);
	return 0;
}
core_initcall(netpoll_init);

static void rcu_cleanup_netpoll_info(struct rcu_head *rcu_head)
{
	struct netpoll_info *npinfo =
			container_of(rcu_head, struct netpoll_info, rcu);

	skb_queue_purge(&npinfo->neigh_tx);
	skb_queue_purge(&npinfo->txq);

	/* we can't call cancel_delayed_work_sync here, as we are in softirq */
	cancel_delayed_work(&npinfo->tx_work);

	/* clean after last, unfinished work */
	__skb_queue_purge(&npinfo->txq);
	/* now cancel it again */
	cancel_delayed_work(&npinfo->tx_work);
	kfree(npinfo);
}

void __netpoll_cleanup(struct netpoll *np)
{
	struct netpoll_info *npinfo;
	unsigned long flags;

	/* rtnl_dereference would be preferable here but
	 * rcu_cleanup_netpoll path can put us in here safely without
	 * holding the rtnl, so plain rcu_dereference it is
	 */
	npinfo = rtnl_dereference(np->dev->npinfo);
	if (!npinfo)
		return;

	if (!list_empty(&npinfo->rx_np)) {
		spin_lock_irqsave(&npinfo->rx_lock, flags);
		list_del(&np->rx);
		if (list_empty(&npinfo->rx_np))
			npinfo->rx_flags &= ~NETPOLL_RX_ENABLED;
		spin_unlock_irqrestore(&npinfo->rx_lock, flags);
	}

	synchronize_srcu(&netpoll_srcu);

	if (atomic_dec_and_test(&npinfo->refcnt)) {
		const struct net_device_ops *ops;

		ops = np->dev->netdev_ops;
		if (ops->ndo_netpoll_cleanup)
			ops->ndo_netpoll_cleanup(np->dev);

		rcu_assign_pointer(np->dev->npinfo, NULL);
		call_rcu_bh(&npinfo->rcu, rcu_cleanup_netpoll_info);
	}
}
EXPORT_SYMBOL_GPL(__netpoll_cleanup);

static void netpoll_async_cleanup(struct work_struct *work)
{
	struct netpoll *np = container_of(work, struct netpoll, cleanup_work);

	rtnl_lock();
	__netpoll_cleanup(np);
	rtnl_unlock();
	kfree(np);
}

void __netpoll_free_async(struct netpoll *np)
{
	schedule_work(&np->cleanup_work);
}
EXPORT_SYMBOL_GPL(__netpoll_free_async);

void netpoll_cleanup(struct netpoll *np)
{
	rtnl_lock();
	if (!np->dev)
		goto out;
	__netpoll_cleanup(np);
	dev_put(np->dev);
	np->dev = NULL;
out:
	rtnl_unlock();
}
EXPORT_SYMBOL(netpoll_cleanup);

int netpoll_trap(void)
{
	return atomic_read(&trapped);
}
EXPORT_SYMBOL(netpoll_trap);

void netpoll_set_trap(int trap)
{
	if (trap)
		atomic_inc(&trapped);
	else
		atomic_dec(&trapped);
}
EXPORT_SYMBOL(netpoll_set_trap);
