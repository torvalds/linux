// SPDX-License-Identifier: GPL-2.0-or-later

#include <net/genetlink.h>

#include "br_private.h"
#include "br_private_cfm.h"

static const struct nla_policy
br_cfm_mep_create_policy[IFLA_BRIDGE_CFM_MEP_CREATE_MAX + 1] = {
	[IFLA_BRIDGE_CFM_MEP_CREATE_UNSPEC]	= { .type = NLA_REJECT },
	[IFLA_BRIDGE_CFM_MEP_CREATE_INSTANCE]	= { .type = NLA_U32 },
	[IFLA_BRIDGE_CFM_MEP_CREATE_DOMAIN]	= { .type = NLA_U32 },
	[IFLA_BRIDGE_CFM_MEP_CREATE_DIRECTION]	= { .type = NLA_U32 },
	[IFLA_BRIDGE_CFM_MEP_CREATE_IFINDEX]	= { .type = NLA_U32 },
};

static const struct nla_policy
br_cfm_mep_delete_policy[IFLA_BRIDGE_CFM_MEP_DELETE_MAX + 1] = {
	[IFLA_BRIDGE_CFM_MEP_DELETE_UNSPEC]	= { .type = NLA_REJECT },
	[IFLA_BRIDGE_CFM_MEP_DELETE_INSTANCE]	= { .type = NLA_U32 },
};

static const struct nla_policy
br_cfm_mep_config_policy[IFLA_BRIDGE_CFM_MEP_CONFIG_MAX + 1] = {
	[IFLA_BRIDGE_CFM_MEP_CONFIG_UNSPEC]	 = { .type = NLA_REJECT },
	[IFLA_BRIDGE_CFM_MEP_CONFIG_INSTANCE]	 = { .type = NLA_U32 },
	[IFLA_BRIDGE_CFM_MEP_CONFIG_UNICAST_MAC] = NLA_POLICY_ETH_ADDR,
	[IFLA_BRIDGE_CFM_MEP_CONFIG_MDLEVEL]	 = NLA_POLICY_MAX(NLA_U32, 7),
	[IFLA_BRIDGE_CFM_MEP_CONFIG_MEPID]	 = NLA_POLICY_MAX(NLA_U32, 0x1FFF),
};

static const struct nla_policy
br_cfm_cc_config_policy[IFLA_BRIDGE_CFM_CC_CONFIG_MAX + 1] = {
	[IFLA_BRIDGE_CFM_CC_CONFIG_UNSPEC]	 = { .type = NLA_REJECT },
	[IFLA_BRIDGE_CFM_CC_CONFIG_INSTANCE]	 = { .type = NLA_U32 },
	[IFLA_BRIDGE_CFM_CC_CONFIG_ENABLE]	 = { .type = NLA_U32 },
	[IFLA_BRIDGE_CFM_CC_CONFIG_EXP_INTERVAL] = { .type = NLA_U32 },
	[IFLA_BRIDGE_CFM_CC_CONFIG_EXP_MAID]	 = {
	.type = NLA_BINARY, .len = CFM_MAID_LENGTH },
};

static const struct nla_policy
br_cfm_cc_peer_mep_policy[IFLA_BRIDGE_CFM_CC_PEER_MEP_MAX + 1] = {
	[IFLA_BRIDGE_CFM_CC_PEER_MEP_UNSPEC]	= { .type = NLA_REJECT },
	[IFLA_BRIDGE_CFM_CC_PEER_MEP_INSTANCE]	= { .type = NLA_U32 },
	[IFLA_BRIDGE_CFM_CC_PEER_MEPID]		= NLA_POLICY_MAX(NLA_U32, 0x1FFF),
};

static const struct nla_policy
br_cfm_cc_rdi_policy[IFLA_BRIDGE_CFM_CC_RDI_MAX + 1] = {
	[IFLA_BRIDGE_CFM_CC_RDI_UNSPEC]		= { .type = NLA_REJECT },
	[IFLA_BRIDGE_CFM_CC_RDI_INSTANCE]	= { .type = NLA_U32 },
	[IFLA_BRIDGE_CFM_CC_RDI_RDI]		= { .type = NLA_U32 },
};

