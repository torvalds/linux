// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/cfm_bridge.h>
#include <uapi/linux/cfm_bridge.h>
#include "br_private_cfm.h"

static struct br_cfm_mep *br_mep_find(struct net_bridge *br, u32 instance)
{
	struct br_cfm_mep *mep;

	hlist_for_each_entry(mep, &br->mep_list, head)
		if (mep->instance == instance)
			return mep;

	return NULL;
}

static struct br_cfm_mep *br_mep_find_ifindex(struct net_bridge *br,
					      u32 ifindex)
{
	struct br_cfm_mep *mep;

	hlist_for_each_entry_rcu(mep, &br->mep_list, head,
				 lockdep_rtnl_is_held())
		if (mep->create.ifindex == ifindex)
			return mep;

	return NULL;
}

static struct br_cfm_peer_mep *br_peer_mep_find(struct br_cfm_mep *mep,
						u32 mepid)
{
	struct br_cfm_peer_mep *peer_mep;

	hlist_for_each_entry_rcu(peer_mep, &mep->peer_mep_list, head,
				 lockdep_rtnl_is_held())
		if (peer_mep->mepid == mepid)
			return peer_mep;

	return NULL;
}

static struct net_bridge_port *br_mep_get_port(struct net_bridge *br,
					       u32 ifindex)
{
	struct net_bridge_port *port;

	list_for_each_entry(port, &br->port_list, list)
		if (port->dev->ifindex == ifindex)
			return port;

	return NULL;
}

/* Calculate the CCM interval in us. */
static u32 interval_to_us(enum br_cfm_ccm_interval interval)
{
	switch (interval) {
	case BR_CFM_CCM_INTERVAL_NONE:
		return 0;
	case BR_CFM_CCM_INTERVAL_3_3_MS:
		return 3300;
	case BR_CFM_CCM_INTERVAL_10_MS:
		return 10 * 1000;
	case BR_CFM_CCM_INTERVAL_100_MS:
		return 100 * 1000;
	case BR_CFM_CCM_INTERVAL_1_SEC:
		return 1000 * 1000;
	case BR_CFM_CCM_INTERVAL_10_SEC:
		return 10 * 1000 * 1000;
	case BR_CFM_CCM_INTERVAL_1_MIN:
		return 60 * 1000 * 1000;
	case BR_CFM_CCM_INTERVAL_10_MIN:
		return 10 * 60 * 1000 * 1000;
	}
	return 0;
}

/* Convert the interface interval to CCM PDU value. */
static u32 interval_to_pdu(enum br_cfm_ccm_interval interval)
{
	switch (interval) {
	case BR_CFM_CCM_INTERVAL_NONE:
		return 0;
	case BR_CFM_CCM_INTERVAL_3_3_MS:
		return 1;
	case BR_CFM_CCM_INTERVAL_10_MS:
		return 2;
	case BR_CFM_CCM_INTERVAL_100_MS:
		return 3;
	case BR_CFM_CCM_INTERVAL_1_SEC:
		return 4;
	case BR_CFM_CCM_INTERVAL_10_SEC:
		return 5;
	case BR_CFM_CCM_INTERVAL_1_MIN:
		return 6;
	case BR_CFM_CCM_INTERVAL_10_MIN:
		return 7;
	}
	return 0;
}

/* Convert the CCM PDU value to interval on interface. */
static u32 pdu_to_interval(u32 value)
{
	switch (value) {
	case 0:
		return BR_CFM_CCM_INTERVAL_NONE;
	case 1:
		return BR_CFM_CCM_INTERVAL_3_3_MS;
	case 2:
		return BR_CFM_CCM_INTERVAL_10_MS;
	case 3:
		return BR_CFM_CCM_INTERVAL_100_MS;
	case 4:
		return BR_CFM_CCM_INTERVAL_1_SEC;
	case 5:
		return BR_CFM_CCM_INTERVAL_10_SEC;
	case 6:
		return BR_CFM_CCM_INTERVAL_1_MIN;
	case 7:
		return BR_CFM_CCM_INTERVAL_10_MIN;
	}
	return BR_CFM_CCM_INTERVAL_NONE;
}

