// SPDX-License-Identifier: GPL-2.0-or-later

#include <net/genetlink.h>

#include <uapi/linux/mrp_bridge.h>
#include "br_private.h"
#include "br_private_mrp.h"

static const struct nla_policy br_mrp_policy[IFLA_BRIDGE_MRP_MAX + 1] = {
	[IFLA_BRIDGE_MRP_UNSPEC]	= { .type = NLA_REJECT },
	[IFLA_BRIDGE_MRP_INSTANCE]	= { .type = NLA_NESTED },
	[IFLA_BRIDGE_MRP_PORT_STATE]	= { .type = NLA_NESTED },
	[IFLA_BRIDGE_MRP_PORT_ROLE]	= { .type = NLA_NESTED },
	[IFLA_BRIDGE_MRP_RING_STATE]	= { .type = NLA_NESTED },
	[IFLA_BRIDGE_MRP_RING_ROLE]	= { .type = NLA_NESTED },
	[IFLA_BRIDGE_MRP_START_TEST]	= { .type = NLA_NESTED },
	[IFLA_BRIDGE_MRP_IN_ROLE]	= { .type = NLA_NESTED },
	[IFLA_BRIDGE_MRP_IN_STATE]	= { .type = NLA_NESTED },
	[IFLA_BRIDGE_MRP_START_IN_TEST]	= { .type = NLA_NESTED },
};

static const struct nla_policy
br_mrp_instance_policy[IFLA_BRIDGE_MRP_INSTANCE_MAX + 1] = {
	[IFLA_BRIDGE_MRP_INSTANCE_UNSPEC]	= { .type = NLA_REJECT },
	[IFLA_BRIDGE_MRP_INSTANCE_RING_ID]	= { .type = NLA_U32 },
	[IFLA_BRIDGE_MRP_INSTANCE_P_IFINDEX]	= { .type = NLA_U32 },
	[IFLA_BRIDGE_MRP_INSTANCE_S_IFINDEX]	= { .type = NLA_U32 },
	[IFLA_BRIDGE_MRP_INSTANCE_PRIO]		= { .type = NLA_U16 },
};

static int br_mrp_instance_parse(struct net_bridge *br, struct nlattr *attr,
				 int cmd, struct netlink_ext_ack *extack)
{
	struct nlattr *tb[IFLA_BRIDGE_MRP_INSTANCE_MAX + 1];
	struct br_mrp_instance inst;
	int err;

	err = nla_parse_nested(tb, IFLA_BRIDGE_MRP_INSTANCE_MAX, attr,
			       br_mrp_instance_policy, extack);
	if (err)
		return err;

	if (!tb[IFLA_BRIDGE_MRP_INSTANCE_RING_ID] ||
	    !tb[IFLA_BRIDGE_MRP_INSTANCE_P_IFINDEX] ||
	    !tb[IFLA_BRIDGE_MRP_INSTANCE_S_IFINDEX]) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Missing attribute: RING_ID or P_IFINDEX or S_IFINDEX");
		return -EINVAL;
	}

	memset(&inst, 0, sizeof(inst));

	inst.ring_id = nla_get_u32(tb[IFLA_BRIDGE_MRP_INSTANCE_RING_ID]);
	inst.p_ifindex = nla_get_u32(tb[IFLA_BRIDGE_MRP_INSTANCE_P_IFINDEX]);
	inst.s_ifindex = nla_get_u32(tb[IFLA_BRIDGE_MRP_INSTANCE_S_IFINDEX]);
	inst.prio = MRP_DEFAULT_PRIO;

	if (tb[IFLA_BRIDGE_MRP_INSTANCE_PRIO])
		inst.prio = nla_get_u16(tb[IFLA_BRIDGE_MRP_INSTANCE_PRIO]);

	if (cmd == RTM_SETLINK)
		return br_mrp_add(br, &inst);
	else
		return br_mrp_del(br, &inst);

	return 0;
}

static const struct nla_policy
br_mrp_port_state_policy[IFLA_BRIDGE_MRP_PORT_STATE_MAX + 1] = {
	[IFLA_BRIDGE_MRP_PORT_STATE_UNSPEC]	= { .type = NLA_REJECT },
	[IFLA_BRIDGE_MRP_PORT_STATE_STATE]	= { .type = NLA_U32 },
};

