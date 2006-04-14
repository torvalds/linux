/* net/atm/clip.c - RFC1577 Classical IP over ATM */

/* Written 1995-2000 by Werner Almesberger, EPFL LRC/ICA */


#include <linux/config.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/kernel.h> /* for UINT_MAX */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/wait.h>
#include <linux/timer.h>
#include <linux/if_arp.h> /* for some manifest constants */
#include <linux/notifier.h>
#include <linux/atm.h>
#include <linux/atmdev.h>
#include <linux/atmclip.h>
#include <linux/atmarp.h>
#include <linux/capability.h>
#include <linux/ip.h> /* for net/route.h */
#include <linux/in.h> /* for struct sockaddr_in */
#include <linux/if.h> /* for IFF_UP */
#include <linux/inetdevice.h>
#include <linux/bitops.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/rcupdate.h>
#include <linux/jhash.h>
#include <net/route.h> /* for struct rtable and routing */
#include <net/icmp.h> /* icmp_send */
#include <asm/param.h> /* for HZ */
#include <asm/byteorder.h> /* for htons etc. */
#include <asm/system.h> /* save/restore_flags */
#include <asm/uaccess.h>
#include <asm/atomic.h>

#include "common.h"
#include "resources.h"
#include "ipcommon.h"
#include <net/atmclip.h>


#if 0
#define DPRINTK(format,args...) printk(format,##args)
#else
#define DPRINTK(format,args...)
#endif


static struct net_device *clip_devs;
static struct atm_vcc *atmarpd;
static struct neigh_table clip_tbl;
static struct timer_list idle_timer;

static int to_atmarpd(enum atmarp_ctrl_type type,int itf,unsigned long ip)
{
	struct sock *sk;
	struct atmarp_ctrl *ctrl;
	struct sk_buff *skb;

	DPRINTK("to_atmarpd(%d)\n",type);
	if (!atmarpd) return -EUNATCH;
	skb = alloc_skb(sizeof(struct atmarp_ctrl),GFP_ATOMIC);
	if (!skb) return -ENOMEM;
	ctrl = (struct atmarp_ctrl *) skb_put(skb,sizeof(struct atmarp_ctrl));
	ctrl->type = type;
	ctrl->itf_num = itf;
	ctrl->ip = ip;
	atm_force_charge(atmarpd,skb->truesize);

	sk = sk_atm(atmarpd);
	skb_queue_tail(&sk->sk_receive_queue, skb);
	sk->sk_data_ready(sk, skb->len);
	return 0;
}


static void link_vcc(struct clip_vcc *clip_vcc,struct atmarp_entry *entry)
{
	DPRINTK("link_vcc %p to entry %p (neigh %p)\n",clip_vcc,entry,
	    entry->neigh);
	clip_vcc->entry = entry;
	clip_vcc->xoff = 0; /* @@@ may overrun buffer by one packet */
	clip_vcc->next = entry->vccs;
	entry->vccs = clip_vcc;
	entry->neigh->used = jiffies;
}


static void unlink_clip_vcc(struct clip_vcc *clip_vcc)
{
	struct atmarp_entry *entry = clip_vcc->entry;
	struct clip_vcc **walk;

	if (!entry) {
		printk(KERN_CRIT "!clip_vcc->entry (clip_vcc %p)\n",clip_vcc);
		return;
	}
	spin_lock_bh(&entry->neigh->dev->xmit_lock);	/* block clip_start_xmit() */
	entry->neigh->used = jiffies;
	for (walk = &entry->vccs; *walk; walk = &(*walk)->next)
		if (*walk == clip_vcc) {
			int error;

			*walk = clip_vcc->next; /* atomic */
			clip_vcc->entry = NULL;
			if (clip_vcc->xoff)
				netif_wake_queue(entry->neigh->dev);
			if (entry->vccs)
				goto out;
			entry->expires = jiffies-1;
				/* force resolution or expiration */
			error = neigh_update(entry->neigh, NULL, NUD_NONE,
					     NEIGH_UPDATE_F_ADMIN);
			if (error)
				printk(KERN_CRIT "unlink_clip_vcc: "
				    "neigh_update failed with %d\n",error);
			goto out;
		}
	printk(KERN_CRIT "ATMARP: unlink_clip_vcc failed (entry %p, vcc "
	  "0x%p)\n",entry,clip_vcc);
out:
	spin_unlock_bh(&entry->neigh->dev->xmit_lock);
}

