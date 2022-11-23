// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2010-2011 EIA Electronics,
//                         Pieter Beyens <pieter.beyens@eia.be>
// Copyright (c) 2010-2011 EIA Electronics,
//                         Kurt Van Dijck <kurt.van.dijck@eia.be>
// Copyright (c) 2018 Protonic,
//                         Robin van der Gracht <robin@protonic.nl>
// Copyright (c) 2017-2019 Pengutronix,
//                         Marc Kleine-Budde <kernel@pengutronix.de>
// Copyright (c) 2017-2019 Pengutronix,
//                         Oleksij Rempel <kernel@pengutronix.de>

/* Core of can-j1939 that links j1939 to CAN. */

#include <linux/can/can-ml.h>
#include <linux/can/core.h>
#include <linux/can/skb.h>
#include <linux/if_arp.h>
#include <linux/module.h>

#include "j1939-priv.h"

MODULE_DESCRIPTION("PF_CAN SAE J1939");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("EIA Electronics (Kurt Van Dijck & Pieter Beyens)");
MODULE_ALIAS("can-proto-" __stringify(CAN_J1939));

/* LOWLEVEL CAN interface */

/* CAN_HDR: #bytes before can_frame data part */
#define J1939_CAN_HDR (offsetof(struct can_frame, data))

/* CAN_FTR: #bytes beyond data part */
#define J1939_CAN_FTR (sizeof(struct can_frame) - J1939_CAN_HDR - \
		 sizeof(((struct can_frame *)0)->data))

/* lowest layer */
static void j1939_can_recv(struct sk_buff *iskb, void *data)
{
	struct j1939_priv *priv = data;
	struct sk_buff *skb;
	struct j1939_sk_buff_cb *skcb, *iskcb;
	struct can_frame *cf;

	/* create a copy of the skb
	 * j1939 only delivers the real data bytes,
	 * the header goes into sockaddr.
	 * j1939 may not touch the incoming skb in such way
	 */
	skb = skb_clone(iskb, GFP_ATOMIC);
	if (!skb)
		return;

	j1939_priv_get(priv);
	can_skb_set_owner(skb, iskb->sk);

	/* get a pointer to the header of the skb
	 * the skb payload (pointer) is moved, so that the next skb_data
	 * returns the actual payload
	 */
	cf = (void *)skb->data;
	skb_pull(skb, J1939_CAN_HDR);

	/* fix length, set to dlc, with 8 maximum */
	skb_trim(skb, min_t(uint8_t, cf->can_dlc, 8));

	/* set addr */
	skcb = j1939_skb_to_cb(skb);
	memset(skcb, 0, sizeof(*skcb));

	iskcb = j1939_skb_to_cb(iskb);
	skcb->tskey = iskcb->tskey;
	skcb->priority = (cf->can_id >> 26) & 0x7;
	skcb->addr.sa = cf->can_id;
	skcb->addr.pgn = (cf->can_id >> 8) & J1939_PGN_MAX;
	/* set default message type */
	skcb->addr.type = J1939_TP;

	if (!j1939_address_is_valid(skcb->addr.sa)) {
		netdev_err_once(priv->ndev, "%s: sa is broadcast address, ignoring!\n",
				__func__);
		goto done;
	}

	if (j1939_pgn_is_pdu1(skcb->addr.pgn)) {
		/* Type 1: with destination address */
		skcb->addr.da = skcb->addr.pgn;
		/* normalize pgn: strip dst address */
		skcb->addr.pgn &= 0x3ff00;
	} else {
		/* set broadcast address */
		skcb->addr.da = J1939_NO_ADDR;
	}

	/* update localflags */
	read_lock_bh(&priv->lock);
	if (j1939_address_is_unicast(skcb->addr.sa) &&
	    priv->ents[skcb->addr.sa].nusers)
		skcb->flags |= J1939_ECU_LOCAL_SRC;
	if (j1939_address_is_unicast(skcb->addr.da) &&
	    priv->ents[skcb->addr.da].nusers)
		skcb->flags |= J1939_ECU_LOCAL_DST;
	read_unlock_bh(&priv->lock);

	/* deliver into the j1939 stack ... */
	j1939_ac_recv(priv, skb);

	if (j1939_tp_recv(priv, skb))
		/* this means the transport layer processed the message */
		goto done;

	j1939_simple_recv(priv, skb);
	j1939_sk_recv(priv, skb);
 done:
	j1939_priv_put(priv);
	kfree_skb(skb);
}

