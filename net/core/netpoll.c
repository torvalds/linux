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

#include <linux/moduleparam.h>
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
#include <net/tcp.h>
#include <net/udp.h>
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

#define USEC_PER_POLL	50
#define NETPOLL_RX_ENABLED  1
#define NETPOLL_RX_DROP     2

#define MAX_SKB_SIZE \
		(MAX_UDP_CHUNK + sizeof(struct udphdr) + \
				sizeof(struct iphdr) + sizeof(struct ethhdr))

static void zap_completion_queue(void);
static void arp_reply(struct sk_buff *skb);

static unsigned int carrier_timeout = 4;
module_param(carrier_timeout, uint, 0644);

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
		if (netif_tx_queue_frozen_or_stopped(txq) ||
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
			budget = poll_one_napi(dev->npinfo, napi, budget);
			spin_unlock(&napi->poll_lock);

			if (!budget)
				break;
		}
	}
}

static void service_arp_queue(struct netpoll_info *npi)
{
	if (npi) {
		struct sk_buff *skb;

		while ((skb = skb_dequeue(&npi->arp_tx)))
			arp_reply(skb);
	}
}

void netpoll_poll_dev(struct net_device *dev)
{
	const struct net_device_ops *ops;

	if (!dev || !netif_running(dev))
		return;

	ops = dev->netdev_ops;
	if (!ops->ndo_poll_controller)
		return;

	/* Process pending work on NIC */
	ops->ndo_poll_controller(dev);

	poll_napi(dev);

	if (dev->flags & IFF_SLAVE) {
		if (dev->npinfo) {
			struct net_device *bond_dev = dev->master;
			struct sk_buff *skb;
			while ((skb = skb_dequeue(&dev->npinfo->arp_tx))) {
				skb->dev = bond_dev;
				skb_queue_tail(&bond_dev->npinfo->arp_tx, skb);
			}
		}
	}

	service_arp_queue(dev->npinfo);

	zap_completion_queue();
}
EXPORT_SYMBOL(netpoll_poll_dev);

void netpoll_poll(struct netpoll *np)
{
	netpoll_poll_dev(np->dev);
}
EXPORT_SYMBOL(netpoll_poll);

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
			netpoll_poll(np);
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

void netpoll_send_skb_on_dev(struct netpoll *np, struct sk_buff *skb,
			     struct net_device *dev)
{
	int status = NETDEV_TX_BUSY;
	unsigned long tries;
	const struct net_device_ops *ops = dev->netdev_ops;
	/* It is up to the caller to keep npinfo alive. */
	struct netpoll_info *npinfo = np->dev->npinfo;

	if (!npinfo || !netif_running(dev) || !netif_device_present(dev)) {
		__kfree_skb(skb);
		return;
	}

