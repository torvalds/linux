/*
 * Copyright 2004, Instant802 Networks, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/module.h>
#include <linux/if_arp.h>
#include <linux/types.h>
#include <net/ip.h>
#include <net/pkt_sched.h>

#include <net/mac80211.h>
#include "ieee80211_i.h"
#include "wme.h"

/* maximum number of hardware queues we support. */
#define TC_80211_MAX_QUEUES 16

const int ieee802_1d_to_ac[8] = { 2, 3, 3, 2, 1, 1, 0, 0 };

struct ieee80211_sched_data
{
	unsigned long qdisc_pool[BITS_TO_LONGS(TC_80211_MAX_QUEUES)];
	struct tcf_proto *filter_list;
	struct Qdisc *queues[TC_80211_MAX_QUEUES];
	struct sk_buff_head requeued[TC_80211_MAX_QUEUES];
};

static const char llc_ip_hdr[8] = {0xAA, 0xAA, 0x3, 0, 0, 0, 0x08, 0};

/* given a data frame determine the 802.1p/1d tag to use */
static inline unsigned classify_1d(struct sk_buff *skb, struct Qdisc *qd)
{
	struct iphdr *ip;
	int dscp;
	int offset;

	struct ieee80211_sched_data *q = qdisc_priv(qd);
	struct tcf_result res = { -1, 0 };

	/* if there is a user set filter list, call out to that */
	if (q->filter_list) {
		tc_classify(skb, q->filter_list, &res);
		if (res.class != -1)
			return res.class;
	}

	/* skb->priority values from 256->263 are magic values to
	 * directly indicate a specific 802.1d priority.
	 * This is used to allow 802.1d priority to be passed directly in
	 * from VLAN tags, etc. */
	if (skb->priority >= 256 && skb->priority <= 263)
		return skb->priority - 256;

	/* check there is a valid IP header present */
	offset = ieee80211_get_hdrlen_from_skb(skb);
	if (skb->len < offset + sizeof(llc_ip_hdr) + sizeof(*ip) ||
	    memcmp(skb->data + offset, llc_ip_hdr, sizeof(llc_ip_hdr)))
		return 0;

	ip = (struct iphdr *) (skb->data + offset + sizeof(llc_ip_hdr));

	dscp = ip->tos & 0xfc;
	if (dscp & 0x1c)
		return 0;
	return dscp >> 5;
}


static inline int wme_downgrade_ac(struct sk_buff *skb)
{
	switch (skb->priority) {
	case 6:
	case 7:
		skb->priority = 5; /* VO -> VI */
		return 0;
	case 4:
	case 5:
		skb->priority = 3; /* VI -> BE */
		return 0;
	case 0:
	case 3:
		skb->priority = 2; /* BE -> BK */
		return 0;
	default:
		return -1;
	}
}


/* positive return value indicates which queue to use
 * negative return value indicates to drop the frame */
static inline int classify80211(struct sk_buff *skb, struct Qdisc *qd)
{
	struct ieee80211_local *local = wdev_priv(qd->dev->ieee80211_ptr);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	unsigned short fc = le16_to_cpu(hdr->frame_control);
	int qos;

	/* see if frame is data or non data frame */
	if (unlikely((fc & IEEE80211_FCTL_FTYPE) != IEEE80211_FTYPE_DATA)) {
		/* management frames go on AC_VO queue, but are sent
		* without QoS control fields */
		return IEEE80211_TX_QUEUE_DATA0;
	}

	if (0 /* injected */) {
		/* use AC from radiotap */
	}

	/* is this a QoS frame? */
	qos = fc & IEEE80211_STYPE_QOS_DATA;

	if (!qos) {
		skb->priority = 0; /* required for correct WPA/11i MIC */
		return ieee802_1d_to_ac[skb->priority];
	}

	/* use the data classifier to determine what 802.1d tag the
	 * data frame has */
	skb->priority = classify_1d(skb, qd);

	/* in case we are a client verify acm is not set for this ac */
	while (unlikely(local->wmm_acm & BIT(skb->priority))) {
		if (wme_downgrade_ac(skb)) {
			/* No AC with lower priority has acm=0, drop packet. */
			return -1;
		}
	}

	/* look up which queue to use for frames with this 1d tag */
	return ieee802_1d_to_ac[skb->priority];
}


