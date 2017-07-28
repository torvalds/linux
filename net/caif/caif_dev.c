/*
 * CAIF Interface registration.
 * Copyright (C) ST-Ericsson AB 2010
 * Author:	Sjur Brendeland
 * License terms: GNU General Public License (GPL) version 2
 *
 * Borrowed heavily from file: pn_dev.c. Thanks to Remi Denis-Courmont
 *  and Sakari Ailus <sakari.ailus@nokia.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ":%s(): " fmt, __func__

#include <linux/kernel.h>
#include <linux/if_arp.h>
#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <net/netns/generic.h>
#include <net/net_namespace.h>
#include <net/pkt_sched.h>
#include <net/caif/caif_device.h>
#include <net/caif/caif_layer.h>
#include <net/caif/caif_dev.h>
#include <net/caif/cfpkt.h>
#include <net/caif/cfcnfg.h>
#include <net/caif/cfserl.h>

MODULE_LICENSE("GPL");

/* Used for local tracking of the CAIF net devices */
struct caif_device_entry {
	struct cflayer layer;
	struct list_head list;
	struct net_device *netdev;
	int __percpu *pcpu_refcnt;
	spinlock_t flow_lock;
	struct sk_buff *xoff_skb;
	void (*xoff_skb_dtor)(struct sk_buff *skb);
	bool xoff;
};

struct caif_device_entry_list {
	struct list_head list;
	/* Protects simulanous deletes in list */
	struct mutex lock;
};

struct caif_net {
	struct cfcnfg *cfg;
	struct caif_device_entry_list caifdevs;
};

static unsigned int caif_net_id;
static int q_high = 50; /* Percent */

struct cfcnfg *get_cfcnfg(struct net *net)
{
	struct caif_net *caifn;
	caifn = net_generic(net, caif_net_id);
	return caifn->cfg;
}
EXPORT_SYMBOL(get_cfcnfg);

static struct caif_device_entry_list *caif_device_list(struct net *net)
{
	struct caif_net *caifn;
	caifn = net_generic(net, caif_net_id);
	return &caifn->caifdevs;
}

static void caifd_put(struct caif_device_entry *e)
{
	this_cpu_dec(*e->pcpu_refcnt);
}

static void caifd_hold(struct caif_device_entry *e)
{
	this_cpu_inc(*e->pcpu_refcnt);
}

static int caifd_refcnt_read(struct caif_device_entry *e)
{
	int i, refcnt = 0;
	for_each_possible_cpu(i)
		refcnt += *per_cpu_ptr(e->pcpu_refcnt, i);
	return refcnt;
}

/* Allocate new CAIF device. */
static struct caif_device_entry *caif_device_alloc(struct net_device *dev)
{
	struct caif_device_entry *caifd;

	caifd = kzalloc(sizeof(*caifd), GFP_KERNEL);
	if (!caifd)
		return NULL;
	caifd->pcpu_refcnt = alloc_percpu(int);
	if (!caifd->pcpu_refcnt) {
		kfree(caifd);
		return NULL;
	}
	caifd->netdev = dev;
	dev_hold(dev);
	return caifd;
}

static struct caif_device_entry *caif_get(struct net_device *dev)
{
	struct caif_device_entry_list *caifdevs =
	    caif_device_list(dev_net(dev));
	struct caif_device_entry *caifd;

	list_for_each_entry_rcu(caifd, &caifdevs->list, list) {
		if (caifd->netdev == dev)
			return caifd;
	}
	return NULL;
}

static void caif_flow_cb(struct sk_buff *skb)
{
	struct caif_device_entry *caifd;
	void (*dtor)(struct sk_buff *skb) = NULL;
	bool send_xoff;

	WARN_ON(skb->dev == NULL);

	rcu_read_lock();
	caifd = caif_get(skb->dev);

	WARN_ON(caifd == NULL);
	if (caifd == NULL)
		return;

	caifd_hold(caifd);
	rcu_read_unlock();

	spin_lock_bh(&caifd->flow_lock);
	send_xoff = caifd->xoff;
	caifd->xoff = 0;
	dtor = caifd->xoff_skb_dtor;

	if (WARN_ON(caifd->xoff_skb != skb))
		skb = NULL;

	caifd->xoff_skb = NULL;
	caifd->xoff_skb_dtor = NULL;

	spin_unlock_bh(&caifd->flow_lock);

	if (dtor && skb)
		dtor(skb);

	if (send_xoff)
		caifd->layer.up->
			ctrlcmd(caifd->layer.up,
				_CAIF_CTRLCMD_PHYIF_FLOW_ON_IND,
				caifd->layer.id);
	caifd_put(caifd);
}