	/* don't get messages out of order, and no recursion */
	if (skb_queue_len(&npinfo->txq) == 0 && !netpoll_owner_active(dev)) {
		struct netdev_queue *txq;
		unsigned long flags;

		txq = netdev_get_tx_queue(dev, skb_get_queue_mapping(skb));

		local_irq_save(flags);
		/* try until next clock tick */
		for (tries = jiffies_to_usecs(1)/USEC_PER_POLL;
		     tries > 0; --tries) {
			if (__netif_tx_trylock(txq)) {
				if (!netif_tx_queue_stopped(txq)) {
					status = ops->ndo_start_xmit(skb, dev);
					if (status == NETDEV_TX_OK)
						txq_trans_update(txq);
				}
				__netif_tx_unlock(txq);

				if (status == NETDEV_TX_OK)
					break;

			}

			/* tickle device maybe there is some cleanup */
			netpoll_poll(np);

			udelay(USEC_PER_POLL);
		}

		WARN_ONCE(!irqs_disabled(),
			"netpoll_send_skb(): %s enabled interrupts in poll (%pF)\n",
			dev->name, ops->ndo_start_xmit);

		local_irq_restore(flags);
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

	udp_len = len + sizeof(*udph);
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
	udph->check = 0;
	udph->check = csum_tcpudp_magic(np->local_ip,
					np->remote_ip,
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
	iph->id       = 0;
	iph->frag_off = 0;
	iph->ttl      = 64;
	iph->protocol = IPPROTO_UDP;
	iph->check    = 0;
	put_unaligned(np->local_ip, &(iph->saddr));
	put_unaligned(np->remote_ip, &(iph->daddr));
	iph->check    = ip_fast_csum((unsigned char *)iph, iph->ihl);

	eth = (struct ethhdr *) skb_push(skb, ETH_HLEN);
	skb_reset_mac_header(skb);
	skb->protocol = eth->h_proto = htons(ETH_P_IP);
	memcpy(eth->h_source, np->dev->dev_addr, ETH_ALEN);
	memcpy(eth->h_dest, np->remote_mac, ETH_ALEN);

	skb->dev = np->dev;

	netpoll_send_skb(np, skb);
}
EXPORT_SYMBOL(netpoll_send_udp);

static void arp_reply(struct sk_buff *skb)
{
	struct netpoll_info *npinfo = skb->dev->npinfo;
	struct arphdr *arp;
	unsigned char *arp_ptr;
	int size, type = ARPOP_REPLY, ptype = ETH_P_ARP;
	__be32 sip, tip;
	unsigned char *sha;
	struct sk_buff *send_skb;
	struct netpoll *np, *tmp;
	unsigned long flags;
	int hits = 0;

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
		if (tip != np->local_ip)
			continue;

		send_skb = find_skb(np, size + LL_ALLOCATED_SPACE(np->dev),
				    LL_RESERVED_SPACE(np->dev));
		if (!send_skb)
			continue;

		skb_reset_network_header(send_skb);
		arp = (struct arphdr *) skb_put(send_skb, size);
		send_skb->dev = skb->dev;
		send_skb->protocol = htons(ETH_P_ARP);

		/* Fill the device header for the ARP frame */
		if (dev_hard_header(send_skb, skb->dev, ptype,
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

		/* If there are several rx_hooks for the same address,
		   we're fine by sending a single reply */
		break;
	}
	spin_unlock_irqrestore(&npinfo->rx_lock, flags);
}

int __netpoll_rx(struct sk_buff *skb)
{
	int proto, len, ulen;
	int hits = 0;
	const struct iphdr *iph;
	struct udphdr *uh;
	struct netpoll_info *npinfo = skb->dev->npinfo;
	struct netpoll *np, *tmp;

	if (list_empty(&npinfo->rx_np))
		goto out;

	if (skb->dev->type != ARPHRD_ETHER)
		goto out;

	/* check if netpoll clients need ARP */
	if (skb->protocol == htons(ETH_P_ARP) &&
	    atomic_read(&trapped)) {
		skb_queue_tail(&npinfo->arp_tx, skb);
		return 1;
	}

	proto = ntohs(eth_hdr(skb)->h_proto);
	if (proto != ETH_P_IP)
		goto out;
	if (skb->pkt_type == PACKET_OTHERHOST)
		goto out;
	if (skb_shared(skb))
		goto out;

	iph = (struct iphdr *)skb->data;
	if (!pskb_may_pull(skb, sizeof(struct iphdr)))
		goto out;
	if (iph->ihl < 5 || iph->version != 4)
		goto out;
	if (!pskb_may_pull(skb, iph->ihl*4))
		goto out;
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

	if (iph->protocol != IPPROTO_UDP)
		goto out;

	len -= iph->ihl*4;
	uh = (struct udphdr *)(((char *)iph) + iph->ihl*4);
	ulen = ntohs(uh->len);

	if (ulen != len)
		goto out;
	if (checksum_udp(skb, uh, ulen, iph->saddr, iph->daddr))
		goto out;

	list_for_each_entry_safe(np, tmp, &npinfo->rx_np, rx) {
		if (np->local_ip && np->local_ip != iph->daddr)
			continue;
		if (np->remote_ip && np->remote_ip != iph->saddr)
			continue;
		if (np->local_port && np->local_port != ntohs(uh->dest))
			continue;

		np->rx_hook(np, ntohs(uh->source),
			       (char *)(uh+1),
			       ulen - sizeof(struct udphdr));
		hits++;
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
	printk(KERN_INFO "%s: local port %d\n",
			 np->name, np->local_port);
	printk(KERN_INFO "%s: local IP %pI4\n",
			 np->name, &np->local_ip);
	printk(KERN_INFO "%s: interface '%s'\n",
			 np->name, np->dev_name);
	printk(KERN_INFO "%s: remote port %d\n",
			 np->name, np->remote_port);
	printk(KERN_INFO "%s: remote IP %pI4\n",
			 np->name, &np->remote_ip);
	printk(KERN_INFO "%s: remote ethernet address %pM\n",
	                 np->name, np->remote_mac);
}
EXPORT_SYMBOL(netpoll_print_options);

int netpoll_parse_options(struct netpoll *np, char *opt)
{
	char *cur=opt, *delim;

	if (*cur != '@') {
		if ((delim = strchr(cur, '@')) == NULL)
			goto parse_failed;
		*delim = 0;
		np->local_port = simple_strtol(cur, NULL, 10);
		cur = delim;
	}
	cur++;

	if (*cur != '/') {
		if ((delim = strchr(cur, '/')) == NULL)
			goto parse_failed;
		*delim = 0;
		np->local_ip = in_aton(cur);
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
			printk(KERN_INFO "%s: warning: whitespace"
					"is not allowed\n", np->name);
		np->remote_port = simple_strtol(cur, NULL, 10);
		cur = delim;
	}
	cur++;

	/* dst ip */
	if ((delim = strchr(cur, '/')) == NULL)
		goto parse_failed;
	*delim = 0;
	np->remote_ip = in_aton(cur);
	cur = delim + 1;

	if (*cur != 0) {
		/* MAC address */
		if (!mac_pton(cur, np->remote_mac))
			goto parse_failed;
	}

	netpoll_print_options(np);

	return 0;

 parse_failed:
	printk(KERN_INFO "%s: couldn't parse config at '%s'!\n",
	       np->name, cur);
	return -1;
}
EXPORT_SYMBOL(netpoll_parse_options);

int __netpoll_setup(struct netpoll *np)
{
	struct net_device *ndev = np->dev;
	struct netpoll_info *npinfo;
	const struct net_device_ops *ops;
	unsigned long flags;
	int err;

	if ((ndev->priv_flags & IFF_DISABLE_NETPOLL) ||
	    !ndev->netdev_ops->ndo_poll_controller) {
		printk(KERN_ERR "%s: %s doesn't support polling, aborting.\n",
		       np->name, np->dev_name);
		err = -ENOTSUPP;
		goto out;
	}

	if (!ndev->npinfo) {
		npinfo = kmalloc(sizeof(*npinfo), GFP_KERNEL);
		if (!npinfo) {
			err = -ENOMEM;
			goto out;
		}

		npinfo->rx_flags = 0;
		INIT_LIST_HEAD(&npinfo->rx_np);

		spin_lock_init(&npinfo->rx_lock);
		skb_queue_head_init(&npinfo->arp_tx);
		skb_queue_head_init(&npinfo->txq);
		INIT_DELAYED_WORK(&npinfo->tx_work, queue_process);

		atomic_set(&npinfo->refcnt, 1);

		ops = np->dev->netdev_ops;
		if (ops->ndo_netpoll_setup) {
			err = ops->ndo_netpoll_setup(ndev, npinfo);
			if (err)
				goto free_npinfo;
		}
	} else {
		npinfo = ndev->npinfo;
		atomic_inc(&npinfo->refcnt);
	}

	npinfo->netpoll = np;

	if (np->rx_hook) {
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

	if (np->dev_name)
		ndev = dev_get_by_name(&init_net, np->dev_name);
	if (!ndev) {
		printk(KERN_ERR "%s: %s doesn't exist, aborting.\n",
		       np->name, np->dev_name);
		return -ENODEV;
	}

	if (ndev->master) {
		printk(KERN_ERR "%s: %s is a slave device, aborting.\n",
		       np->name, np->dev_name);
		err = -EBUSY;
		goto put;
	}

	if (!netif_running(ndev)) {
		unsigned long atmost, atleast;

		printk(KERN_INFO "%s: device %s not up yet, forcing it\n",
		       np->name, np->dev_name);

		rtnl_lock();
		err = dev_open(ndev);
		rtnl_unlock();

		if (err) {
			printk(KERN_ERR "%s: failed to open %s\n",
			       np->name, ndev->name);
			goto put;
		}

		atleast = jiffies + HZ/10;
		atmost = jiffies + carrier_timeout * HZ;
		while (!netif_carrier_ok(ndev)) {
			if (time_after(jiffies, atmost)) {
				printk(KERN_NOTICE
				       "%s: timeout waiting for carrier\n",
				       np->name);
				break;
			}
			msleep(1);
		}

		/* If carrier appears to come up instantly, we don't
		 * trust it and pause so that we don't pump all our
		 * queued console messages into the bitbucket.
		 */

		if (time_before(jiffies, atleast)) {
			printk(KERN_NOTICE "%s: carrier detect appears"
			       " untrustworthy, waiting 4 seconds\n",
			       np->name);
			msleep(4000);
		}
	}

	if (!np->local_ip) {
		rcu_read_lock();
		in_dev = __in_dev_get_rcu(ndev);

		if (!in_dev || !in_dev->ifa_list) {
			rcu_read_unlock();
			printk(KERN_ERR "%s: no IP address for %s, aborting\n",
			       np->name, np->dev_name);
			err = -EDESTADDRREQ;
			goto put;
		}

		np->local_ip = in_dev->ifa_list->ifa_local;
		rcu_read_unlock();
		printk(KERN_INFO "%s: local IP %pI4\n", np->name, &np->local_ip);
	}

	np->dev = ndev;

	/* fill up the skb queue */
	refill_skbs();

	rtnl_lock();
	err = __netpoll_setup(np);
	rtnl_unlock();

	if (err)
		goto put;

	return 0;

put:
	dev_put(ndev);
	return err;
}
EXPORT_SYMBOL(netpoll_setup);

static int __init netpoll_init(void)
{
	skb_queue_head_init(&skb_pool);
	return 0;
}
core_initcall(netpoll_init);

void __netpoll_cleanup(struct netpoll *np)
{
	struct netpoll_info *npinfo;
	unsigned long flags;

	npinfo = np->dev->npinfo;
	if (!npinfo)
		return;

	if (!list_empty(&npinfo->rx_np)) {
		spin_lock_irqsave(&npinfo->rx_lock, flags);
		list_del(&np->rx);
		if (list_empty(&npinfo->rx_np))
			npinfo->rx_flags &= ~NETPOLL_RX_ENABLED;
		spin_unlock_irqrestore(&npinfo->rx_lock, flags);
	}

	if (atomic_dec_and_test(&npinfo->refcnt)) {
		const struct net_device_ops *ops;

		ops = np->dev->netdev_ops;
		if (ops->ndo_netpoll_cleanup)
			ops->ndo_netpoll_cleanup(np->dev);

		rcu_assign_pointer(np->dev->npinfo, NULL);

		/* avoid racing with NAPI reading npinfo */
		synchronize_rcu_bh();

		skb_queue_purge(&npinfo->arp_tx);
		skb_queue_purge(&npinfo->txq);
		cancel_delayed_work_sync(&npinfo->tx_work);

		/* clean after last, unfinished work */
		__skb_queue_purge(&npinfo->txq);
		kfree(npinfo);
	}
}
EXPORT_SYMBOL_GPL(__netpoll_cleanup);

void netpoll_cleanup(struct netpoll *np)
{
	if (!np->dev)
		return;

	rtnl_lock();
	__netpoll_cleanup(np);
	rtnl_unlock();

	dev_put(np->dev);
	np->dev = NULL;
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