static int wme_qdiscop_enqueue(struct sk_buff *skb, struct Qdisc* qd)
{
	struct ieee80211_local *local = wdev_priv(qd->dev->ieee80211_ptr);
	struct ieee80211_sched_data *q = qdisc_priv(qd);
	struct ieee80211_tx_packet_data *pkt_data =
		(struct ieee80211_tx_packet_data *) skb->cb;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	unsigned short fc = le16_to_cpu(hdr->frame_control);
	struct Qdisc *qdisc;
	int err, queue;
	struct sta_info *sta;
	u8 tid;

	if (pkt_data->flags & IEEE80211_TXPD_REQUEUE) {
		queue = pkt_data->queue;
		rcu_read_lock();
		sta = sta_info_get(local, hdr->addr1);
		tid = skb->priority & QOS_CONTROL_TAG1D_MASK;
		if (sta) {
			int ampdu_queue = sta->tid_to_tx_q[tid];
			if ((ampdu_queue < local->hw.queues) &&
			    test_bit(ampdu_queue, q->qdisc_pool)) {
				queue = ampdu_queue;
				pkt_data->flags |= IEEE80211_TXPD_AMPDU;
			} else {
				pkt_data->flags &= ~IEEE80211_TXPD_AMPDU;
			}
		}
		rcu_read_unlock();
		skb_queue_tail(&q->requeued[queue], skb);
		qd->q.qlen++;
		return 0;
	}

	queue = classify80211(skb, qd);

	/* now we know the 1d priority, fill in the QoS header if there is one
	 */
	if (WLAN_FC_IS_QOS_DATA(fc)) {
		u8 *p = skb->data + ieee80211_get_hdrlen(fc) - 2;
		u8 ack_policy = 0;
		tid = skb->priority & QOS_CONTROL_TAG1D_MASK;
		if (local->wifi_wme_noack_test)
			ack_policy |= QOS_CONTROL_ACK_POLICY_NOACK <<
					QOS_CONTROL_ACK_POLICY_SHIFT;
		/* qos header is 2 bytes, second reserved */
		*p = ack_policy | tid;
		p++;
		*p = 0;

		rcu_read_lock();

		sta = sta_info_get(local, hdr->addr1);
		if (sta) {
			int ampdu_queue = sta->tid_to_tx_q[tid];
			if ((ampdu_queue < local->hw.queues) &&
				test_bit(ampdu_queue, q->qdisc_pool)) {
				queue = ampdu_queue;
				pkt_data->flags |= IEEE80211_TXPD_AMPDU;
			} else {
				pkt_data->flags &= ~IEEE80211_TXPD_AMPDU;
			}
		}

		rcu_read_unlock();
	}

	if (unlikely(queue >= local->hw.queues)) {
#if 0
		if (net_ratelimit()) {
			printk(KERN_DEBUG "%s - queue=%d (hw does not "
			       "support) -> %d\n",
			       __func__, queue, local->hw.queues - 1);
		}
#endif
		queue = local->hw.queues - 1;
	}

	if (unlikely(queue < 0)) {
			kfree_skb(skb);
			err = NET_XMIT_DROP;
	} else {
		tid = skb->priority & QOS_CONTROL_TAG1D_MASK;
		pkt_data->queue = (unsigned int) queue;
		qdisc = q->queues[queue];
		err = qdisc->enqueue(skb, qdisc);
		if (err == NET_XMIT_SUCCESS) {
			qd->q.qlen++;
			qd->bstats.bytes += skb->len;
			qd->bstats.packets++;
			return NET_XMIT_SUCCESS;
		}
	}
	qd->qstats.drops++;
	return err;
}