/* The neighbour entry n->lock is held. */
static int neigh_check_cb(struct neighbour *n)
{
	struct atmarp_entry *entry = NEIGH2ENTRY(n);
	struct clip_vcc *cv;

	for (cv = entry->vccs; cv; cv = cv->next) {
		unsigned long exp = cv->last_use + cv->idle_timeout;

		if (cv->idle_timeout && time_after(jiffies, exp)) {
			DPRINTK("releasing vcc %p->%p of entry %p\n",
				cv, cv->vcc, entry);
			vcc_release_async(cv->vcc, -ETIMEDOUT);
		}
	}

	if (entry->vccs || time_before(jiffies, entry->expires))
		return 0;

	if (atomic_read(&n->refcnt) > 1) {
		struct sk_buff *skb;

		DPRINTK("destruction postponed with ref %d\n",
			atomic_read(&n->refcnt));

		while ((skb = skb_dequeue(&n->arp_queue)) != NULL) 
			dev_kfree_skb(skb);

		return 0;
	}

	DPRINTK("expired neigh %p\n",n);
	return 1;
}

static void idle_timer_check(unsigned long dummy)
{
	write_lock(&clip_tbl.lock);
	__neigh_for_each_release(&clip_tbl, neigh_check_cb);
	mod_timer(&idle_timer, jiffies+CLIP_CHECK_INTERVAL*HZ);
	write_unlock(&clip_tbl.lock);
}

static int clip_arp_rcv(struct sk_buff *skb)
{
	struct atm_vcc *vcc;

	DPRINTK("clip_arp_rcv\n");
	vcc = ATM_SKB(skb)->vcc;
	if (!vcc || !atm_charge(vcc,skb->truesize)) {
		dev_kfree_skb_any(skb);
		return 0;
	}
	DPRINTK("pushing to %p\n",vcc);
	DPRINTK("using %p\n",CLIP_VCC(vcc)->old_push);
	CLIP_VCC(vcc)->old_push(vcc,skb);
	return 0;
}

static const unsigned char llc_oui[] = {
	0xaa,	/* DSAP: non-ISO */
	0xaa,	/* SSAP: non-ISO */
	0x03,	/* Ctrl: Unnumbered Information Command PDU */
	0x00,	/* OUI: EtherType */
	0x00,
	0x00 };

static void clip_push(struct atm_vcc *vcc,struct sk_buff *skb)
{
	struct clip_vcc *clip_vcc = CLIP_VCC(vcc);

	DPRINTK("clip push\n");
	if (!skb) {
		DPRINTK("removing VCC %p\n",clip_vcc);
		if (clip_vcc->entry) unlink_clip_vcc(clip_vcc);
		clip_vcc->old_push(vcc,NULL); /* pass on the bad news */
		kfree(clip_vcc);
		return;
	}
	atm_return(vcc,skb->truesize);
	skb->dev = clip_vcc->entry ? clip_vcc->entry->neigh->dev : clip_devs;
		/* clip_vcc->entry == NULL if we don't have an IP address yet */
	if (!skb->dev) {
		dev_kfree_skb_any(skb);
		return;
	}
	ATM_SKB(skb)->vcc = vcc;
	skb->mac.raw = skb->data;
	if (!clip_vcc->encap || skb->len < RFC1483LLC_LEN || memcmp(skb->data,
	    llc_oui,sizeof(llc_oui))) skb->protocol = htons(ETH_P_IP);
	else {
		skb->protocol = ((u16 *) skb->data)[3];
		skb_pull(skb,RFC1483LLC_LEN);
		if (skb->protocol == htons(ETH_P_ARP)) {
			PRIV(skb->dev)->stats.rx_packets++;
			PRIV(skb->dev)->stats.rx_bytes += skb->len;
			clip_arp_rcv(skb);
			return;
		}
	}
	clip_vcc->last_use = jiffies;
	PRIV(skb->dev)->stats.rx_packets++;
	PRIV(skb->dev)->stats.rx_bytes += skb->len;
	memset(ATM_SKB(skb), 0, sizeof(struct atm_skb_data));
	netif_rx(skb);
}


/*
 * Note: these spinlocks _must_not_ block on non-SMP. The only goal is that
 * clip_pop is atomic with respect to the critical section in clip_start_xmit.
 */


