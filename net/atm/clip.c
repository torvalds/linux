/* net/atm/clip.c - RFC1577 Classical IP over ATM */

/* Written 1995-2000 by Werner Almesberger, EPFL LRC/ICA */

#define pr_fmt(fmt) KBUILD_MODNAME ":%s: " fmt, __func__

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
#include <linux/poison.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/rcupdate.h>
#include <linux/jhash.h>
#include <linux/slab.h>
#include <net/route.h> /* for struct rtable and routing */
#include <net/icmp.h> /* icmp_send */
#include <net/arp.h>
#include <linux/param.h> /* for HZ */
#include <linux/uaccess.h>
#include <asm/byteorder.h> /* for htons etc. */
#include <linux/atomic.h>

#include "common.h"
#include "resources.h"
#include <net/atmclip.h>

static struct net_device *clip_devs;
static struct atm_vcc *atmarpd;
static struct timer_list idle_timer;
static const struct neigh_ops clip_neigh_ops;

static int to_atmarpd(enum atmarp_ctrl_type type, int itf, __be32 ip)
{
	struct sock *sk;
	struct atmarp_ctrl *ctrl;
	struct sk_buff *skb;

	pr_debug("(%d)\n", type);
	if (!atmarpd)
		return -EUNATCH;
	skb = alloc_skb(sizeof(struct atmarp_ctrl), GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;
	ctrl = (struct atmarp_ctrl *)skb_put(skb, sizeof(struct atmarp_ctrl));
	ctrl->type = type;
	ctrl->itf_num = itf;
	ctrl->ip = ip;
	atm_force_charge(atmarpd, skb->truesize);

	sk = sk_atm(atmarpd);
	skb_queue_tail(&sk->sk_receive_queue, skb);
	sk->sk_data_ready(sk);
	return 0;
}

static void link_vcc(struct clip_vcc *clip_vcc, struct atmarp_entry *entry)
{
	pr_debug("%p to entry %p (neigh %p)\n", clip_vcc, entry, entry->neigh);
	clip_vcc->entry = entry;
	clip_vcc->xoff = 0;	/* @@@ may overrun buffer by one packet */
	clip_vcc->next = entry->vccs;
	entry->vccs = clip_vcc;
	entry->neigh->used = jiffies;
}

static void unlink_clip_vcc(struct clip_vcc *clip_vcc)
{
	struct atmarp_entry *entry = clip_vcc->entry;
	struct clip_vcc **walk;

	if (!entry) {
		pr_crit("!clip_vcc->entry (clip_vcc %p)\n", clip_vcc);
		return;
	}
	netif_tx_lock_bh(entry->neigh->dev);	/* block clip_start_xmit() */
	entry->neigh->used = jiffies;
	for (walk = &entry->vccs; *walk; walk = &(*walk)->next)
		if (*walk == clip_vcc) {
			int error;

			*walk = clip_vcc->next;	/* atomic */
			clip_vcc->entry = NULL;
			if (clip_vcc->xoff)
				netif_wake_queue(entry->neigh->dev);
			if (entry->vccs)
				goto out;
			entry->expires = jiffies - 1;
			/* force resolution or expiration */
			error = neigh_update(entry->neigh, NULL, NUD_NONE,
					     NEIGH_UPDATE_F_ADMIN);
			if (error)
				pr_crit("neigh_update failed with %d\n", error);
			goto out;
		}
	pr_crit("ATMARP: failed (entry %p, vcc 0x%p)\n", entry, clip_vcc);
out:
	netif_tx_unlock_bh(entry->neigh->dev);
}

/* The neighbour entry n->lock is held. */
static int neigh_check_cb(struct neighbour *n)
{
	struct atmarp_entry *entry = neighbour_priv(n);
	struct clip_vcc *cv;

	if (n->ops != &clip_neigh_ops)
		return 0;
	for (cv = entry->vccs; cv; cv = cv->next) {
		unsigned long exp = cv->last_use + cv->idle_timeout;

		if (cv->idle_timeout && time_after(jiffies, exp)) {
			pr_debug("releasing vcc %p->%p of entry %p\n",
				 cv, cv->vcc, entry);
			vcc_release_async(cv->vcc, -ETIMEDOUT);
		}
	}

	if (entry->vccs || time_before(jiffies, entry->expires))
		return 0;

	if (atomic_read(&n->refcnt) > 1) {
		struct sk_buff *skb;

		pr_debug("destruction postponed with ref %d\n",
			 atomic_read(&n->refcnt));

		while ((skb = skb_dequeue(&n->arp_queue)) != NULL)
			dev_kfree_skb(skb);

		return 0;
	}

	pr_debug("expired neigh %p\n", n);
	return 1;
}

static void idle_timer_check(unsigned long dummy)
{
	write_lock(&arp_tbl.lock);
	__neigh_for_each_release(&arp_tbl, neigh_check_cb);
	mod_timer(&idle_timer, jiffies + CLIP_CHECK_INTERVAL * HZ);
	write_unlock(&arp_tbl.lock);
}

static int clip_arp_rcv(struct sk_buff *skb)
{
	struct atm_vcc *vcc;

	pr_debug("\n");
	vcc = ATM_SKB(skb)->vcc;
	if (!vcc || !atm_charge(vcc, skb->truesize)) {
		dev_kfree_skb_any(skb);
		return 0;
	}
	pr_debug("pushing to %p\n", vcc);
	pr_debug("using %p\n", CLIP_VCC(vcc)->old_push);
	CLIP_VCC(vcc)->old_push(vcc, skb);
	return 0;
}

static const unsigned char llc_oui[] = {
	0xaa,	/* DSAP: non-ISO */
	0xaa,	/* SSAP: non-ISO */
	0x03,	/* Ctrl: Unnumbered Information Command PDU */
	0x00,	/* OUI: EtherType */
	0x00,
	0x00
};

static void clip_push(struct atm_vcc *vcc, struct sk_buff *skb)
{
	struct clip_vcc *clip_vcc = CLIP_VCC(vcc);

	pr_debug("\n");

	if (!clip_devs) {
		atm_return(vcc, skb->truesize);
		kfree_skb(skb);
		return;
	}

	if (!skb) {
		pr_debug("removing VCC %p\n", clip_vcc);
		if (clip_vcc->entry)
			unlink_clip_vcc(clip_vcc);
		clip_vcc->old_push(vcc, NULL);	/* pass on the bad news */
		kfree(clip_vcc);
		return;
	}
	atm_return(vcc, skb->truesize);
	skb->dev = clip_vcc->entry ? clip_vcc->entry->neigh->dev : clip_devs;
	/* clip_vcc->entry == NULL if we don't have an IP address yet */
	if (!skb->dev) {
		dev_kfree_skb_any(skb);
		return;
	}
	ATM_SKB(skb)->vcc = vcc;
	skb_reset_mac_header(skb);
	if (!clip_vcc->encap ||
	    skb->len < RFC1483LLC_LEN ||
	    memcmp(skb->data, llc_oui, sizeof(llc_oui)))
		skb->protocol = htons(ETH_P_IP);
	else {
		skb->protocol = ((__be16 *)skb->data)[3];
		skb_pull(skb, RFC1483LLC_LEN);
		if (skb->protocol == htons(ETH_P_ARP)) {
			skb->dev->stats.rx_packets++;
			skb->dev->stats.rx_bytes += skb->len;
			clip_arp_rcv(skb);
			return;
		}
	}
	clip_vcc->last_use = jiffies;
	skb->dev->stats.rx_packets++;
	skb->dev->stats.rx_bytes += skb->len;
	memset(ATM_SKB(skb), 0, sizeof(struct atm_skb_data));
	netif_rx(skb);
}

/*
 * Note: these spinlocks _must_not_ block on non-SMP. The only goal is that
 * clip_pop is atomic with respect to the critical section in clip_start_xmit.
 */

static void clip_pop(struct atm_vcc *vcc, struct sk_buff *skb)
{
	struct clip_vcc *clip_vcc = CLIP_VCC(vcc);
	struct net_device *dev = skb->dev;
	int old;
	unsigned long flags;

	pr_debug("(vcc %p)\n", vcc);
	clip_vcc->old_pop(vcc, skb);
	/* skb->dev == NULL in outbound ARP packets */
	if (!dev)
		return;
	spin_lock_irqsave(&PRIV(dev)->xoff_lock, flags);
	if (atm_may_send(vcc, 0)) {
		old = xchg(&clip_vcc->xoff, 0);
		if (old)
			netif_wake_queue(dev);
	}
	spin_unlock_irqrestore(&PRIV(dev)->xoff_lock, flags);
}

static void clip_neigh_solicit(struct neighbour *neigh, struct sk_buff *skb)
{
	__be32 *ip = (__be32 *) neigh->primary_key;

	pr_debug("(neigh %p, skb %p)\n", neigh, skb);
	to_atmarpd(act_need, PRIV(neigh->dev)->number, *ip);
}

static void clip_neigh_error(struct neighbour *neigh, struct sk_buff *skb)
{
#ifndef CONFIG_ATM_CLIP_NO_ICMP
	icmp_send(skb, ICMP_DEST_UNREACH, ICMP_HOST_UNREACH, 0);
#endif
	kfree_skb(skb);
}

static const struct neigh_ops clip_neigh_ops = {
	.family =		AF_INET,
	.solicit =		clip_neigh_solicit,
	.error_report =		clip_neigh_error,
	.output =		neigh_direct_output,
	.connected_output =	neigh_direct_output,
};

static int clip_constructor(struct neighbour *neigh)
{
	struct atmarp_entry *entry = neighbour_priv(neigh);

	if (neigh->tbl->family != AF_INET)
		return -EINVAL;

	if (neigh->type != RTN_UNICAST)
		return -EINVAL;

	neigh->nud_state = NUD_NONE;
	neigh->ops = &clip_neigh_ops;
	neigh->output = neigh->ops->output;
	entry->neigh = neigh;
	entry->vccs = NULL;
	entry->expires = jiffies - 1;

	return 0;
}

/* @@@ copy bh locking from arp.c -- need to bh-enable atm code before */

/*
 * We play with the resolve flag: 0 and 1 have the usual meaning, but -1 means
 * to allocate the neighbour entry but not to ask atmarpd for resolution. Also,
 * don't increment the usage count. This is used to create entries in
 * clip_setentry.
 */

static int clip_encap(struct atm_vcc *vcc, int mode)
{
	if (!CLIP_VCC(vcc))
		return -EBADFD;

	CLIP_VCC(vcc)->encap = mode;
	return 0;
}

static netdev_tx_t clip_start_xmit(struct sk_buff *skb,
				   struct net_device *dev)
{
	struct clip_priv *clip_priv = PRIV(dev);
	struct dst_entry *dst = skb_dst(skb);
	struct atmarp_entry *entry;
	struct neighbour *n;
	struct atm_vcc *vcc;
	struct rtable *rt;
	__be32 *daddr;
	int old;
	unsigned long flags;

	pr_debug("(skb %p)\n", skb);
	if (!dst) {
		pr_err("skb_dst(skb) == NULL\n");
		dev_kfree_skb(skb);
		dev->stats.tx_dropped++;
		return NETDEV_TX_OK;
	}
	rt = (struct rtable *) dst;
	if (rt->rt_gateway)
		daddr = &rt->rt_gateway;
	else
		daddr = &ip_hdr(skb)->daddr;
	n = dst_neigh_lookup(dst, daddr);
	if (!n) {
		pr_err("NO NEIGHBOUR !\n");
		dev_kfree_skb(skb);
		dev->stats.tx_dropped++;
		return NETDEV_TX_OK;
	}
	entry = neighbour_priv(n);
	if (!entry->vccs) {
		if (time_after(jiffies, entry->expires)) {
			/* should be resolved */
			entry->expires = jiffies + ATMARP_RETRY_DELAY * HZ;
			to_atmarpd(act_need, PRIV(dev)->number, *((__be32 *)n->primary_key));
		}
		if (entry->neigh->arp_queue.qlen < ATMARP_MAX_UNRES_PACKETS)
			skb_queue_tail(&entry->neigh->arp_queue, skb);
		else {
			dev_kfree_skb(skb);
			dev->stats.tx_dropped++;
		}
		goto out_release_neigh;
	}
	pr_debug("neigh %p, vccs %p\n", entry, entry->vccs);
	ATM_SKB(skb)->vcc = vcc = entry->vccs->vcc;
	pr_debug("using neighbour %p, vcc %p\n", n, vcc);
	if (entry->vccs->encap) {
		void *here;

		here = skb_push(skb, RFC1483LLC_LEN);
		memcpy(here, llc_oui, sizeof(llc_oui));
		((__be16 *) here)[3] = skb->protocol;
	}
	atomic_add(skb->truesize, &sk_atm(vcc)->sk_wmem_alloc);
	ATM_SKB(skb)->atm_options = vcc->atm_options;
	entry->vccs->last_use = jiffies;
	pr_debug("atm_skb(%p)->vcc(%p)->dev(%p)\n", skb, vcc, vcc->dev);
	old = xchg(&entry->vccs->xoff, 1);	/* assume XOFF ... */
	if (old) {
		pr_warn("XOFF->XOFF transition\n");
		goto out_release_neigh;
	}
	dev->stats.tx_packets++;
	dev->stats.tx_bytes += skb->len;
	vcc->send(vcc, skb);
	if (atm_may_send(vcc, 0)) {
		entry->vccs->xoff = 0;
		goto out_release_neigh;
	}
	spin_lock_irqsave(&clip_priv->xoff_lock, flags);
	netif_stop_queue(dev);	/* XOFF -> throttle immediately */
	barrier();
	if (!entry->vccs->xoff)
		netif_start_queue(dev);
	/* Oh, we just raced with clip_pop. netif_start_queue should be
	   good enough, because nothing should really be asleep because
	   of the brief netif_stop_queue. If this isn't true or if it
	   changes, use netif_wake_queue instead. */
	spin_unlock_irqrestore(&clip_priv->xoff_lock, flags);
out_release_neigh:
	neigh_release(n);
	return NETDEV_TX_OK;
}

static int clip_mkip(struct atm_vcc *vcc, int timeout)
{
	struct clip_vcc *clip_vcc;

	if (!vcc->push)
		return -EBADFD;
	clip_vcc = kmalloc(sizeof(struct clip_vcc), GFP_KERNEL);
	if (!clip_vcc)
		return -ENOMEM;
	pr_debug("%p vcc %p\n", clip_vcc, vcc);
	clip_vcc->vcc = vcc;
	vcc->user_back = clip_vcc;
	set_bit(ATM_VF_IS_CLIP, &vcc->flags);
	clip_vcc->entry = NULL;
	clip_vcc->xoff = 0;
	clip_vcc->encap = 1;
	clip_vcc->last_use = jiffies;
	clip_vcc->idle_timeout = timeout * HZ;
	clip_vcc->old_push = vcc->push;
	clip_vcc->old_pop = vcc->pop;
	vcc->push = clip_push;
	vcc->pop = clip_pop;

	/* re-process everything received between connection setup and MKIP */
	vcc_process_recv_queue(vcc);

	return 0;
}

static int clip_setentry(struct atm_vcc *vcc, __be32 ip)
{
	struct neighbour *neigh;
	struct atmarp_entry *entry;
	int error;
	struct clip_vcc *clip_vcc;
	struct rtable *rt;

	if (vcc->push != clip_push) {
		pr_warn("non-CLIP VCC\n");
		return -EBADF;
	}
	clip_vcc = CLIP_VCC(vcc);
	if (!ip) {
		if (!clip_vcc->entry) {
			pr_err("hiding hidden ATMARP entry\n");
			return 0;
		}
		pr_debug("remove\n");
		unlink_clip_vcc(clip_vcc);
		return 0;
	}
	rt = ip_route_output(&init_net, ip, 0, 1, 0);
	if (IS_ERR(rt))
		return PTR_ERR(rt);
	neigh = __neigh_lookup(&arp_tbl, &ip, rt->dst.dev, 1);
	ip_rt_put(rt);
	if (!neigh)
		return -ENOMEM;
	entry = neighbour_priv(neigh);
	if (entry != clip_vcc->entry) {
		if (!clip_vcc->entry)
			pr_debug("add\n");
		else {
			pr_debug("update\n");
			unlink_clip_vcc(clip_vcc);
		}
		link_vcc(clip_vcc, entry);
	}
	error = neigh_update(neigh, llc_oui, NUD_PERMANENT,
			     NEIGH_UPDATE_F_OVERRIDE | NEIGH_UPDATE_F_ADMIN);
	neigh_release(neigh);
	return error;
}

static const struct net_device_ops clip_netdev_ops = {
	.ndo_start_xmit		= clip_start_xmit,
	.ndo_neigh_construct	= clip_constructor,
};

static void clip_setup(struct net_device *dev)
{
	dev->netdev_ops = &clip_netdev_ops;
	dev->type = ARPHRD_ATM;
	dev->neigh_priv_len = sizeof(struct atmarp_entry);
	dev->hard_header_len = RFC1483LLC_LEN;
	dev->mtu = RFC1626_MTU;
	dev->tx_queue_len = 100;	/* "normal" queue (packets) */
	/* When using a "real" qdisc, the qdisc determines the queue */
	/* length. tx_queue_len is only used for the default case, */
	/* without any more elaborate queuing. 100 is a reasonable */
	/* compromise between decent burst-tolerance and protection */
	/* against memory hogs. */
	netif_keep_dst(dev);
}

static int clip_create(int number)
{
	struct net_device *dev;
	struct clip_priv *clip_priv;
	int error;

	if (number != -1) {
		for (dev = clip_devs; dev; dev = PRIV(dev)->next)
			if (PRIV(dev)->number == number)
				return -EEXIST;
	} else {
		number = 0;
		for (dev = clip_devs; dev; dev = PRIV(dev)->next)
			if (PRIV(dev)->number >= number)
				number = PRIV(dev)->number + 1;
	}
	dev = alloc_netdev(sizeof(struct clip_priv), "", NET_NAME_UNKNOWN,
			   clip_setup);
	if (!dev)
		return -ENOMEM;
	clip_priv = PRIV(dev);
	sprintf(dev->name, "atm%d", number);
	spin_lock_init(&clip_priv->xoff_lock);
	clip_priv->number = number;
	error = register_netdev(dev);
	if (error) {
		free_netdev(dev);
		return error;
	}
	clip_priv->next = clip_devs;
	clip_devs = dev;
	pr_debug("registered (net:%s)\n", dev->name);
	return number;
}

static int clip_device_event(struct notifier_block *this, unsigned long event,
			     void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);

	if (!net_eq(dev_net(dev), &init_net))
		return NOTIFY_DONE;

	if (event == NETDEV_UNREGISTER)
		return NOTIFY_DONE;

	/* ignore non-CLIP devices */
	if (dev->type != ARPHRD_ATM || dev->netdev_ops != &clip_netdev_ops)
		return NOTIFY_DONE;

	switch (event) {
	case NETDEV_UP:
		pr_debug("NETDEV_UP\n");
		to_atmarpd(act_up, PRIV(dev)->number, 0);
		break;
	case NETDEV_GOING_DOWN:
		pr_debug("NETDEV_DOWN\n");
		to_atmarpd(act_down, PRIV(dev)->number, 0);
		break;
	case NETDEV_CHANGE:
	case NETDEV_CHANGEMTU:
		pr_debug("NETDEV_CHANGE*\n");
		to_atmarpd(act_change, PRIV(dev)->number, 0);
		break;
	}
	return NOTIFY_DONE;
}

