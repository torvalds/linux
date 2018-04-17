/*
 *	AARP:		An implementation of the AppleTalk AARP protocol for
 *			Ethernet 'ELAP'.
 *
 *		Alan Cox  <Alan.Cox@linux.org>
 *
 *	This doesn't fit cleanly with the IP arp. Potentially we can use
 *	the generic neighbour discovery code to clean this up.
 *
 *	FIXME:
 *		We ought to handle the retransmits with a single list and a
 *	separate fast timer for when it is needed.
 *		Use neighbour discovery code.
 *		Token Ring Support.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *
 *	References:
 *		Inside AppleTalk (2nd Ed).
 *	Fixes:
 *		Jaume Grau	-	flush caches on AARP_PROBE
 *		Rob Newberry	-	Added proxy AARP and AARP proc fs,
 *					moved probing from DDP module.
 *		Arnaldo C. Melo -	don't mangle rx packets
 *
 */

#include <linux/if_arp.h>
#include <linux/slab.h>
#include <net/sock.h>
#include <net/datalink.h>
#include <net/psnap.h>
#include <linux/atalk.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/export.h>
#include <linux/etherdevice.h>

int sysctl_aarp_expiry_time = AARP_EXPIRY_TIME;
int sysctl_aarp_tick_time = AARP_TICK_TIME;
int sysctl_aarp_retransmit_limit = AARP_RETRANSMIT_LIMIT;
int sysctl_aarp_resolve_time = AARP_RESOLVE_TIME;

/* Lists of aarp entries */
/**
 *	struct aarp_entry - AARP entry
 *	@last_sent - Last time we xmitted the aarp request
 *	@packet_queue - Queue of frames wait for resolution
 *	@status - Used for proxy AARP
 *	expires_at - Entry expiry time
 *	target_addr - DDP Address
 *	dev - Device to use
 *	hwaddr - Physical i/f address of target/router
 *	xmit_count - When this hits 10 we give up
 *	next - Next entry in chain
 */
struct aarp_entry {
	/* These first two are only used for unresolved entries */
	unsigned long		last_sent;
	struct sk_buff_head	packet_queue;
	int			status;
	unsigned long		expires_at;
	struct atalk_addr	target_addr;
	struct net_device	*dev;
	char			hwaddr[ETH_ALEN];
	unsigned short		xmit_count;
	struct aarp_entry	*next;
};

/* Hashed list of resolved, unresolved and proxy entries */
static struct aarp_entry *resolved[AARP_HASH_SIZE];
static struct aarp_entry *unresolved[AARP_HASH_SIZE];
static struct aarp_entry *proxies[AARP_HASH_SIZE];
static int unresolved_count;

/* One lock protects it all. */
static DEFINE_RWLOCK(aarp_lock);

/* Used to walk the list and purge/kick entries.  */
static struct timer_list aarp_timer;

/*
 *	Delete an aarp queue
 *
 *	Must run under aarp_lock.
 */
static void __aarp_expire(struct aarp_entry *a)
{
	skb_queue_purge(&a->packet_queue);
	kfree(a);
}

/*
 *	Send an aarp queue entry request
 *
 *	Must run under aarp_lock.
 */