static const struct nla_policy
br_cfm_cc_ccm_tx_policy[IFLA_BRIDGE_CFM_CC_CCM_TX_MAX + 1] = {
	[IFLA_BRIDGE_CFM_CC_CCM_TX_UNSPEC]	   = { .type = NLA_REJECT },
	[IFLA_BRIDGE_CFM_CC_CCM_TX_INSTANCE]	   = { .type = NLA_U32 },
	[IFLA_BRIDGE_CFM_CC_CCM_TX_DMAC]	   = NLA_POLICY_ETH_ADDR,
	[IFLA_BRIDGE_CFM_CC_CCM_TX_SEQ_NO_UPDATE]  = { .type = NLA_U32 },
	[IFLA_BRIDGE_CFM_CC_CCM_TX_PERIOD]	   = { .type = NLA_U32 },
	[IFLA_BRIDGE_CFM_CC_CCM_TX_IF_TLV]	   = { .type = NLA_U32 },
	[IFLA_BRIDGE_CFM_CC_CCM_TX_IF_TLV_VALUE]   = { .type = NLA_U8 },
	[IFLA_BRIDGE_CFM_CC_CCM_TX_PORT_TLV]	   = { .type = NLA_U32 },
	[IFLA_BRIDGE_CFM_CC_CCM_TX_PORT_TLV_VALUE] = { .type = NLA_U8 },
};

static const struct nla_policy
br_cfm_policy[IFLA_BRIDGE_CFM_MAX + 1] = {
	[IFLA_BRIDGE_CFM_UNSPEC]		= { .type = NLA_REJECT },
	[IFLA_BRIDGE_CFM_MEP_CREATE]		=
				NLA_POLICY_NESTED(br_cfm_mep_create_policy),
	[IFLA_BRIDGE_CFM_MEP_DELETE]		=
				NLA_POLICY_NESTED(br_cfm_mep_delete_policy),
	[IFLA_BRIDGE_CFM_MEP_CONFIG]		=
				NLA_POLICY_NESTED(br_cfm_mep_config_policy),
	[IFLA_BRIDGE_CFM_CC_CONFIG]		=
				NLA_POLICY_NESTED(br_cfm_cc_config_policy),
	[IFLA_BRIDGE_CFM_CC_PEER_MEP_ADD]	=
				NLA_POLICY_NESTED(br_cfm_cc_peer_mep_policy),
	[IFLA_BRIDGE_CFM_CC_PEER_MEP_REMOVE]	=
				NLA_POLICY_NESTED(br_cfm_cc_peer_mep_policy),
	[IFLA_BRIDGE_CFM_CC_RDI]		=
				NLA_POLICY_NESTED(br_cfm_cc_rdi_policy),
	[IFLA_BRIDGE_CFM_CC_CCM_TX]		=
				NLA_POLICY_NESTED(br_cfm_cc_ccm_tx_policy),
};

static int br_mep_create_parse(struct net_bridge *br, struct nlattr *attr,
			       struct netlink_ext_ack *extack)
{
	struct nlattr *tb[IFLA_BRIDGE_CFM_MEP_CREATE_MAX + 1];
	struct br_cfm_mep_create create;
	u32 instance;
	int err;

	err = nla_parse_nested(tb, IFLA_BRIDGE_CFM_MEP_CREATE_MAX, attr,
			       br_cfm_mep_create_policy, extack);
	if (err)
		return err;

	if (!tb[IFLA_BRIDGE_CFM_MEP_CREATE_INSTANCE]) {
		NL_SET_ERR_MSG_MOD(extack, "Missing INSTANCE attribute");
		return -EINVAL;
	}
	if (!tb[IFLA_BRIDGE_CFM_MEP_CREATE_DOMAIN]) {
		NL_SET_ERR_MSG_MOD(extack, "Missing DOMAIN attribute");
		return -EINVAL;
	}
	if (!tb[IFLA_BRIDGE_CFM_MEP_CREATE_DIRECTION]) {
		NL_SET_ERR_MSG_MOD(extack, "Missing DIRECTION attribute");
		return -EINVAL;
	}
	if (!tb[IFLA_BRIDGE_CFM_MEP_CREATE_IFINDEX]) {
		NL_SET_ERR_MSG_MOD(extack, "Missing IFINDEX attribute");
		return -EINVAL;
	}

	memset(&create, 0, sizeof(create));

	instance =  nla_get_u32(tb[IFLA_BRIDGE_CFM_MEP_CREATE_INSTANCE]);
	create.domain = nla_get_u32(tb[IFLA_BRIDGE_CFM_MEP_CREATE_DOMAIN]);
	create.direction = nla_get_u32(tb[IFLA_BRIDGE_CFM_MEP_CREATE_DIRECTION]);
	create.ifindex = nla_get_u32(tb[IFLA_BRIDGE_CFM_MEP_CREATE_IFINDEX]);