static int clip_inet_event(struct notifier_block *this, unsigned long event,
			   void *ifa)
{
	struct in_device *in_dev;
	struct netdev_notifier_info info;

	in_dev = ((struct in_ifaddr *)ifa)->ifa_dev;
	/*
	 * Transitions are of the down-change-up type, so it's sufficient to
	 * handle the change on up.
	 */
	if (event != NETDEV_UP)
		return NOTIFY_DONE;
	netdev_notifier_info_init(&info, in_dev->dev);
	return clip_device_event(this, NETDEV_CHANGE, &info);
}

static struct notifier_block clip_dev_notifier = {
	.notifier_call = clip_device_event,
};



static struct notifier_block clip_inet_notifier = {
	.notifier_call = clip_inet_event,
};



static void atmarpd_close(struct atm_vcc *vcc)
{
	pr_debug("\n");

	rtnl_lock();
	atmarpd = NULL;
	skb_queue_purge(&sk_atm(vcc)->sk_receive_queue);
	rtnl_unlock();

	pr_debug("(done)\n");
	module_put(THIS_MODULE);
}

static struct atmdev_ops atmarpd_dev_ops = {
	.close = atmarpd_close
};


static struct atm_dev atmarpd_dev = {
	.ops =			&atmarpd_dev_ops,
	.type =			"arpd",
	.number = 		999,
	.lock =			__SPIN_LOCK_UNLOCKED(atmarpd_dev.lock)
};