static void __aarp_send_query(struct aarp_entry *a)
{
	static unsigned char aarp_eth_multicast[ETH_ALEN] =
					{ 0x09, 0x00, 0x07, 0xFF, 0xFF, 0xFF };
	struct net_device *dev = a->dev;
	struct elapaarp *eah;
	int len = dev->hard_header_len + sizeof(*eah) + aarp_dl->header_length;
	struct sk_buff *skb = alloc_skb(len, GFP_ATOMIC);
	struct atalk_addr *sat = atalk_find_dev_addr(dev);

	if (!skb)
		return;

	if (!sat) {
		kfree_skb(skb);
		return;
	}

	/* Set up the buffer */
	skb_reserve(skb, dev->hard_header_len + aarp_dl->header_length);
	skb_reset_network_header(skb);
	skb_reset_transport_header(skb);
	skb_put(skb, sizeof(*eah));
	skb->protocol    = htons(ETH_P_ATALK);
	skb->dev	 = dev;
	eah		 = aarp_hdr(skb);

	/* Set up the ARP */
	eah->hw_type	 = htons(AARP_HW_TYPE_ETHERNET);
	eah->pa_type	 = htons(ETH_P_ATALK);
	eah->hw_len	 = ETH_ALEN;
	eah->pa_len	 = AARP_PA_ALEN;
	eah->function	 = htons(AARP_REQUEST);

	ether_addr_copy(eah->hw_src, dev->dev_addr);

	eah->pa_src_zero = 0;
	eah->pa_src_net	 = sat->s_net;
	eah->pa_src_node = sat->s_node;

	eth_zero_addr(eah->hw_dst);

	eah->pa_dst_zero = 0;
	eah->pa_dst_net	 = a->target_addr.s_net;
	eah->pa_dst_node = a->target_addr.s_node;

	/* Send it */
	aarp_dl->request(aarp_dl, skb, aarp_eth_multicast);
	/* Update the sending count */
	a->xmit_count++;
	a->last_sent = jiffies;
}

/* This runs under aarp_lock and in softint context, so only atomic memory
 * allocations can be used. */
static void aarp_send_reply(struct net_device *dev, struct atalk_addr *us,
			    struct atalk_addr *them, unsigned char *sha)
{
	struct elapaarp *eah;
	int len = dev->hard_header_len + sizeof(*eah) + aarp_dl->header_length;
	struct sk_buff *skb = alloc_skb(len, GFP_ATOMIC);

	if (!skb)
		return;

	/* Set up the buffer */
	skb_reserve(skb, dev->hard_header_len + aarp_dl->header_length);
	skb_reset_network_header(skb);
	skb_reset_transport_header(skb);
	skb_put(skb, sizeof(*eah));
	skb->protocol    = htons(ETH_P_ATALK);
	skb->dev	 = dev;
	eah		 = aarp_hdr(skb);

	/* Set up the ARP */
	eah->hw_type	 = htons(AARP_HW_TYPE_ETHERNET);
	eah->pa_type	 = htons(ETH_P_ATALK);
	eah->hw_len	 = ETH_ALEN;
	eah->pa_len	 = AARP_PA_ALEN;
	eah->function	 = htons(AARP_REPLY);

	ether_addr_copy(eah->hw_src, dev->dev_addr);

	eah->pa_src_zero = 0;
	eah->pa_src_net	 = us->s_net;
	eah->pa_src_node = us->s_node;

	if (!sha)
		eth_zero_addr(eah->hw_dst);
	else
		ether_addr_copy(eah->hw_dst, sha);

	eah->pa_dst_zero = 0;
	eah->pa_dst_net	 = them->s_net;
	eah->pa_dst_node = them->s_node;

	/* Send it */
	aarp_dl->request(aarp_dl, skb, sha);
}

/*
 *	Send probe frames. Called from aarp_probe_network and
 *	aarp_proxy_probe_network.
 */

static void aarp_send_probe(struct net_device *dev, struct atalk_addr *us)
{
	struct elapaarp *eah;
	int len = dev->hard_header_len + sizeof(*eah) + aarp_dl->header_length;
	struct sk_buff *skb = alloc_skb(len, GFP_ATOMIC);
	static unsigned char aarp_eth_multicast[ETH_ALEN] =
					{ 0x09, 0x00, 0x07, 0xFF, 0xFF, 0xFF };

	if (!skb)
		return;

	/* Set up the buffer */
	skb_reserve(skb, dev->hard_header_len + aarp_dl->header_length);
	skb_reset_network_header(skb);
	skb_reset_transport_header(skb);
	skb_put(skb, sizeof(*eah));
	skb->protocol    = htons(ETH_P_ATALK);
	skb->dev	 = dev;
	eah		 = aarp_hdr(skb);

	/* Set up the ARP */
	eah->hw_type	 = htons(AARP_HW_TYPE_ETHERNET);
	eah->pa_type	 = htons(ETH_P_ATALK);
	eah->hw_len	 = ETH_ALEN;
	eah->pa_len	 = AARP_PA_ALEN;
	eah->function	 = htons(AARP_PROBE);

	ether_addr_copy(eah->hw_src, dev->dev_addr);

	eah->pa_src_zero = 0;
	eah->pa_src_net	 = us->s_net;
	eah->pa_src_node = us->s_node;

	eth_zero_addr(eah->hw_dst);

	eah->pa_dst_zero = 0;
	eah->pa_dst_net	 = us->s_net;
	eah->pa_dst_node = us->s_node;

	/* Send it */
	aarp_dl->request(aarp_dl, skb, aarp_eth_multicast);
}