/* NETDEV MANAGEMENT */

/* values for can_rx_(un)register */
#define J1939_CAN_ID CAN_EFF_FLAG
#define J1939_CAN_MASK (CAN_EFF_FLAG | CAN_RTR_FLAG)

static DEFINE_SPINLOCK(j1939_netdev_lock);

static struct j1939_priv *j1939_priv_create(struct net_device *ndev)
{
	struct j1939_priv *priv;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return NULL;

	rwlock_init(&priv->lock);
	INIT_LIST_HEAD(&priv->ecus);
	priv->ndev = ndev;
	kref_init(&priv->kref);
	kref_init(&priv->rx_kref);
	dev_hold(ndev);

	netdev_dbg(priv->ndev, "%s : 0x%p\n", __func__, priv);

	return priv;
}

static inline void j1939_priv_set(struct net_device *ndev,
				  struct j1939_priv *priv)
{
	struct can_ml_priv *can_ml = can_get_ml_priv(ndev);

	can_ml->j1939_priv = priv;
}

static void __j1939_priv_release(struct kref *kref)
{
	struct j1939_priv *priv = container_of(kref, struct j1939_priv, kref);
	struct net_device *ndev = priv->ndev;

	netdev_dbg(priv->ndev, "%s: 0x%p\n", __func__, priv);

	WARN_ON_ONCE(!list_empty(&priv->active_session_list));
	WARN_ON_ONCE(!list_empty(&priv->ecus));
	WARN_ON_ONCE(!list_empty(&priv->j1939_socks));

	dev_put(ndev);
	kfree(priv);
}

void j1939_priv_put(struct j1939_priv *priv)
{
	kref_put(&priv->kref, __j1939_priv_release);
}

void j1939_priv_get(struct j1939_priv *priv)
{
	kref_get(&priv->kref);
}

static int j1939_can_rx_register(struct j1939_priv *priv)
{
	struct net_device *ndev = priv->ndev;
	int ret;

	j1939_priv_get(priv);
	ret = can_rx_register(dev_net(ndev), ndev, J1939_CAN_ID, J1939_CAN_MASK,
			      j1939_can_recv, priv, "j1939", NULL);
	if (ret < 0) {
		j1939_priv_put(priv);
		return ret;
	}

	return 0;
}

static void j1939_can_rx_unregister(struct j1939_priv *priv)
{
	struct net_device *ndev = priv->ndev;

	can_rx_unregister(dev_net(ndev), ndev, J1939_CAN_ID, J1939_CAN_MASK,
			  j1939_can_recv, priv);

	/* The last reference of priv is dropped by the RCU deferred
	 * j1939_sk_sock_destruct() of the last socket, so we can
	 * safely drop this reference here.
	 */
	j1939_priv_put(priv);
}

static void __j1939_rx_release(struct kref *kref)
	__releases(&j1939_netdev_lock)
{
	struct j1939_priv *priv = container_of(kref, struct j1939_priv,
					       rx_kref);

	j1939_can_rx_unregister(priv);
	j1939_ecu_unmap_all(priv);
	j1939_priv_set(priv->ndev, NULL);
	spin_unlock(&j1939_netdev_lock);
}

/* get pointer to priv without increasing ref counter */
static inline struct j1939_priv *j1939_ndev_to_priv(struct net_device *ndev)
{
	struct can_ml_priv *can_ml = can_get_ml_priv(ndev);

	return can_ml->j1939_priv;
}

static struct j1939_priv *j1939_priv_get_by_ndev_locked(struct net_device *ndev)
{
	struct j1939_priv *priv;

	lockdep_assert_held(&j1939_netdev_lock);

	priv = j1939_ndev_to_priv(ndev);
	if (priv)
		j1939_priv_get(priv);

	return priv;
}

static struct j1939_priv *j1939_priv_get_by_ndev(struct net_device *ndev)
{
	struct j1939_priv *priv;

	spin_lock(&j1939_netdev_lock);
	priv = j1939_priv_get_by_ndev_locked(ndev);
	spin_unlock(&j1939_netdev_lock);

	return priv;
}

struct j1939_priv *j1939_netdev_start(struct net_device *ndev)
{
	struct j1939_priv *priv, *priv_new;
	int ret;

	spin_lock(&j1939_netdev_lock);
	priv = j1939_priv_get_by_ndev_locked(ndev);
	if (priv) {
		kref_get(&priv->rx_kref);
		spin_unlock(&j1939_netdev_lock);
		return priv;
	}
	spin_unlock(&j1939_netdev_lock);

