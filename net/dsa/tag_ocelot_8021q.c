// SPDX-License-Identifier: GPL-2.0
/* Copyright 2020-2021 NXP
 *
 * An implementation of the software-defined tag_8021q.c tagger format, which
 * also preserves full functionality under a vlan_filtering bridge. It does
 * this by using the TCAM engines for:
 * - pushing the RX VLAN as a second, outer tag, on egress towards the CPU port
 * - redirecting towards the correct front port based on TX VLAN and popping
 *   that on egress
 */
#include <linux/dsa/8021q.h>
#include <linux/dsa/ocelot.h>
#include "dsa_priv.h"

struct ocelot_8021q_tagger_private {
	struct ocelot_8021q_tagger_data data; /* Must be first */
	struct kthread_worker *xmit_worker;
};

static struct sk_buff *ocelot_defer_xmit(struct dsa_port *dp,
					 struct sk_buff *skb)
{
	struct ocelot_8021q_tagger_private *priv = dp->ds->tagger_data;
	struct ocelot_8021q_tagger_data *data = &priv->data;
	void (*xmit_work_fn)(struct kthread_work *work);
	struct felix_deferred_xmit_work *xmit_work;
	struct kthread_worker *xmit_worker;

	xmit_work_fn = data->xmit_work_fn;
	xmit_worker = priv->xmit_worker;

	if (!xmit_work_fn || !xmit_worker)
		return NULL;

	xmit_work = kzalloc(sizeof(*xmit_work), GFP_ATOMIC);
	if (!xmit_work)
		return NULL;

	/* Calls felix_port_deferred_xmit in felix.c */
	kthread_init_work(&xmit_work->work, xmit_work_fn);
	/* Increase refcount so the kfree_skb in dsa_slave_xmit
	 * won't really free the packet.
	 */
	xmit_work->dp = dp;
	xmit_work->skb = skb_get(skb);

	kthread_queue_work(xmit_worker, &xmit_work->work);

	return NULL;
}

static struct sk_buff *ocelot_xmit(struct sk_buff *skb,
				   struct net_device *netdev)
{
	struct dsa_port *dp = dsa_slave_to_port(netdev);
	u16 queue_mapping = skb_get_queue_mapping(skb);
	u8 pcp = netdev_txq_to_tc(netdev, queue_mapping);
	u16 tx_vid = dsa_tag_8021q_tx_vid(dp);
	struct ethhdr *hdr = eth_hdr(skb);

	if (ocelot_ptp_rew_op(skb) || is_link_local_ether_addr(hdr->h_dest))
		return ocelot_defer_xmit(dp, skb);

	return dsa_8021q_xmit(skb, netdev, ETH_P_8021Q,
			      ((pcp << VLAN_PRIO_SHIFT) | tx_vid));
}

static struct sk_buff *ocelot_rcv(struct sk_buff *skb,
				  struct net_device *netdev)
{
	int src_port, switch_id;

	dsa_8021q_rcv(skb, &src_port, &switch_id);

	skb->dev = dsa_master_find_slave(netdev, switch_id, src_port);
	if (!skb->dev)
		return NULL;

	dsa_default_offload_fwd_mark(skb);

	return skb;
}

static void ocelot_disconnect(struct dsa_switch *ds)
{
	struct ocelot_8021q_tagger_private *priv = ds->tagger_data;

	kthread_destroy_worker(priv->xmit_worker);
	kfree(priv);
	ds->tagger_data = NULL;
}

static int ocelot_connect(struct dsa_switch *ds)
{
	struct ocelot_8021q_tagger_private *priv;
	int err;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->xmit_worker = kthread_create_worker(0, "felix_xmit");
	if (IS_ERR(priv->xmit_worker)) {
		err = PTR_ERR(priv->xmit_worker);
		kfree(priv);
		return err;
	}

	ds->tagger_data = priv;

	return 0;
}

static const struct dsa_device_ops ocelot_8021q_netdev_ops = {
	.name			= "ocelot-8021q",
	.proto			= DSA_TAG_PROTO_OCELOT_8021Q,
	.xmit			= ocelot_xmit,
	.rcv			= ocelot_rcv,
	.connect		= ocelot_connect,
	.disconnect		= ocelot_disconnect,
	.needed_headroom	= VLAN_HLEN,
	.promisc_on_master	= true,
};

MODULE_LICENSE("GPL v2");
MODULE_ALIAS_DSA_TAG_DRIVER(DSA_TAG_PROTO_OCELOT_8021Q);

module_dsa_tag_driver(ocelot_8021q_netdev_ops);
