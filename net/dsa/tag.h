/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef __DSA_TAG_H
#define __DSA_TAG_H

#include <linux/if_vlan.h>
#include <linux/list.h>
#include <linux/types.h>
#include <net/dsa.h>

#include "port.h"
#include "user.h"

struct dsa_tag_driver {
	const struct dsa_device_ops *ops;
	struct list_head list;
	struct module *owner;
};

extern struct packet_type dsa_pack_type;

const struct dsa_device_ops *dsa_tag_driver_get_by_id(int tag_protocol);
const struct dsa_device_ops *dsa_tag_driver_get_by_name(const char *name);
void dsa_tag_driver_put(const struct dsa_device_ops *ops);
const char *dsa_tag_protocol_to_str(const struct dsa_device_ops *ops);

static inline int dsa_tag_protocol_overhead(const struct dsa_device_ops *ops)
{
	return ops->needed_headroom + ops->needed_tailroom;
}

static inline struct net_device *dsa_conduit_find_user(struct net_device *dev,
						       int device, int port)
{
	struct dsa_port *cpu_dp = dev->dsa_ptr;
	struct dsa_switch_tree *dst = cpu_dp->dst;
	struct dsa_port *dp;

	list_for_each_entry(dp, &dst->ports, list)
		if (dp->ds->index == device && dp->index == port &&
		    dp->type == DSA_PORT_TYPE_USER)
			return dp->user;

	return NULL;
}

/**
 * dsa_software_untag_vlan_aware_bridge: Software untagging for VLAN-aware bridge
 * @skb: Pointer to received socket buffer (packet)
 * @br: Pointer to bridge upper interface of ingress port
 * @vid: Parsed VID from packet
 *
 * The bridge can process tagged packets. Software like STP/PTP may not. The
 * bridge can also process untagged packets, to the same effect as if they were
 * tagged with the PVID of the ingress port. So packets tagged with the PVID of
 * the bridge port must be software-untagged, to support both use cases.
 */
static inline void dsa_software_untag_vlan_aware_bridge(struct sk_buff *skb,
							struct net_device *br,
							u16 vid)
{
	u16 pvid, proto;
	int err;

	err = br_vlan_get_proto(br, &proto);
	if (err)
		return;

	err = br_vlan_get_pvid_rcu(skb->dev, &pvid);
	if (err)
		return;

	if (vid == pvid && skb->vlan_proto == htons(proto))
		__vlan_hwaccel_clear_tag(skb);
}

/**
 * dsa_software_untag_vlan_unaware_bridge: Software untagging for VLAN-unaware bridge
 * @skb: Pointer to received socket buffer (packet)
 * @br: Pointer to bridge upper interface of ingress port
 * @vid: Parsed VID from packet
 *
 * The bridge ignores all VLAN tags. Software like STP/PTP may not (it may run
 * on the plain port, or on a VLAN upper interface). Maybe packets are coming
 * to software as tagged with a driver-defined VID which is NOT equal to the
 * PVID of the bridge port (since the bridge is VLAN-unaware, its configuration
 * should NOT be committed to hardware). DSA needs a method for this private
 * VID to be communicated by software to it, and if packets are tagged with it,
 * software-untag them. Note: the private VID may be different per bridge, to
 * support the FDB isolation use case.
 *
 * FIXME: this is currently implemented based on the broken assumption that
 * the "private VID" used by the driver in VLAN-unaware mode is equal to the
 * bridge PVID. It should not be, except for a coincidence; the bridge PVID is
 * irrelevant to the data path in the VLAN-unaware mode. Thus, the VID that
 * this function removes is wrong.
 *
 * All users of ds->untag_bridge_pvid should fix their drivers, if necessary,
 * to make the two independent. Only then, if there still remains a need to
 * strip the private VID from packets, then a new ds->ops->get_private_vid()
 * API shall be introduced to communicate to DSA what this VID is, which needs
 * to be stripped here.
 */
static inline void dsa_software_untag_vlan_unaware_bridge(struct sk_buff *skb,
							  struct net_device *br,
							  u16 vid)
{
	struct net_device *upper_dev;
	u16 pvid, proto;
	int err;

	err = br_vlan_get_proto(br, &proto);
	if (err)
		return;

	err = br_vlan_get_pvid_rcu(skb->dev, &pvid);
	if (err)
		return;

	if (vid != pvid || skb->vlan_proto != htons(proto))
		return;

	/* The sad part about attempting to untag from DSA is that we
	 * don't know, unless we check, if the skb will end up in
	 * the bridge's data path - br_allowed_ingress() - or not.
	 * For example, there might be an 8021q upper for the
	 * default_pvid of the bridge, which will steal VLAN-tagged traffic
	 * from the bridge's data path. This is a configuration that DSA
	 * supports because vlan_filtering is 0. In that case, we should
	 * definitely keep the tag, to make sure it keeps working.
	 */
	upper_dev = __vlan_find_dev_deep_rcu(br, htons(proto), vid);
	if (!upper_dev)
		__vlan_hwaccel_clear_tag(skb);
}