/* TODO: clean up the cases where master_hard_start_xmit
 * returns non 0 - it shouldn't ever do that. Once done we
 * can remove this function */
static int wme_qdiscop_requeue(struct sk_buff *skb, struct Qdisc* qd)
{
	struct ieee80211_sched_data *q = qdisc_priv(qd);
	struct ieee80211_tx_packet_data *pkt_data =
		(struct ieee80211_tx_packet_data *) skb->cb;
	struct Qdisc *qdisc;
	int err;

	/* we recorded which queue to use earlier! */
	qdisc = q->queues[pkt_data->queue];

	if ((err = qdisc->ops->requeue(skb, qdisc)) == 0) {
		qd->q.qlen++;
		return 0;
	}
	qd->qstats.drops++;
	return err;
}


static struct sk_buff *wme_qdiscop_dequeue(struct Qdisc* qd)
{
	struct ieee80211_sched_data *q = qdisc_priv(qd);
	struct net_device *dev = qd->dev;
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_hw *hw = &local->hw;
	struct sk_buff *skb;
	struct Qdisc *qdisc;
	int queue;

	/* check all the h/w queues in numeric/priority order */
	for (queue = 0; queue < hw->queues; queue++) {
		/* see if there is room in this hardware queue */
		if ((test_bit(IEEE80211_LINK_STATE_XOFF,
				&local->state[queue])) ||
		    (test_bit(IEEE80211_LINK_STATE_PENDING,
				&local->state[queue])) ||
			 (!test_bit(queue, q->qdisc_pool)))
			continue;

		/* there is space - try and get a frame */
		skb = skb_dequeue(&q->requeued[queue]);
		if (skb) {
			qd->q.qlen--;
			return skb;
		}

		qdisc = q->queues[queue];
		skb = qdisc->dequeue(qdisc);
		if (skb) {
			qd->q.qlen--;
			return skb;
		}
	}
	/* returning a NULL here when all the h/w queues are full means we
	 * never need to call netif_stop_queue in the driver */
	return NULL;
}


static void wme_qdiscop_reset(struct Qdisc* qd)
{
	struct ieee80211_sched_data *q = qdisc_priv(qd);
	struct ieee80211_local *local = wdev_priv(qd->dev->ieee80211_ptr);
	struct ieee80211_hw *hw = &local->hw;
	int queue;

	/* QUESTION: should we have some hardware flush functionality here? */

	for (queue = 0; queue < hw->queues; queue++) {
		skb_queue_purge(&q->requeued[queue]);
		qdisc_reset(q->queues[queue]);
	}
	qd->q.qlen = 0;
}


static void wme_qdiscop_destroy(struct Qdisc* qd)
{
	struct ieee80211_sched_data *q = qdisc_priv(qd);
	struct ieee80211_local *local = wdev_priv(qd->dev->ieee80211_ptr);
	struct ieee80211_hw *hw = &local->hw;
	int queue;

	tcf_destroy_chain(q->filter_list);
	q->filter_list = NULL;

	for (queue=0; queue < hw->queues; queue++) {
		skb_queue_purge(&q->requeued[queue]);
		qdisc_destroy(q->queues[queue]);
		q->queues[queue] = &noop_qdisc;
	}
}


/* called whenever parameters are updated on existing qdisc */
static int wme_qdiscop_tune(struct Qdisc *qd, struct nlattr *opt)
{
/*	struct ieee80211_sched_data *q = qdisc_priv(qd);
*/
	/* check our options block is the right size */
	/* copy any options to our local structure */
/*	Ignore options block for now - always use static mapping
	struct tc_ieee80211_qopt *qopt = nla_data(opt);

	if (opt->nla_len < nla_attr_size(sizeof(*qopt)))
		return -EINVAL;
	memcpy(q->tag2queue, qopt->tag2queue, sizeof(qopt->tag2queue));
*/
	return 0;
}