	return br_cfm_mep_create(br, instance, &create, extack);
}

static int br_mep_delete_parse(struct net_bridge *br, struct nlattr *attr,
			       struct netlink_ext_ack *extack)
{
	struct nlattr *tb[IFLA_BRIDGE_CFM_MEP_DELETE_MAX + 1];
	u32 instance;
	int err;

	err = nla_parse_nested(tb, IFLA_BRIDGE_CFM_MEP_DELETE_MAX, attr,
			       br_cfm_mep_delete_policy, extack);
	if (err)
		return err;

	if (!tb[IFLA_BRIDGE_CFM_MEP_DELETE_INSTANCE]) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Missing INSTANCE attribute");
		return -EINVAL;
	}

	instance =  nla_get_u32(tb[IFLA_BRIDGE_CFM_MEP_DELETE_INSTANCE]);

	return br_cfm_mep_delete(br, instance, extack);
}

static int br_mep_config_parse(struct net_bridge *br, struct nlattr *attr,
			       struct netlink_ext_ack *extack)
{
	struct nlattr *tb[IFLA_BRIDGE_CFM_MEP_CONFIG_MAX + 1];
	struct br_cfm_mep_config config;
	u32 instance;
	int err;

	err = nla_parse_nested(tb, IFLA_BRIDGE_CFM_MEP_CONFIG_MAX, attr,
			       br_cfm_mep_config_policy, extack);
	if (err)
		return err;

	if (!tb[IFLA_BRIDGE_CFM_MEP_CONFIG_INSTANCE]) {
		NL_SET_ERR_MSG_MOD(extack, "Missing INSTANCE attribute");
		return -EINVAL;
	}
	if (!tb[IFLA_BRIDGE_CFM_MEP_CONFIG_UNICAST_MAC]) {
		NL_SET_ERR_MSG_MOD(extack, "Missing UNICAST_MAC attribute");
		return -EINVAL;
	}
	if (!tb[IFLA_BRIDGE_CFM_MEP_CONFIG_MDLEVEL]) {
		NL_SET_ERR_MSG_MOD(extack, "Missing MDLEVEL attribute");
		return -EINVAL;
	}
	if (!tb[IFLA_BRIDGE_CFM_MEP_CONFIG_MEPID]) {
		NL_SET_ERR_MSG_MOD(extack, "Missing MEPID attribute");
		return -EINVAL;
	}

	memset(&config, 0, sizeof(config));

	instance =  nla_get_u32(tb[IFLA_BRIDGE_CFM_MEP_CONFIG_INSTANCE]);
	nla_memcpy(&config.unicast_mac.addr,
		   tb[IFLA_BRIDGE_CFM_MEP_CONFIG_UNICAST_MAC],
		   sizeof(config.unicast_mac.addr));
	config.mdlevel = nla_get_u32(tb[IFLA_BRIDGE_CFM_MEP_CONFIG_MDLEVEL]);
	config.mepid = nla_get_u32(tb[IFLA_BRIDGE_CFM_MEP_CONFIG_MEPID]);

	return br_cfm_mep_config_set(br, instance, &config, extack);
}

static int br_cc_config_parse(struct net_bridge *br, struct nlattr *attr,
			      struct netlink_ext_ack *extack)
{
	struct nlattr *tb[IFLA_BRIDGE_CFM_CC_CONFIG_MAX + 1];
	struct br_cfm_cc_config config;
	u32 instance;
	int err;

	err = nla_parse_nested(tb, IFLA_BRIDGE_CFM_CC_CONFIG_MAX, attr,
			       br_cfm_cc_config_policy, extack);
	if (err)
		return err;

	if (!tb[IFLA_BRIDGE_CFM_CC_CONFIG_INSTANCE]) {
		NL_SET_ERR_MSG_MOD(extack, "Missing INSTANCE attribute");
		return -EINVAL;
	}
	if (!tb[IFLA_BRIDGE_CFM_CC_CONFIG_ENABLE]) {
		NL_SET_ERR_MSG_MOD(extack, "Missing ENABLE attribute");
		return -EINVAL;
	}
	if (!tb[IFLA_BRIDGE_CFM_CC_CONFIG_EXP_INTERVAL]) {
		NL_SET_ERR_MSG_MOD(extack, "Missing INTERVAL attribute");
		return -EINVAL;
	}
	if (!tb[IFLA_BRIDGE_CFM_CC_CONFIG_EXP_MAID]) {
		NL_SET_ERR_MSG_MOD(extack, "Missing MAID attribute");
		return -EINVAL;
	}

