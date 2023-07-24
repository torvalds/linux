// SPDX-License-Identifier: GPL-2.0-only
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
#include <linux/kconfig.h>

/*
 * We maintain a small pool of fully-sized skbs, to make sure the
 * message gets out even in extreme OOM situations.
 */

#define MAX_UDP_CHUNK 1460
#define MAX_SKBS 32

static struct sk_buff_head skb_pool;

DEFINE_STATIC_SRCU(netpoll_srcu);

#define USEC_PER_POLL	50

#define MAX_SKB_SIZE							\
	(sizeof(struct ethhdr) +					\
	 sizeof(struct iphdr) +						\
	 sizeof(struct udphdr) +					\
	 MAX_UDP_CHUNK)

static void zap_completion_queue(void);

static unsigned int carrier_timeout = 4;
module_param(carrier_timeout, uint, 0644);

#define np_info(np, fmt, ...)				\
	pr_info("%s: " fmt, np->name, ##__VA_ARGS__)
#define np_err(np, fmt, ...)				\
	pr_err("%s: " fmt, np->name, ##__VA_ARGS__)
#define np_notice(np, fmt, ...)				\
	pr_notice("%s: " fmt, np->name, ##__VA_ARGS__)

static netdev_tx_t netpoll_start_xmit(struct sk_buff *skb,
				      struct net_device *dev,
				      struct netdev_queue *txq)
{
	netdev_tx_t status = NETDEV_TX_OK;
	netdev_features_t features;

	features = netif_skb_features(skb);

	if (skb_vlan_tag_present(skb) &&
	    !vlan_hw_offload_capable(features, skb->vlan_proto)) {
		skb = __vlan_hwaccel_push_inside(skb);
		if (unlikely(!skb)) {
			/* This is actually a packet drop, but we
			 * don't want the code that calls this
			 * function to try and operate on a NULL skb.
			 */
			goto out;
		}
	}

	status = netdev_start_xmit(skb, dev, txq, false);

out:
	return status;
}

static void queue_process(struct work_struct *work)
{
	struct netpoll_info *npinfo =
		container_of(work, struct netpoll_info, tx_work.work);
	struct sk_buff *skb;
	unsigned long flags;

	while ((skb = skb_dequeue(&npinfo->txq))) {
		struct net_device *dev = skb->dev;
		struct netdev_queue *txq;
		unsigned int q_index;

		if (!netif_device_present(dev) || !netif_running(dev)) {
			kfree_skb(skb);
			continue;
		}

		local_irq_save(flags);
		/* check if skb->queue_mapping is still valid */
		q_index = skb_get_queue_mapping(skb);
		if (unlikely(q_index >= dev->real_num_tx_queues)) {
			q_index = q_index % dev->real_num_tx_queues;
			skb_set_queue_mapping(skb, q_index);
		}
		txq = netdev_get_tx_queue(dev, q_index);
		HARD_TX_LOCK(dev, txq, smp_processor_id());
		if (netif_xmit_frozen_or_stopped(txq) ||
		    !dev_xmit_complete(netpoll_start_xmit(skb, dev, txq))) {
			skb_queue_head(&npinfo->txq, skb);
			HARD_TX_UNLOCK(dev, txq);
			local_irq_restore(flags);

			schedule_delayed_work(&npinfo->tx_work, HZ/10);
			return;
		}
		HARD_TX_UNLOCK(dev, txq);
		local_irq_restore(flags);
	}
}

static int netif_local_xmit_active(struct net_device *dev)
{
	int i;

	for (i = 0; i < dev->num_tx_queues; i++) {
		struct netdev_queue *txq = netdev_get_tx_queue(dev, i);

		if (READ_ONCE(txq->xmit_lock_owner) == smp_processor_id())
			return 1;
	}

	return 0;
}

static void poll_one_napi(struct napi_struct *napi)
{
	int work;

	/* If we set this bit but see that it has already been set,
	 * that indicates that napi has been disabled and we need
	 * to abort this operation
	 */
	if (test_and_set_bit(NAPI_STATE_NPSVC, &napi->state))
		return;

	/* We explicilty pass the polling call a budget of 0 to
	 * indicate that we are clearing the Tx path only.
	 */
	work = napi->poll(napi, 0);
	WARN_ONCE(work, "%pS exceeded budget in poll\n", napi->poll);
	trace_napi_poll(napi, work, 0);

	clear_bit(NAPI_STATE_NPSVC, &napi->state);
}

static void poll_napi(struct net_device *dev)
{
	struct napi_struct *napi;
	int cpu = smp_processor_id();

	list_for_each_entry_rcu(napi, &dev->napi_list, dev_list) {
		if (cmpxchg(&napi->poll_owner, -1, cpu) == -1) {
			poll_one_napi(napi);
			smp_store_release(&napi->poll_owner, -1);
		}
	}
}

void netpoll_poll_dev(struct net_device *dev)
{
	struct netpoll_info *ni = rcu_dereference_bh(dev->npinfo);
	const struct net_device_ops *ops;

	/* Don't do any rx activity if the dev_lock mutex is held
	 * the dev_open/close paths use this to block netpoll activity
	 * while changing device state
	 */
	if (!ni || down_trylock(&ni->dev_lock))
		return;

	/* Some drivers will take the same locks in poll and xmit,
	 * we can't poll if local CPU is already in xmit.
	 */
	if (!netif_running(dev) || netif_local_xmit_active(dev)) {
		up(&ni->dev_lock);
		return;
	}

	ops = dev->netdev_ops;
	if (ops->ndo_poll_controller)
		ops->ndo_poll_controller(dev);

	poll_napi(dev);

	up(&ni->dev_lock);

	zap_completion_queue();
}
EXPORT_SYMBOL(netpoll_poll_dev);

void netpoll_poll_disable(struct net_device *dev)
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
EXPORT_SYMBOL(netpoll_poll_disable);

void netpoll_poll_enable(struct net_device *dev)
{
	struct netpoll_info *ni;
	rcu_read_lock();
	ni = rcu_dereference(dev->npinfo);
	if (ni)
		up(&ni->dev_lock);
	rcu_read_unlock();
}
EXPORT_SYMBOL(netpoll_poll_enable);

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
			if (!skb_irq_freeable(skb)) {
				refcount_set(&skb->users, 1);
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

	refcount_set(&skb->users, 1);
	skb_reserve(skb, reserve);
	return skb;
}

static int netpoll_owner_active(struct net_device *dev)
{
	struct napi_struct *napi;

	list_for_each_entry_rcu(napi, &dev->napi_list, dev_list) {
		if (napi->poll_owner == smp_processor_id())
			return 1;
	}
	return 0;
}

/* call with IRQ disabled */
static netdev_tx_t __netpoll_send_skb(struct netpoll *np, struct sk_buff *skb)
{
	netdev_tx_t status = NETDEV_TX_BUSY;
	struct net_device *dev;
	unsigned long tries;
	/* It is up to the caller to keep npinfo alive. */
	struct netpoll_info *npinfo;

	lockdep_assert_irqs_disabled();

	dev = np->dev;
	npinfo = rcu_dereference_bh(dev->npinfo);

	if (!npinfo || !netif_running(dev) || !netif_device_present(dev)) {
		dev_kfree_skb_irq(skb);
		return NET_XMIT_DROP;
	}

	/* don't get messages out of order, and no recursion */
	if (skb_queue_len(&npinfo->txq) == 0 && !netpoll_owner_active(dev)) {
		struct netdev_queue *txq;

		txq = netdev_core_pick_tx(dev, skb, NULL);

		/* try until next clock tick */
		for (tries = jiffies_to_usecs(1)/USEC_PER_POLL;
		     tries > 0; --tries) {
			if (HARD_TX_TRYLOCK(dev, txq)) {
				if (!netif_xmit_stopped(txq))
					status = netpoll_start_xmit(skb, dev, txq);

				HARD_TX_UNLOCK(dev, txq);

				if (dev_xmit_complete(status))
					break;

			}

			/* tickle device maybe there is some cleanup */
			netpoll_poll_dev(np->dev);

			udelay(USEC_PER_POLL);
		}

		WARN_ONCE(!irqs_disabled(),
			"netpoll_send_skb_on_dev(): %s enabled interrupts in poll (%pS)\n",
			dev->name, dev->netdev_ops->ndo_start_xmit);

	}

	if (!dev_xmit_complete(status)) {
		skb_queue_tail(&npinfo->txq, skb);
		schedule_delayed_work(&npinfo->tx_work,0);
	}
	return NETDEV_TX_OK;
}

netdev_tx_t netpoll_send_skb(struct netpoll *np, struct sk_buff *skb)
{
	unsigned long flags;
	netdev_tx_t ret;

	if (unlikely(!np)) {
		dev_kfree_skb_irq(skb);
		ret = NET_XMIT_DROP;
	} else {
		local_irq_save(flags);
		ret = __netpoll_send_skb(np, skb);
		local_irq_restore(flags);
	}
	return ret;
}
EXPORT_SYMBOL(netpoll_send_skb);

void netpoll_send_udp(struct netpoll *np, const char *msg, int len)
{
	int total_len, ip_len, udp_len;
	struct sk_buff *skb;
	struct udphdr *udph;
	struct iphdr *iph;
	struct ethhdr *eth;
	static atomic_t ip_ident;
	struct ipv6hdr *ip6h;

	if (!IS_ENABLED(CONFIG_PREEMPT_RT))
		WARN_ON_ONCE(!irqs_disabled());

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
		*(unsigned char *)ip6h = 0x60;
		ip6h->flow_lbl[0] = 0;
		ip6h->flow_lbl[1] = 0;
		ip6h->flow_lbl[2] = 0;

		ip6h->payload_len = htons(sizeof(struct udphdr) + len);
		ip6h->nexthdr = IPPROTO_UDP;
		ip6h->hop_limit = 32;
		ip6h->saddr = np->local_ip.in6;
		ip6h->daddr = np->remote_ip.in6;

		eth = skb_push(skb, ETH_HLEN);
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
		*(unsigned char *)iph = 0x45;
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

		eth = skb_push(skb, ETH_HLEN);
		skb_reset_mac_header(skb);
		skb->protocol = eth->h_proto = htons(ETH_P_IP);
	}

	ether_addr_copy(eth->h_source, np->dev->dev_addr);
	ether_addr_copy(eth->h_dest, np->remote_mac);

	skb->dev = np->dev;

	netpoll_send_skb(np, skb);
}
EXPORT_SYMBOL(netpoll_send_udp);

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
		strscpy(np->dev_name, cur, sizeof(np->dev_name));
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

int __netpoll_setup(struct netpoll *np, struct net_device *ndev)
{
	struct netpoll_info *npinfo;
	const struct net_device_ops *ops;
	int err;

	np->dev = ndev;
	strscpy(np->dev_name, ndev->name, IFNAMSIZ);

	if (ndev->priv_flags & IFF_DISABLE_NETPOLL) {
		np_err(np, "%s doesn't support polling, aborting\n",
		       np->dev_name);
		err = -ENOTSUPP;
		goto out;
	}

	if (!ndev->npinfo) {
		npinfo = kmalloc(sizeof(*npinfo), GFP_KERNEL);
		if (!npinfo) {
			err = -ENOMEM;
			goto out;
		}

		sema_init(&npinfo->dev_lock, 1);
		skb_queue_head_init(&npinfo->txq);
		INIT_DELAYED_WORK(&npinfo->tx_work, queue_process);

		refcount_set(&npinfo->refcnt, 1);

		ops = np->dev->netdev_ops;
		if (ops->ndo_netpoll_setup) {
			err = ops->ndo_netpoll_setup(ndev, npinfo);
			if (err)
				goto free_npinfo;
		}
	} else {
		npinfo = rtnl_dereference(ndev->npinfo);
		refcount_inc(&npinfo->refcnt);
	}

	npinfo->netpoll = np;

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
	if (np->dev_name[0]) {
		struct net *net = current->nsproxy->net_ns;
		ndev = __dev_get_by_name(net, np->dev_name);
	}
	if (!ndev) {
		np_err(np, "%s doesn't exist, aborting\n", np->dev_name);
		err = -ENODEV;
		goto unlock;
	}
	netdev_hold(ndev, &np->dev_tracker, GFP_KERNEL);

	if (netdev_master_upper_dev_get(ndev)) {
		np_err(np, "%s is a slave device, aborting\n", np->dev_name);
		err = -EBUSY;
		goto put;
	}

	if (!netif_running(ndev)) {
		unsigned long atmost;

		np_info(np, "device %s not up yet, forcing it\n", np->dev_name);

		err = dev_open(ndev, NULL);

		if (err) {
			np_err(np, "failed to open %s\n", ndev->name);
			goto put;
		}

		rtnl_unlock();
		atmost = jiffies + carrier_timeout * HZ;
		while (!netif_carrier_ok(ndev)) {
			if (time_after(jiffies, atmost)) {
				np_notice(np, "timeout waiting for carrier\n");
				break;
			}
			msleep(1);
		}

		rtnl_lock();
	}

	if (!np->local_ip.ip) {
		if (!np->ipv6) {
			const struct in_ifaddr *ifa;

			in_dev = __in_dev_get_rtnl(ndev);
			if (!in_dev)
				goto put_noaddr;

			ifa = rtnl_dereference(in_dev->ifa_list);
			if (!ifa) {
put_noaddr:
				np_err(np, "no IP address for %s, aborting\n",
				       np->dev_name);
				err = -EDESTADDRREQ;
				goto put;
			}

			np->local_ip.ip = ifa->ifa_local;
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
					if (!!(ipv6_addr_type(&ifp->addr) & IPV6_ADDR_LINKLOCAL) !=
					    !!(ipv6_addr_type(&np->remote_ip.in6) & IPV6_ADDR_LINKLOCAL))
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

	err = __netpoll_setup(np, ndev);
	if (err)
		goto put;
	rtnl_unlock();
	return 0;

put:
	netdev_put(ndev, &np->dev_tracker);
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

	npinfo = rtnl_dereference(np->dev->npinfo);
	if (!npinfo)
		return;

	synchronize_srcu(&netpoll_srcu);

	if (refcount_dec_and_test(&npinfo->refcnt)) {
		const struct net_device_ops *ops;

		ops = np->dev->netdev_ops;
		if (ops->ndo_netpoll_cleanup)
			ops->ndo_netpoll_cleanup(np->dev);

		RCU_INIT_POINTER(np->dev->npinfo, NULL);
		call_rcu(&npinfo->rcu, rcu_cleanup_netpoll_info);
	} else
		RCU_INIT_POINTER(np->dev->npinfo, NULL);
}
EXPORT_SYMBOL_GPL(__netpoll_cleanup);

void __netpoll_free(struct netpoll *np)
{
	ASSERT_RTNL();

	/* Wait for transmitting packets to finish before freeing. */
	synchronize_rcu();
	__netpoll_cleanup(np);
	kfree(np);
}
EXPORT_SYMBOL_GPL(__netpoll_free);

void netpoll_cleanup(struct netpoll *np)
{
	rtnl_lock();
	if (!np->dev)
		goto out;
	__netpoll_cleanup(np);
	netdev_put(np->dev, &np->dev_tracker);
	np->dev = NULL;
out:
	rtnl_unlock();
}
EXPORT_SYMBOL(netpoll_cleanup);
