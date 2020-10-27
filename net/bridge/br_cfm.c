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
		hlist_del_rcu(&peer_mep->head);
		kfree_rcu(peer_mep, rcu);
	}

	RCU_INIT_POINTER(mep->b_port, NULL);
	hlist_del_rcu(&mep->head);
	kfree_rcu(mep, rcu);
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

	hlist_del_rcu(&peer_mep->head);
	kfree_rcu(peer_mep, rcu);

	return 0;
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