/* called during initial creation of qdisc on device */
static int wme_qdiscop_init(struct Qdisc *qd, struct nlattr *opt)
{
	struct ieee80211_sched_data *q = qdisc_priv(qd);
	struct net_device *dev = qd->dev;
	struct ieee80211_local *local;
	int queues;
	int err = 0, i;

	/* check that device is a mac80211 device */
	if (!dev->ieee80211_ptr ||
	    dev->ieee80211_ptr->wiphy->privid != mac80211_wiphy_privid)
		return -EINVAL;

	/* check this device is an ieee80211 master type device */
	if (dev->type != ARPHRD_IEEE80211)
		return -EINVAL;

	/* check that there is no qdisc currently attached to device
	 * this ensures that we will be the root qdisc. (I can't find a better
	 * way to test this explicitly) */
	if (dev->qdisc_sleeping != &noop_qdisc)
		return -EINVAL;

	if (qd->flags & TCQ_F_INGRESS)
		return -EINVAL;

	local = wdev_priv(dev->ieee80211_ptr);
	queues = local->hw.queues;

	/* if options were passed in, set them */
	if (opt) {
		err = wme_qdiscop_tune(qd, opt);
	}

	/* create child queues */
	for (i = 0; i < queues; i++) {
		skb_queue_head_init(&q->requeued[i]);
		q->queues[i] = qdisc_create_dflt(qd->dev, &pfifo_qdisc_ops,
						 qd->handle);
		if (!q->queues[i]) {
			q->queues[i] = &noop_qdisc;
			printk(KERN_ERR "%s child qdisc %i creation failed", dev->name, i);
		}
	}

	/* reserve all legacy QoS queues */
	for (i = 0; i < min(IEEE80211_TX_QUEUE_DATA4, queues); i++)
		set_bit(i, q->qdisc_pool);

	return err;
}

static int wme_qdiscop_dump(struct Qdisc *qd, struct sk_buff *skb)
{
/*	struct ieee80211_sched_data *q = qdisc_priv(qd);
	unsigned char *p = skb->tail;
	struct tc_ieee80211_qopt opt;

	memcpy(&opt.tag2queue, q->tag2queue, TC_80211_MAX_TAG + 1);
	NLA_PUT(skb, TCA_OPTIONS, sizeof(opt), &opt);
*/	return skb->len;
/*
nla_put_failure:
	skb_trim(skb, p - skb->data);*/
	return -1;
}


static int wme_classop_graft(struct Qdisc *qd, unsigned long arg,
			     struct Qdisc *new, struct Qdisc **old)
{
	struct ieee80211_sched_data *q = qdisc_priv(qd);
	struct ieee80211_local *local = wdev_priv(qd->dev->ieee80211_ptr);
	struct ieee80211_hw *hw = &local->hw;
	unsigned long queue = arg - 1;

	if (queue >= hw->queues)
		return -EINVAL;

	if (!new)
		new = &noop_qdisc;

	sch_tree_lock(qd);
	*old = q->queues[queue];
	q->queues[queue] = new;
	qdisc_reset(*old);
	sch_tree_unlock(qd);

	return 0;
}


static struct Qdisc *
wme_classop_leaf(struct Qdisc *qd, unsigned long arg)
{
	struct ieee80211_sched_data *q = qdisc_priv(qd);
	struct ieee80211_local *local = wdev_priv(qd->dev->ieee80211_ptr);
	struct ieee80211_hw *hw = &local->hw;
	unsigned long queue = arg - 1;

	if (queue >= hw->queues)
		return NULL;

	return q->queues[queue];
}


static unsigned long wme_classop_get(struct Qdisc *qd, u32 classid)
{
	struct ieee80211_local *local = wdev_priv(qd->dev->ieee80211_ptr);
	struct ieee80211_hw *hw = &local->hw;
	unsigned long queue = TC_H_MIN(classid);

	if (queue - 1 >= hw->queues)
		return 0;

	return queue;
}