	memset(&config, 0, sizeof(config));

	instance =  nla_get_u32(tb[IFLA_BRIDGE_CFM_CC_CONFIG_INSTANCE]);
	config.enable = nla_get_u32(tb[IFLA_BRIDGE_CFM_CC_CONFIG_ENABLE]);
	config.exp_interval = nla_get_u32(tb[IFLA_BRIDGE_CFM_CC_CONFIG_EXP_INTERVAL]);
	nla_memcpy(&config.exp_maid.data, tb[IFLA_BRIDGE_CFM_CC_CONFIG_EXP_MAID],
		   sizeof(config.exp_maid.data));

	return br_cfm_cc_config_set(br, instance, &config, extack);
}

static int br_cc_peer_mep_add_parse(struct net_bridge *br, struct nlattr *attr,
				    struct netlink_ext_ack *extack)
{
	struct nlattr *tb[IFLA_BRIDGE_CFM_CC_PEER_MEP_MAX + 1];
	u32 instance, peer_mep_id;
	int err;

	err = nla_parse_nested(tb, IFLA_BRIDGE_CFM_CC_PEER_MEP_MAX, attr,
			       br_cfm_cc_peer_mep_policy, extack);
	if (err)
		return err;

	if (!tb[IFLA_BRIDGE_CFM_CC_PEER_MEP_INSTANCE]) {
		NL_SET_ERR_MSG_MOD(extack, "Missing INSTANCE attribute");
		return -EINVAL;
	}
	if (!tb[IFLA_BRIDGE_CFM_CC_PEER_MEPID]) {
		NL_SET_ERR_MSG_MOD(extack, "Missing PEER_MEP_ID attribute");
		return -EINVAL;
	}

	instance =  nla_get_u32(tb[IFLA_BRIDGE_CFM_CC_PEER_MEP_INSTANCE]);
	peer_mep_id =  nla_get_u32(tb[IFLA_BRIDGE_CFM_CC_PEER_MEPID]);

	return br_cfm_cc_peer_mep_add(br, instance, peer_mep_id, extack);
}

static int br_cc_peer_mep_remove_parse(struct net_bridge *br, struct nlattr *attr,
				       struct netlink_ext_ack *extack)
{
	struct nlattr *tb[IFLA_BRIDGE_CFM_CC_PEER_MEP_MAX + 1];
	u32 instance, peer_mep_id;
	int err;

	err = nla_parse_nested(tb, IFLA_BRIDGE_CFM_CC_PEER_MEP_MAX, attr,
			       br_cfm_cc_peer_mep_policy, extack);
	if (err)
		return err;

	if (!tb[IFLA_BRIDGE_CFM_CC_PEER_MEP_INSTANCE]) {
		NL_SET_ERR_MSG_MOD(extack, "Missing INSTANCE attribute");
		return -EINVAL;
	}
	if (!tb[IFLA_BRIDGE_CFM_CC_PEER_MEPID]) {
		NL_SET_ERR_MSG_MOD(extack, "Missing PEER_MEP_ID attribute");
		return -EINVAL;
	}

	instance =  nla_get_u32(tb[IFLA_BRIDGE_CFM_CC_PEER_MEP_INSTANCE]);
	peer_mep_id =  nla_get_u32(tb[IFLA_BRIDGE_CFM_CC_PEER_MEPID]);

	return br_cfm_cc_peer_mep_remove(br, instance, peer_mep_id, extack);
}

static int br_cc_rdi_parse(struct net_bridge *br, struct nlattr *attr,
			   struct netlink_ext_ack *extack)
{
	struct nlattr *tb[IFLA_BRIDGE_CFM_CC_RDI_MAX + 1];
	u32 instance, rdi;
	int err;

	err = nla_parse_nested(tb, IFLA_BRIDGE_CFM_CC_RDI_MAX, attr,
			       br_cfm_cc_rdi_policy, extack);
	if (err)
		return err;

	if (!tb[IFLA_BRIDGE_CFM_CC_RDI_INSTANCE]) {
		NL_SET_ERR_MSG_MOD(extack, "Missing INSTANCE attribute");
		return -EINVAL;
	}
	if (!tb[IFLA_BRIDGE_CFM_CC_RDI_RDI]) {
		NL_SET_ERR_MSG_MOD(extack, "Missing RDI attribute");
		return -EINVAL;
	}