/*
 *	Handle an aarp timer expire
 *
 *	Must run under the aarp_lock.
 */

static void __aarp_expire_timer(struct aarp_entry **n)
{
	struct aarp_entry *t;

	while (*n)
		/* Expired ? */
		if (time_after(jiffies, (*n)->expires_at)) {
			t = *n;
			*n = (*n)->next;
			__aarp_expire(t);
		} else
			n = &((*n)->next);
}

/*
 *	Kick all pending requests 5 times a second.
 *
 *	Must run under the aarp_lock.
 */
static void __aarp_kick(struct aarp_entry **n)
{
	struct aarp_entry *t;

	while (*n)
		/* Expired: if this will be the 11th tx, we delete instead. */
		if ((*n)->xmit_count >= sysctl_aarp_retransmit_limit) {
			t = *n;
			*n = (*n)->next;
			__aarp_expire(t);
		} else {
			__aarp_send_query(*n);
			n = &((*n)->next);
		}
}

/*
 *	A device has gone down. Take all entries referring to the device
 *	and remove them.
 *
 *	Must run under the aarp_lock.
 */
static void __aarp_expire_device(struct aarp_entry **n, struct net_device *dev)
{
	struct aarp_entry *t;

	while (*n)
		if ((*n)->dev == dev) {
			t = *n;
			*n = (*n)->next;
			__aarp_expire(t);
		} else
			n = &((*n)->next);
}

/* Handle the timer event */
static void aarp_expire_timeout(struct timer_list *unused)
{
	int ct;

	write_lock_bh(&aarp_lock);

	for (ct = 0; ct < AARP_HASH_SIZE; ct++) {
		__aarp_expire_timer(&resolved[ct]);
		__aarp_kick(&unresolved[ct]);
		__aarp_expire_timer(&unresolved[ct]);
		__aarp_expire_timer(&proxies[ct]);
	}

	write_unlock_bh(&aarp_lock);
	mod_timer(&aarp_timer, jiffies +
			       (unresolved_count ? sysctl_aarp_tick_time :
				sysctl_aarp_expiry_time));
}

/* Network device notifier chain handler. */
static int aarp_device_event(struct notifier_block *this, unsigned long event,
			     void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	int ct;

	if (!net_eq(dev_net(dev), &init_net))
		return NOTIFY_DONE;

	if (event == NETDEV_DOWN) {
		write_lock_bh(&aarp_lock);

		for (ct = 0; ct < AARP_HASH_SIZE; ct++) {
			__aarp_expire_device(&resolved[ct], dev);
			__aarp_expire_device(&unresolved[ct], dev);
			__aarp_expire_device(&proxies[ct], dev);
		}

		write_unlock_bh(&aarp_lock);
	}
	return NOTIFY_DONE;
}

/* Expire all entries in a hash chain */
static void __aarp_expire_all(struct aarp_entry **n)
{
	struct aarp_entry *t;

	while (*n) {
		t = *n;
		*n = (*n)->next;
		__aarp_expire(t);
	}
}

/* Cleanup all hash chains -- module unloading */
static void aarp_purge(void)
{
	int ct;

	write_lock_bh(&aarp_lock);
	for (ct = 0; ct < AARP_HASH_SIZE; ct++) {
		__aarp_expire_all(&resolved[ct]);
		__aarp_expire_all(&unresolved[ct]);
		__aarp_expire_all(&proxies[ct]);
	}
	write_unlock_bh(&aarp_lock);
}

/*
 *	Create a new aarp entry.  This must use GFP_ATOMIC because it
 *	runs while holding spinlocks.
 */
static struct aarp_entry *aarp_alloc(void)
{
	struct aarp_entry *a = kmalloc(sizeof(*a), GFP_ATOMIC);