static void ccm_rx_timer_start(struct br_cfm_peer_mep *peer_mep)
{
	u32 interval_us;

	interval_us = interval_to_us(peer_mep->mep->cc_config.exp_interval);
	/* Function ccm_rx_dwork must be called with 1/4
	 * of the configured CC 'expected_interval'
	 * in order to detect CCM defect after 3.25 interval.
	 */
	queue_delayed_work(system_wq, &peer_mep->ccm_rx_dwork,
			   usecs_to_jiffies(interval_us / 4));
}

static void br_cfm_notify(int event, const struct net_bridge_port *port)
{
	u32 filter = RTEXT_FILTER_CFM_STATUS;

	return br_info_notify(event, port->br, NULL, filter);
}

static void cc_peer_enable(struct br_cfm_peer_mep *peer_mep)
{
	memset(&peer_mep->cc_status, 0, sizeof(peer_mep->cc_status));
	peer_mep->ccm_rx_count_miss = 0;

	ccm_rx_timer_start(peer_mep);
}

static void cc_peer_disable(struct br_cfm_peer_mep *peer_mep)
{
	cancel_delayed_work_sync(&peer_mep->ccm_rx_dwork);
}

static struct sk_buff *ccm_frame_build(struct br_cfm_mep *mep,
				       const struct br_cfm_cc_ccm_tx_info *const tx_info)

{
	struct br_cfm_common_hdr *common_hdr;
	struct net_bridge_port *b_port;
	struct br_cfm_maid *maid;
	u8 *itu_reserved, *e_tlv;
	struct ethhdr *eth_hdr;
	struct sk_buff *skb;
	__be32 *status_tlv;
	__be32 *snumber;
	__be16 *mepid;

	skb = dev_alloc_skb(CFM_CCM_MAX_FRAME_LENGTH);
	if (!skb)
		return NULL;

	rcu_read_lock();
	b_port = rcu_dereference(mep->b_port);
	if (!b_port) {
		kfree_skb(skb);
		rcu_read_unlock();
		return NULL;
	}
	skb->dev = b_port->dev;
	rcu_read_unlock();
	/* The device cannot be deleted until the work_queue functions has
	 * completed. This function is called from ccm_tx_work_expired()
	 * that is a work_queue functions.
	 */

	skb->protocol = htons(ETH_P_CFM);
	skb->priority = CFM_FRAME_PRIO;

	/* Ethernet header */
	eth_hdr = skb_put(skb, sizeof(*eth_hdr));
	ether_addr_copy(eth_hdr->h_dest, tx_info->dmac.addr);
	ether_addr_copy(eth_hdr->h_source, mep->config.unicast_mac.addr);
	eth_hdr->h_proto = htons(ETH_P_CFM);

	/* Common CFM Header */
	common_hdr = skb_put(skb, sizeof(*common_hdr));
	common_hdr->mdlevel_version = mep->config.mdlevel << 5;
	common_hdr->opcode = BR_CFM_OPCODE_CCM;
	common_hdr->flags = (mep->rdi << 7) |
			    interval_to_pdu(mep->cc_config.exp_interval);
	common_hdr->tlv_offset = CFM_CCM_TLV_OFFSET;

	/* Sequence number */
	snumber = skb_put(skb, sizeof(*snumber));
	if (tx_info->seq_no_update) {
		*snumber = cpu_to_be32(mep->ccm_tx_snumber);
		mep->ccm_tx_snumber += 1;
	} else {
		*snumber = 0;
	}

	mepid = skb_put(skb, sizeof(*mepid));
	*mepid = cpu_to_be16((u16)mep->config.mepid);

	maid = skb_put(skb, sizeof(*maid));
	memcpy(maid->data, mep->cc_config.exp_maid.data, sizeof(maid->data));

	/* ITU reserved (CFM_CCM_ITU_RESERVED_SIZE octets) */
	itu_reserved = skb_put(skb, CFM_CCM_ITU_RESERVED_SIZE);
	memset(itu_reserved, 0, CFM_CCM_ITU_RESERVED_SIZE);

	/* Generel CFM TLV format:
	 * TLV type:		one byte
	 * TLV value length:	two bytes
	 * TLV value:		'TLV value length' bytes
	 */

	/* Port status TLV. The value length is 1. Total of 4 bytes. */
	if (tx_info->port_tlv) {
		status_tlv = skb_put(skb, sizeof(*status_tlv));
		*status_tlv = cpu_to_be32((CFM_PORT_STATUS_TLV_TYPE << 24) |
					  (1 << 8) |	/* Value length */
					  (tx_info->port_tlv_value & 0xFF));
	}

	/* Interface status TLV. The value length is 1. Total of 4 bytes. */
	if (tx_info->if_tlv) {
		status_tlv = skb_put(skb, sizeof(*status_tlv));
		*status_tlv = cpu_to_be32((CFM_IF_STATUS_TLV_TYPE << 24) |
					  (1 << 8) |	/* Value length */
					  (tx_info->if_tlv_value & 0xFF));
	}

	/* End TLV */
	e_tlv = skb_put(skb, sizeof(*e_tlv));
	*e_tlv = CFM_ENDE_TLV_TYPE;

	return skb;
}