static int atm_init_atmarp(struct atm_vcc *vcc)
{
	rtnl_lock();
	if (atmarpd) {
		rtnl_unlock();
		return -EADDRINUSE;
	}

	mod_timer(&idle_timer, jiffies + CLIP_CHECK_INTERVAL * HZ);

	atmarpd = vcc;
	set_bit(ATM_VF_META, &vcc->flags);
	set_bit(ATM_VF_READY, &vcc->flags);
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
		err = clip_mkip(vcc, arg);
		break;
	case ATMARP_SETENTRY:
		err = clip_setentry(vcc, (__force __be32)arg);
		break;
	case ATMARP_ENCAP:
		err = clip_encap(vcc, arg);
		break;
	}
	return err;
}

static struct atm_ioctl clip_ioctl_ops = {
	.owner = THIS_MODULE,
	.ioctl = clip_ioctl,
};

#ifdef CONFIG_PROC_FS

static void svc_addr(struct seq_file *seq, struct sockaddr_atmsvc *addr)
{
	static int code[] = { 1, 2, 10, 6, 1, 0 };
	static int e164[] = { 1, 8, 4, 6, 1, 0 };

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
			if (fields[i + 1])
				seq_putc(seq, '.');
		}
	}
}

/* This means the neighbour entry has no attached VCC objects. */
#define SEQ_NO_VCC_TOKEN	((void *) 2)