	if (a)
		skb_queue_head_init(&a->packet_queue);
	return a;
}

/*
 * Find an entry. We might return an expired but not yet purged entry. We
 * don't care as it will do no harm.
 *
 * This must run under the aarp_lock.
 */
static struct aarp_entry *__aarp_find_entry(struct aarp_entry *list,
					    struct net_device *dev,
					    struct atalk_addr *sat)
{
	while (list) {
		if (list->target_addr.s_net == sat->s_net &&
		    list->target_addr.s_node == sat->s_node &&
		    list->dev == dev)
			break;
		list = list->next;
	}

	return list;
}

/* Called from the DDP code, and thus must be exported. */
void aarp_proxy_remove(struct net_device *dev, struct atalk_addr *sa)
{
	int hash = sa->s_node % (AARP_HASH_SIZE - 1);
	struct aarp_entry *a;

	write_lock_bh(&aarp_lock);

	a = __aarp_find_entry(proxies[hash], dev, sa);
	if (a)
		a->expires_at = jiffies - 1;

	write_unlock_bh(&aarp_lock);
}

/* This must run under aarp_lock. */
static struct atalk_addr *__aarp_proxy_find(struct net_device *dev,
					    struct atalk_addr *sa)
{
	int hash = sa->s_node % (AARP_HASH_SIZE - 1);
	struct aarp_entry *a = __aarp_find_entry(proxies[hash], dev, sa);

	return a ? sa : NULL;
}

/*
 * Probe a Phase 1 device or a device that requires its Net:Node to
 * be set via an ioctl.
 */
static void aarp_send_probe_phase1(struct atalk_iface *iface)
{
	struct ifreq atreq;
	struct sockaddr_at *sa = (struct sockaddr_at *)&atreq.ifr_addr;
	const struct net_device_ops *ops = iface->dev->netdev_ops;

	sa->sat_addr.s_node = iface->address.s_node;
	sa->sat_addr.s_net = ntohs(iface->address.s_net);

	/* We pass the Net:Node to the drivers/cards by a Device ioctl. */
	if (!(ops->ndo_do_ioctl(iface->dev, &atreq, SIOCSIFADDR))) {
		ops->ndo_do_ioctl(iface->dev, &atreq, SIOCGIFADDR);
		if (iface->address.s_net != htons(sa->sat_addr.s_net) ||
		    iface->address.s_node != sa->sat_addr.s_node)
			iface->status |= ATIF_PROBE_FAIL;

		iface->address.s_net  = htons(sa->sat_addr.s_net);
		iface->address.s_node = sa->sat_addr.s_node;
	}
}


void aarp_probe_network(struct atalk_iface *atif)
{
	if (atif->dev->type == ARPHRD_LOCALTLK ||
	    atif->dev->type == ARPHRD_PPP)
		aarp_send_probe_phase1(atif);
	else {
		unsigned int count;

		for (count = 0; count < AARP_RETRANSMIT_LIMIT; count++) {
			aarp_send_probe(atif->dev, &atif->address);

			/* Defer 1/10th */
			msleep(100);

			if (atif->status & ATIF_PROBE_FAIL)
				break;
		}
	}
}

int aarp_proxy_probe_network(struct atalk_iface *atif, struct atalk_addr *sa)
{
	int hash, retval = -EPROTONOSUPPORT;
	struct aarp_entry *entry;
	unsigned int count;

	/*
	 * we don't currently support LocalTalk or PPP for proxy AARP;
	 * if someone wants to try and add it, have fun
	 */
	if (atif->dev->type == ARPHRD_LOCALTLK ||
	    atif->dev->type == ARPHRD_PPP)
		goto out;

	/*
	 * create a new AARP entry with the flags set to be published --
	 * we need this one to hang around even if it's in use
	 */
	entry = aarp_alloc();
	retval = -ENOMEM;
	if (!entry)
		goto out;

	entry->expires_at = -1;
	entry->status = ATIF_PROBE;
	entry->target_addr.s_node = sa->s_node;
	entry->target_addr.s_net = sa->s_net;
	entry->dev = atif->dev;

	write_lock_bh(&aarp_lock);

	hash = sa->s_node % (AARP_HASH_SIZE - 1);
	entry->next = proxies[hash];
	proxies[hash] = entry;

	for (count = 0; count < AARP_RETRANSMIT_LIMIT; count++) {
		aarp_send_probe(atif->dev, sa);

		/* Defer 1/10th */
		write_unlock_bh(&aarp_lock);
		msleep(100);
		write_lock_bh(&aarp_lock);

		if (entry->status & ATIF_PROBE_FAIL)
			break;
	}

	if (entry->status & ATIF_PROBE_FAIL) {
		entry->expires_at = jiffies - 1; /* free the entry */
		retval = -EADDRINUSE; /* return network full */
	} else { /* clear the probing flag */
		entry->status &= ~ATIF_PROBE;
		retval = 1;
	}

	write_unlock_bh(&aarp_lock);
out:
	return retval;
}

