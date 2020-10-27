/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef _BR_PRIVATE_CFM_H_
#define _BR_PRIVATE_CFM_H_

#include "br_private.h"
#include <uapi/linux/cfm_bridge.h>

struct br_cfm_mep_create {
	enum br_cfm_domain domain; /* Domain for this MEP */
	enum br_cfm_mep_direction direction; /* Up or Down MEP direction */
	u32 ifindex; /* Residence port */
};

int br_cfm_mep_create(struct net_bridge *br,
		      const u32 instance,
		      struct br_cfm_mep_create *const create,
		      struct netlink_ext_ack *extack);

int br_cfm_mep_delete(struct net_bridge *br,
		      const u32 instance,
		      struct netlink_ext_ack *extack);

struct br_cfm_mep_config {
	u32 mdlevel;
	u32 mepid; /* MEPID for this MEP */
	struct mac_addr unicast_mac; /* The MEP unicast MAC */
};

int br_cfm_mep_config_set(struct net_bridge *br,
			  const u32 instance,
			  const struct br_cfm_mep_config *const config,
			  struct netlink_ext_ack *extack);

int br_cfm_cc_peer_mep_add(struct net_bridge *br, const u32 instance,
			   u32 peer_mep_id,
			   struct netlink_ext_ack *extack);
int br_cfm_cc_peer_mep_remove(struct net_bridge *br, const u32 instance,
			      u32 peer_mep_id,
			      struct netlink_ext_ack *extack);

struct br_cfm_mep {
	/* list header of MEP instances */
	struct hlist_node		head;
	u32				instance;
	struct br_cfm_mep_create	create;
	struct br_cfm_mep_config	config;
	/* List of multiple peer MEPs */
	struct hlist_head		peer_mep_list;
	struct net_bridge_port __rcu	*b_port;
	struct rcu_head			rcu;
};

struct br_cfm_peer_mep {
	struct hlist_node		head;
	struct br_cfm_mep		*mep;
	u32				mepid;
	struct rcu_head			rcu;
};

#endif /* _BR_PRIVATE_CFM_H_ */