static void ccm_frame_tx(struct sk_buff *skb)
{
	skb_reset_network_header(skb);
	dev_queue_xmit(skb);
}

/* This function is called with the configured CC 'expected_interval'
 * in order to drive CCM transmission when enabled.
 */
static void ccm_tx_work_expired(struct work_struct *work)
{
	struct delayed_work *del_work;
	struct br_cfm_mep *mep;
	struct sk_buff *skb;
	u32 interval_us;

	del_work = to_delayed_work(work);
	mep = container_of(del_work, struct br_cfm_mep, ccm_tx_dwork);

	if (time_before_eq(mep->ccm_tx_end, jiffies)) {
		/* Transmission period has ended */
		mep->cc_ccm_tx_info.period = 0;
		return;
	}

	skb = ccm_frame_build(mep, &mep->cc_ccm_tx_info);
	if (skb)
		ccm_frame_tx(skb);

	interval_us = interval_to_us(mep->cc_config.exp_interval);
	queue_delayed_work(system_wq, &mep->ccm_tx_dwork,
			   usecs_to_jiffies(interval_us));
}

/* This function is called with 1/4 of the configured CC 'expected_interval'
 * in order to detect CCM defect after 3.25 interval.
 */
static void ccm_rx_work_expired(struct work_struct *work)
{
	struct br_cfm_peer_mep *peer_mep;
	struct net_bridge_port *b_port;
	struct delayed_work *del_work;

	del_work = to_delayed_work(work);
	peer_mep = container_of(del_work, struct br_cfm_peer_mep, ccm_rx_dwork);

	/* After 13 counts (4 * 3,25) then 3.25 intervals are expired */
	if (peer_mep->ccm_rx_count_miss < 13) {
		/* 3.25 intervals are NOT expired without CCM reception */
		peer_mep->ccm_rx_count_miss++;

		/* Start timer again */
		ccm_rx_timer_start(peer_mep);
	} else {
		/* 3.25 intervals are expired without CCM reception.
		 * CCM defect detected
		 */
		peer_mep->cc_status.ccm_defect = true;

		/* Change in CCM defect status - notify */
		rcu_read_lock();
		b_port = rcu_dereference(peer_mep->mep->b_port);
		if (b_port)
			br_cfm_notify(RTM_NEWLINK, b_port);
		rcu_read_unlock();
	}
}

static u32 ccm_tlv_extract(struct sk_buff *skb, u32 index,
			   struct br_cfm_peer_mep *peer_mep)
{
	__be32 *s_tlv;
	__be32 _s_tlv;
	u32 h_s_tlv;
	u8 *e_tlv;
	u8 _e_tlv;

	e_tlv = skb_header_pointer(skb, index, sizeof(_e_tlv), &_e_tlv);
	if (!e_tlv)
		return 0;