/* Send a DDP frame */
int aarp_send_ddp(struct net_device *dev, struct sk_buff *skb,
		  struct atalk_addr *sa, void *hwaddr)
{
	static char ddp_eth_multicast[ETH_ALEN] =
		{ 0x09, 0x00, 0x07, 0xFF, 0xFF, 0xFF };
	int hash;
	struct aarp_entry *a;

	skb_reset_network_header(skb);

	/* Check for LocalTalk first */
	if (dev->type == ARPHRD_LOCALTLK) {
		struct atalk_addr *at = atalk_find_dev_addr(dev);
		struct ddpehdr *ddp = (struct ddpehdr *)skb->data;
		int ft = 2;

		/*
		 * Compressible ?
		 *
		 * IFF: src_net == dest_net == device_net
		 * (zero matches anything)
		 */

		if ((!ddp->deh_snet || at->s_net == ddp->deh_snet) &&
		    (!ddp->deh_dnet || at->s_net == ddp->deh_dnet)) {
			skb_pull(skb, sizeof(*ddp) - 4);

			/*
			 *	The upper two remaining bytes are the port
			 *	numbers	we just happen to need. Now put the
			 *	length in the lower two.
			 */
			*((__be16 *)skb->data) = htons(skb->len);
			ft = 1;
		}
		/*
		 * Nice and easy. No AARP type protocols occur here so we can
		 * just shovel it out with a 3 byte LLAP header
		 */

		skb_push(skb, 3);
		skb->data[0] = sa->s_node;
		skb->data[1] = at->s_node;
		skb->data[2] = ft;
		skb->dev     = dev;
		goto sendit;
	}

	/* On a PPP link we neither compress nor aarp.  */
	if (dev->type == ARPHRD_PPP) {
		skb->protocol = htons(ETH_P_PPPTALK);
		skb->dev = dev;
		goto sendit;
	}

	/* Non ELAP we cannot do. */
	if (dev->type != ARPHRD_ETHER)
		goto free_it;

	skb->dev = dev;
	skb->protocol = htons(ETH_P_ATALK);
	hash = sa->s_node % (AARP_HASH_SIZE - 1);

	/* Do we have a resolved entry? */
	if (sa->s_node == ATADDR_BCAST) {
		/* Send it */
		ddp_dl->request(ddp_dl, skb, ddp_eth_multicast);
		goto sent;
	}

	write_lock_bh(&aarp_lock);
	a = __aarp_find_entry(resolved[hash], dev, sa);

	if (a) { /* Return 1 and fill in the address */
		a->expires_at = jiffies + (sysctl_aarp_expiry_time * 10);
		ddp_dl->request(ddp_dl, skb, a->hwaddr);
		write_unlock_bh(&aarp_lock);
		goto sent;
	}

	/* Do we have an unresolved entry: This is the less common path */
	a = __aarp_find_entry(unresolved[hash], dev, sa);
	if (a) { /* Queue onto the unresolved queue */
		skb_queue_tail(&a->packet_queue, skb);
		goto out_unlock;
	}

	/* Allocate a new entry */
	a = aarp_alloc();
	if (!a) {
		/* Whoops slipped... good job it's an unreliable protocol 8) */
		write_unlock_bh(&aarp_lock);
		goto free_it;
	}

