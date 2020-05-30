// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/mrp_bridge.h>
#include "br_private_mrp.h"

static const u8 mrp_test_dmac[ETH_ALEN] = { 0x1, 0x15, 0x4e, 0x0, 0x0, 0x1 };

static struct net_bridge_port *br_mrp_get_port(struct net_bridge *br,
					       u32 ifindex)
{
	struct net_bridge_port *res = NULL;
	struct net_bridge_port *port;

	list_for_each_entry(port, &br->port_list, list) {
		if (port->dev->ifindex == ifindex) {
			res = port;
			break;
		}
	}

	return res;
}

static struct br_mrp *br_mrp_find_id(struct net_bridge *br, u32 ring_id)
{
	struct br_mrp *res = NULL;
	struct br_mrp *mrp;

	list_for_each_entry_rcu(mrp, &br->mrp_list, list,
				lockdep_rtnl_is_held()) {
		if (mrp->ring_id == ring_id) {
			res = mrp;
			break;
		}
	}

	return res;
}

static bool br_mrp_unique_ifindex(struct net_bridge *br, u32 ifindex)
{
	struct br_mrp *mrp;

	list_for_each_entry_rcu(mrp, &br->mrp_list, list,
				lockdep_rtnl_is_held()) {
		struct net_bridge_port *p;

		p = rtnl_dereference(mrp->p_port);
		if (p && p->dev->ifindex == ifindex)
			return false;

		p = rtnl_dereference(mrp->s_port);
		if (p && p->dev->ifindex == ifindex)
			return false;
	}

	return true;
}

static struct br_mrp *br_mrp_find_port(struct net_bridge *br,
				       struct net_bridge_port *p)
{
	struct br_mrp *res = NULL;
	struct br_mrp *mrp;

	list_for_each_entry_rcu(mrp, &br->mrp_list, list,
				lockdep_rtnl_is_held()) {
		if (rcu_access_pointer(mrp->p_port) == p ||
		    rcu_access_pointer(mrp->s_port) == p) {
			res = mrp;
			break;
		}
	}

	return res;
}

static int br_mrp_next_seq(struct br_mrp *mrp)
{
	mrp->seq_id++;
	return mrp->seq_id;
}

static struct sk_buff *br_mrp_skb_alloc(struct net_bridge_port *p,
					const u8 *src, const u8 *dst)
{
	struct ethhdr *eth_hdr;
	struct sk_buff *skb;
	u16 *version;

	skb = dev_alloc_skb(MRP_MAX_FRAME_LENGTH);
	if (!skb)
		return NULL;

	skb->dev = p->dev;
	skb->protocol = htons(ETH_P_MRP);
	skb->priority = MRP_FRAME_PRIO;
	skb_reserve(skb, sizeof(*eth_hdr));

	eth_hdr = skb_push(skb, sizeof(*eth_hdr));
	ether_addr_copy(eth_hdr->h_dest, dst);
	ether_addr_copy(eth_hdr->h_source, src);
	eth_hdr->h_proto = htons(ETH_P_MRP);

	version = skb_put(skb, sizeof(*version));
	*version = cpu_to_be16(MRP_VERSION);

	return skb;
}

static void br_mrp_skb_tlv(struct sk_buff *skb,
			   enum br_mrp_tlv_header_type type,
			   u8 length)
{
	struct br_mrp_tlv_hdr *hdr;

	hdr = skb_put(skb, sizeof(*hdr));
	hdr->type = type;
	hdr->length = length;
}

static void br_mrp_skb_common(struct sk_buff *skb, struct br_mrp *mrp)
{
	struct br_mrp_common_hdr *hdr;

	br_mrp_skb_tlv(skb, BR_MRP_TLV_HEADER_COMMON, sizeof(*hdr));

	hdr = skb_put(skb, sizeof(*hdr));
	hdr->seq_id = cpu_to_be16(br_mrp_next_seq(mrp));
	memset(hdr->domain, 0xff, MRP_DOMAIN_UUID_LENGTH);
}

static struct sk_buff *br_mrp_alloc_test_skb(struct br_mrp *mrp,
					     struct net_bridge_port *p,
					     enum br_mrp_port_role_type port_role)
{
	struct br_mrp_ring_test_hdr *hdr = NULL;
	struct sk_buff *skb = NULL;

	if (!p)
		return NULL;

	skb = br_mrp_skb_alloc(p, p->dev->dev_addr, mrp_test_dmac);
	if (!skb)
		return NULL;