	/* TLV is present - get the status TLV */
	s_tlv = skb_header_pointer(skb,
				   index,
				   sizeof(_s_tlv), &_s_tlv);
	if (!s_tlv)
		return 0;

	h_s_tlv = ntohl(*s_tlv);
	if ((h_s_tlv >> 24) == CFM_IF_STATUS_TLV_TYPE) {
		/* Interface status TLV */
		peer_mep->cc_status.tlv_seen = true;
		peer_mep->cc_status.if_tlv_value = (h_s_tlv & 0xFF);
	}

	if ((h_s_tlv >> 24) == CFM_PORT_STATUS_TLV_TYPE) {
		/* Port status TLV */
		peer_mep->cc_status.tlv_seen = true;
		peer_mep->cc_status.port_tlv_value = (h_s_tlv & 0xFF);
	}

	/* The Sender ID TLV is not handled */
	/* The Organization-Specific TLV is not handled */

	/* Return the length of this tlv.
	 * This is the length of the value field plus 3 bytes for size of type
	 * field and length field
	 */
	return ((h_s_tlv >> 8) & 0xFFFF) + 3;
}

/* note: already called with rcu_read_lock */
static int br_cfm_frame_rx(struct net_bridge_port *port, struct sk_buff *skb)
{
	u32 mdlevel, interval, size, index, max;
	const struct br_cfm_common_hdr *hdr;
	struct br_cfm_peer_mep *peer_mep;
	const struct br_cfm_maid *maid;
	struct br_cfm_common_hdr _hdr;
	struct br_cfm_maid _maid;
	struct br_cfm_mep *mep;
	struct net_bridge *br;
	__be32 *snumber;
	__be32 _snumber;
	__be16 *mepid;
	__be16 _mepid;

	if (port->state == BR_STATE_DISABLED)
		return 0;

	hdr = skb_header_pointer(skb, 0, sizeof(_hdr), &_hdr);
	if (!hdr)
		return 1;

	br = port->br;
	mep = br_mep_find_ifindex(br, port->dev->ifindex);
	if (unlikely(!mep))
		/* No MEP on this port - must be forwarded */
		return 0;

	mdlevel = hdr->mdlevel_version >> 5;
	if (mdlevel > mep->config.mdlevel)
		/* The level is above this MEP level - must be forwarded */
		return 0;

	if ((hdr->mdlevel_version & 0x1F) != 0) {
		/* Invalid version */
		mep->status.version_unexp_seen = true;
		return 1;
	}

	if (mdlevel < mep->config.mdlevel) {
		/* The level is below this MEP level */
		mep->status.rx_level_low_seen = true;
		return 1;
	}

	if (hdr->opcode == BR_CFM_OPCODE_CCM) {
		/* CCM PDU received. */
		/* MA ID is after common header + sequence number + MEP ID */
		maid = skb_header_pointer(skb,
					  CFM_CCM_PDU_MAID_OFFSET,
					  sizeof(_maid), &_maid);
		if (!maid)
			return 1;
		if (memcmp(maid->data, mep->cc_config.exp_maid.data,
			   sizeof(maid->data)))
			/* MA ID not as expected */
			return 1;

		/* MEP ID is after common header + sequence number */
		mepid = skb_header_pointer(skb,
					   CFM_CCM_PDU_MEPID_OFFSET,
					   sizeof(_mepid), &_mepid);
		if (!mepid)
			return 1;
		peer_mep = br_peer_mep_find(mep, (u32)ntohs(*mepid));
		if (!peer_mep)
			return 1;

		/* Interval is in common header flags */
		interval = hdr->flags & 0x07;
		if (mep->cc_config.exp_interval != pdu_to_interval(interval))
			/* Interval not as expected */
			return 1;

		/* A valid CCM frame is received */
		if (peer_mep->cc_status.ccm_defect) {
			peer_mep->cc_status.ccm_defect = false;

			/* Change in CCM defect status - notify */
			br_cfm_notify(RTM_NEWLINK, port);

			/* Start CCM RX timer */
			ccm_rx_timer_start(peer_mep);
		}

		peer_mep->cc_status.seen = true;
		peer_mep->ccm_rx_count_miss = 0;

		/* RDI is in common header flags */
		peer_mep->cc_status.rdi = (hdr->flags & 0x80) ? true : false;

		/* Sequence number is after common header */
		snumber = skb_header_pointer(skb,
					     CFM_CCM_PDU_SEQNR_OFFSET,
					     sizeof(_snumber), &_snumber);
		if (!snumber)
			return 1;
		if (ntohl(*snumber) != (mep->ccm_rx_snumber + 1))
			/* Unexpected sequence number */
			peer_mep->cc_status.seq_unexp_seen = true;

		mep->ccm_rx_snumber = ntohl(*snumber);

		/* TLV end is after common header + sequence number + MEP ID +
		 * MA ID + ITU reserved
		 */
		index = CFM_CCM_PDU_TLV_OFFSET;
		max = 0;
		do { /* Handle all TLVs */
			size = ccm_tlv_extract(skb, index, peer_mep);
			index += size;
			max += 1;
		} while (size != 0 && max < 4); /* Max four TLVs possible */

		return 1;
	}

	mep->status.opcode_unexp_seen = true;

	return 1;
}