static int br_mrp_port_state_parse(struct net_bridge_port *p,
				   struct nlattr *attr,
				   struct netlink_ext_ack *extack)
{
	struct nlattr *tb[IFLA_BRIDGE_MRP_PORT_STATE_MAX + 1];
	enum br_mrp_port_state_type state;
	int err;

	err = nla_parse_nested(tb, IFLA_BRIDGE_MRP_PORT_STATE_MAX, attr,
			       br_mrp_port_state_policy, extack);
	if (err)
		return err;

	if (!tb[IFLA_BRIDGE_MRP_PORT_STATE_STATE]) {
		NL_SET_ERR_MSG_MOD(extack, "Missing attribute: STATE");
		return -EINVAL;
	}

	state = nla_get_u32(tb[IFLA_BRIDGE_MRP_PORT_STATE_STATE]);

	return br_mrp_set_port_state(p, state);
}

static const struct nla_policy
br_mrp_port_role_policy[IFLA_BRIDGE_MRP_PORT_ROLE_MAX + 1] = {
	[IFLA_BRIDGE_MRP_PORT_ROLE_UNSPEC]	= { .type = NLA_REJECT },
	[IFLA_BRIDGE_MRP_PORT_ROLE_ROLE]	= { .type = NLA_U32 },
};

static int br_mrp_port_role_parse(struct net_bridge_port *p,
				  struct nlattr *attr,
				  struct netlink_ext_ack *extack)
{
	struct nlattr *tb[IFLA_BRIDGE_MRP_PORT_ROLE_MAX + 1];
	enum br_mrp_port_role_type role;
	int err;

	err = nla_parse_nested(tb, IFLA_BRIDGE_MRP_PORT_ROLE_MAX, attr,
			       br_mrp_port_role_policy, extack);
	if (err)
		return err;

	if (!tb[IFLA_BRIDGE_MRP_PORT_ROLE_ROLE]) {
		NL_SET_ERR_MSG_MOD(extack, "Missing attribute: ROLE");
		return -EINVAL;
	}

	role = nla_get_u32(tb[IFLA_BRIDGE_MRP_PORT_ROLE_ROLE]);

	return br_mrp_set_port_role(p, role);
}

static const struct nla_policy
br_mrp_ring_state_policy[IFLA_BRIDGE_MRP_RING_STATE_MAX + 1] = {
	[IFLA_BRIDGE_MRP_RING_STATE_UNSPEC]	= { .type = NLA_REJECT },
	[IFLA_BRIDGE_MRP_RING_STATE_RING_ID]	= { .type = NLA_U32 },
	[IFLA_BRIDGE_MRP_RING_STATE_STATE]	= { .type = NLA_U32 },
};

static int br_mrp_ring_state_parse(struct net_bridge *br, struct nlattr *attr,
				   struct netlink_ext_ack *extack)
{
	struct nlattr *tb[IFLA_BRIDGE_MRP_RING_STATE_MAX + 1];
	struct br_mrp_ring_state state;
	int err;

	err = nla_parse_nested(tb, IFLA_BRIDGE_MRP_RING_STATE_MAX, attr,
			       br_mrp_ring_state_policy, extack);
	if (err)
		return err;

	if (!tb[IFLA_BRIDGE_MRP_RING_STATE_RING_ID] ||
	    !tb[IFLA_BRIDGE_MRP_RING_STATE_STATE]) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Missing attribute: RING_ID or STATE");
		return -EINVAL;
	}

	memset(&state, 0x0, sizeof(state));

	state.ring_id = nla_get_u32(tb[IFLA_BRIDGE_MRP_RING_STATE_RING_ID]);
	state.ring_state = nla_get_u32(tb[IFLA_BRIDGE_MRP_RING_STATE_STATE]);

	return br_mrp_set_ring_state(br, &state);
}

static const struct nla_policy
br_mrp_ring_role_policy[IFLA_BRIDGE_MRP_RING_ROLE_MAX + 1] = {
	[IFLA_BRIDGE_MRP_RING_ROLE_UNSPEC]	= { .type = NLA_REJECT },
	[IFLA_BRIDGE_MRP_RING_ROLE_RING_ID]	= { .type = NLA_U32 },
	[IFLA_BRIDGE_MRP_RING_ROLE_ROLE]	= { .type = NLA_U32 },
};

