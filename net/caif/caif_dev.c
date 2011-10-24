/*
 * CAIF Interface registration.
 * Copyright (C) ST-Ericsson AB 2010
 * Author:	Sjur Brendeland/sjur.brandeland@stericsson.com
 * License terms: GNU General Public License (GPL) version 2
 *
 * Borrowed heavily from file: pn_dev.c. Thanks to
 *  Remi Denis-Courmont <remi.denis-courmont@nokia.com>
 *  and Sakari Ailus <sakari.ailus@nokia.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ":%s(): " fmt, __func__

#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/if_arp.h>
#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/mutex.h>
#include <net/netns/generic.h>
#include <net/net_namespace.h>
#include <net/pkt_sched.h>
#include <net/caif/caif_device.h>
#include <net/caif/caif_layer.h>
#include <net/caif/cfpkt.h>
#include <net/caif/cfcnfg.h>

MODULE_LICENSE("GPL");

/* Used for local tracking of the CAIF net devices */
struct caif_device_entry {
	struct cflayer layer;
	struct list_head list;
	struct net_device *netdev;
	int __percpu *pcpu_refcnt;
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

static int caif_net_id;

struct cfcnfg *get_cfcnfg(struct net *net)
{
	struct caif_net *caifn;
	BUG_ON(!net);
	caifn = net_generic(net, caif_net_id);
	BUG_ON(!caifn);
	return caifn->cfg;
}
EXPORT_SYMBOL(get_cfcnfg);

static struct caif_device_entry_list *caif_device_list(struct net *net)
{
	struct caif_net *caifn;
	BUG_ON(!net);
	caifn = net_generic(net, caif_net_id);
	BUG_ON(!caifn);
	return &caifn->caifdevs;
}

static void caifd_put(struct caif_device_entry *e)
{
	irqsafe_cpu_dec(*e->pcpu_refcnt);
}

static void caifd_hold(struct caif_device_entry *e)
{
	irqsafe_cpu_inc(*e->pcpu_refcnt);
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
	struct caif_device_entry_list *caifdevs;
	struct caif_device_entry *caifd;

	caifdevs = caif_device_list(dev_net(dev));
	BUG_ON(!caifdevs);

	caifd = kzalloc(sizeof(*caifd), GFP_ATOMIC);
	if (!caifd)
		return NULL;
	caifd->pcpu_refcnt = alloc_percpu(int);
	caifd->netdev = dev;
	dev_hold(dev);
	return caifd;
}

static struct caif_device_entry *caif_get(struct net_device *dev)
{
	struct caif_device_entry_list *caifdevs =
	    caif_device_list(dev_net(dev));
	struct caif_device_entry *caifd;
	BUG_ON(!caifdevs);
	list_for_each_entry_rcu(caifd, &caifdevs->list, list) {
		if (caifd->netdev == dev)
			return caifd;
	}
	return NULL;
}

static int transmit(struct cflayer *layer, struct cfpkt *pkt)
{
	int err;
	struct caif_device_entry *caifd =
	    container_of(layer, struct caif_device_entry, layer);
	struct sk_buff *skb;

	skb = cfpkt_tonative(pkt);
	skb->dev = caifd->netdev;

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
	return 0;
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

/* notify Caif of device events */
static int caif_device_notify(struct notifier_block *me, unsigned long what,
			      void *arg)
{
	struct net_device *dev = arg;
	struct caif_device_entry *caifd = NULL;
	struct caif_dev_common *caifdev;
	enum cfcnfg_phy_preference pref;
	enum cfcnfg_phy_type phy_type;
	struct cfcnfg *cfg;
	struct caif_device_entry_list *caifdevs;

	if (dev->type != ARPHRD_CAIF)
		return 0;

	cfg = get_cfcnfg(dev_net(dev));
	if (cfg == NULL)
		return 0;

	caifdevs = caif_device_list(dev_net(dev));

	switch (what) {
	case NETDEV_REGISTER:
		caifd = caif_device_alloc(dev);
		if (!caifd)
			return 0;

		caifdev = netdev_priv(dev);
		caifdev->flowctrl = dev_flowctrl;

		caifd->layer.transmit = transmit;

		if (caifdev->use_frag)
			phy_type = CFPHYTYPE_FRAG;
		else
			phy_type = CFPHYTYPE_CAIF;

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
		strncpy(caifd->layer.name, dev->name,
			sizeof(caifd->layer.name) - 1);
		caifd->layer.name[sizeof(caifd->layer.name) - 1] = 0;

		mutex_lock(&caifdevs->lock);
		list_add_rcu(&caifd->list, &caifdevs->list);

		cfcnfg_add_phy_layer(cfg,
				     phy_type,
				     dev,
				     &caifd->layer,
				     pref,
				     caifdev->use_fcs,
				     caifdev->use_stx);
		mutex_unlock(&caifdevs->lock);
		break;

	case NETDEV_UP:
		rcu_read_lock();

		caifd = caif_get(dev);
		if (caifd == NULL) {
			rcu_read_unlock();
			break;
		}

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
	BUG_ON(!caifn);
	INIT_LIST_HEAD(&caifn->caifdevs.list);
	mutex_init(&caifn->caifdevs.lock);

	caifn->cfg = cfcnfg_create();
	if (!caifn->cfg) {
		pr_warn("can't create cfcnfg\n");
		return -ENOMEM;
	}

	return 0;
}

static void caif_exit_net(struct net *net)
{
	struct caif_device_entry *caifd, *tmp;
	struct caif_device_entry_list *caifdevs =
	    caif_device_list(net);
	struct cfcnfg *cfg;

	rtnl_lock();
	mutex_lock(&caifdevs->lock);

	cfg = get_cfcnfg(net);
	if (cfg == NULL) {
		mutex_unlock(&caifdevs->lock);
		return;
	}

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

	result = register_pernet_device(&caif_net_ops);

	if (result)
		return result;

	register_netdevice_notifier(&caif_device_notifier);
	dev_add_pack(&caif_packet_type);

	return result;
}

static void __exit caif_device_exit(void)
{
	unregister_pernet_device(&caif_net_ops);
	unregister_netdevice_notifier(&caif_device_notifier);
	dev_remove_pack(&caif_packet_type);
}

module_init(caif_device_init);
module_exit(caif_device_exit);