static int transmit(struct cflayer *layer, struct cfpkt *pkt)
{
	int err, high = 0, qlen = 0;
	struct caif_device_entry *caifd =
	    container_of(layer, struct caif_device_entry, layer);
	struct sk_buff *skb;
	struct netdev_queue *txq;

	rcu_read_lock_bh();

	skb = cfpkt_tonative(pkt);
	skb->dev = caifd->netdev;
	skb_reset_network_header(skb);
	skb->protocol = htons(ETH_P_CAIF);

	/* Check if we need to handle xoff */
	if (likely(caifd->netdev->priv_flags & IFF_NO_QUEUE))
		goto noxoff;

	if (unlikely(caifd->xoff))
		goto noxoff;

	if (likely(!netif_queue_stopped(caifd->netdev))) {
		/* If we run with a TX queue, check if the queue is too long*/
		txq = netdev_get_tx_queue(skb->dev, 0);
		qlen = qdisc_qlen(rcu_dereference_bh(txq->qdisc));

		if (likely(qlen == 0))
			goto noxoff;

		high = (caifd->netdev->tx_queue_len * q_high) / 100;
		if (likely(qlen < high))
			goto noxoff;
	}

	/* Hold lock while accessing xoff */
	spin_lock_bh(&caifd->flow_lock);
	if (caifd->xoff) {
		spin_unlock_bh(&caifd->flow_lock);
		goto noxoff;
	}

	/*
	 * Handle flow off, we do this by temporary hi-jacking this
	 * skb's destructor function, and replace it with our own
	 * flow-on callback. The callback will set flow-on and call
	 * the original destructor.
	 */

	pr_debug("queue has stopped(%d) or is full (%d > %d)\n",
			netif_queue_stopped(caifd->netdev),
			qlen, high);
	caifd->xoff = 1;
	caifd->xoff_skb = skb;
	caifd->xoff_skb_dtor = skb->destructor;
	skb->destructor = caif_flow_cb;
	spin_unlock_bh(&caifd->flow_lock);

	caifd->layer.up->ctrlcmd(caifd->layer.up,
					_CAIF_CTRLCMD_PHYIF_FLOW_OFF_IND,
					caifd->layer.id);
noxoff:
	rcu_read_unlock_bh();

	err = dev_queue_xmit(skb);
	if (err > 0)
		err = -EIO;

	return err;
}

/*
 * Stuff received packets into the CAIF stack.
 * On error, returns non-zero and releases the skb.
 */
static int receive(struct sk_buff *skb, struct net_device *dev,
		   struct packet_type *pkttype, struct net_device *orig_dev)
{
	struct cfpkt *pkt;
	struct caif_device_entry *caifd;
	int err;

	pkt = cfpkt_fromnative(CAIF_DIR_IN, skb);

	rcu_read_lock();
	caifd = caif_get(dev);

	if (!caifd || !caifd->layer.up || !caifd->layer.up->receive ||
			!netif_oper_up(caifd->netdev)) {
		rcu_read_unlock();
		kfree_skb(skb);
		return NET_RX_DROP;
	}

	/* Hold reference to netdevice while using CAIF stack */
	caifd_hold(caifd);
	rcu_read_unlock();

	err = caifd->layer.up->receive(caifd->layer.up, pkt);

	/* For -EILSEQ the packet is not freed so so it now */
	if (err == -EILSEQ)
		cfpkt_destroy(pkt);

	/* Release reference to stack upwards */
	caifd_put(caifd);

	if (err != 0)
		err = NET_RX_DROP;
	return err;
}

static struct packet_type caif_packet_type __read_mostly = {
	.type = cpu_to_be16(ETH_P_CAIF),
	.func = receive,
};

static void dev_flowctrl(struct net_device *dev, int on)
{
	struct caif_device_entry *caifd;

	rcu_read_lock();

	caifd = caif_get(dev);
	if (!caifd || !caifd->layer.up || !caifd->layer.up->ctrlcmd) {
		rcu_read_unlock();
		return;
	}

	caifd_hold(caifd);
	rcu_read_unlock();

	caifd->layer.up->ctrlcmd(caifd->layer.up,
				 on ?
				 _CAIF_CTRLCMD_PHYIF_FLOW_ON_IND :
				 _CAIF_CTRLCMD_PHYIF_FLOW_OFF_IND,
				 caifd->layer.id);
	caifd_put(caifd);
}