static unsigned long wme_classop_bind(struct Qdisc *qd, unsigned long parent,
				      u32 classid)
{
	return wme_classop_get(qd, classid);
}


static void wme_classop_put(struct Qdisc *q, unsigned long cl)
{
}


static int wme_classop_change(struct Qdisc *qd, u32 handle, u32 parent,
			      struct nlattr **tca, unsigned long *arg)
{
	unsigned long cl = *arg;
	struct ieee80211_local *local = wdev_priv(qd->dev->ieee80211_ptr);
	struct ieee80211_hw *hw = &local->hw;

	if (cl - 1 > hw->queues)
		return -ENOENT;

	/* TODO: put code to program hardware queue parameters here,
	 * to allow programming from tc command line */

	return 0;
}


/* we don't support deleting hardware queues
 * when we add WMM-SA support - TSPECs may be deleted here */
static int wme_classop_delete(struct Qdisc *qd, unsigned long cl)
{
	struct ieee80211_local *local = wdev_priv(qd->dev->ieee80211_ptr);
	struct ieee80211_hw *hw = &local->hw;

	if (cl - 1 > hw->queues)
		return -ENOENT;
	return 0;
}


static int wme_classop_dump_class(struct Qdisc *qd, unsigned long cl,
				  struct sk_buff *skb, struct tcmsg *tcm)
{
	struct ieee80211_sched_data *q = qdisc_priv(qd);
	struct ieee80211_local *local = wdev_priv(qd->dev->ieee80211_ptr);
	struct ieee80211_hw *hw = &local->hw;

	if (cl - 1 > hw->queues)
		return -ENOENT;
	tcm->tcm_handle = TC_H_MIN(cl);
	tcm->tcm_parent = qd->handle;
	tcm->tcm_info = q->queues[cl-1]->handle; /* do we need this? */
	return 0;
}


static void wme_classop_walk(struct Qdisc *qd, struct qdisc_walker *arg)
{
	struct ieee80211_local *local = wdev_priv(qd->dev->ieee80211_ptr);
	struct ieee80211_hw *hw = &local->hw;
	int queue;

	if (arg->stop)
		return;

	for (queue = 0; queue < hw->queues; queue++) {
		if (arg->count < arg->skip) {
			arg->count++;
			continue;
		}
		/* we should return classids for our internal queues here
		 * as well as the external ones */
		if (arg->fn(qd, queue+1, arg) < 0) {
			arg->stop = 1;
			break;
		}
		arg->count++;
	}
}


static struct tcf_proto ** wme_classop_find_tcf(struct Qdisc *qd,
						unsigned long cl)
{
	struct ieee80211_sched_data *q = qdisc_priv(qd);

	if (cl)
		return NULL;

	return &q->filter_list;
}


/* this qdisc is classful (i.e. has classes, some of which may have leaf qdiscs attached)
 * - these are the operations on the classes */
static const struct Qdisc_class_ops class_ops =
{
	.graft = wme_classop_graft,
	.leaf = wme_classop_leaf,

	.get = wme_classop_get,
	.put = wme_classop_put,
	.change = wme_classop_change,
	.delete = wme_classop_delete,
	.walk = wme_classop_walk,

	.tcf_chain = wme_classop_find_tcf,
	.bind_tcf = wme_classop_bind,
	.unbind_tcf = wme_classop_put,

	.dump = wme_classop_dump_class,
};


/* queueing discipline operations */
static struct Qdisc_ops wme_qdisc_ops __read_mostly =
{
	.next = NULL,
	.cl_ops = &class_ops,
	.id = "ieee80211",
	.priv_size = sizeof(struct ieee80211_sched_data),

	.enqueue = wme_qdiscop_enqueue,
	.dequeue = wme_qdiscop_dequeue,
	.requeue = wme_qdiscop_requeue,
	.drop = NULL, /* drop not needed since we are always the root qdisc */

	.init = wme_qdiscop_init,
	.reset = wme_qdiscop_reset,
	.destroy = wme_qdiscop_destroy,
	.change = wme_qdiscop_tune,