	/* Set up the queue */
	skb_queue_tail(&a->packet_queue, skb);
	a->expires_at	 = jiffies + sysctl_aarp_resolve_time;
	a->dev		 = dev;
	a->next		 = unresolved[hash];
	a->target_addr	 = *sa;
	a->xmit_count	 = 0;
	unresolved[hash] = a;
	unresolved_count++;

	/* Send an initial request for the address */
	__aarp_send_query(a);

	/*
	 * Switch to fast timer if needed (That is if this is the first
	 * unresolved entry to get added)
	 */

	if (unresolved_count == 1)
		mod_timer(&aarp_timer, jiffies + sysctl_aarp_tick_time);

	/* Now finally, it is safe to drop the lock. */
out_unlock:
	write_unlock_bh(&aarp_lock);

	/* Tell the ddp layer we have taken over for this frame. */
	goto sent;

sendit:
	if (skb->sk)
		skb->priority = skb->sk->sk_priority;
	if (dev_queue_xmit(skb))
		goto drop;
sent:
	return NET_XMIT_SUCCESS;
free_it:
	kfree_skb(skb);
drop:
	return NET_XMIT_DROP;
}
EXPORT_SYMBOL(aarp_send_ddp);

/*
 *	An entry in the aarp unresolved queue has become resolved. Send
 *	all the frames queued under it.
 *
 *	Must run under aarp_lock.
 */
static void __aarp_resolved(struct aarp_entry **list, struct aarp_entry *a,
			    int hash)
{
	struct sk_buff *skb;

	while (*list)
		if (*list == a) {
			unresolved_count--;
			*list = a->next;

			/* Move into the resolved list */
			a->next = resolved[hash];
			resolved[hash] = a;

			/* Kick frames off */
			while ((skb = skb_dequeue(&a->packet_queue)) != NULL) {
				a->expires_at = jiffies +
						sysctl_aarp_expiry_time * 10;
				ddp_dl->request(ddp_dl, skb, a->hwaddr);
			}
		} else
			list = &((*list)->next);
}

/*
 *	This is called by the SNAP driver whenever we see an AARP SNAP
 *	frame. We currently only support Ethernet.
 */
static int aarp_rcv(struct sk_buff *skb, struct net_device *dev,
		    struct packet_type *pt, struct net_device *orig_dev)
{
	struct elapaarp *ea = aarp_hdr(skb);
	int hash, ret = 0;
	__u16 function;
	struct aarp_entry *a;
	struct atalk_addr sa, *ma, da;
	struct atalk_iface *ifa;

	if (!net_eq(dev_net(dev), &init_net))
		goto out0;

	/* We only do Ethernet SNAP AARP. */
	if (dev->type != ARPHRD_ETHER)
		goto out0;

	/* Frame size ok? */
	if (!skb_pull(skb, sizeof(*ea)))
		goto out0;

	function = ntohs(ea->function);

	/* Sanity check fields. */
	if (function < AARP_REQUEST || function > AARP_PROBE ||
	    ea->hw_len != ETH_ALEN || ea->pa_len != AARP_PA_ALEN ||
	    ea->pa_src_zero || ea->pa_dst_zero)
		goto out0;

	/* Looks good. */
	hash = ea->pa_src_node % (AARP_HASH_SIZE - 1);

	/* Build an address. */
	sa.s_node = ea->pa_src_node;
	sa.s_net = ea->pa_src_net;

	/* Process the packet. Check for replies of me. */
	ifa = atalk_find_dev(dev);
	if (!ifa)
		goto out1;

	if (ifa->status & ATIF_PROBE &&
	    ifa->address.s_node == ea->pa_dst_node &&
	    ifa->address.s_net == ea->pa_dst_net) {
		ifa->status |= ATIF_PROBE_FAIL; /* Fail the probe (in use) */
		goto out1;
	}

	/* Check for replies of proxy AARP entries */
	da.s_node = ea->pa_dst_node;
	da.s_net  = ea->pa_dst_net;

	write_lock_bh(&aarp_lock);
	a = __aarp_find_entry(proxies[hash], dev, &da);