static int br_mrp_ring_role_parse(struct net_bridge *br, struct nlattr *attr,
				  struct netlink_ext_ack *extack)
{
	struct nlattr *tb[IFLA_BRIDGE_MRP_RING_ROLE_MAX + 1];
	struct br_mrp_ring_role role;
	int err;

	err = nla_parse_nested(tb, IFLA_BRIDGE_MRP_RING_ROLE_MAX, attr,
			       br_mrp_ring_role_policy, extack);
	if (err)
		return err;

	if (!tb[IFLA_BRIDGE_MRP_RING_ROLE_RING_ID] ||
	    !tb[IFLA_BRIDGE_MRP_RING_ROLE_ROLE]) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Missing attribute: RING_ID or ROLE");
		return -EINVAL;
	}

	memset(&role, 0x0, sizeof(role));

	role.ring_id = nla_get_u32(tb[IFLA_BRIDGE_MRP_RING_ROLE_RING_ID]);
	role.ring_role = nla_get_u32(tb[IFLA_BRIDGE_MRP_RING_ROLE_ROLE]);

	return br_mrp_set_ring_role(br, &role);
}

static const struct nla_policy
br_mrp_start_test_policy[IFLA_BRIDGE_MRP_START_TEST_MAX + 1] = {
	[IFLA_BRIDGE_MRP_START_TEST_UNSPEC]	= { .type = NLA_REJECT },
	[IFLA_BRIDGE_MRP_START_TEST_RING_ID]	= { .type = NLA_U32 },
	[IFLA_BRIDGE_MRP_START_TEST_INTERVAL]	= { .type = NLA_U32 },
	[IFLA_BRIDGE_MRP_START_TEST_MAX_MISS]	= { .type = NLA_U32 },
	[IFLA_BRIDGE_MRP_START_TEST_PERIOD]	= { .type = NLA_U32 },
	[IFLA_BRIDGE_MRP_START_TEST_MONITOR]	= { .type = NLA_U32 },
};

static int br_mrp_start_test_parse(struct net_bridge *br, struct nlattr *attr,
				   struct netlink_ext_ack *extack)
{
	struct nlattr *tb[IFLA_BRIDGE_MRP_START_TEST_MAX + 1];
	struct br_mrp_start_test test;
	int err;

	err = nla_parse_nested(tb, IFLA_BRIDGE_MRP_START_TEST_MAX, attr,
			       br_mrp_start_test_policy, extack);
	if (err)
		return err;

	if (!tb[IFLA_BRIDGE_MRP_START_TEST_RING_ID] ||
	    !tb[IFLA_BRIDGE_MRP_START_TEST_INTERVAL] ||
	    !tb[IFLA_BRIDGE_MRP_START_TEST_MAX_MISS] ||
	    !tb[IFLA_BRIDGE_MRP_START_TEST_PERIOD]) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Missing attribute: RING_ID or INTERVAL or MAX_MISS or PERIOD");
		return -EINVAL;
	}

	memset(&test, 0x0, sizeof(test));

	test.ring_id = nla_get_u32(tb[IFLA_BRIDGE_MRP_START_TEST_RING_ID]);
	test.interval = nla_get_u32(tb[IFLA_BRIDGE_MRP_START_TEST_INTERVAL]);
	test.max_miss = nla_get_u32(tb[IFLA_BRIDGE_MRP_START_TEST_MAX_MISS]);
	test.period = nla_get_u32(tb[IFLA_BRIDGE_MRP_START_TEST_PERIOD]);
	test.monitor = false;

	if (tb[IFLA_BRIDGE_MRP_START_TEST_MONITOR])
		test.monitor =
			nla_get_u32(tb[IFLA_BRIDGE_MRP_START_TEST_MONITOR]);

	return br_mrp_start_test(br, &test);
}

static const struct nla_policy
br_mrp_in_state_policy[IFLA_BRIDGE_MRP_IN_STATE_MAX + 1] = {
	[IFLA_BRIDGE_MRP_IN_STATE_UNSPEC]	= { .type = NLA_REJECT },
	[IFLA_BRIDGE_MRP_IN_STATE_IN_ID]	= { .type = NLA_U32 },
	[IFLA_BRIDGE_MRP_IN_STATE_STATE]	= { .type = NLA_U32 },
};