static void clip_pop(struct atm_vcc *vcc,struct sk_buff *skb)
{
	struct clip_vcc *clip_vcc = CLIP_VCC(vcc);
	struct net_device *dev = skb->dev;
	int old;
	unsigned long flags;

	DPRINTK("clip_pop(vcc %p)\n",vcc);
	clip_vcc->old_pop(vcc,skb);
	/* skb->dev == NULL in outbound ARP packets */
	if (!dev) return;
	spin_lock_irqsave(&PRIV(dev)->xoff_lock,flags);
	if (atm_may_send(vcc,0)) {
		old = xchg(&clip_vcc->xoff,0);
		if (old) netif_wake_queue(dev);
	}
	spin_unlock_irqrestore(&PRIV(dev)->xoff_lock,flags);
}


static void clip_neigh_destroy(struct neighbour *neigh)
{
	DPRINTK("clip_neigh_destroy (neigh %p)\n",neigh);
	if (NEIGH2ENTRY(neigh)->vccs)
		printk(KERN_CRIT "clip_neigh_destroy: vccs != NULL !!!\n");
	NEIGH2ENTRY(neigh)->vccs = (void *) 0xdeadbeef;
}


static void clip_neigh_solicit(struct neighbour *neigh,struct sk_buff *skb)
{
	DPRINTK("clip_neigh_solicit (neigh %p, skb %p)\n",neigh,skb);
	to_atmarpd(act_need,PRIV(neigh->dev)->number,NEIGH2ENTRY(neigh)->ip);
}


static void clip_neigh_error(struct neighbour *neigh,struct sk_buff *skb)
{
#ifndef CONFIG_ATM_CLIP_NO_ICMP
	icmp_send(skb,ICMP_DEST_UNREACH,ICMP_HOST_UNREACH,0);
#endif
	kfree_skb(skb);
}


static struct neigh_ops clip_neigh_ops = {
	.family =		AF_INET,
	.solicit =		clip_neigh_solicit,
	.error_report =		clip_neigh_error,
	.output =		dev_queue_xmit,
	.connected_output =	dev_queue_xmit,
	.hh_output =		dev_queue_xmit,
	.queue_xmit =		dev_queue_xmit,
};


static int clip_constructor(struct neighbour *neigh)
{
	struct atmarp_entry *entry = NEIGH2ENTRY(neigh);
	struct net_device *dev = neigh->dev;
	struct in_device *in_dev;
	struct neigh_parms *parms;

	DPRINTK("clip_constructor (neigh %p, entry %p)\n",neigh,entry);
	neigh->type = inet_addr_type(entry->ip);
	if (neigh->type != RTN_UNICAST) return -EINVAL;

	rcu_read_lock();
	in_dev = __in_dev_get_rcu(dev);
	if (!in_dev) {
		rcu_read_unlock();
		return -EINVAL;
	}

	parms = in_dev->arp_parms;
	__neigh_parms_put(neigh->parms);
	neigh->parms = neigh_parms_clone(parms);
	rcu_read_unlock();

	neigh->ops = &clip_neigh_ops;
	neigh->output = neigh->nud_state & NUD_VALID ?
	    neigh->ops->connected_output : neigh->ops->output;
	entry->neigh = neigh;
	entry->vccs = NULL;
	entry->expires = jiffies-1;
	return 0;
}

static u32 clip_hash(const void *pkey, const struct net_device *dev)
{
	return jhash_2words(*(u32 *)pkey, dev->ifindex, clip_tbl.hash_rnd);
}

static struct neigh_table clip_tbl = {
	.family 	= AF_INET,
	.entry_size 	= sizeof(struct neighbour)+sizeof(struct atmarp_entry),
	.key_len 	= 4,
	.hash 		= clip_hash,
	.constructor 	= clip_constructor,
	.id 		= "clip_arp_cache",

	/* parameters are copied from ARP ... */
	.parms = {
		.tbl 			= &clip_tbl,
		.neigh_destructor	= clip_neigh_destroy,
		.base_reachable_time 	= 30 * HZ,
		.retrans_time 		= 1 * HZ,
		.gc_staletime 		= 60 * HZ,
		.reachable_time 	= 30 * HZ,
		.delay_probe_time 	= 5 * HZ,
		.queue_len 		= 3,
		.ucast_probes 		= 3,
		.mcast_probes 		= 3,
		.anycast_delay 		= 1 * HZ,
		.proxy_delay 		= (8 * HZ) / 10,
		.proxy_qlen 		= 64,
		.locktime 		= 1 * HZ,
	},
	.gc_interval 	= 30 * HZ,
	.gc_thresh1 	= 128,
	.gc_thresh2 	= 512,
	.gc_thresh3 	= 1024,
};