	if (a && a->status & ATIF_PROBE) {
		a->status |= ATIF_PROBE_FAIL;
		/*
		 * we do not respond to probe or request packets for
		 * this address while we are probing this address
		 */
		goto unlock;
	}

	switch (function) {
	case AARP_REPLY:
		if (!unresolved_count)	/* Speed up */
			break;

		/* Find the entry.  */
		a = __aarp_find_entry(unresolved[hash], dev, &sa);
		if (!a || dev != a->dev)
			break;

		/* We can fill one in - this is good. */
		ether_addr_copy(a->hwaddr, ea->hw_src);
		__aarp_resolved(&unresolved[hash], a, hash);
		if (!unresolved_count)
			mod_timer(&aarp_timer,
				  jiffies + sysctl_aarp_expiry_time);
		break;

	case AARP_REQUEST:
	case AARP_PROBE:

		/*
		 * If it is my address set ma to my address and reply.
		 * We can treat probe and request the same.  Probe
		 * simply means we shouldn't cache the querying host,
		 * as in a probe they are proposing an address not
		 * using one.
		 *
		 * Support for proxy-AARP added. We check if the
		 * address is one of our proxies before we toss the
		 * packet out.
		 */

		sa.s_node = ea->pa_dst_node;
		sa.s_net  = ea->pa_dst_net;

		/* See if we have a matching proxy. */
		ma = __aarp_proxy_find(dev, &sa);
		if (!ma)
			ma = &ifa->address;
		else { /* We need to make a copy of the entry. */
			da.s_node = sa.s_node;
			da.s_net = sa.s_net;
			ma = &da;
		}

		if (function == AARP_PROBE) {
			/*
			 * A probe implies someone trying to get an
			 * address. So as a precaution flush any
			 * entries we have for this address.
			 */
			a = __aarp_find_entry(resolved[sa.s_node %
						       (AARP_HASH_SIZE - 1)],
					      skb->dev, &sa);

			/*
			 * Make it expire next tick - that avoids us
			 * getting into a probe/flush/learn/probe/
			 * flush/learn cycle during probing of a slow
			 * to respond host addr.
			 */
			if (a) {
				a->expires_at = jiffies - 1;
				mod_timer(&aarp_timer, jiffies +
					  sysctl_aarp_tick_time);
			}
		}

		if (sa.s_node != ma->s_node)
			break;

		if (sa.s_net && ma->s_net && sa.s_net != ma->s_net)
			break;

		sa.s_node = ea->pa_src_node;
		sa.s_net = ea->pa_src_net;

		/* aarp_my_address has found the address to use for us.
		 */
		aarp_send_reply(dev, ma, &sa, ea->hw_src);
		break;
	}

unlock:
	write_unlock_bh(&aarp_lock);
out1:
	ret = 1;
out0:
	kfree_skb(skb);
	return ret;
}

static struct notifier_block aarp_notifier = {
	.notifier_call = aarp_device_event,
};

static unsigned char aarp_snap_id[] = { 0x00, 0x00, 0x00, 0x80, 0xF3 };

void __init aarp_proto_init(void)
{
	aarp_dl = register_snap_client(aarp_snap_id, aarp_rcv);
	if (!aarp_dl)
		printk(KERN_CRIT "Unable to register AARP with SNAP.\n");
	timer_setup(&aarp_timer, aarp_expire_timeout, 0);
	aarp_timer.expires  = jiffies + sysctl_aarp_expiry_time;
	add_timer(&aarp_timer);
	register_netdevice_notifier(&aarp_notifier);
}

/* Remove the AARP entries associated with a device. */
void aarp_device_down(struct net_device *dev)
{
	int ct;

	write_lock_bh(&aarp_lock);

	for (ct = 0; ct < AARP_HASH_SIZE; ct++) {
		__aarp_expire_device(&resolved[ct], dev);
		__aarp_expire_device(&unresolved[ct], dev);
		__aarp_expire_device(&proxies[ct], dev);
	}

	write_unlock_bh(&aarp_lock);
}

#ifdef CONFIG_PROC_FS
struct aarp_iter_state {
	int bucket;
	struct aarp_entry **table;
};

