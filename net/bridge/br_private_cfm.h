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

struct br_cfm_maid {
	u8 data[CFM_MAID_LENGTH];
};

struct br_cfm_cc_config {
	/* Expected received CCM PDU MAID. */
	struct br_cfm_maid exp_maid;

	/* Expected received CCM PDU interval. */
	/* Transmitting CCM PDU interval when CCM tx is enabled. */
	enum br_cfm_ccm_interval exp_interval;

	bool enable; /* Enable/disable CCM PDU handling */
};

int br_cfm_cc_config_set(struct net_bridge *br,
			 const u32 instance,
			 const struct br_cfm_cc_config *const config,
			 struct netlink_ext_ack *extack);

int br_cfm_cc_peer_mep_add(struct net_bridge *br, const u32 instance,
			   u32 peer_mep_id,
			   struct netlink_ext_ack *extack);
int br_cfm_cc_peer_mep_remove(struct net_bridge *br, const u32 instance,
			      u32 peer_mep_id,
			      struct netlink_ext_ack *extack);

/* Transmitted CCM Remote Defect Indication status set.
 * This RDI is inserted in transmitted CCM PDUs if CCM transmission is enabled.
 * See br_cfm_cc_ccm_tx() with interval != BR_CFM_CCM_INTERVAL_NONE
 */
int br_cfm_cc_rdi_set(struct net_bridge *br, const u32 instance,
		      const bool rdi, struct netlink_ext_ack *extack);

/* OAM PDU Tx information */
struct br_cfm_cc_ccm_tx_info {
	struct mac_addr dmac;
	/* The CCM will be transmitted for this period in seconds.
	 * Call br_cfm_cc_ccm_tx before timeout to keep transmission alive.
	 * When period is zero any ongoing transmission will be stopped.
	 */
	u32 period;

	bool seq_no_update; /* Update Tx CCM sequence number */
	bool if_tlv; /* Insert Interface Status TLV */
	u8 if_tlv_value; /* Interface Status TLV value */
	bool port_tlv; /* Insert Port Status TLV */
	u8 port_tlv_value; /* Port Status TLV value */
	/* Sender ID TLV ??
	 * Organization-Specific TLV ??
	 */
};

int br_cfm_cc_ccm_tx(struct net_bridge *br, const u32 instance,
		     const struct br_cfm_cc_ccm_tx_info *const tx_info,
		     struct netlink_ext_ack *extack);

struct br_cfm_mep_status {
	/* Indications that an OAM PDU has been seen. */
	bool opcode_unexp_seen; /* RX of OAM PDU with unexpected opcode */
	bool version_unexp_seen; /* RX of OAM PDU with unexpected version */
	bool rx_level_low_seen; /* Rx of OAM PDU with level low */
};

struct br_cfm_cc_peer_status {
	/* This CCM related status is based on the latest received CCM PDU. */
	u8 port_tlv_value; /* Port Status TLV value */
	u8 if_tlv_value; /* Interface Status TLV value */

	/* CCM has not been received for 3.25 intervals */
	u8 ccm_defect:1;

	/* (RDI == 1) for last received CCM PDU */
	u8 rdi:1;

	/* Indications that a CCM PDU has been seen. */
	u8 seen:1; /* CCM PDU received */
	u8 tlv_seen:1; /* CCM PDU with TLV received */
	/* CCM PDU with unexpected sequence number received */
	u8 seq_unexp_seen:1;
};

struct br_cfm_mep {
	/* list header of MEP instances */
	struct hlist_node		head;
	u32				instance;
	struct br_cfm_mep_create	create;
	struct br_cfm_mep_config	config;
	struct br_cfm_cc_config		cc_config;
	struct br_cfm_cc_ccm_tx_info	cc_ccm_tx_info;
	/* List of multiple peer MEPs */
	struct hlist_head		peer_mep_list;
	struct net_bridge_port __rcu	*b_port;
	unsigned long			ccm_tx_end;
	struct delayed_work		ccm_tx_dwork;
	u32				ccm_tx_snumber;
	u32				ccm_rx_snumber;
	struct br_cfm_mep_status	status;
	bool				rdi;
	struct rcu_head			rcu;
};

struct br_cfm_peer_mep {
	struct hlist_node		head;
	struct br_cfm_mep		*mep;
	struct delayed_work		ccm_rx_dwork;
	u32				mepid;
	struct br_cfm_cc_peer_status	cc_status;
	u32				ccm_rx_count_miss;
	struct rcu_head			rcu;
};

#endif /* _BR_PRIVATE_CFM_H_ */