	.dump = wme_qdiscop_dump,
};


void ieee80211_install_qdisc(struct net_device *dev)
{
	struct Qdisc *qdisc;

	qdisc = qdisc_create_dflt(dev, &wme_qdisc_ops, TC_H_ROOT);
	if (!qdisc) {
		printk(KERN_ERR "%s: qdisc installation failed\n", dev->name);
		return;
	}

	/* same handle as would be allocated by qdisc_alloc_handle() */
	qdisc->handle = 0x80010000;

	qdisc_lock_tree(dev);
	list_add_tail(&qdisc->list, &dev->qdisc_list);
	dev->qdisc_sleeping = qdisc;
	qdisc_unlock_tree(dev);
}


int ieee80211_qdisc_installed(struct net_device *dev)
{
	return dev->qdisc_sleeping->ops == &wme_qdisc_ops;
}


int ieee80211_wme_register(void)
{
	return register_qdisc(&wme_qdisc_ops);
}


void ieee80211_wme_unregister(void)
{
	unregister_qdisc(&wme_qdisc_ops);
}

int ieee80211_ht_agg_queue_add(struct ieee80211_local *local,
			struct sta_info *sta, u16 tid)
{
	int i;
	struct ieee80211_sched_data *q =
			qdisc_priv(local->mdev->qdisc_sleeping);
	DECLARE_MAC_BUF(mac);

	/* prepare the filter and save it for the SW queue
	 * matching the recieved HW queue */

	/* try to get a Qdisc from the pool */
	for (i = IEEE80211_TX_QUEUE_BEACON; i < local->hw.queues; i++)
		if (!test_and_set_bit(i, q->qdisc_pool)) {
			ieee80211_stop_queue(local_to_hw(local), i);
			sta->tid_to_tx_q[tid] = i;

			/* IF there are already pending packets
			 * on this tid first we need to drain them
			 * on the previous queue
			 * since HT is strict in order */
#ifdef CONFIG_MAC80211_HT_DEBUG
			if (net_ratelimit())
				printk(KERN_DEBUG "allocated aggregation queue"
					" %d tid %d addr %s pool=0x%lX",
					i, tid, print_mac(mac, sta->addr),
					q->qdisc_pool[0]);
#endif /* CONFIG_MAC80211_HT_DEBUG */
			return 0;
		}

	return -EAGAIN;
}

/**
 * the caller needs to hold local->mdev->queue_lock
 */
void ieee80211_ht_agg_queue_remove(struct ieee80211_local *local,
				   struct sta_info *sta, u16 tid,
				   u8 requeue)
{
	struct ieee80211_sched_data *q =
		qdisc_priv(local->mdev->qdisc_sleeping);
	int agg_queue = sta->tid_to_tx_q[tid];

	/* return the qdisc to the pool */
	clear_bit(agg_queue, q->qdisc_pool);
	sta->tid_to_tx_q[tid] = local->hw.queues;

	if (requeue)
		ieee80211_requeue(local, agg_queue);
	else
		q->queues[agg_queue]->ops->reset(q->queues[agg_queue]);
}

void ieee80211_requeue(struct ieee80211_local *local, int queue)
{
	struct Qdisc *root_qd = local->mdev->qdisc_sleeping;
	struct ieee80211_sched_data *q = qdisc_priv(root_qd);
	struct Qdisc *qdisc = q->queues[queue];
	struct sk_buff *skb = NULL;
	u32 len = qdisc->q.qlen;

	if (!qdisc || !qdisc->dequeue)
		return;

	printk(KERN_DEBUG "requeue: qlen = %d\n", qdisc->q.qlen);
	for (len = qdisc->q.qlen; len > 0; len--) {
		skb = qdisc->dequeue(qdisc);
		root_qd->q.qlen--;
		/* packet will be classified again and */
		/* skb->packet_data->queue will be overridden if needed */
		if (skb)
			wme_qdiscop_enqueue(skb, root_qd);
	}
}