	instance =  nla_get_u32(tb[IFLA_BRIDGE_CFM_CC_RDI_INSTANCE]);
	rdi =  nla_get_u32(tb[IFLA_BRIDGE_CFM_CC_RDI_RDI]);

	return br_cfm_cc_rdi_set(br, instance, rdi, extack);
}

static int br_cc_ccm_tx_parse(struct net_bridge *br, struct nlattr *attr,
			      struct netlink_ext_ack *extack)
{
	struct nlattr *tb[IFLA_BRIDGE_CFM_CC_CCM_TX_MAX + 1];
	struct br_cfm_cc_ccm_tx_info tx_info;
	u32 instance;
	int err;

	err = nla_parse_nested(tb, IFLA_BRIDGE_CFM_CC_CCM_TX_MAX, attr,
			       br_cfm_cc_ccm_tx_policy, extack);
	if (err)
		return err;

	if (!tb[IFLA_BRIDGE_CFM_CC_CCM_TX_INSTANCE]) {
		NL_SET_ERR_MSG_MOD(extack, "Missing INSTANCE attribute");
		return -EINVAL;
	}
	if (!tb[IFLA_BRIDGE_CFM_CC_CCM_TX_DMAC]) {
		NL_SET_ERR_MSG_MOD(extack, "Missing DMAC attribute");
		return -EINVAL;
	}
	if (!tb[IFLA_BRIDGE_CFM_CC_CCM_TX_SEQ_NO_UPDATE]) {
		NL_SET_ERR_MSG_MOD(extack, "Missing SEQ_NO_UPDATE attribute");
		return -EINVAL;
	}
	if (!tb[IFLA_BRIDGE_CFM_CC_CCM_TX_PERIOD]) {
		NL_SET_ERR_MSG_MOD(extack, "Missing PERIOD attribute");
		return -EINVAL;
	}
	if (!tb[IFLA_BRIDGE_CFM_CC_CCM_TX_IF_TLV]) {
		NL_SET_ERR_MSG_MOD(extack, "Missing IF_TLV attribute");
		return -EINVAL;
	}
	if (!tb[IFLA_BRIDGE_CFM_CC_CCM_TX_IF_TLV_VALUE]) {
		NL_SET_ERR_MSG_MOD(extack, "Missing IF_TLV_VALUE attribute");
		return -EINVAL;
	}
	if (!tb[IFLA_BRIDGE_CFM_CC_CCM_TX_PORT_TLV]) {
		NL_SET_ERR_MSG_MOD(extack, "Missing PORT_TLV attribute");
		return -EINVAL;
	}
	if (!tb[IFLA_BRIDGE_CFM_CC_CCM_TX_PORT_TLV_VALUE]) {
		NL_SET_ERR_MSG_MOD(extack, "Missing PORT_TLV_VALUE attribute");
		return -EINVAL;
	}

	memset(&tx_info, 0, sizeof(tx_info));

	instance = nla_get_u32(tb[IFLA_BRIDGE_CFM_CC_CCM_TX_INSTANCE]);
	nla_memcpy(&tx_info.dmac.addr,
		   tb[IFLA_BRIDGE_CFM_CC_CCM_TX_DMAC],
		   sizeof(tx_info.dmac.addr));
	tx_info.seq_no_update = nla_get_u32(tb[IFLA_BRIDGE_CFM_CC_CCM_TX_SEQ_NO_UPDATE]);
	tx_info.period = nla_get_u32(tb[IFLA_BRIDGE_CFM_CC_CCM_TX_PERIOD]);
	tx_info.if_tlv = nla_get_u32(tb[IFLA_BRIDGE_CFM_CC_CCM_TX_IF_TLV]);
	tx_info.if_tlv_value = nla_get_u8(tb[IFLA_BRIDGE_CFM_CC_CCM_TX_IF_TLV_VALUE]);
	tx_info.port_tlv = nla_get_u32(tb[IFLA_BRIDGE_CFM_CC_CCM_TX_PORT_TLV]);
	tx_info.port_tlv_value = nla_get_u8(tb[IFLA_BRIDGE_CFM_CC_CCM_TX_PORT_TLV_VALUE]);

	return br_cfm_cc_ccm_tx(br, instance, &tx_info, extack);
}