	br_mrp_skb_tlv(skb, BR_MRP_TLV_HEADER_RING_TEST, sizeof(*hdr));
	hdr = skb_put(skb, sizeof(*hdr));

	hdr->prio = cpu_to_be16(mrp->prio);
	ether_addr_copy(hdr->sa, p->br->dev->dev_addr);
	hdr->port_role = cpu_to_be16(port_role);
	hdr->state = cpu_to_be16(mrp->ring_state);
	hdr->transitions = cpu_to_be16(mrp->ring_transitions);
	hdr->timestamp = cpu_to_be32(jiffies_to_msecs(jiffies));

	br_mrp_skb_common(skb, mrp);
	br_mrp_skb_tlv(skb, BR_MRP_TLV_HEADER_END, 0x0);

	return skb;
}

/* This function is continuously called in the following cases:
 * - when node role is MRM, in this case test_monitor is always set to false
 *   because it needs to notify the userspace that the ring is open and needs to
 *   send MRP_Test frames
 * - when node role is MRA, there are 2 subcases:
 *     - when MRA behaves as MRM, in this case is similar with MRM role
 *     - when MRA behaves as MRC, in this case test_monitor is set to true,
 *       because it needs to detect when it stops seeing MRP_Test frames
 *       from MRM node but it doesn't need to send MRP_Test frames.
 */
static void br_mrp_test_work_expired(struct work_struct *work)
{
	struct delayed_work *del_work = to_delayed_work(work);
	struct br_mrp *mrp = container_of(del_work, struct br_mrp, test_work);
	struct net_bridge_port *p;
	bool notify_open = false;
	struct sk_buff *skb;

	if (time_before_eq(mrp->test_end, jiffies))
		return;

	if (mrp->test_count_miss < mrp->test_max_miss) {
		mrp->test_count_miss++;
	} else {
		/* Notify that the ring is open only if the ring state is
		 * closed, otherwise it would continue to notify at every
		 * interval.
		 * Also notify that the ring is open when the node has the
		 * role MRA and behaves as MRC. The reason is that the
		 * userspace needs to know when the MRM stopped sending
		 * MRP_Test frames so that the current node to try to take
		 * the role of a MRM.
		 */
		if (mrp->ring_state == BR_MRP_RING_STATE_CLOSED ||
		    mrp->test_monitor)
			notify_open = true;
	}

	rcu_read_lock();

	p = rcu_dereference(mrp->p_port);
	if (p) {
		if (!mrp->test_monitor) {
			skb = br_mrp_alloc_test_skb(mrp, p,
						    BR_MRP_PORT_ROLE_PRIMARY);
			if (!skb)
				goto out;

			skb_reset_network_header(skb);
			dev_queue_xmit(skb);
		}

		if (notify_open && !mrp->ring_role_offloaded)
			br_mrp_port_open(p->dev, true);
	}

	p = rcu_dereference(mrp->s_port);
	if (p) {
		if (!mrp->test_monitor) {
			skb = br_mrp_alloc_test_skb(mrp, p,
						    BR_MRP_PORT_ROLE_SECONDARY);
			if (!skb)
				goto out;

			skb_reset_network_header(skb);
			dev_queue_xmit(skb);
		}

		if (notify_open && !mrp->ring_role_offloaded)
			br_mrp_port_open(p->dev, true);
	}

out:
	rcu_read_unlock();

	queue_delayed_work(system_wq, &mrp->test_work,
			   usecs_to_jiffies(mrp->test_interval));
}

/* Deletes the MRP instance.
 * note: called under rtnl_lock
 */
static void br_mrp_del_impl(struct net_bridge *br, struct br_mrp *mrp)
{
	struct net_bridge_port *p;
	u8 state;

	/* Stop sending MRP_Test frames */
	cancel_delayed_work_sync(&mrp->test_work);
	br_mrp_switchdev_send_ring_test(br, mrp, 0, 0, 0, 0);

	br_mrp_switchdev_del(br, mrp);

	/* Reset the ports */
	p = rtnl_dereference(mrp->p_port);
	if (p) {
		spin_lock_bh(&br->lock);
		state = netif_running(br->dev) ?
				BR_STATE_FORWARDING : BR_STATE_DISABLED;
		p->state = state;
		p->flags &= ~BR_MRP_AWARE;
		spin_unlock_bh(&br->lock);
		br_mrp_port_switchdev_set_state(p, state);
		rcu_assign_pointer(mrp->p_port, NULL);
	}

	p = rtnl_dereference(mrp->s_port);
	if (p) {
		spin_lock_bh(&br->lock);
		state = netif_running(br->dev) ?
				BR_STATE_FORWARDING : BR_STATE_DISABLED;
		p->state = state;
		p->flags &= ~BR_MRP_AWARE;
		spin_unlock_bh(&br->lock);
		br_mrp_port_switchdev_set_state(p, state);
		rcu_assign_pointer(mrp->s_port, NULL);
	}

	list_del_rcu(&mrp->list);
	kfree_rcu(mrp, rcu);
}