/* @@@ copy bh locking from arp.c -- need to bh-enable atm code before */

/*
 * We play with the resolve flag: 0 and 1 have the usual meaning, but -1 means
 * to allocate the neighbour entry but not to ask atmarpd for resolution. Also,
 * don't increment the usage count. This is used to create entries in
 * clip_setentry.
 */


static int clip_encap(struct atm_vcc *vcc,int mode)
{
	CLIP_VCC(vcc)->encap = mode;
	return 0;
}


static int clip_start_xmit(struct sk_buff *skb,struct net_device *dev)
{
	struct clip_priv *clip_priv = PRIV(dev);
	struct atmarp_entry *entry;
	struct atm_vcc *vcc;
	int old;
	unsigned long flags;

	DPRINTK("clip_start_xmit (skb %p)\n",skb);
	if (!skb->dst) {
		printk(KERN_ERR "clip_start_xmit: skb->dst == NULL\n");
		dev_kfree_skb(skb);
		clip_priv->stats.tx_dropped++;
		return 0;
	}
	if (!skb->dst->neighbour) {
#if 0
		skb->dst->neighbour = clip_find_neighbour(skb->dst,1);
		if (!skb->dst->neighbour) {
			dev_kfree_skb(skb); /* lost that one */
			clip_priv->stats.tx_dropped++;
			return 0;
		}
#endif
		printk(KERN_ERR "clip_start_xmit: NO NEIGHBOUR !\n");
		dev_kfree_skb(skb);
		clip_priv->stats.tx_dropped++;
		return 0;
	}
	entry = NEIGH2ENTRY(skb->dst->neighbour);
	if (!entry->vccs) {
		if (time_after(jiffies, entry->expires)) {
			/* should be resolved */
			entry->expires = jiffies+ATMARP_RETRY_DELAY*HZ;
			to_atmarpd(act_need,PRIV(dev)->number,entry->ip);
		}
		if (entry->neigh->arp_queue.qlen < ATMARP_MAX_UNRES_PACKETS)
			skb_queue_tail(&entry->neigh->arp_queue,skb);
		else {
			dev_kfree_skb(skb);
			clip_priv->stats.tx_dropped++;
		}
		return 0;
	}
	DPRINTK("neigh %p, vccs %p\n",entry,entry->vccs);
	ATM_SKB(skb)->vcc = vcc = entry->vccs->vcc;
	DPRINTK("using neighbour %p, vcc %p\n",skb->dst->neighbour,vcc);
	if (entry->vccs->encap) {
		void *here;

		here = skb_push(skb,RFC1483LLC_LEN);
		memcpy(here,llc_oui,sizeof(llc_oui));
		((u16 *) here)[3] = skb->protocol;
	}
	atomic_add(skb->truesize, &sk_atm(vcc)->sk_wmem_alloc);
	ATM_SKB(skb)->atm_options = vcc->atm_options;
	entry->vccs->last_use = jiffies;
	DPRINTK("atm_skb(%p)->vcc(%p)->dev(%p)\n",skb,vcc,vcc->dev);
	old = xchg(&entry->vccs->xoff,1); /* assume XOFF ... */
	if (old) {
		printk(KERN_WARNING "clip_start_xmit: XOFF->XOFF transition\n");
		return 0;
	}
	clip_priv->stats.tx_packets++;
	clip_priv->stats.tx_bytes += skb->len;
	(void) vcc->send(vcc,skb);
	if (atm_may_send(vcc,0)) {
		entry->vccs->xoff = 0;
		return 0;
	}
	spin_lock_irqsave(&clip_priv->xoff_lock,flags);
	netif_stop_queue(dev); /* XOFF -> throttle immediately */
	barrier();
	if (!entry->vccs->xoff)
		netif_start_queue(dev);
		/* Oh, we just raced with clip_pop. netif_start_queue should be
		   good enough, because nothing should really be asleep because
		   of the brief netif_stop_queue. If this isn't true or if it
		   changes, use netif_wake_queue instead. */
	spin_unlock_irqrestore(&clip_priv->xoff_lock,flags);
	return 0;
}


static struct net_device_stats *clip_get_stats(struct net_device *dev)
{
	return &PRIV(dev)->stats;
}


