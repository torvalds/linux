#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <linux/skbuff.h>
#include <net/switchdev.h>

#include "br_private.h"

static int br_switchdev_mark_get(struct net_bridge *br, struct net_device *dev)
{
	struct net_bridge_port *p;

	/* dev is yet to be added to the port list. */
	list_for_each_entry(p, &br->port_list, list) {
		if (switchdev_port_same_parent_id(dev, p->dev))
			return p->offload_fwd_mark;
	}

	return ++br->offload_fwd_mark;
}

int nbp_switchdev_mark_set(struct net_bridge_port *p)
{
	struct switchdev_attr attr = {
		.orig_dev = p->dev,
		.id = SWITCHDEV_ATTR_ID_PORT_PARENT_ID,
	};
	int err;

	ASSERT_RTNL();

	err = switchdev_port_attr_get(p->dev, &attr);
	if (err) {
		if (err == -EOPNOTSUPP)
			return 0;
		return err;
	}

	p->offload_fwd_mark = br_switchdev_mark_get(p->br, p->dev);

	return 0;
}

void nbp_switchdev_frame_mark(const struct net_bridge_port *p,
			      struct sk_buff *skb)
{
	if (skb->offload_fwd_mark && !WARN_ON_ONCE(!p->offload_fwd_mark))
		BR_INPUT_SKB_CB(skb)->offload_fwd_mark = p->offload_fwd_mark;
}

bool nbp_switchdev_allowed_egress(const struct net_bridge_port *p,
				  const struct sk_buff *skb)
{
	return !skb->offload_fwd_mark ||
	       BR_INPUT_SKB_CB(skb)->offload_fwd_mark != p->offload_fwd_mark;
}

/* Flags that can be offloaded to hardware */
#define BR_PORT_FLAGS_HW_OFFLOAD (BR_LEARNING | BR_FLOOD | \
				  BR_MCAST_FLOOD | BR_BCAST_FLOOD)

int br_switchdev_set_port_flag(struct net_bridge_port *p,
			       unsigned long flags,
			       unsigned long mask)
{
	struct switchdev_attr attr = {
		.orig_dev = p->dev,
		.id = SWITCHDEV_ATTR_ID_PORT_BRIDGE_FLAGS_SUPPORT,
	};
	int err;

	if (mask & ~BR_PORT_FLAGS_HW_OFFLOAD)
		return 0;

	err = switchdev_port_attr_get(p->dev, &attr);
	if (err == -EOPNOTSUPP)
		return 0;
	if (err)
		return err;

	/* Check if specific bridge flag attribute offload is supported */
	if (!(attr.u.brport_flags_support & mask)) {
		br_warn(p->br, "bridge flag offload is not supported %u(%s)\n",
			(unsigned int)p->port_no, p->dev->name);
		return -EOPNOTSUPP;
	}

	attr.id = SWITCHDEV_ATTR_ID_PORT_BRIDGE_FLAGS;
	attr.flags = SWITCHDEV_F_DEFER;
	attr.u.brport_flags = flags;
	err = switchdev_port_attr_set(p->dev, &attr);
	if (err) {
		br_warn(p->br, "error setting offload flag on port %u(%s)\n",
			(unsigned int)p->port_no, p->dev->name);
		return err;
	}

	return 0;
}
