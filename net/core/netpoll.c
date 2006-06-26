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

#include <linux/smp_lock.h>
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
#include <net/tcp.h>
#include <net/udp.h>
#include <asm/unaligned.h>

/*
 * We maintain a small pool of fully-sized skbs, to make sure the
 * message gets out even in extreme OOM situations.
 */

#define MAX_UDP_CHUNK 1460
#define MAX_SKBS 32
#define MAX_QUEUE_DEPTH (MAX_SKBS / 2)
#define MAX_RETRIES 20000

static DEFINE_SPINLOCK(skb_list_lock);
static int nr_skbs;
static struct sk_buff *skbs;

static DEFINE_SPINLOCK(queue_lock);
static int queue_depth;
static struct sk_buff *queue_head, *queue_tail;

static atomic_t trapped;

#define NETPOLL_RX_ENABLED  1
#define NETPOLL_RX_DROP     2

#define MAX_SKB_SIZE \
		(MAX_UDP_CHUNK + sizeof(struct udphdr) + \
				sizeof(struct iphdr) + sizeof(struct ethhdr))

static void zap_completion_queue(void);
static void arp_reply(struct sk_buff *skb);

static void queue_process(void *p)
{
	unsigned long flags;
	struct sk_buff *skb;

	while (queue_head) {
		spin_lock_irqsave(&queue_lock, flags);

		skb = queue_head;
		queue_head = skb->next;
		if (skb == queue_tail)
			queue_head = NULL;

		queue_depth--;

		spin_unlock_irqrestore(&queue_lock, flags);

		dev_queue_xmit(skb);
	}
}

static DECLARE_WORK(send_queue, queue_process, NULL);

void netpoll_queue(struct sk_buff *skb)
{
	unsigned long flags;

	if (queue_depth == MAX_QUEUE_DEPTH) {
		__kfree_skb(skb);
		return;
	}

	spin_lock_irqsave(&queue_lock, flags);
	if (!queue_head)
		queue_head = skb;
	else
		queue_tail->next = skb;
	queue_tail = skb;
	queue_depth++;
	spin_unlock_irqrestore(&queue_lock, flags);

	schedule_work(&send_queue);
}

static int checksum_udp(struct sk_buff *skb, struct udphdr *uh,
			     unsigned short ulen, u32 saddr, u32 daddr)
{
	unsigned int psum;

	if (uh->check == 0 || skb->ip_summed == CHECKSUM_UNNECESSARY)
		return 0;

	psum = csum_tcpudp_nofold(saddr, daddr, ulen, IPPROTO_UDP, 0);

	if (skb->ip_summed == CHECKSUM_HW &&
	    !(u16)csum_fold(csum_add(psum, skb->csum)))
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
static void poll_napi(struct netpoll *np)
{
	struct netpoll_info *npinfo = np->dev->npinfo;
	int budget = 16;

	if (test_bit(__LINK_STATE_RX_SCHED, &np->dev->state) &&
	    npinfo->poll_owner != smp_processor_id() &&
	    spin_trylock(&npinfo->poll_lock)) {
		npinfo->rx_flags |= NETPOLL_RX_DROP;
		atomic_inc(&trapped);

		np->dev->poll(np->dev, &budget);

		atomic_dec(&trapped);
		npinfo->rx_flags &= ~NETPOLL_RX_DROP;
		spin_unlock(&npinfo->poll_lock);
	}
}

static void service_arp_queue(struct netpoll_info *npi)
{
	struct sk_buff *skb;

	if (unlikely(!npi))
		return;

	skb = skb_dequeue(&npi->arp_tx);

	while (skb != NULL) {
		arp_reply(skb);
		skb = skb_dequeue(&npi->arp_tx);
	}
	return;
}

void netpoll_poll(struct netpoll *np)
{
	if(!np->dev || !netif_running(np->dev) || !np->dev->poll_controller)
		return;

	/* Process pending work on NIC */
	np->dev->poll_controller(np->dev);
	if (np->dev->poll)
		poll_napi(np);

	service_arp_queue(np->dev->npinfo);

	zap_completion_queue();
}

static void refill_skbs(void)
{
	struct sk_buff *skb;
	unsigned long flags;

	spin_lock_irqsave(&skb_list_lock, flags);
	while (nr_skbs < MAX_SKBS) {
		skb = alloc_skb(MAX_SKB_SIZE, GFP_ATOMIC);
		if (!skb)
			break;

		skb->next = skbs;
		skbs = skb;
		nr_skbs++;
	}
	spin_unlock_irqrestore(&skb_list_lock, flags);
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
			if(skb->destructor)
				dev_kfree_skb_any(skb); /* put this one back */
			else
				__kfree_skb(skb);
		}
	}

	put_cpu_var(softnet_data);
}