/* Adds a new MRP instance.
 * note: called under rtnl_lock
 */
int br_mrp_add(struct net_bridge *br, struct br_mrp_instance *instance)
{
	struct net_bridge_port *p;
	struct br_mrp *mrp;
	int err;

	/* If the ring exists, it is not possible to create another one with the
	 * same ring_id
	 */
	mrp = br_mrp_find_id(br, instance->ring_id);
	if (mrp)
		return -EINVAL;

	if (!br_mrp_get_port(br, instance->p_ifindex) ||
	    !br_mrp_get_port(br, instance->s_ifindex))
		return -EINVAL;

	/* It is not possible to have the same port part of multiple rings */
	if (!br_mrp_unique_ifindex(br, instance->p_ifindex) ||
	    !br_mrp_unique_ifindex(br, instance->s_ifindex))
		return -EINVAL;

	mrp = kzalloc(sizeof(*mrp), GFP_KERNEL);
	if (!mrp)
		return -ENOMEM;

	mrp->ring_id = instance->ring_id;
	mrp->prio = instance->prio;

	p = br_mrp_get_port(br, instance->p_ifindex);
	spin_lock_bh(&br->lock);
	p->state = BR_STATE_FORWARDING;
	p->flags |= BR_MRP_AWARE;
	spin_unlock_bh(&br->lock);
	rcu_assign_pointer(mrp->p_port, p);

	p = br_mrp_get_port(br, instance->s_ifindex);
	spin_lock_bh(&br->lock);
	p->state = BR_STATE_FORWARDING;
	p->flags |= BR_MRP_AWARE;
	spin_unlock_bh(&br->lock);
	rcu_assign_pointer(mrp->s_port, p);

	INIT_DELAYED_WORK(&mrp->test_work, br_mrp_test_work_expired);
	list_add_tail_rcu(&mrp->list, &br->mrp_list);

	err = br_mrp_switchdev_add(br, mrp);
	if (err)
		goto delete_mrp;

	return 0;

delete_mrp:
	br_mrp_del_impl(br, mrp);

	return err;
}

/* Deletes the MRP instance from which the port is part of
 * note: called under rtnl_lock
 */
void br_mrp_port_del(struct net_bridge *br, struct net_bridge_port *p)
{
	struct br_mrp *mrp = br_mrp_find_port(br, p);

	/* If the port is not part of a MRP instance just bail out */
	if (!mrp)
		return;

	br_mrp_del_impl(br, mrp);
}

/* Deletes existing MRP instance based on ring_id
 * note: called under rtnl_lock
 */
int br_mrp_del(struct net_bridge *br, struct br_mrp_instance *instance)
{
	struct br_mrp *mrp = br_mrp_find_id(br, instance->ring_id);

	if (!mrp)
		return -EINVAL;

	br_mrp_del_impl(br, mrp);

	return 0;
}

/* Set port state, port state can be forwarding, blocked or disabled
 * note: already called with rtnl_lock
 */
int br_mrp_set_port_state(struct net_bridge_port *p,
			  enum br_mrp_port_state_type state)
{
	if (!p || !(p->flags & BR_MRP_AWARE))
		return -EINVAL;

	spin_lock_bh(&p->br->lock);

	if (state == BR_MRP_PORT_STATE_FORWARDING)
		p->state = BR_STATE_FORWARDING;
	else
		p->state = BR_STATE_BLOCKING;

	spin_unlock_bh(&p->br->lock);

	br_mrp_port_switchdev_set_state(p, state);

	return 0;
}

/* Set port role, port role can be primary or secondary
 * note: already called with rtnl_lock
 */
int br_mrp_set_port_role(struct net_bridge_port *p,
			 enum br_mrp_port_role_type role)
{
	struct br_mrp *mrp;