int br_cfm_parse(struct net_bridge *br, struct net_bridge_port *p,
		 struct nlattr *attr, int cmd, struct netlink_ext_ack *extack)
{
	struct nlattr *tb[IFLA_BRIDGE_CFM_MAX + 1];
	int err;

	/* When this function is called for a port then the br pointer is
	 * invalid, therefor set the br to point correctly
	 */
	if (p)
		br = p->br;

	err = nla_parse_nested(tb, IFLA_BRIDGE_CFM_MAX, attr,
			       br_cfm_policy, extack);
	if (err)
		return err;

	if (tb[IFLA_BRIDGE_CFM_MEP_CREATE]) {
		err = br_mep_create_parse(br, tb[IFLA_BRIDGE_CFM_MEP_CREATE],
					  extack);
		if (err)
			return err;
	}

	if (tb[IFLA_BRIDGE_CFM_MEP_DELETE]) {
		err = br_mep_delete_parse(br, tb[IFLA_BRIDGE_CFM_MEP_DELETE],
					  extack);
		if (err)
			return err;
	}

	if (tb[IFLA_BRIDGE_CFM_MEP_CONFIG]) {
		err = br_mep_config_parse(br, tb[IFLA_BRIDGE_CFM_MEP_CONFIG],
					  extack);
		if (err)
			return err;
	}

	if (tb[IFLA_BRIDGE_CFM_CC_CONFIG]) {
		err = br_cc_config_parse(br, tb[IFLA_BRIDGE_CFM_CC_CONFIG],
					 extack);
		if (err)
			return err;
	}

	if (tb[IFLA_BRIDGE_CFM_CC_PEER_MEP_ADD]) {
		err = br_cc_peer_mep_add_parse(br, tb[IFLA_BRIDGE_CFM_CC_PEER_MEP_ADD],
					       extack);
		if (err)
			return err;
	}

	if (tb[IFLA_BRIDGE_CFM_CC_PEER_MEP_REMOVE]) {
		err = br_cc_peer_mep_remove_parse(br, tb[IFLA_BRIDGE_CFM_CC_PEER_MEP_REMOVE],
						  extack);
		if (err)
			return err;
	}

	if (tb[IFLA_BRIDGE_CFM_CC_RDI]) {
		err = br_cc_rdi_parse(br, tb[IFLA_BRIDGE_CFM_CC_RDI],
				      extack);
		if (err)
			return err;
	}

	if (tb[IFLA_BRIDGE_CFM_CC_CCM_TX]) {
		err = br_cc_ccm_tx_parse(br, tb[IFLA_BRIDGE_CFM_CC_CCM_TX],
					 extack);
		if (err)
			return err;
	}

	return 0;
}