static struct br_frame_type cfm_frame_type __read_mostly = {
	.type = cpu_to_be16(ETH_P_CFM),
	.frame_handler = br_cfm_frame_rx,
};

int br_cfm_mep_create(struct net_bridge *br,
		      const u32 instance,
		      struct br_cfm_mep_create *const create,
		      struct netlink_ext_ack *extack)
{
	struct net_bridge_port *p;
	struct br_cfm_mep *mep;

	ASSERT_RTNL();

	if (create->domain == BR_CFM_VLAN) {
		NL_SET_ERR_MSG_MOD(extack,
				   "VLAN domain not supported");
		return -EINVAL;
	}
	if (create->domain != BR_CFM_PORT) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Invalid domain value");
		return -EINVAL;
	}
	if (create->direction == BR_CFM_MEP_DIRECTION_UP) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Up-MEP not supported");
		return -EINVAL;
	}
	if (create->direction != BR_CFM_MEP_DIRECTION_DOWN) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Invalid direction value");
		return -EINVAL;
	}
	p = br_mep_get_port(br, create->ifindex);
	if (!p) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Port is not related to bridge");
		return -EINVAL;
	}
	mep = br_mep_find(br, instance);
	if (mep) {
		NL_SET_ERR_MSG_MOD(extack,
				   "MEP instance already exists");
		return -EEXIST;
	}

	/* In PORT domain only one instance can be created per port */
	if (create->domain == BR_CFM_PORT) {
		mep = br_mep_find_ifindex(br, create->ifindex);
		if (mep) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Only one Port MEP on a port allowed");
			return -EINVAL;
		}
	}

	mep = kzalloc(sizeof(*mep), GFP_KERNEL);
	if (!mep)
		return -ENOMEM;

	mep->create = *create;
	mep->instance = instance;
	rcu_assign_pointer(mep->b_port, p);

	INIT_HLIST_HEAD(&mep->peer_mep_list);
	INIT_DELAYED_WORK(&mep->ccm_tx_dwork, ccm_tx_work_expired);

	if (hlist_empty(&br->mep_list))
		br_add_frame(br, &cfm_frame_type);

	hlist_add_tail_rcu(&mep->head, &br->mep_list);

	return 0;
}

static void mep_delete_implementation(struct net_bridge *br,
				      struct br_cfm_mep *mep)
{
	struct br_cfm_peer_mep *peer_mep;
	struct hlist_node *n_store;

	ASSERT_RTNL();

	/* Empty and free peer MEP list */
	hlist_for_each_entry_safe(peer_mep, n_store, &mep->peer_mep_list, head) {
		cancel_delayed_work_sync(&peer_mep->ccm_rx_dwork);
		hlist_del_rcu(&peer_mep->head);
		kfree_rcu(peer_mep, rcu);
	}

	cancel_delayed_work_sync(&mep->ccm_tx_dwork);