static int clip_mkip(struct atm_vcc *vcc,int timeout)
{
	struct clip_vcc *clip_vcc;
	struct sk_buff_head copy;
	struct sk_buff *skb;

	if (!vcc->push) return -EBADFD;
	clip_vcc = kmalloc(sizeof(struct clip_vcc),GFP_KERNEL);
	if (!clip_vcc) return -ENOMEM;
	DPRINTK("mkip clip_vcc %p vcc %p\n",clip_vcc,vcc);
	clip_vcc->vcc = vcc;
	vcc->user_back = clip_vcc;
	set_bit(ATM_VF_IS_CLIP, &vcc->flags);
	clip_vcc->entry = NULL;
	clip_vcc->xoff = 0;
	clip_vcc->encap = 1;
	clip_vcc->last_use = jiffies;
	clip_vcc->idle_timeout = timeout*HZ;
	clip_vcc->old_push = vcc->push;
	clip_vcc->old_pop = vcc->pop;
	vcc->push = clip_push;
	vcc->pop = clip_pop;
	skb_queue_head_init(&copy);
	skb_migrate(&sk_atm(vcc)->sk_receive_queue, &copy);
	/* re-process everything received between connection setup and MKIP */
	while ((skb = skb_dequeue(&copy)) != NULL)
		if (!clip_devs) {
			atm_return(vcc,skb->truesize);
			kfree_skb(skb);
		}
		else {
			unsigned int len = skb->len;

			clip_push(vcc,skb);
			PRIV(skb->dev)->stats.rx_packets--;
			PRIV(skb->dev)->stats.rx_bytes -= len;
		}
	return 0;
}


static int clip_setentry(struct atm_vcc *vcc,u32 ip)
{
	struct neighbour *neigh;
	struct atmarp_entry *entry;
	int error;
	struct clip_vcc *clip_vcc;
	struct flowi fl = { .nl_u = { .ip4_u = { .daddr = ip, .tos = 1 } } };
	struct rtable *rt;

	if (vcc->push != clip_push) {
		printk(KERN_WARNING "clip_setentry: non-CLIP VCC\n");
		return -EBADF;
	}
	clip_vcc = CLIP_VCC(vcc);
	if (!ip) {
		if (!clip_vcc->entry) {
			printk(KERN_ERR "hiding hidden ATMARP entry\n");
			return 0;
		}
		DPRINTK("setentry: remove\n");
		unlink_clip_vcc(clip_vcc);
		return 0;
	}
	error = ip_route_output_key(&rt,&fl);
	if (error) return error;
	neigh = __neigh_lookup(&clip_tbl,&ip,rt->u.dst.dev,1);
	ip_rt_put(rt);
	if (!neigh)
		return -ENOMEM;
	entry = NEIGH2ENTRY(neigh);
	if (entry != clip_vcc->entry) {
		if (!clip_vcc->entry) DPRINTK("setentry: add\n");
		else {
			DPRINTK("setentry: update\n");
			unlink_clip_vcc(clip_vcc);
		}
		link_vcc(clip_vcc,entry);
	}
	error = neigh_update(neigh, llc_oui, NUD_PERMANENT, 
			     NEIGH_UPDATE_F_OVERRIDE|NEIGH_UPDATE_F_ADMIN);
	neigh_release(neigh);
	return error;
}


static void clip_setup(struct net_device *dev)
{
	dev->hard_start_xmit = clip_start_xmit;
	/* sg_xmit ... */
	dev->get_stats = clip_get_stats;
	dev->type = ARPHRD_ATM;
	dev->hard_header_len = RFC1483LLC_LEN;
	dev->mtu = RFC1626_MTU;
	dev->tx_queue_len = 100; /* "normal" queue (packets) */
	    /* When using a "real" qdisc, the qdisc determines the queue */
	    /* length. tx_queue_len is only used for the default case, */
	    /* without any more elaborate queuing. 100 is a reasonable */
	    /* compromise between decent burst-tolerance and protection */
	    /* against memory hogs. */
}


static int clip_create(int number)
{
	struct net_device *dev;
	struct clip_priv *clip_priv;
	int error;

	if (number != -1) {
		for (dev = clip_devs; dev; dev = PRIV(dev)->next)
			if (PRIV(dev)->number == number) return -EEXIST;
	}
	else {
		number = 0;
		for (dev = clip_devs; dev; dev = PRIV(dev)->next)
			if (PRIV(dev)->number >= number)
				number = PRIV(dev)->number+1;
	}
	dev = alloc_netdev(sizeof(struct clip_priv), "", clip_setup);
	if (!dev)
		return -ENOMEM;
	clip_priv = PRIV(dev);
	sprintf(dev->name,"atm%d",number);
	spin_lock_init(&clip_priv->xoff_lock);
	clip_priv->number = number;
	error = register_netdev(dev);
	if (error) {
		free_netdev(dev);
		return error;
	}
	clip_priv->next = clip_devs;
	clip_devs = dev;
	DPRINTK("registered (net:%s)\n",dev->name);
	return number;
}