static int br_mrp_in_state_parse(struct net_bridge *br, struct nlattr *attr,
				 struct netlink_ext_ack *extack)
{
	struct nlattr *tb[IFLA_BRIDGE_MRP_IN_STATE_MAX + 1];
	struct br_mrp_in_state state;
	int err;

	err = nla_parse_nested(tb, IFLA_BRIDGE_MRP_IN_STATE_MAX, attr,
			       br_mrp_in_state_policy, extack);
	if (err)
		return err;

	if (!tb[IFLA_BRIDGE_MRP_IN_STATE_IN_ID] ||
	    !tb[IFLA_BRIDGE_MRP_IN_STATE_STATE]) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Missing attribute: IN_ID or STATE");
		return -EINVAL;
	}

	memset(&state, 0x0, sizeof(state));

	state.in_id = nla_get_u32(tb[IFLA_BRIDGE_MRP_IN_STATE_IN_ID]);
	state.in_state = nla_get_u32(tb[IFLA_BRIDGE_MRP_IN_STATE_STATE]);

	return br_mrp_set_in_state(br, &state);
}

static const struct nla_policy
br_mrp_in_role_policy[IFLA_BRIDGE_MRP_IN_ROLE_MAX + 1] = {
	[IFLA_BRIDGE_MRP_IN_ROLE_UNSPEC]	= { .type = NLA_REJECT },
	[IFLA_BRIDGE_MRP_IN_ROLE_RING_ID]	= { .type = NLA_U32 },
	[IFLA_BRIDGE_MRP_IN_ROLE_IN_ID]		= { .type = NLA_U16 },
	[IFLA_BRIDGE_MRP_IN_ROLE_ROLE]		= { .type = NLA_U32 },
	[IFLA_BRIDGE_MRP_IN_ROLE_I_IFINDEX]	= { .type = NLA_U32 },
};

static int br_mrp_in_role_parse(struct net_bridge *br, struct nlattr *attr,
				struct netlink_ext_ack *extack)
{
	struct nlattr *tb[IFLA_BRIDGE_MRP_IN_ROLE_MAX + 1];
	struct br_mrp_in_role role;
	int err;

	err = nla_parse_nested(tb, IFLA_BRIDGE_MRP_IN_ROLE_MAX, attr,
			       br_mrp_in_role_policy, extack);
	if (err)
		return err;

	if (!tb[IFLA_BRIDGE_MRP_IN_ROLE_RING_ID] ||
	    !tb[IFLA_BRIDGE_MRP_IN_ROLE_IN_ID] ||
	    !tb[IFLA_BRIDGE_MRP_IN_ROLE_I_IFINDEX] ||
	    !tb[IFLA_BRIDGE_MRP_IN_ROLE_ROLE]) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Missing attribute: RING_ID or ROLE or IN_ID or I_IFINDEX");
		return -EINVAL;
	}

	memset(&role, 0x0, sizeof(role));

	role.ring_id = nla_get_u32(tb[IFLA_BRIDGE_MRP_IN_ROLE_RING_ID]);
	role.in_id = nla_get_u16(tb[IFLA_BRIDGE_MRP_IN_ROLE_IN_ID]);
	role.i_ifindex = nla_get_u32(tb[IFLA_BRIDGE_MRP_IN_ROLE_I_IFINDEX]);
	role.in_role = nla_get_u32(tb[IFLA_BRIDGE_MRP_IN_ROLE_ROLE]);

	return br_mrp_set_in_role(br, &role);
}

static const struct nla_policy
br_mrp_start_in_test_policy[IFLA_BRIDGE_MRP_START_IN_TEST_MAX + 1] = {
	[IFLA_BRIDGE_MRP_START_IN_TEST_UNSPEC]	= { .type = NLA_REJECT },
	[IFLA_BRIDGE_MRP_START_IN_TEST_IN_ID]	= { .type = NLA_U32 },
	[IFLA_BRIDGE_MRP_START_IN_TEST_INTERVAL]	= { .type = NLA_U32 },
	[IFLA_BRIDGE_MRP_START_IN_TEST_MAX_MISS]	= { .type = NLA_U32 },
	[IFLA_BRIDGE_MRP_START_IN_TEST_PERIOD]	= { .type = NLA_U32 },
};