static struct sk_buff * find_skb(struct netpoll *np, int len, int reserve)
{
	int once = 1, count = 0;
	unsigned long flags;
	struct sk_buff *skb = NULL;

	zap_completion_queue();
repeat:
	if (nr_skbs < MAX_SKBS)
		refill_skbs();

	skb = alloc_skb(len, GFP_ATOMIC);

	if (!skb) {
		spin_lock_irqsave(&skb_list_lock, flags);
		skb = skbs;
		if (skb) {
			skbs = skb->next;
			skb->next = NULL;
			nr_skbs--;
		}
		spin_unlock_irqrestore(&skb_list_lock, flags);
	}

	if(!skb) {
		count++;
		if (once && (count == 1000000)) {
			printk("out of netpoll skbs!\n");
			once = 0;
		}
		netpoll_poll(np);
		goto repeat;
	}

	atomic_set(&skb->users, 1);
	skb_reserve(skb, reserve);
	return skb;
}

static void netpoll_send_skb(struct netpoll *np, struct sk_buff *skb)
{
	int status;
	struct netpoll_info *npinfo;

	if (!np || !np->dev || !netif_running(np->dev)) {
		__kfree_skb(skb);
		return;
	}

	npinfo = np->dev->npinfo;

	/* avoid recursion */
	if (npinfo->poll_owner == smp_processor_id() ||
	    np->dev->xmit_lock_owner == smp_processor_id()) {
		if (np->drop)
			np->drop(skb);
		else
			__kfree_skb(skb);
		return;
	}

	do {
		npinfo->tries--;
		netif_tx_lock(np->dev);

		/*
		 * network drivers do not expect to be called if the queue is
		 * stopped.
		 */
		status = NETDEV_TX_BUSY;
		if (!netif_queue_stopped(np->dev))
			status = np->dev->hard_start_xmit(skb, np->dev);

		netif_tx_unlock(np->dev);

		/* success */
		if(!status) {
			npinfo->tries = MAX_RETRIES; /* reset */
			return;
		}

		/* transmit busy */
		netpoll_poll(np);
		udelay(50);
	} while (npinfo->tries > 0);
}

void netpoll_send_udp(struct netpoll *np, const char *msg, int len)
{
	int total_len, eth_len, ip_len, udp_len;
	struct sk_buff *skb;
	struct udphdr *udph;
	struct iphdr *iph;
	struct ethhdr *eth;

	udp_len = len + sizeof(*udph);
	ip_len = eth_len = udp_len + sizeof(*iph);
	total_len = eth_len + ETH_HLEN + NET_IP_ALIGN;

	skb = find_skb(np, total_len, total_len - len);
	if (!skb)
		return;

	memcpy(skb->data, msg, len);
	skb->len += len;

	udph = (struct udphdr *) skb_push(skb, sizeof(*udph));
	udph->source = htons(np->local_port);
	udph->dest = htons(np->remote_port);
	udph->len = htons(udp_len);
	udph->check = 0;

	iph = (struct iphdr *)skb_push(skb, sizeof(*iph));

	/* iph->version = 4; iph->ihl = 5; */
	put_unaligned(0x45, (unsigned char *)iph);
	iph->tos      = 0;
	put_unaligned(htons(ip_len), &(iph->tot_len));
	iph->id       = 0;
	iph->frag_off = 0;
	iph->ttl      = 64;
	iph->protocol = IPPROTO_UDP;
	iph->check    = 0;
	put_unaligned(htonl(np->local_ip), &(iph->saddr));
	put_unaligned(htonl(np->remote_ip), &(iph->daddr));
	iph->check    = ip_fast_csum((unsigned char *)iph, iph->ihl);

	eth = (struct ethhdr *) skb_push(skb, ETH_HLEN);

	eth->h_proto = htons(ETH_P_IP);
	memcpy(eth->h_source, np->local_mac, 6);
	memcpy(eth->h_dest, np->remote_mac, 6);

	skb->dev = np->dev;

	netpoll_send_skb(np, skb);
}