static int clip_device_event(struct notifier_block *this,unsigned long event,
			     void *arg)
{
	struct net_device *dev = arg;

	if (event == NETDEV_UNREGISTER) {
		neigh_ifdown(&clip_tbl, dev);
		return NOTIFY_DONE;
	}

	/* ignore non-CLIP devices */
	if (dev->type != ARPHRD_ATM || dev->hard_start_xmit != clip_start_xmit)
		return NOTIFY_DONE;

	switch (event) {
		case NETDEV_UP:
			DPRINTK("clip_device_event NETDEV_UP\n");
			(void) to_atmarpd(act_up,PRIV(dev)->number,0);
			break;
		case NETDEV_GOING_DOWN:
			DPRINTK("clip_device_event NETDEV_DOWN\n");
			(void) to_atmarpd(act_down,PRIV(dev)->number,0);
			break;
		case NETDEV_CHANGE:
		case NETDEV_CHANGEMTU:
			DPRINTK("clip_device_event NETDEV_CHANGE*\n");
			(void) to_atmarpd(act_change,PRIV(dev)->number,0);
			break;
		case NETDEV_REBOOT:
		case NETDEV_REGISTER:
		case NETDEV_DOWN:
			DPRINTK("clip_device_event %ld\n",event);
			/* ignore */
			break;
		default:
			printk(KERN_WARNING "clip_device_event: unknown event "
			    "%ld\n",event);
			break;
	}
	return NOTIFY_DONE;
}


static int clip_inet_event(struct notifier_block *this,unsigned long event,
    void *ifa)
{
	struct in_device *in_dev;

	in_dev = ((struct in_ifaddr *) ifa)->ifa_dev;
	if (!in_dev || !in_dev->dev) {
		printk(KERN_WARNING "clip_inet_event: no device\n");
		return NOTIFY_DONE;
	}
	/*
	 * Transitions are of the down-change-up type, so it's sufficient to
	 * handle the change on up.
	 */
	if (event != NETDEV_UP) return NOTIFY_DONE;
	return clip_device_event(this,NETDEV_CHANGE,in_dev->dev);
}


static struct notifier_block clip_dev_notifier = {
	clip_device_event,
	NULL,
	0
};



static struct notifier_block clip_inet_notifier = {
	clip_inet_event,
	NULL,
	0
};



static void atmarpd_close(struct atm_vcc *vcc)
{
	DPRINTK("atmarpd_close\n");

	rtnl_lock();
	atmarpd = NULL;
	skb_queue_purge(&sk_atm(vcc)->sk_receive_queue);
	rtnl_unlock();

	DPRINTK("(done)\n");
	module_put(THIS_MODULE);
}


static struct atmdev_ops atmarpd_dev_ops = {
	.close = atmarpd_close
};


static struct atm_dev atmarpd_dev = {
	.ops =			&atmarpd_dev_ops,
	.type =			"arpd",
	.number = 		999,
	.lock =			SPIN_LOCK_UNLOCKED
};


static int atm_init_atmarp(struct atm_vcc *vcc)
{
	rtnl_lock();
	if (atmarpd) {
		rtnl_unlock();
		return -EADDRINUSE;
	}

	mod_timer(&idle_timer, jiffies+CLIP_CHECK_INTERVAL*HZ);

	atmarpd = vcc;
	set_bit(ATM_VF_META,&vcc->flags);
	set_bit(ATM_VF_READY,&vcc->flags);
	    /* allow replies and avoid getting closed if signaling dies */
	vcc->dev = &atmarpd_dev;
	vcc_insert_socket(sk_atm(vcc));
	vcc->push = NULL;
	vcc->pop = NULL; /* crash */
	vcc->push_oam = NULL; /* crash */
	rtnl_unlock();
	return 0;
}