int br_cfm_config_fill_info(struct sk_buff *skb, struct net_bridge *br)
{
	struct br_cfm_peer_mep *peer_mep;
	struct br_cfm_mep *mep;
	struct nlattr *tb;

	hlist_for_each_entry_rcu(mep, &br->mep_list, head) {
		tb = nla_nest_start(skb, IFLA_BRIDGE_CFM_MEP_CREATE_INFO);
		if (!tb)
			goto nla_info_failure;

		if (nla_put_u32(skb, IFLA_BRIDGE_CFM_MEP_CREATE_INSTANCE,
				mep->instance))
			goto nla_put_failure;

		if (nla_put_u32(skb, IFLA_BRIDGE_CFM_MEP_CREATE_DOMAIN,
				mep->create.domain))
			goto nla_put_failure;

		if (nla_put_u32(skb, IFLA_BRIDGE_CFM_MEP_CREATE_DIRECTION,
				mep->create.direction))
			goto nla_put_failure;

		if (nla_put_u32(skb, IFLA_BRIDGE_CFM_MEP_CREATE_IFINDEX,
				mep->create.ifindex))
			goto nla_put_failure;

		nla_nest_end(skb, tb);

		tb = nla_nest_start(skb, IFLA_BRIDGE_CFM_MEP_CONFIG_INFO);

		if (!tb)
			goto nla_info_failure;

		if (nla_put_u32(skb, IFLA_BRIDGE_CFM_MEP_CONFIG_INSTANCE,
				mep->instance))
			goto nla_put_failure;

		if (nla_put(skb, IFLA_BRIDGE_CFM_MEP_CONFIG_UNICAST_MAC,
			    sizeof(mep->config.unicast_mac.addr),
			    mep->config.unicast_mac.addr))
			goto nla_put_failure;

		if (nla_put_u32(skb, IFLA_BRIDGE_CFM_MEP_CONFIG_MDLEVEL,
				mep->config.mdlevel))
			goto nla_put_failure;

		if (nla_put_u32(skb, IFLA_BRIDGE_CFM_MEP_CONFIG_MEPID,
				mep->config.mepid))
			goto nla_put_failure;

		nla_nest_end(skb, tb);

		tb = nla_nest_start(skb, IFLA_BRIDGE_CFM_CC_CONFIG_INFO);

		if (!tb)
			goto nla_info_failure;

		if (nla_put_u32(skb, IFLA_BRIDGE_CFM_CC_CONFIG_INSTANCE,
				mep->instance))
			goto nla_put_failure;

		if (nla_put_u32(skb, IFLA_BRIDGE_CFM_CC_CONFIG_ENABLE,
				mep->cc_config.enable))
			goto nla_put_failure;

		if (nla_put_u32(skb, IFLA_BRIDGE_CFM_CC_CONFIG_EXP_INTERVAL,
				mep->cc_config.exp_interval))
			goto nla_put_failure;

		if (nla_put(skb, IFLA_BRIDGE_CFM_CC_CONFIG_EXP_MAID,
			    sizeof(mep->cc_config.exp_maid.data),
			    mep->cc_config.exp_maid.data))
			goto nla_put_failure;

		nla_nest_end(skb, tb);

		tb = nla_nest_start(skb, IFLA_BRIDGE_CFM_CC_RDI_INFO);

		if (!tb)
			goto nla_info_failure;

		if (nla_put_u32(skb, IFLA_BRIDGE_CFM_CC_RDI_INSTANCE,
				mep->instance))
			goto nla_put_failure;

		if (nla_put_u32(skb, IFLA_BRIDGE_CFM_CC_RDI_RDI,
				mep->rdi))
			goto nla_put_failure;

		nla_nest_end(skb, tb);

		tb = nla_nest_start(skb, IFLA_BRIDGE_CFM_CC_CCM_TX_INFO);

		if (!tb)
			goto nla_info_failure;

		if (nla_put_u32(skb, IFLA_BRIDGE_CFM_CC_CCM_TX_INSTANCE,
				mep->instance))
			goto nla_put_failure;

		if (nla_put(skb, IFLA_BRIDGE_CFM_CC_CCM_TX_DMAC,
			    sizeof(mep->cc_ccm_tx_info.dmac),
			    mep->cc_ccm_tx_info.dmac.addr))
			goto nla_put_failure;

		if (nla_put_u32(skb, IFLA_BRIDGE_CFM_CC_CCM_TX_SEQ_NO_UPDATE,
				mep->cc_ccm_tx_info.seq_no_update))
			goto nla_put_failure;

		if (nla_put_u32(skb, IFLA_BRIDGE_CFM_CC_CCM_TX_PERIOD,
				mep->cc_ccm_tx_info.period))
			goto nla_put_failure;

		if (nla_put_u32(skb, IFLA_BRIDGE_CFM_CC_CCM_TX_IF_TLV,
				mep->cc_ccm_tx_info.if_tlv))
			goto nla_put_failure;

		if (nla_put_u8(skb, IFLA_BRIDGE_CFM_CC_CCM_TX_IF_TLV_VALUE,
			       mep->cc_ccm_tx_info.if_tlv_value))
			goto nla_put_failure;

		if (nla_put_u32(skb, IFLA_BRIDGE_CFM_CC_CCM_TX_PORT_TLV,
				mep->cc_ccm_tx_info.port_tlv))
			goto nla_put_failure;

		if (nla_put_u8(skb, IFLA_BRIDGE_CFM_CC_CCM_TX_PORT_TLV_VALUE,
			       mep->cc_ccm_tx_info.port_tlv_value))
			goto nla_put_failure;

		nla_nest_end(skb, tb);

		hlist_for_each_entry_rcu(peer_mep, &mep->peer_mep_list, head) {
			tb = nla_nest_start(skb,
					    IFLA_BRIDGE_CFM_CC_PEER_MEP_INFO);

			if (!tb)
				goto nla_info_failure;

			if (nla_put_u32(skb,
					IFLA_BRIDGE_CFM_CC_PEER_MEP_INSTANCE,
					mep->instance))
				goto nla_put_failure;

			if (nla_put_u32(skb, IFLA_BRIDGE_CFM_CC_PEER_MEPID,
					peer_mep->mepid))
				goto nla_put_failure;

			nla_nest_end(skb, tb);
		}
	}

	return 0;

nla_put_failure:
	nla_nest_cancel(skb, tb);

nla_info_failure:
	return -EMSGSIZE;
}