static void arp_reply(struct sk_buff *skb)
{
	struct netpoll_info *npinfo = skb->dev->npinfo;
	struct arphdr *arp;
	unsigned char *arp_ptr;
	int size, type = ARPOP_REPLY, ptype = ETH_P_ARP;
	u32 sip, tip;
	struct sk_buff *send_skb;
	struct netpoll *np = NULL;

	if (npinfo->rx_np && npinfo->rx_np->dev == skb->dev)
		np = npinfo->rx_np;
	if (!np)
		return;

	/* No arp on this interface */
	if (skb->dev->flags & IFF_NOARP)
		return;

	if (!pskb_may_pull(skb, (sizeof(struct arphdr) +
				 (2 * skb->dev->addr_len) +
				 (2 * sizeof(u32)))))
		return;

	skb->h.raw = skb->nh.raw = skb->data;
	arp = skb->nh.arph;

	if ((arp->ar_hrd != htons(ARPHRD_ETHER) &&
	     arp->ar_hrd != htons(ARPHRD_IEEE802)) ||
	    arp->ar_pro != htons(ETH_P_IP) ||
	    arp->ar_op != htons(ARPOP_REQUEST))
		return;

	arp_ptr = (unsigned char *)(arp+1) + skb->dev->addr_len;
	memcpy(&sip, arp_ptr, 4);
	arp_ptr += 4 + skb->dev->addr_len;
	memcpy(&tip, arp_ptr, 4);

	/* Should we ignore arp? */
	if (tip != htonl(np->local_ip) || LOOPBACK(tip) || MULTICAST(tip))
		return;

	size = sizeof(struct arphdr) + 2 * (skb->dev->addr_len + 4);
	send_skb = find_skb(np, size + LL_RESERVED_SPACE(np->dev),
			    LL_RESERVED_SPACE(np->dev));

	if (!send_skb)
		return;

	send_skb->nh.raw = send_skb->data;
	arp = (struct arphdr *) skb_put(send_skb, size);
	send_skb->dev = skb->dev;
	send_skb->protocol = htons(ETH_P_ARP);

	/* Fill the device header for the ARP frame */

	if (np->dev->hard_header &&
	    np->dev->hard_header(send_skb, skb->dev, ptype,
				       np->remote_mac, np->local_mac,
				       send_skb->len) < 0) {
		kfree_skb(send_skb);
		return;
	}

	/*
	 * Fill out the arp protocol part.
	 *
	 * we only support ethernet device type,
	 * which (according to RFC 1390) should always equal 1 (Ethernet).
	 */

	arp->ar_hrd = htons(np->dev->type);
	arp->ar_pro = htons(ETH_P_IP);
	arp->ar_hln = np->dev->addr_len;
	arp->ar_pln = 4;
	arp->ar_op = htons(type);

	arp_ptr=(unsigned char *)(arp + 1);
	memcpy(arp_ptr, np->dev->dev_addr, np->dev->addr_len);
	arp_ptr += np->dev->addr_len;
	memcpy(arp_ptr, &tip, 4);
	arp_ptr += 4;
	memcpy(arp_ptr, np->remote_mac, np->dev->addr_len);
	arp_ptr += np->dev->addr_len;
	memcpy(arp_ptr, &sip, 4);

	netpoll_send_skb(np, send_skb);
}