static void atmarp_info(struct seq_file *seq, struct neighbour *n,
			struct atmarp_entry *entry, struct clip_vcc *clip_vcc)
{
	struct net_device *dev = n->dev;
	unsigned long exp;
	char buf[17];
	int svc, llc, off;

	svc = ((clip_vcc == SEQ_NO_VCC_TOKEN) ||
	       (sk_atm(clip_vcc->vcc)->sk_family == AF_ATMSVC));

	llc = ((clip_vcc == SEQ_NO_VCC_TOKEN) || clip_vcc->encap);

	if (clip_vcc == SEQ_NO_VCC_TOKEN)
		exp = entry->neigh->used;
	else
		exp = clip_vcc->last_use;

	exp = (jiffies - exp) / HZ;

	seq_printf(seq, "%-6s%-4s%-4s%5ld ",
		   dev->name, svc ? "SVC" : "PVC", llc ? "LLC" : "NULL", exp);

	off = scnprintf(buf, sizeof(buf) - 1, "%pI4", n->primary_key);
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
			   clip_vcc->vcc->vpi, clip_vcc->vcc->vci);
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
			       struct atmarp_entry *e, loff_t * pos)
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
			       struct neighbour *n, loff_t * pos)
{
	struct clip_seq_state *state = (struct clip_seq_state *)_state;