/**
 * dsa_software_vlan_untag: Software VLAN untagging in DSA receive path
 * @skb: Pointer to socket buffer (packet)
 *
 * Receive path method for switches which send some packets as VLAN-tagged
 * towards the CPU port (generally from VLAN-aware bridge ports) even when the
 * packet was not tagged on the wire. Called when ds->untag_bridge_pvid
 * (legacy) or ds->untag_vlan_aware_bridge_pvid is set to true.
 *
 * As a side effect of this method, any VLAN tag from the skb head is moved
 * to hwaccel.
 */
static inline struct sk_buff *dsa_software_vlan_untag(struct sk_buff *skb)
{
	struct dsa_port *dp = dsa_user_to_port(skb->dev);
	struct net_device *br = dsa_port_bridge_dev_get(dp);
	u16 vid, proto;
	int err;

	/* software untagging for standalone ports not yet necessary */
	if (!br)
		return skb;

	err = br_vlan_get_proto(br, &proto);
	if (err)
		return skb;

	/* Move VLAN tag from data to hwaccel */
	if (!skb_vlan_tag_present(skb) && skb->protocol == htons(proto)) {
		skb = skb_vlan_untag(skb);
		if (!skb)
			return NULL;
	}

	if (!skb_vlan_tag_present(skb))
		return skb;

	vid = skb_vlan_tag_get_id(skb);

	if (br_vlan_enabled(br)) {
		if (dp->ds->untag_vlan_aware_bridge_pvid)
			dsa_software_untag_vlan_aware_bridge(skb, br, vid);
	} else {
		if (dp->ds->untag_bridge_pvid)
			dsa_software_untag_vlan_unaware_bridge(skb, br, vid);
	}

	return skb;
}

/* For switches without hardware support for DSA tagging to be able
 * to support termination through the bridge.
 */
static inline struct net_device *
dsa_find_designated_bridge_port_by_vid(struct net_device *conduit, u16 vid)
{
	struct dsa_port *cpu_dp = conduit->dsa_ptr;
	struct dsa_switch_tree *dst = cpu_dp->dst;
	struct bridge_vlan_info vinfo;
	struct net_device *user;
	struct dsa_port *dp;
	int err;

	list_for_each_entry(dp, &dst->ports, list) {
		if (dp->type != DSA_PORT_TYPE_USER)
			continue;

		if (!dp->bridge)
			continue;

		if (dp->stp_state != BR_STATE_LEARNING &&
		    dp->stp_state != BR_STATE_FORWARDING)
			continue;

		/* Since the bridge might learn this packet, keep the CPU port
		 * affinity with the port that will be used for the reply on
		 * xmit.
		 */
		if (dp->cpu_dp != cpu_dp)
			continue;

		user = dp->user;

		err = br_vlan_get_info_rcu(user, vid, &vinfo);
		if (err)
			continue;

		return user;
	}

	return NULL;
}

/* If the ingress port offloads the bridge, we mark the frame as autonomously
 * forwarded by hardware, so the software bridge doesn't forward in twice, back
 * to us, because we already did. However, if we're in fallback mode and we do
 * software bridging, we are not offloading it, therefore the dp->bridge
 * pointer is not populated, and flooding needs to be done by software (we are
 * effectively operating in standalone ports mode).
 */
static inline void dsa_default_offload_fwd_mark(struct sk_buff *skb)
{
	struct dsa_port *dp = dsa_user_to_port(skb->dev);

	skb->offload_fwd_mark = !!(dp->bridge);
}

/* Helper for removing DSA header tags from packets in the RX path.
 * Must not be called before skb_pull(len).
 *                                                                 skb->data
 *                                                                         |
 *                                                                         v
 * |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |
 * +-----------------------+-----------------------+---------------+-------+
 * |    Destination MAC    |      Source MAC       |  DSA header   | EType |
 * +-----------------------+-----------------------+---------------+-------+
 *                                                 |               |
 * <----- len ----->                               <----- len ----->
 *                 |
 *       >>>>>>>   v
 *       >>>>>>>   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |
 *       >>>>>>>   +-----------------------+-----------------------+-------+
 *       >>>>>>>   |    Destination MAC    |      Source MAC       | EType |
 *                 +-----------------------+-----------------------+-------+
 *                                                                         ^
 *                                                                         |
 *                                                                 skb->data
 */
static inline void dsa_strip_etype_header(struct sk_buff *skb, int len)
{
	memmove(skb->data - ETH_HLEN, skb->data - ETH_HLEN - len, 2 * ETH_ALEN);
}