/*
 * Get the aarp entry that is in the chain described
 * by the iterator.
 * If pos is set then skip till that index.
 * pos = 1 is the first entry
 */
static struct aarp_entry *iter_next(struct aarp_iter_state *iter, loff_t *pos)
{
	int ct = iter->bucket;
	struct aarp_entry **table = iter->table;
	loff_t off = 0;
	struct aarp_entry *entry;

 rescan:
	while (ct < AARP_HASH_SIZE) {
		for (entry = table[ct]; entry; entry = entry->next) {
			if (!pos || ++off == *pos) {
				iter->table = table;
				iter->bucket = ct;
				return entry;
			}
		}
		++ct;
	}

	if (table == resolved) {
		ct = 0;
		table = unresolved;
		goto rescan;
	}
	if (table == unresolved) {
		ct = 0;
		table = proxies;
		goto rescan;
	}
	return NULL;
}

static void *aarp_seq_start(struct seq_file *seq, loff_t *pos)
	__acquires(aarp_lock)
{
	struct aarp_iter_state *iter = seq->private;

	read_lock_bh(&aarp_lock);
	iter->table     = resolved;
	iter->bucket    = 0;

	return *pos ? iter_next(iter, pos) : SEQ_START_TOKEN;
}

static void *aarp_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct aarp_entry *entry = v;
	struct aarp_iter_state *iter = seq->private;

	++*pos;

	/* first line after header */
	if (v == SEQ_START_TOKEN)
		entry = iter_next(iter, NULL);

	/* next entry in current bucket */
	else if (entry->next)
		entry = entry->next;

	/* next bucket or table */
	else {
		++iter->bucket;
		entry = iter_next(iter, NULL);
	}
	return entry;
}

static void aarp_seq_stop(struct seq_file *seq, void *v)
	__releases(aarp_lock)
{
	read_unlock_bh(&aarp_lock);
}

static const char *dt2str(unsigned long ticks)
{
	static char buf[32];

	sprintf(buf, "%ld.%02ld", ticks / HZ, ((ticks % HZ) * 100) / HZ);

	return buf;
}

static int aarp_seq_show(struct seq_file *seq, void *v)
{
	struct aarp_iter_state *iter = seq->private;
	struct aarp_entry *entry = v;
	unsigned long now = jiffies;

	if (v == SEQ_START_TOKEN)
		seq_puts(seq,
			 "Address  Interface   Hardware Address"
			 "   Expires LastSend  Retry Status\n");
	else {
		seq_printf(seq, "%04X:%02X  %-12s",
			   ntohs(entry->target_addr.s_net),
			   (unsigned int) entry->target_addr.s_node,
			   entry->dev ? entry->dev->name : "????");
		seq_printf(seq, "%pM", entry->hwaddr);
		seq_printf(seq, " %8s",
			   dt2str((long)entry->expires_at - (long)now));
		if (iter->table == unresolved)
			seq_printf(seq, " %8s %6hu",
				   dt2str(now - entry->last_sent),
				   entry->xmit_count);
		else
			seq_puts(seq, "                ");
		seq_printf(seq, " %s\n",
			   (iter->table == resolved) ? "resolved"
			   : (iter->table == unresolved) ? "unresolved"
			   : (iter->table == proxies) ? "proxies"
			   : "unknown");
	}
	return 0;
}

static const struct seq_operations aarp_seq_ops = {
	.start  = aarp_seq_start,
	.next   = aarp_seq_next,
	.stop   = aarp_seq_stop,
	.show   = aarp_seq_show,
};

static int aarp_seq_open(struct inode *inode, struct file *file)
{
	return seq_open_private(file, &aarp_seq_ops,
			sizeof(struct aarp_iter_state));
}

const struct file_operations atalk_seq_arp_fops = {
	.owner		= THIS_MODULE,
	.open           = aarp_seq_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release	= seq_release_private,
};
#endif

/* General module cleanup. Called from cleanup_module() in ddp.c. */
void aarp_cleanup_module(void)
{
	del_timer_sync(&aarp_timer);
	unregister_netdevice_notifier(&aarp_notifier);
	unregister_snap_client(aarp_dl);
	aarp_purge();
}