void caif_enroll_dev(struct net_device *dev, struct caif_dev_common *caifdev,
		     struct cflayer *link_support, int head_room,
		     struct cflayer **layer,
		     int (**rcv_func)(struct sk_buff *, struct net_device *,
				      struct packet_type *,
				      struct net_device *))
{
	struct caif_device_entry *caifd;
	enum cfcnfg_phy_preference pref;
	struct cfcnfg *cfg = get_cfcnfg(dev_net(dev));
	struct caif_device_entry_list *caifdevs;

	caifdevs = caif_device_list(dev_net(dev));
	caifd = caif_device_alloc(dev);
	if (!caifd)
		return;
	*layer = &caifd->layer;
	spin_lock_init(&caifd->flow_lock);

	switch (caifdev->link_select) {
	case CAIF_LINK_HIGH_BANDW:
		pref = CFPHYPREF_HIGH_BW;
		break;
	case CAIF_LINK_LOW_LATENCY:
		pref = CFPHYPREF_LOW_LAT;
		break;
	default:
		pref = CFPHYPREF_HIGH_BW;
		break;
	}
	mutex_lock(&caifdevs->lock);
	list_add_rcu(&caifd->list, &caifdevs->list);

	strncpy(caifd->layer.name, dev->name,
		sizeof(caifd->layer.name) - 1);
	caifd->layer.name[sizeof(caifd->layer.name) - 1] = 0;
	caifd->layer.transmit = transmit;
	cfcnfg_add_phy_layer(cfg,
				dev,
				&caifd->layer,
				pref,
				link_support,
				caifdev->use_fcs,
				head_room);
	mutex_unlock(&caifdevs->lock);
	if (rcv_func)
		*rcv_func = receive;
}
EXPORT_SYMBOL(caif_enroll_dev);

/* notify Caif of device events */
static int caif_device_notify(struct notifier_block *me, unsigned long what,
			      void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct caif_device_entry *caifd = NULL;
	struct caif_dev_common *caifdev;
	struct cfcnfg *cfg;
	struct cflayer *layer, *link_support;
	int head_room = 0;
	struct caif_device_entry_list *caifdevs;

	cfg = get_cfcnfg(dev_net(dev));
	caifdevs = caif_device_list(dev_net(dev));

	caifd = caif_get(dev);
	if (caifd == NULL && dev->type != ARPHRD_CAIF)
		return 0;

	switch (what) {
	case NETDEV_REGISTER:
		if (caifd != NULL)
			break;

		caifdev = netdev_priv(dev);

		link_support = NULL;
		if (caifdev->use_frag) {
			head_room = 1;
			link_support = cfserl_create(dev->ifindex,
							caifdev->use_stx);
			if (!link_support) {
				pr_warn("Out of memory\n");
				break;
			}
		}
		caif_enroll_dev(dev, caifdev, link_support, head_room,
				&layer, NULL);
		caifdev->flowctrl = dev_flowctrl;
		break;

	case NETDEV_UP:
		rcu_read_lock();

		caifd = caif_get(dev);
		if (caifd == NULL) {
			rcu_read_unlock();
			break;
		}

		caifd->xoff = 0;
		cfcnfg_set_phy_state(cfg, &caifd->layer, true);
		rcu_read_unlock();

		break;

	case NETDEV_DOWN:
		rcu_read_lock();

		caifd = caif_get(dev);
		if (!caifd || !caifd->layer.up || !caifd->layer.up->ctrlcmd) {
			rcu_read_unlock();
			return -EINVAL;
		}

		cfcnfg_set_phy_state(cfg, &caifd->layer, false);
		caifd_hold(caifd);
		rcu_read_unlock();

		caifd->layer.up->ctrlcmd(caifd->layer.up,
					 _CAIF_CTRLCMD_PHYIF_DOWN_IND,
					 caifd->layer.id);

		spin_lock_bh(&caifd->flow_lock);

		/*
		 * Replace our xoff-destructor with original destructor.
		 * We trust that skb->destructor *always* is called before
		 * the skb reference is invalid. The hijacked SKB destructor
		 * takes the flow_lock so manipulating the skb->destructor here
		 * should be safe.
		*/
		if (caifd->xoff_skb_dtor != NULL && caifd->xoff_skb != NULL)
			caifd->xoff_skb->destructor = caifd->xoff_skb_dtor;

		caifd->xoff = 0;
		caifd->xoff_skb_dtor = NULL;
		caifd->xoff_skb = NULL;

		spin_unlock_bh(&caifd->flow_lock);
		caifd_put(caifd);
		break;

	case NETDEV_UNREGISTER:
		mutex_lock(&caifdevs->lock);

		caifd = caif_get(dev);
		if (caifd == NULL) {
			mutex_unlock(&caifdevs->lock);
			break;
		}
		list_del_rcu(&caifd->list);

		/*
		 * NETDEV_UNREGISTER is called repeatedly until all reference
		 * counts for the net-device are released. If references to
		 * caifd is taken, simply ignore NETDEV_UNREGISTER and wait for
		 * the next call to NETDEV_UNREGISTER.
		 *
		 * If any packets are in flight down the CAIF Stack,
		 * cfcnfg_del_phy_layer will return nonzero.
		 * If no packets are in flight, the CAIF Stack associated
		 * with the net-device un-registering is freed.
		 */

		if (caifd_refcnt_read(caifd) != 0 ||
			cfcnfg_del_phy_layer(cfg, &caifd->layer) != 0) {

			pr_info("Wait for device inuse\n");
			/* Enrole device if CAIF Stack is still in use */
			list_add_rcu(&caifd->list, &caifdevs->list);
			mutex_unlock(&caifdevs->lock);
			break;
		}

		synchronize_rcu();
		dev_put(caifd->netdev);
		free_percpu(caifd->pcpu_refcnt);
		kfree(caifd);

		mutex_unlock(&caifdevs->lock);
		break;
	}
	return 0;
}