static int br_mrp_start_in_test_parse(struct net_bridge *br,
				      struct nlattr *attr,
				      struct netlink_ext_ack *extack)
{
	struct nlattr *tb[IFLA_BRIDGE_MRP_START_IN_TEST_MAX + 1];
	struct br_mrp_start_in_test test;
	int err;

	err = nla_parse_nested(tb, IFLA_BRIDGE_MRP_START_IN_TEST_MAX, attr,
			       br_mrp_start_in_test_policy, extack);
	if (err)
		return err;

	if (!tb[IFLA_BRIDGE_MRP_START_IN_TEST_IN_ID] ||
	    !tb[IFLA_BRIDGE_MRP_START_IN_TEST_INTERVAL] ||
	    !tb[IFLA_BRIDGE_MRP_START_IN_TEST_MAX_MISS] ||
	    !tb[IFLA_BRIDGE_MRP_START_IN_TEST_PERIOD]) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Missing attribute: RING_ID or INTERVAL or MAX_MISS or PERIOD");
		return -EINVAL;
	}

	memset(&test, 0x0, sizeof(test));

	test.in_id = nla_get_u32(tb[IFLA_BRIDGE_MRP_START_IN_TEST_IN_ID]);
	test.interval = nla_get_u32(tb[IFLA_BRIDGE_MRP_START_IN_TEST_INTERVAL]);
	test.max_miss = nla_get_u32(tb[IFLA_BRIDGE_MRP_START_IN_TEST_MAX_MISS]);
	test.period = nla_get_u32(tb[IFLA_BRIDGE_MRP_START_IN_TEST_PERIOD]);

	return br_mrp_start_in_test(br, &test);
}

int br_mrp_parse(struct net_bridge *br, struct net_bridge_port *p,
		 struct nlattr *attr, int cmd, struct netlink_ext_ack *extack)
{
	struct nlattr *tb[IFLA_BRIDGE_MRP_MAX + 1];
	int err;

	/* When this function is called for a port then the br pointer is
	 * invalid, therefor set the br to point correctly
	 */
	if (p)
		br = p->br;

	if (br->stp_enabled != BR_NO_STP) {
		NL_SET_ERR_MSG_MOD(extack, "MRP can't be enabled if STP is already enabled");
		return -EINVAL;
	}

	err = nla_parse_nested(tb, IFLA_BRIDGE_MRP_MAX, attr,
			       br_mrp_policy, extack);
	if (err)
		return err;

	if (tb[IFLA_BRIDGE_MRP_INSTANCE]) {
		err = br_mrp_instance_parse(br, tb[IFLA_BRIDGE_MRP_INSTANCE],
					    cmd, extack);
		if (err)
			return err;
	}

	if (tb[IFLA_BRIDGE_MRP_PORT_STATE]) {
		err = br_mrp_port_state_parse(p, tb[IFLA_BRIDGE_MRP_PORT_STATE],
					      extack);
		if (err)
			return err;
	}

	if (tb[IFLA_BRIDGE_MRP_PORT_ROLE]) {
		err = br_mrp_port_role_parse(p, tb[IFLA_BRIDGE_MRP_PORT_ROLE],
					     extack);
		if (err)
			return err;
	}

	if (tb[IFLA_BRIDGE_MRP_RING_STATE]) {
		err = br_mrp_ring_state_parse(br,
					      tb[IFLA_BRIDGE_MRP_RING_STATE],
					      extack);
		if (err)
			return err;
	}

	if (tb[IFLA_BRIDGE_MRP_RING_ROLE]) {
		err = br_mrp_ring_role_parse(br, tb[IFLA_BRIDGE_MRP_RING_ROLE],
					     extack);
		if (err)
			return err;
	}

	if (tb[IFLA_BRIDGE_MRP_START_TEST]) {
		err = br_mrp_start_test_parse(br,
					      tb[IFLA_BRIDGE_MRP_START_TEST],
					      extack);
		if (err)
			return err;
	}

	if (tb[IFLA_BRIDGE_MRP_IN_STATE]) {
		err = br_mrp_in_state_parse(br, tb[IFLA_BRIDGE_MRP_IN_STATE],
					    extack);
		if (err)
			return err;
	}

	if (tb[IFLA_BRIDGE_MRP_IN_ROLE]) {
		err = br_mrp_in_role_parse(br, tb[IFLA_BRIDGE_MRP_IN_ROLE],
					   extack);
		if (err)
			return err;
	}