	RCU_INIT_POINTER(mep->b_port, NULL);
	hlist_del_rcu(&mep->head);
	kfree_rcu(mep, rcu);

	if (hlist_empty(&br->mep_list))
		br_del_frame(br, &cfm_frame_type);
}

int br_cfm_mep_delete(struct net_bridge *br,
		      const u32 instance,
		      struct netlink_ext_ack *extack)
{
	struct br_cfm_mep *mep;

	ASSERT_RTNL();

	mep = br_mep_find(br, instance);
	if (!mep) {
		NL_SET_ERR_MSG_MOD(extack,
				   "MEP instance does not exists");
		return -ENOENT;
	}

	mep_delete_implementation(br, mep);

	return 0;
}

int br_cfm_mep_config_set(struct net_bridge *br,
			  const u32 instance,
			  const struct br_cfm_mep_config *const config,
			  struct netlink_ext_ack *extack)
{
	struct br_cfm_mep *mep;

	ASSERT_RTNL();

	mep = br_mep_find(br, instance);
	if (!mep) {
		NL_SET_ERR_MSG_MOD(extack,
				   "MEP instance does not exists");
		return -ENOENT;
	}

	mep->config = *config;

	return 0;
}

int br_cfm_cc_config_set(struct net_bridge *br,
			 const u32 instance,
			 const struct br_cfm_cc_config *const config,
			 struct netlink_ext_ack *extack)
{
	struct br_cfm_peer_mep *peer_mep;
	struct br_cfm_mep *mep;

	ASSERT_RTNL();

	mep = br_mep_find(br, instance);
	if (!mep) {
		NL_SET_ERR_MSG_MOD(extack,
				   "MEP instance does not exists");
		return -ENOENT;
	}

	/* Check for no change in configuration */
	if (memcmp(config, &mep->cc_config, sizeof(*config)) == 0)
		return 0;

	if (config->enable && !mep->cc_config.enable)
		/* CC is enabled */
		hlist_for_each_entry(peer_mep, &mep->peer_mep_list, head)
			cc_peer_enable(peer_mep);

	if (!config->enable && mep->cc_config.enable)
		/* CC is disabled */
		hlist_for_each_entry(peer_mep, &mep->peer_mep_list, head)
			cc_peer_disable(peer_mep);

	mep->cc_config = *config;
	mep->ccm_rx_snumber = 0;
	mep->ccm_tx_snumber = 1;

	return 0;
}

int br_cfm_cc_peer_mep_add(struct net_bridge *br, const u32 instance,
			   u32 mepid,
			   struct netlink_ext_ack *extack)
{
	struct br_cfm_peer_mep *peer_mep;
	struct br_cfm_mep *mep;

	ASSERT_RTNL();

	mep = br_mep_find(br, instance);
	if (!mep) {
		NL_SET_ERR_MSG_MOD(extack,
				   "MEP instance does not exists");
		return -ENOENT;
	}

	peer_mep = br_peer_mep_find(mep, mepid);
	if (peer_mep) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Peer MEP-ID already exists");
		return -EEXIST;
	}

	peer_mep = kzalloc(sizeof(*peer_mep), GFP_KERNEL);
	if (!peer_mep)
		return -ENOMEM;

	peer_mep->mepid = mepid;
	peer_mep->mep = mep;
	INIT_DELAYED_WORK(&peer_mep->ccm_rx_dwork, ccm_rx_work_expired);

	if (mep->cc_config.enable)
		cc_peer_enable(peer_mep);

	hlist_add_tail_rcu(&peer_mep->head, &mep->peer_mep_list);

	return 0;
}

int br_cfm_cc_peer_mep_remove(struct net_bridge *br, const u32 instance,
			      u32 mepid,
			      struct netlink_ext_ack *extack)
{
	struct br_cfm_peer_mep *peer_mep;
	struct br_cfm_mep *mep;

	ASSERT_RTNL();

	mep = br_mep_find(br, instance);
	if (!mep) {
		NL_SET_ERR_MSG_MOD(extack,
				   "MEP instance does not exists");
		return -ENOENT;
	}