int br_cfm_status_fill_info(struct sk_buff *skb,
			    struct net_bridge *br,
			    bool getlink)
{
	struct br_cfm_peer_mep *peer_mep;
	struct br_cfm_mep *mep;
	struct nlattr *tb;

	hlist_for_each_entry_rcu(mep, &br->mep_list, head) {
		tb = nla_nest_start(skb, IFLA_BRIDGE_CFM_MEP_STATUS_INFO);
		if (!tb)
			goto nla_info_failure;

		if (nla_put_u32(skb, IFLA_BRIDGE_CFM_MEP_STATUS_INSTANCE,
				mep->instance))
			goto nla_put_failure;

		if (nla_put_u32(skb,
				IFLA_BRIDGE_CFM_MEP_STATUS_OPCODE_UNEXP_SEEN,
				mep->status.opcode_unexp_seen))
			goto nla_put_failure;

		if (nla_put_u32(skb,
				IFLA_BRIDGE_CFM_MEP_STATUS_VERSION_UNEXP_SEEN,
				mep->status.version_unexp_seen))
			goto nla_put_failure;

		if (nla_put_u32(skb,
				IFLA_BRIDGE_CFM_MEP_STATUS_RX_LEVEL_LOW_SEEN,
				mep->status.rx_level_low_seen))
			goto nla_put_failure;

		/* Only clear if this is a GETLINK */
		if (getlink) {
			/* Clear all 'seen' indications */
			mep->status.opcode_unexp_seen = false;
			mep->status.version_unexp_seen = false;
			mep->status.rx_level_low_seen = false;
		}

		nla_nest_end(skb, tb);

		hlist_for_each_entry_rcu(peer_mep, &mep->peer_mep_list, head) {
			tb = nla_nest_start(skb,
					    IFLA_BRIDGE_CFM_CC_PEER_STATUS_INFO);
			if (!tb)
				goto nla_info_failure;

			if (nla_put_u32(skb,
					IFLA_BRIDGE_CFM_CC_PEER_STATUS_INSTANCE,
					mep->instance))
				goto nla_put_failure;

			if (nla_put_u32(skb,
					IFLA_BRIDGE_CFM_CC_PEER_STATUS_PEER_MEPID,
					peer_mep->mepid))
				goto nla_put_failure;

			if (nla_put_u32(skb,
					IFLA_BRIDGE_CFM_CC_PEER_STATUS_CCM_DEFECT,
					peer_mep->cc_status.ccm_defect))
				goto nla_put_failure;

			if (nla_put_u32(skb, IFLA_BRIDGE_CFM_CC_PEER_STATUS_RDI,
					peer_mep->cc_status.rdi))
				goto nla_put_failure;

			if (nla_put_u8(skb,
				       IFLA_BRIDGE_CFM_CC_PEER_STATUS_PORT_TLV_VALUE,
				       peer_mep->cc_status.port_tlv_value))
				goto nla_put_failure;

			if (nla_put_u8(skb,
				       IFLA_BRIDGE_CFM_CC_PEER_STATUS_IF_TLV_VALUE,
				       peer_mep->cc_status.if_tlv_value))
				goto nla_put_failure;

			if (nla_put_u32(skb,
					IFLA_BRIDGE_CFM_CC_PEER_STATUS_SEEN,
					peer_mep->cc_status.seen))
				goto nla_put_failure;

			if (nla_put_u32(skb,
					IFLA_BRIDGE_CFM_CC_PEER_STATUS_TLV_SEEN,
					peer_mep->cc_status.tlv_seen))
				goto nla_put_failure;

			if (nla_put_u32(skb,
					IFLA_BRIDGE_CFM_CC_PEER_STATUS_SEQ_UNEXP_SEEN,
					peer_mep->cc_status.seq_unexp_seen))
				goto nla_put_failure;

			if (getlink) { /* Only clear if this is a GETLINK */
				/* Clear all 'seen' indications */
				peer_mep->cc_status.seen = false;
				peer_mep->cc_status.tlv_seen = false;
				peer_mep->cc_status.seq_unexp_seen = false;
			}

			nla_nest_end(skb, tb);
		}
	}

	return 0;

nla_put_failure:
	nla_nest_cancel(skb, tb);

nla_info_failure:
	return -EMSGSIZE;
}