	if (!p || !(p->flags & BR_MRP_AWARE))
		return -EINVAL;

	mrp = br_mrp_find_port(p->br, p);

	if (!mrp)
		return -EINVAL;

	if (role == BR_MRP_PORT_ROLE_PRIMARY)
		rcu_assign_pointer(mrp->p_port, p);
	else
		rcu_assign_pointer(mrp->s_port, p);

	br_mrp_port_switchdev_set_role(p, role);

	return 0;
}

/* Set ring state, ring state can be only Open or Closed
 * note: already called with rtnl_lock
 */
int br_mrp_set_ring_state(struct net_bridge *br,
			  struct br_mrp_ring_state *state)
{
	struct br_mrp *mrp = br_mrp_find_id(br, state->ring_id);

	if (!mrp)
		return -EINVAL;

	if (mrp->ring_state == BR_MRP_RING_STATE_CLOSED &&
	    state->ring_state != BR_MRP_RING_STATE_CLOSED)
		mrp->ring_transitions++;

	mrp->ring_state = state->ring_state;

	br_mrp_switchdev_set_ring_state(br, mrp, state->ring_state);

	return 0;
}

/* Set ring role, ring role can be only MRM(Media Redundancy Manager) or
 * MRC(Media Redundancy Client).
 * note: already called with rtnl_lock
 */
int br_mrp_set_ring_role(struct net_bridge *br,
			 struct br_mrp_ring_role *role)
{
	struct br_mrp *mrp = br_mrp_find_id(br, role->ring_id);
	int err;

	if (!mrp)
		return -EINVAL;

	mrp->ring_role = role->ring_role;

	/* If there is an error just bailed out */
	err = br_mrp_switchdev_set_ring_role(br, mrp, role->ring_role);
	if (err && err != -EOPNOTSUPP)
		return err;

	/* Now detect if the HW actually applied the role or not. If the HW
	 * applied the role it means that the SW will not to do those operations
	 * anymore. For example if the role ir MRM then the HW will notify the
	 * SW when ring is open, but if the is not pushed to the HW the SW will
	 * need to detect when the ring is open
	 */
	mrp->ring_role_offloaded = err == -EOPNOTSUPP ? 0 : 1;

	return 0;
}

/* Start to generate or monitor MRP test frames, the frames are generated by
 * HW and if it fails, they are generated by the SW.
 * note: already called with rtnl_lock
 */
int br_mrp_start_test(struct net_bridge *br,
		      struct br_mrp_start_test *test)
{
	struct br_mrp *mrp = br_mrp_find_id(br, test->ring_id);

	if (!mrp)
		return -EINVAL;

	/* Try to push it to the HW and if it fails then continue with SW
	 * implementation and if that also fails then return error.
	 */
	if (!br_mrp_switchdev_send_ring_test(br, mrp, test->interval,
					     test->max_miss, test->period,
					     test->monitor))
		return 0;

	mrp->test_interval = test->interval;
	mrp->test_end = jiffies + usecs_to_jiffies(test->period);
	mrp->test_max_miss = test->max_miss;
	mrp->test_monitor = test->monitor;
	mrp->test_count_miss = 0;
	queue_delayed_work(system_wq, &mrp->test_work,
			   usecs_to_jiffies(test->interval));

	return 0;
}

/* Process only MRP Test frame. All the other MRP frames are processed by
 * userspace application
 * note: already called with rcu_read_lock
 */
static void br_mrp_mrm_process(struct br_mrp *mrp, struct net_bridge_port *port,
			       struct sk_buff *skb)
{
	const struct br_mrp_tlv_hdr *hdr;
	struct br_mrp_tlv_hdr _hdr;

	/* Each MRP header starts with a version field which is 16 bits.
	 * Therefore skip the version and get directly the TLV header.
	 */
	hdr = skb_header_pointer(skb, sizeof(uint16_t), sizeof(_hdr), &_hdr);
	if (!hdr)
		return;

	if (hdr->type != BR_MRP_TLV_HEADER_RING_TEST)
		return;

	mrp->test_count_miss = 0;

	/* Notify the userspace that the ring is closed only when the ring is
	 * not closed
	 */
	if (mrp->ring_state != BR_MRP_RING_STATE_CLOSED)
		br_mrp_port_open(port->dev, false);
}