/* Helper for creating space for DSA header tags in TX path packets.
 * Must not be called before skb_push(len).
 *
 * Before:
 *
 *       <<<<<<<   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |
 * ^     <<<<<<<   +-----------------------+-----------------------+-------+
 * |     <<<<<<<   |    Destination MAC    |      Source MAC       | EType |
 * |               +-----------------------+-----------------------+-------+
 * <----- len ----->
 * |
 * |
 * skb->data
 *
 * After:
 *
 * |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |
 * +-----------------------+-----------------------+---------------+-------+
 * |    Destination MAC    |      Source MAC       |  DSA header   | EType |
 * +-----------------------+-----------------------+---------------+-------+
 * ^                                               |               |
 * |                                               <----- len ----->
 * skb->data
 */
static inline void dsa_alloc_etype_header(struct sk_buff *skb, int len)
{
	memmove(skb->data, skb->data + len, 2 * ETH_ALEN);
}

/* On RX, eth_type_trans() on the DSA conduit pulls ETH_HLEN bytes starting from
 * skb_mac_header(skb), which leaves skb->data pointing at the first byte after
 * what the DSA conduit perceives as the EtherType (the beginning of the L3
 * protocol). Since DSA EtherType header taggers treat the EtherType as part of
 * the DSA tag itself, and the EtherType is 2 bytes in length, the DSA header
 * is located 2 bytes behind skb->data. Note that EtherType in this context
 * means the first 2 bytes of the DSA header, not the encapsulated EtherType
 * that will become visible after the DSA header is stripped.
 */
static inline void *dsa_etype_header_pos_rx(struct sk_buff *skb)
{
	return skb->data - 2;
}

/* On TX, skb->data points to the MAC header, which means that EtherType
 * header taggers start exactly where the EtherType is (the EtherType is
 * treated as part of the DSA header).
 */
static inline void *dsa_etype_header_pos_tx(struct sk_buff *skb)
{
	return skb->data + 2 * ETH_ALEN;
}

/* Create 2 modaliases per tagging protocol, one to auto-load the module
 * given the ID reported by get_tag_protocol(), and the other by name.
 */
#define DSA_TAG_DRIVER_ALIAS "dsa_tag:"
#define MODULE_ALIAS_DSA_TAG_DRIVER(__proto, __name) \
	MODULE_ALIAS(DSA_TAG_DRIVER_ALIAS __name); \
	MODULE_ALIAS(DSA_TAG_DRIVER_ALIAS "id-" \
		     __stringify(__proto##_VALUE))

void dsa_tag_drivers_register(struct dsa_tag_driver *dsa_tag_driver_array[],
			      unsigned int count,
			      struct module *owner);
void dsa_tag_drivers_unregister(struct dsa_tag_driver *dsa_tag_driver_array[],
				unsigned int count);

#define dsa_tag_driver_module_drivers(__dsa_tag_drivers_array, __count)	\
static int __init dsa_tag_driver_module_init(void)			\
{									\
	dsa_tag_drivers_register(__dsa_tag_drivers_array, __count,	\
				 THIS_MODULE);				\
	return 0;							\
}									\
module_init(dsa_tag_driver_module_init);				\
									\
static void __exit dsa_tag_driver_module_exit(void)			\
{									\
	dsa_tag_drivers_unregister(__dsa_tag_drivers_array, __count);	\
}									\
module_exit(dsa_tag_driver_module_exit)

/**
 * module_dsa_tag_drivers() - Helper macro for registering DSA tag
 * drivers
 * @__ops_array: Array of tag driver structures
 *
 * Helper macro for DSA tag drivers which do not do anything special
 * in module init/exit. Each module may only use this macro once, and
 * calling it replaces module_init() and module_exit().
 */
#define module_dsa_tag_drivers(__ops_array)				\
dsa_tag_driver_module_drivers(__ops_array, ARRAY_SIZE(__ops_array))

#define DSA_TAG_DRIVER_NAME(__ops) dsa_tag_driver ## _ ## __ops

/* Create a static structure we can build a linked list of dsa_tag
 * drivers
 */
#define DSA_TAG_DRIVER(__ops)						\
static struct dsa_tag_driver DSA_TAG_DRIVER_NAME(__ops) = {		\
	.ops = &__ops,							\
}

/**
 * module_dsa_tag_driver() - Helper macro for registering a single DSA tag
 * driver
 * @__ops: Single tag driver structures
 *
 * Helper macro for DSA tag drivers which do not do anything special
 * in module init/exit. Each module may only use this macro once, and
 * calling it replaces module_init() and module_exit().
 */
#define module_dsa_tag_driver(__ops)					\
DSA_TAG_DRIVER(__ops);							\
									\
static struct dsa_tag_driver *dsa_tag_driver_array[] =	{		\
	&DSA_TAG_DRIVER_NAME(__ops)					\
};									\
module_dsa_tag_drivers(dsa_tag_driver_array)

#endif