static int clip_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	struct atm_vcc *vcc = ATM_SD(sock);
	int err = 0;

	switch (cmd) {
		case SIOCMKCLIP:
		case ATMARPD_CTRL:
		case ATMARP_MKIP:
		case ATMARP_SETENTRY:
		case ATMARP_ENCAP:
			if (!capable(CAP_NET_ADMIN))
				return -EPERM;
			break;
		default:
			return -ENOIOCTLCMD;
	}

	switch (cmd) {
		case SIOCMKCLIP:
			err = clip_create(arg);
			break;
		case ATMARPD_CTRL:
			err = atm_init_atmarp(vcc);
			if (!err) {
				sock->state = SS_CONNECTED;
				__module_get(THIS_MODULE);
			}
			break;
		case ATMARP_MKIP:
			err = clip_mkip(vcc ,arg);
			break;
		case ATMARP_SETENTRY:
			err = clip_setentry(vcc, arg);
			break;
		case ATMARP_ENCAP:
			err = clip_encap(vcc, arg);
			break;
	}
	return err;
}

static struct atm_ioctl clip_ioctl_ops = {
	.owner 	= THIS_MODULE,
	.ioctl	= clip_ioctl,
};

#ifdef CONFIG_PROC_FS

static void svc_addr(struct seq_file *seq, struct sockaddr_atmsvc *addr)
{
	static int code[] = { 1,2,10,6,1,0 };
	static int e164[] = { 1,8,4,6,1,0 };

	if (*addr->sas_addr.pub) {
		seq_printf(seq, "%s", addr->sas_addr.pub);
		if (*addr->sas_addr.prv)
			seq_putc(seq, '+');
	} else if (!*addr->sas_addr.prv) {
		seq_printf(seq, "%s", "(none)");
		return;
	}
	if (*addr->sas_addr.prv) {
		unsigned char *prv = addr->sas_addr.prv;
		int *fields;
		int i, j;

		fields = *prv == ATM_AFI_E164 ? e164 : code;
		for (i = 0; fields[i]; i++) {
			for (j = fields[i]; j; j--)
				seq_printf(seq, "%02X", *prv++);
			if (fields[i+1])
				seq_putc(seq, '.');
		}
	}
}

/* This means the neighbour entry has no attached VCC objects. */
#define SEQ_NO_VCC_TOKEN	((void *) 2)

static void atmarp_info(struct seq_file *seq, struct net_device *dev,
			struct atmarp_entry *entry, struct clip_vcc *clip_vcc)
{
	unsigned long exp;
	char buf[17];
	int svc, llc, off;

	svc = ((clip_vcc == SEQ_NO_VCC_TOKEN) ||
	       (sk_atm(clip_vcc->vcc)->sk_family == AF_ATMSVC));

	llc = ((clip_vcc == SEQ_NO_VCC_TOKEN) ||
	       clip_vcc->encap);

	if (clip_vcc == SEQ_NO_VCC_TOKEN)
		exp = entry->neigh->used;
	else
		exp = clip_vcc->last_use;

	exp = (jiffies - exp) / HZ;

	seq_printf(seq, "%-6s%-4s%-4s%5ld ",
		   dev->name,
		   svc ? "SVC" : "PVC",
		   llc ? "LLC" : "NULL",
		   exp);

	off = scnprintf(buf, sizeof(buf) - 1, "%d.%d.%d.%d",
			NIPQUAD(entry->ip));
	while (off < 16)
		buf[off++] = ' ';
	buf[off] = '\0';
	seq_printf(seq, "%s", buf);

	if (clip_vcc == SEQ_NO_VCC_TOKEN) {
		if (time_before(jiffies, entry->expires))
			seq_printf(seq, "(resolving)\n");
		else
			seq_printf(seq, "(expired, ref %d)\n",
				   atomic_read(&entry->neigh->refcnt));
	} else if (!svc) {
		seq_printf(seq, "%d.%d.%d\n",
			   clip_vcc->vcc->dev->number,
			   clip_vcc->vcc->vpi,
			   clip_vcc->vcc->vci);
	} else {
		svc_addr(seq, &clip_vcc->vcc->remote);
		seq_putc(seq, '\n');
	}
}

struct clip_seq_state {
	/* This member must be first. */
	struct neigh_seq_state ns;

	/* Local to clip specific iteration. */
	struct clip_vcc *vcc;
};

static struct clip_vcc *clip_seq_next_vcc(struct atmarp_entry *e,
					  struct clip_vcc *curr)
{
	if (!curr) {
		curr = e->vccs;
		if (!curr)
			return SEQ_NO_VCC_TOKEN;
		return curr;
	}
	if (curr == SEQ_NO_VCC_TOKEN)
		return NULL;