	if (n->dev->type != ARPHRD_ATM)
		return NULL;

	return clip_seq_vcc_walk(state, neighbour_priv(n), pos);
}

static void *clip_seq_start(struct seq_file *seq, loff_t * pos)
{
	struct clip_seq_state *state = seq->private;
	state->ns.neigh_sub_iter = clip_seq_sub_iter;
	return neigh_seq_start(seq, pos, &arp_tbl, NEIGH_SEQ_NEIGH_ONLY);
}

static int clip_seq_show(struct seq_file *seq, void *v)
{
	static char atm_arp_banner[] =
	    "IPitf TypeEncp Idle IP address      ATM address\n";

	if (v == SEQ_START_TOKEN) {
		seq_puts(seq, atm_arp_banner);
	} else {
		struct clip_seq_state *state = seq->private;
		struct clip_vcc *vcc = state->vcc;
		struct neighbour *n = v;

		atmarp_info(seq, n, neighbour_priv(n), vcc);
	}
	return 0;
}

static const struct seq_operations arp_seq_ops = {
	.start	= clip_seq_start,
	.next	= neigh_seq_next,
	.stop	= neigh_seq_stop,
	.show	= clip_seq_show,
};

static int arp_seq_open(struct inode *inode, struct file *file)
{
	return seq_open_net(inode, file, &arp_seq_ops,
			    sizeof(struct clip_seq_state));
}