/* Determin if the test hdr has a better priority than the node */
static bool br_mrp_test_better_than_own(struct br_mrp *mrp,
					struct net_bridge *br,
					const struct br_mrp_ring_test_hdr *hdr)
{
	u16 prio = be16_to_cpu(hdr->prio);

	if (prio < mrp->prio ||
	    (prio == mrp->prio &&
	    ether_addr_to_u64(hdr->sa) < ether_addr_to_u64(br->dev->dev_addr)))
		return true;

	return false;
}

/* Process only MRP Test frame. All the other MRP frames are processed by
 * userspace application
 * note: already called with rcu_read_lock
 */
static void br_mrp_mra_process(struct br_mrp *mrp, struct net_bridge *br,
			       struct net_bridge_port *port,
			       struct sk_buff *skb)
{
	const struct br_mrp_ring_test_hdr *test_hdr;
	struct br_mrp_ring_test_hdr _test_hdr;
	const struct br_mrp_tlv_hdr *hdr;
	struct br_mrp_tlv_hdr _hdr;

	/* Each MRP header starts with a version field which is 16 bits.
	 * Therefore skip the version and get directly the TLV header.
	 */
	hdr = skb_header_pointer(skb, sizeof(uint16_t), sizeof(_hdr), &_hdr);
	if (!hdr)
		return;

	if (hdr->type != BR_MRP_TLV_HEADER_RING_TEST)
		return;

	test_hdr = skb_header_pointer(skb, sizeof(uint16_t) + sizeof(_hdr),
				      sizeof(_test_hdr), &_test_hdr);
	if (!test_hdr)
		return;

	/* Only frames that have a better priority than the node will
	 * clear the miss counter because otherwise the node will need to behave
	 * as MRM.
	 */
	if (br_mrp_test_better_than_own(mrp, br, test_hdr))
		mrp->test_count_miss = 0;
}

/* This will just forward the frame to the other mrp ring port(MRC role) or will
 * not do anything.
 * note: already called with rcu_read_lock
 */
static int br_mrp_rcv(struct net_bridge_port *p,
		      struct sk_buff *skb, struct net_device *dev)
{
	struct net_device *s_dev, *p_dev, *d_dev;
	struct net_bridge_port *p_port, *s_port;
	struct net_bridge *br;
	struct sk_buff *nskb;
	struct br_mrp *mrp;

	/* If port is disabled don't accept any frames */
	if (p->state == BR_STATE_DISABLED)
		return 0;

	br = p->br;
	mrp =  br_mrp_find_port(br, p);
	if (unlikely(!mrp))
		return 0;

	p_port = rcu_dereference(mrp->p_port);
	if (!p_port)
		return 0;

	s_port = rcu_dereference(mrp->s_port);
	if (!s_port)
		return 0;

	/* If the role is MRM then don't forward the frames */
	if (mrp->ring_role == BR_MRP_RING_ROLE_MRM) {
		br_mrp_mrm_process(mrp, p, skb);
		return 1;
	}

	/* If the role is MRA then don't forward the frames if it behaves as
	 * MRM node
	 */
	if (mrp->ring_role == BR_MRP_RING_ROLE_MRA) {
		if (!mrp->test_monitor) {
			br_mrp_mrm_process(mrp, p, skb);
			return 1;
		}

		br_mrp_mra_process(mrp, br, p, skb);
	}

	/* Clone the frame and forward it on the other MRP port */
	nskb = skb_clone(skb, GFP_ATOMIC);
	if (!nskb)
		return 0;

	p_dev = p_port->dev;
	s_dev = s_port->dev;

	if (p_dev == dev)
		d_dev = s_dev;
	else
		d_dev = p_dev;

	nskb->dev = d_dev;
	skb_push(nskb, ETH_HLEN);
	dev_queue_xmit(nskb);

	return 1;
}

/* Check if the frame was received on a port that is part of MRP ring
 * and if the frame has MRP eth. In that case process the frame otherwise do
 * normal forwarding.
 * note: already called with rcu_read_lock
 */
int br_mrp_process(struct net_bridge_port *p, struct sk_buff *skb)
{
	/* If there is no MRP instance do normal forwarding */
	if (likely(!(p->flags & BR_MRP_AWARE)))
		goto out;

	if (unlikely(skb->protocol == htons(ETH_P_MRP)))
		return br_mrp_rcv(p, skb, p->dev);

out:
	return 0;
}

bool br_mrp_enabled(struct net_bridge *br)
{
	return !list_empty(&br->mrp_list);
}