	if (tb[IFLA_BRIDGE_MRP_START_IN_TEST]) {
		err = br_mrp_start_in_test_parse(br,
						 tb[IFLA_BRIDGE_MRP_START_IN_TEST],
						 extack);
		if (err)
			return err;
	}

	return 0;
}

int br_mrp_fill_info(struct sk_buff *skb, struct net_bridge *br)
{
	struct nlattr *tb, *mrp_tb;
	struct br_mrp *mrp;

	mrp_tb = nla_nest_start_noflag(skb, IFLA_BRIDGE_MRP);
	if (!mrp_tb)
		return -EMSGSIZE;

	hlist_for_each_entry_rcu(mrp, &br->mrp_list, list) {
		struct net_bridge_port *p;

		tb = nla_nest_start_noflag(skb, IFLA_BRIDGE_MRP_INFO);
		if (!tb)
			goto nla_info_failure;

		if (nla_put_u32(skb, IFLA_BRIDGE_MRP_INFO_RING_ID,
				mrp->ring_id))
			goto nla_put_failure;

		p = rcu_dereference(mrp->p_port);
		if (p && nla_put_u32(skb, IFLA_BRIDGE_MRP_INFO_P_IFINDEX,
				     p->dev->ifindex))
			goto nla_put_failure;

		p = rcu_dereference(mrp->s_port);
		if (p && nla_put_u32(skb, IFLA_BRIDGE_MRP_INFO_S_IFINDEX,
				     p->dev->ifindex))
			goto nla_put_failure;

		p = rcu_dereference(mrp->i_port);
		if (p && nla_put_u32(skb, IFLA_BRIDGE_MRP_INFO_I_IFINDEX,
				     p->dev->ifindex))
			goto nla_put_failure;

		if (nla_put_u16(skb, IFLA_BRIDGE_MRP_INFO_PRIO,
				mrp->prio))
			goto nla_put_failure;
		if (nla_put_u32(skb, IFLA_BRIDGE_MRP_INFO_RING_STATE,
				mrp->ring_state))
			goto nla_put_failure;
		if (nla_put_u32(skb, IFLA_BRIDGE_MRP_INFO_RING_ROLE,
				mrp->ring_role))
			goto nla_put_failure;
		if (nla_put_u32(skb, IFLA_BRIDGE_MRP_INFO_TEST_INTERVAL,
				mrp->test_interval))
			goto nla_put_failure;
		if (nla_put_u32(skb, IFLA_BRIDGE_MRP_INFO_TEST_MAX_MISS,
				mrp->test_max_miss))
			goto nla_put_failure;
		if (nla_put_u32(skb, IFLA_BRIDGE_MRP_INFO_TEST_MONITOR,
				mrp->test_monitor))
			goto nla_put_failure;

		if (nla_put_u32(skb, IFLA_BRIDGE_MRP_INFO_IN_STATE,
				mrp->in_state))
			goto nla_put_failure;
		if (nla_put_u32(skb, IFLA_BRIDGE_MRP_INFO_IN_ROLE,
				mrp->in_role))
			goto nla_put_failure;
		if (nla_put_u32(skb, IFLA_BRIDGE_MRP_INFO_IN_TEST_INTERVAL,
				mrp->in_test_interval))
			goto nla_put_failure;
		if (nla_put_u32(skb, IFLA_BRIDGE_MRP_INFO_IN_TEST_MAX_MISS,
				mrp->in_test_max_miss))
			goto nla_put_failure;

		nla_nest_end(skb, tb);
	}
	nla_nest_end(skb, mrp_tb);

	return 0;

nla_put_failure:
	nla_nest_cancel(skb, tb);

nla_info_failure:
	nla_nest_cancel(skb, mrp_tb);

	return -EMSGSIZE;
}

int br_mrp_ring_port_open(struct net_device *dev, u8 loc)
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

int br_mrp_in_port_open(struct net_device *dev, u8 loc)
{
	struct net_bridge_port *p;
	int err = 0;

	p = br_port_get_rcu(dev);
	if (!p) {
		err = -EINVAL;
		goto out;
	}

	if (loc)
		p->flags |= BR_MRP_LOST_IN_CONT;
	else
		p->flags &= ~BR_MRP_LOST_IN_CONT;

	br_ifinfo_notify(RTM_NEWLINK, NULL, p);

out:
	return err;
}