	peer_mep = br_peer_mep_find(mep, mepid);
	if (!peer_mep) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Peer MEP-ID does not exists");
		return -ENOENT;
	}

	cc_peer_disable(peer_mep);

	hlist_del_rcu(&peer_mep->head);
	kfree_rcu(peer_mep, rcu);

	return 0;
}

int br_cfm_cc_rdi_set(struct net_bridge *br, const u32 instance,
		      const bool rdi, struct netlink_ext_ack *extack)
{
	struct br_cfm_mep *mep;

	ASSERT_RTNL();

	mep = br_mep_find(br, instance);
	if (!mep) {
		NL_SET_ERR_MSG_MOD(extack,
				   "MEP instance does not exists");
		return -ENOENT;
	}

	mep->rdi = rdi;

	return 0;
}

int br_cfm_cc_ccm_tx(struct net_bridge *br, const u32 instance,
		     const struct br_cfm_cc_ccm_tx_info *const tx_info,
		     struct netlink_ext_ack *extack)
{
	struct br_cfm_mep *mep;

	ASSERT_RTNL();

	mep = br_mep_find(br, instance);
	if (!mep) {
		NL_SET_ERR_MSG_MOD(extack,
				   "MEP instance does not exists");
		return -ENOENT;
	}

	if (memcmp(tx_info, &mep->cc_ccm_tx_info, sizeof(*tx_info)) == 0) {
		/* No change in tx_info. */
		if (mep->cc_ccm_tx_info.period == 0)
			/* Transmission is not enabled - just return */
			return 0;

		/* Transmission is ongoing, the end time is recalculated */
		mep->ccm_tx_end = jiffies +
				  usecs_to_jiffies(tx_info->period * 1000000);
		return 0;
	}

	if (tx_info->period == 0 && mep->cc_ccm_tx_info.period == 0)
		/* Some change in info and transmission is not ongoing */
		goto save;

	if (tx_info->period != 0 && mep->cc_ccm_tx_info.period != 0) {
		/* Some change in info and transmission is ongoing
		 * The end time is recalculated
		 */
		mep->ccm_tx_end = jiffies +
				  usecs_to_jiffies(tx_info->period * 1000000);

		goto save;
	}

	if (tx_info->period == 0 && mep->cc_ccm_tx_info.period != 0) {
		cancel_delayed_work_sync(&mep->ccm_tx_dwork);
		goto save;
	}

	/* Start delayed work to transmit CCM frames. It is done with zero delay
	 * to send first frame immediately
	 */
	mep->ccm_tx_end = jiffies + usecs_to_jiffies(tx_info->period * 1000000);
	queue_delayed_work(system_wq, &mep->ccm_tx_dwork, 0);

save:
	mep->cc_ccm_tx_info = *tx_info;

	return 0;
}

int br_cfm_mep_count(struct net_bridge *br, u32 *count)
{
	struct br_cfm_mep *mep;

	*count = 0;

	rcu_read_lock();
	hlist_for_each_entry_rcu(mep, &br->mep_list, head)
		*count += 1;
	rcu_read_unlock();

	return 0;
}

int br_cfm_peer_mep_count(struct net_bridge *br, u32 *count)
{
	struct br_cfm_peer_mep *peer_mep;
	struct br_cfm_mep *mep;

	*count = 0;

	rcu_read_lock();
	hlist_for_each_entry_rcu(mep, &br->mep_list, head)
		hlist_for_each_entry_rcu(peer_mep, &mep->peer_mep_list, head)
			*count += 1;
	rcu_read_unlock();

	return 0;
}

bool br_cfm_created(struct net_bridge *br)
{
	return !hlist_empty(&br->mep_list);
}

/* Deletes the CFM instances on a specific bridge port
 */
void br_cfm_port_del(struct net_bridge *br, struct net_bridge_port *port)
{
	struct hlist_node *n_store;
	struct br_cfm_mep *mep;

	ASSERT_RTNL();

	hlist_for_each_entry_safe(mep, n_store, &br->mep_list, head)
		if (mep->create.ifindex == port->dev->ifindex)
			mep_delete_implementation(br, mep);
}
