// SPDX-License-Identifier: GPL-2.0-or-later

#include <net/genetlink.h>

#include <uapi/linux/mrp_bridge.h>
#include "br_private.h"
#include "br_private_mrp.h"

static const struct nla_policy br_mrp_policy[IFLA_BRIDGE_MRP_MAX + 1] = {
	[IFLA_BRIDGE_MRP_UNSPEC]	= { .type = NLA_REJECT },
	[IFLA_BRIDGE_MRP_INSTANCE]	= { .type = NLA_EXACT_LEN,
				    .len = sizeof(struct br_mrp_instance)},
	[IFLA_BRIDGE_MRP_PORT_STATE]	= { .type = NLA_U32 },
	[IFLA_BRIDGE_MRP_PORT_ROLE]	= { .type = NLA_EXACT_LEN,
				    .len = sizeof(struct br_mrp_port_role)},
	[IFLA_BRIDGE_MRP_RING_STATE]	= { .type = NLA_EXACT_LEN,
				    .len = sizeof(struct br_mrp_ring_state)},
	[IFLA_BRIDGE_MRP_RING_ROLE]	= { .type = NLA_EXACT_LEN,
				    .len = sizeof(struct br_mrp_ring_role)},
	[IFLA_BRIDGE_MRP_START_TEST]	= { .type = NLA_EXACT_LEN,
				    .len = sizeof(struct br_mrp_start_test)},
};

int br_mrp_parse(struct net_bridge *br, struct net_bridge_port *p,
		 struct nlattr *attr, int cmd, struct netlink_ext_ack *extack)
{
	struct nlattr *tb[IFLA_BRIDGE_MRP_MAX + 1];
	int err;

	if (br->stp_enabled != BR_NO_STP) {
		NL_SET_ERR_MSG_MOD(extack, "MRP can't be enabled if STP is already enabled");
		return -EINVAL;
	}

	err = nla_parse_nested(tb, IFLA_BRIDGE_MRP_MAX, attr,
			       br_mrp_policy, extack);
	if (err)
		return err;

	if (tb[IFLA_BRIDGE_MRP_INSTANCE]) {
		struct br_mrp_instance *instance =
			nla_data(tb[IFLA_BRIDGE_MRP_INSTANCE]);

		if (cmd == RTM_SETLINK)
			err = br_mrp_add(br, instance);
		else
			err = br_mrp_del(br, instance);
		if (err)
			return err;
	}

	if (tb[IFLA_BRIDGE_MRP_PORT_STATE]) {
		enum br_mrp_port_state_type state =
			nla_get_u32(tb[IFLA_BRIDGE_MRP_PORT_STATE]);

		err = br_mrp_set_port_state(p, state);
		if (err)
			return err;
	}

	if (tb[IFLA_BRIDGE_MRP_PORT_ROLE]) {
		struct br_mrp_port_role *role =
			nla_data(tb[IFLA_BRIDGE_MRP_PORT_ROLE]);

		err = br_mrp_set_port_role(p, role);
		if (err)
			return err;
	}

	if (tb[IFLA_BRIDGE_MRP_RING_STATE]) {
		struct br_mrp_ring_state *state =
			nla_data(tb[IFLA_BRIDGE_MRP_RING_STATE]);

		err = br_mrp_set_ring_state(br, state);
		if (err)
			return err;
	}

	if (tb[IFLA_BRIDGE_MRP_RING_ROLE]) {
		struct br_mrp_ring_role *role =
			nla_data(tb[IFLA_BRIDGE_MRP_RING_ROLE]);

		err = br_mrp_set_ring_role(br, role);
		if (err)
			return err;
	}

	if (tb[IFLA_BRIDGE_MRP_START_TEST]) {
		struct br_mrp_start_test *test =
			nla_data(tb[IFLA_BRIDGE_MRP_START_TEST]);

		err = br_mrp_start_test(br, test);
		if (err)
			return err;
	}

	return 0;
}

int br_mrp_port_open(struct net_device *dev, u8 loc)
{
	struct net_bridge_port *p;
	int err = 0;

	p = br_port_get_rcu(dev);
	if (!p) {
		err = -EINVAL;
		goto out;
	}

	if (loc)
		p->flags |= BR_MRP_LOST_CONT;
	else
		p->flags &= ~BR_MRP_LOST_CONT;

	br_ifinfo_notify(RTM_NEWLINK, NULL, p);

out:
	return err;
}