int __netpoll_rx(struct sk_buff *skb)
{
	int proto, len, ulen;
	struct iphdr *iph;
	struct udphdr *uh;
	struct netpoll_info *npi = skb->dev->npinfo;
	struct netpoll *np = npi->rx_np;


	if (!np)
		goto out;
	if (skb->dev->type != ARPHRD_ETHER)
		goto out;

	/* check if netpoll clients need ARP */
	if (skb->protocol == __constant_htons(ETH_P_ARP) &&
	    atomic_read(&trapped)) {
		skb_queue_tail(&npi->arp_tx, skb);
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

	if (iph->protocol != IPPROTO_UDP)
		goto out;

	len -= iph->ihl*4;
	uh = (struct udphdr *)(((char *)iph) + iph->ihl*4);
	ulen = ntohs(uh->len);

	if (ulen != len)
		goto out;
	if (checksum_udp(skb, uh, ulen, iph->saddr, iph->daddr))
		goto out;
	if (np->local_ip && np->local_ip != ntohl(iph->daddr))
		goto out;
	if (np->remote_ip && np->remote_ip != ntohl(iph->saddr))
		goto out;
	if (np->local_port && np->local_port != ntohs(uh->dest))
		goto out;

	np->rx_hook(np, ntohs(uh->source),
		    (char *)(uh+1),
		    ulen - sizeof(struct udphdr));

	kfree_skb(skb);
	return 1;

out:
	if (atomic_read(&trapped)) {
		kfree_skb(skb);
		return 1;
	}

	return 0;
}

int netpoll_parse_options(struct netpoll *np, char *opt)
{
	char *cur=opt, *delim;

	if(*cur != '@') {
		if ((delim = strchr(cur, '@')) == NULL)
			goto parse_failed;
		*delim=0;
		np->local_port=simple_strtol(cur, NULL, 10);
		cur=delim;
	}
	cur++;
	printk(KERN_INFO "%s: local port %d\n", np->name, np->local_port);

	if(*cur != '/') {
		if ((delim = strchr(cur, '/')) == NULL)
			goto parse_failed;
		*delim=0;
		np->local_ip=ntohl(in_aton(cur));
		cur=delim;

		printk(KERN_INFO "%s: local IP %d.%d.%d.%d\n",
		       np->name, HIPQUAD(np->local_ip));
	}
	cur++;

	if ( *cur != ',') {
		/* parse out dev name */
		if ((delim = strchr(cur, ',')) == NULL)
			goto parse_failed;
		*delim=0;
		strlcpy(np->dev_name, cur, sizeof(np->dev_name));
		cur=delim;
	}
	cur++;

	printk(KERN_INFO "%s: interface %s\n", np->name, np->dev_name);

	if ( *cur != '@' ) {
		/* dst port */
		if ((delim = strchr(cur, '@')) == NULL)
			goto parse_failed;
		*delim=0;
		np->remote_port=simple_strtol(cur, NULL, 10);
		cur=delim;
	}
	cur++;
	printk(KERN_INFO "%s: remote port %d\n", np->name, np->remote_port);

	/* dst ip */
	if ((delim = strchr(cur, '/')) == NULL)
		goto parse_failed;
	*delim=0;
	np->remote_ip=ntohl(in_aton(cur));
	cur=delim+1;

	printk(KERN_INFO "%s: remote IP %d.%d.%d.%d\n",
		       np->name, HIPQUAD(np->remote_ip));

	if( *cur != 0 )
	{
		/* MAC address */
		if ((delim = strchr(cur, ':')) == NULL)
			goto parse_failed;
		*delim=0;
		np->remote_mac[0]=simple_strtol(cur, NULL, 16);
		cur=delim+1;
		if ((delim = strchr(cur, ':')) == NULL)
			goto parse_failed;
		*delim=0;
		np->remote_mac[1]=simple_strtol(cur, NULL, 16);
		cur=delim+1;
		if ((delim = strchr(cur, ':')) == NULL)
			goto parse_failed;
		*delim=0;
		np->remote_mac[2]=simple_strtol(cur, NULL, 16);
		cur=delim+1;
		if ((delim = strchr(cur, ':')) == NULL)
			goto parse_failed;
		*delim=0;
		np->remote_mac[3]=simple_strtol(cur, NULL, 16);
		cur=delim+1;
		if ((delim = strchr(cur, ':')) == NULL)
			goto parse_failed;
		*delim=0;
		np->remote_mac[4]=simple_strtol(cur, NULL, 16);
		cur=delim+1;
		np->remote_mac[5]=simple_strtol(cur, NULL, 16);
	}

	printk(KERN_INFO "%s: remote ethernet address "
	       "%02x:%02x:%02x:%02x:%02x:%02x\n",
	       np->name,
	       np->remote_mac[0],
	       np->remote_mac[1],
	       np->remote_mac[2],
	       np->remote_mac[3],
	       np->remote_mac[4],
	       np->remote_mac[5]);

	return 0;

 parse_failed:
	printk(KERN_INFO "%s: couldn't parse config at %s!\n",
	       np->name, cur);
	return -1;
}

int netpoll_setup(struct netpoll *np)
{
	struct net_device *ndev = NULL;
	struct in_device *in_dev;
	struct netpoll_info *npinfo;
	unsigned long flags;

	if (np->dev_name)
		ndev = dev_get_by_name(np->dev_name);
	if (!ndev) {
		printk(KERN_ERR "%s: %s doesn't exist, aborting.\n",
		       np->name, np->dev_name);
		return -1;
	}

	np->dev = ndev;
	if (!ndev->npinfo) {
		npinfo = kmalloc(sizeof(*npinfo), GFP_KERNEL);
		if (!npinfo)
			goto release;

		npinfo->rx_flags = 0;
		npinfo->rx_np = NULL;
		spin_lock_init(&npinfo->poll_lock);
		npinfo->poll_owner = -1;
		npinfo->tries = MAX_RETRIES;
		spin_lock_init(&npinfo->rx_lock);
		skb_queue_head_init(&npinfo->arp_tx);
	} else
		npinfo = ndev->npinfo;

	if (!ndev->poll_controller) {
		printk(KERN_ERR "%s: %s doesn't support polling, aborting.\n",
		       np->name, np->dev_name);
		goto release;
	}

	if (!netif_running(ndev)) {
		unsigned long atmost, atleast;

		printk(KERN_INFO "%s: device %s not up yet, forcing it\n",
		       np->name, np->dev_name);

		rtnl_lock();
		if (dev_change_flags(ndev, ndev->flags | IFF_UP) < 0) {
			printk(KERN_ERR "%s: failed to open %s\n",
			       np->name, np->dev_name);
			rtnl_unlock();
			goto release;
		}
		rtnl_unlock();

		atleast = jiffies + HZ/10;
 		atmost = jiffies + 4*HZ;
		while (!netif_carrier_ok(ndev)) {
			if (time_after(jiffies, atmost)) {
				printk(KERN_NOTICE
				       "%s: timeout waiting for carrier\n",
				       np->name);
				break;
			}
			cond_resched();
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

	if (is_zero_ether_addr(np->local_mac) && ndev->dev_addr)
		memcpy(np->local_mac, ndev->dev_addr, 6);

	if (!np->local_ip) {
		rcu_read_lock();
		in_dev = __in_dev_get_rcu(ndev);

		if (!in_dev || !in_dev->ifa_list) {
			rcu_read_unlock();
			printk(KERN_ERR "%s: no IP address for %s, aborting\n",
			       np->name, np->dev_name);
			goto release;
		}

		np->local_ip = ntohl(in_dev->ifa_list->ifa_local);
		rcu_read_unlock();
		printk(KERN_INFO "%s: local IP %d.%d.%d.%d\n",
		       np->name, HIPQUAD(np->local_ip));
	}

	if (np->rx_hook) {
		spin_lock_irqsave(&npinfo->rx_lock, flags);
		npinfo->rx_flags |= NETPOLL_RX_ENABLED;
		npinfo->rx_np = np;
		spin_unlock_irqrestore(&npinfo->rx_lock, flags);
	}

	/* fill up the skb queue */
	refill_skbs();

	/* last thing to do is link it to the net device structure */
	ndev->npinfo = npinfo;

	/* avoid racing with NAPI reading npinfo */
	synchronize_rcu();

	return 0;

 release:
	if (!ndev->npinfo)
		kfree(npinfo);
	np->dev = NULL;
	dev_put(ndev);
	return -1;
}

void netpoll_cleanup(struct netpoll *np)
{
	struct netpoll_info *npinfo;
	unsigned long flags;

	if (np->dev) {
		npinfo = np->dev->npinfo;
		if (npinfo && npinfo->rx_np == np) {
			spin_lock_irqsave(&npinfo->rx_lock, flags);
			npinfo->rx_np = NULL;
			npinfo->rx_flags &= ~NETPOLL_RX_ENABLED;
			spin_unlock_irqrestore(&npinfo->rx_lock, flags);
		}
		dev_put(np->dev);
	}

	np->dev = NULL;
}

int netpoll_trap(void)
{
	return atomic_read(&trapped);
}

void netpoll_set_trap(int trap)
{
	if (trap)
		atomic_inc(&trapped);
	else
		atomic_dec(&trapped);
}

EXPORT_SYMBOL(netpoll_set_trap);
EXPORT_SYMBOL(netpoll_trap);
EXPORT_SYMBOL(netpoll_parse_options);
EXPORT_SYMBOL(netpoll_setup);
EXPORT_SYMBOL(netpoll_cleanup);
EXPORT_SYMBOL(netpoll_send_udp);
EXPORT_SYMBOL(netpoll_poll);
EXPORT_SYMBOL(netpoll_queue);