static const struct file_operations arp_seq_fops = {
	.open		= arp_seq_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release_net,
	.owner		= THIS_MODULE
};
#endif

static void atm_clip_exit_noproc(void);

static int __init atm_clip_init(void)
{
	register_atm_ioctl(&clip_ioctl_ops);
	register_netdevice_notifier(&clip_dev_notifier);
	register_inetaddr_notifier(&clip_inet_notifier);

	setup_timer(&idle_timer, idle_timer_check, 0);

#ifdef CONFIG_PROC_FS
	{
		struct proc_dir_entry *p;

		p = proc_create("arp", S_IRUGO, atm_proc_root, &arp_seq_fops);
		if (!p) {
			pr_err("Unable to initialize /proc/net/atm/arp\n");
			atm_clip_exit_noproc();
			return -ENOMEM;
		}
	}
#endif

	return 0;
}

static void atm_clip_exit_noproc(void)
{
	struct net_device *dev, *next;

	unregister_inetaddr_notifier(&clip_inet_notifier);
	unregister_netdevice_notifier(&clip_dev_notifier);

	deregister_atm_ioctl(&clip_ioctl_ops);

	/* First, stop the idle timer, so it stops banging
	 * on the table.
	 */
	del_timer_sync(&idle_timer);

	dev = clip_devs;
	while (dev) {
		next = PRIV(dev)->next;
		unregister_netdev(dev);
		free_netdev(dev);
		dev = next;
	}
}

static void __exit atm_clip_exit(void)
{
	remove_proc_entry("arp", atm_proc_root);

	atm_clip_exit_noproc();
}

module_init(atm_clip_init);
module_exit(atm_clip_exit);
MODULE_AUTHOR("Werner Almesberger");
MODULE_DESCRIPTION("Classical/IP over ATM interface");
MODULE_LICENSE("GPL");