	curr = curr->next;

	return curr;
}

static void *clip_seq_vcc_walk(struct clip_seq_state *state,
			       struct atmarp_entry *e, loff_t *pos)
{
	struct clip_vcc *vcc = state->vcc;

	vcc = clip_seq_next_vcc(e, vcc);
	if (vcc && pos != NULL) {
		while (*pos) {
			vcc = clip_seq_next_vcc(e, vcc);
			if (!vcc)
				break;
			--(*pos);
		}
	}
	state->vcc = vcc;

	return vcc;
}
  
static void *clip_seq_sub_iter(struct neigh_seq_state *_state,
			       struct neighbour *n, loff_t *pos)
{
	struct clip_seq_state *state = (struct clip_seq_state *) _state;

	return clip_seq_vcc_walk(state, NEIGH2ENTRY(n), pos);
}

static void *clip_seq_start(struct seq_file *seq, loff_t *pos)
{
	return neigh_seq_start(seq, pos, &clip_tbl, NEIGH_SEQ_NEIGH_ONLY);
}

static int clip_seq_show(struct seq_file *seq, void *v)
{
	static char atm_arp_banner[] = 
		"IPitf TypeEncp Idle IP address      ATM address\n";

	if (v == SEQ_START_TOKEN) {
		seq_puts(seq, atm_arp_banner);
	} else {
		struct clip_seq_state *state = seq->private;
		struct neighbour *n = v;
		struct clip_vcc *vcc = state->vcc;

		atmarp_info(seq, n->dev, NEIGH2ENTRY(n), vcc);
	}
  	return 0;
}

static struct seq_operations arp_seq_ops = {
	.start	= clip_seq_start,
	.next	= neigh_seq_next,
	.stop	= neigh_seq_stop,
	.show	= clip_seq_show,
};

static int arp_seq_open(struct inode *inode, struct file *file)
{
	struct clip_seq_state *state;
	struct seq_file *seq;
	int rc = -EAGAIN;

	state = kmalloc(sizeof(*state), GFP_KERNEL);
	if (!state) {
		rc = -ENOMEM;
		goto out_kfree;
	}
	memset(state, 0, sizeof(*state));
	state->ns.neigh_sub_iter = clip_seq_sub_iter;

	rc = seq_open(file, &arp_seq_ops);
	if (rc)
		goto out_kfree;

	seq = file->private_data;
	seq->private = state;
out:
	return rc;

out_kfree:
	kfree(state);
	goto out;
}

static struct file_operations arp_seq_fops = {
	.open		= arp_seq_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release_private,
	.owner		= THIS_MODULE
};
#endif

static int __init atm_clip_init(void)
{
	neigh_table_init(&clip_tbl);

	clip_tbl_hook = &clip_tbl;
	register_atm_ioctl(&clip_ioctl_ops);
	register_netdevice_notifier(&clip_dev_notifier);
	register_inetaddr_notifier(&clip_inet_notifier);

	setup_timer(&idle_timer, idle_timer_check, 0);

#ifdef CONFIG_PROC_FS
{
	struct proc_dir_entry *p;

	p = create_proc_entry("arp", S_IRUGO, atm_proc_root);
	if (p)
		p->proc_fops = &arp_seq_fops;
}
#endif

	return 0;
}

static void __exit atm_clip_exit(void)
{
	struct net_device *dev, *next;

	remove_proc_entry("arp", atm_proc_root);

	unregister_inetaddr_notifier(&clip_inet_notifier);
	unregister_netdevice_notifier(&clip_dev_notifier);

	deregister_atm_ioctl(&clip_ioctl_ops);

	/* First, stop the idle timer, so it stops banging
	 * on the table.
	 */
	del_timer_sync(&idle_timer);

	/* Next, purge the table, so that the device
	 * unregister loop below does not hang due to
	 * device references remaining in the table.
	 */
	neigh_ifdown(&clip_tbl, NULL);

	dev = clip_devs;
	while (dev) {
		next = PRIV(dev)->next;
		unregister_netdev(dev);
		free_netdev(dev);
		dev = next;
	}

	/* Now it is safe to fully shutdown whole table. */
	neigh_table_clear(&clip_tbl);

	clip_tbl_hook = NULL;
}

module_init(atm_clip_init);
module_exit(atm_clip_exit);

MODULE_LICENSE("GPL");