static struct notifier_block caif_device_notifier = {
	.notifier_call = caif_device_notify,
	.priority = 0,
};

/* Per-namespace Caif devices handling */
static int caif_init_net(struct net *net)
{
	struct caif_net *caifn = net_generic(net, caif_net_id);
	INIT_LIST_HEAD(&caifn->caifdevs.list);
	mutex_init(&caifn->caifdevs.lock);

	caifn->cfg = cfcnfg_create();
	if (!caifn->cfg)
		return -ENOMEM;

	return 0;
}

static void caif_exit_net(struct net *net)
{
	struct caif_device_entry *caifd, *tmp;
	struct caif_device_entry_list *caifdevs =
	    caif_device_list(net);
	struct cfcnfg *cfg =  get_cfcnfg(net);

	rtnl_lock();
	mutex_lock(&caifdevs->lock);

	list_for_each_entry_safe(caifd, tmp, &caifdevs->list, list) {
		int i = 0;
		list_del_rcu(&caifd->list);
		cfcnfg_set_phy_state(cfg, &caifd->layer, false);

		while (i < 10 &&
			(caifd_refcnt_read(caifd) != 0 ||
			cfcnfg_del_phy_layer(cfg, &caifd->layer) != 0)) {

			pr_info("Wait for device inuse\n");
			msleep(250);
			i++;
		}
		synchronize_rcu();
		dev_put(caifd->netdev);
		free_percpu(caifd->pcpu_refcnt);
		kfree(caifd);
	}
	cfcnfg_remove(cfg);

	mutex_unlock(&caifdevs->lock);
	rtnl_unlock();
}

static struct pernet_operations caif_net_ops = {
	.init = caif_init_net,
	.exit = caif_exit_net,
	.id   = &caif_net_id,
	.size = sizeof(struct caif_net),
};

/* Initialize Caif devices list */
static int __init caif_device_init(void)
{
	int result;

	result = register_pernet_subsys(&caif_net_ops);

	if (result)
		return result;

	register_netdevice_notifier(&caif_device_notifier);
	dev_add_pack(&caif_packet_type);

	return result;
}

static void __exit caif_device_exit(void)
{
	unregister_netdevice_notifier(&caif_device_notifier);
	dev_remove_pack(&caif_packet_type);
	unregister_pernet_subsys(&caif_net_ops);
}

module_init(caif_device_init);
module_exit(caif_device_exit);