	priv = j1939_priv_create(ndev);
	if (!priv)
		return ERR_PTR(-ENOMEM);

	j1939_tp_init(priv);
	spin_lock_init(&priv->j1939_socks_lock);
	INIT_LIST_HEAD(&priv->j1939_socks);

	spin_lock(&j1939_netdev_lock);
	priv_new = j1939_priv_get_by_ndev_locked(ndev);
	if (priv_new) {
		/* Someone was faster than us, use their priv and roll
		 * back our's.
		 */
		kref_get(&priv_new->rx_kref);
		spin_unlock(&j1939_netdev_lock);
		dev_put(ndev);
		kfree(priv);
		return priv_new;
	}
	j1939_priv_set(ndev, priv);
	spin_unlock(&j1939_netdev_lock);

	ret = j1939_can_rx_register(priv);
	if (ret < 0)
		goto out_priv_put;

	return priv;

 out_priv_put:
	j1939_priv_set(ndev, NULL);
	dev_put(ndev);
	kfree(priv);

	return ERR_PTR(ret);
}

void j1939_netdev_stop(struct j1939_priv *priv)
{
	kref_put_lock(&priv->rx_kref, __j1939_rx_release, &j1939_netdev_lock);
	j1939_priv_put(priv);
}

int j1939_send_one(struct j1939_priv *priv, struct sk_buff *skb)
{
	int ret, dlc;
	canid_t canid;
	struct j1939_sk_buff_cb *skcb = j1939_skb_to_cb(skb);
	struct can_frame *cf;

	/* apply sanity checks */
	if (j1939_pgn_is_pdu1(skcb->addr.pgn))
		skcb->addr.pgn &= J1939_PGN_PDU1_MAX;
	else
		skcb->addr.pgn &= J1939_PGN_MAX;

	if (skcb->priority > 7)
		skcb->priority = 6;

	ret = j1939_ac_fixup(priv, skb);
	if (unlikely(ret))
		goto failed;
	dlc = skb->len;

	/* re-claim the CAN_HDR from the SKB */
	cf = skb_push(skb, J1939_CAN_HDR);

	/* initialize header structure */
	memset(cf, 0, J1939_CAN_HDR);

	/* make it a full can frame again */
	skb_put(skb, J1939_CAN_FTR + (8 - dlc));

	canid = CAN_EFF_FLAG |
		(skcb->priority << 26) |
		(skcb->addr.pgn << 8) |
		skcb->addr.sa;
	if (j1939_pgn_is_pdu1(skcb->addr.pgn))
		canid |= skcb->addr.da << 8;

	cf->can_id = canid;
	cf->can_dlc = dlc;

	return can_send(skb, 1);

 failed:
	kfree_skb(skb);
	return ret;
}

static int j1939_netdev_notify(struct notifier_block *nb,
			       unsigned long msg, void *data)
{
	struct net_device *ndev = netdev_notifier_info_to_dev(data);
	struct can_ml_priv *can_ml = can_get_ml_priv(ndev);
	struct j1939_priv *priv;

	if (!can_ml)
		goto notify_done;

	priv = j1939_priv_get_by_ndev(ndev);
	if (!priv)
		goto notify_done;

	switch (msg) {
	case NETDEV_DOWN:
		j1939_cancel_active_session(priv, NULL);
		j1939_sk_netdev_event_netdown(priv);
		j1939_ecu_unmap_all(priv);
		break;
	}

	j1939_priv_put(priv);

notify_done:
	return NOTIFY_DONE;
}

static struct notifier_block j1939_netdev_notifier = {
	.notifier_call = j1939_netdev_notify,
};

/* MODULE interface */
static __init int j1939_module_init(void)
{
	int ret;

	pr_info("can: SAE J1939\n");

	ret = register_netdevice_notifier(&j1939_netdev_notifier);
	if (ret)
		goto fail_notifier;

	ret = can_proto_register(&j1939_can_proto);
	if (ret < 0) {
		pr_err("can: registration of j1939 protocol failed\n");
		goto fail_sk;
	}

	return 0;

 fail_sk:
	unregister_netdevice_notifier(&j1939_netdev_notifier);
 fail_notifier:
	return ret;
}

static __exit void j1939_module_exit(void)
{
	can_proto_unregister(&j1939_can_proto);

	unregister_netdevice_notifier(&j1939_netdev_notifier);
}

module_init(j1939_module_init);
module_exit(j1939_module_exit);
