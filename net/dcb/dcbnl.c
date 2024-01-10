// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2008-2011, Intel Corporation.
 *
 * Description: Data Center Bridging netlink interface
 * Author: Lucy Liu <lucy.liu@intel.com>
 */

#include <linux/netdevice.h>
#include <linux/netlink.h>
#include <linux/slab.h>
#include <net/netlink.h>
#include <net/rtnetlink.h>
#include <linux/dcbnl.h>
#include <net/dcbevent.h>
#include <linux/rtnetlink.h>
#include <linux/init.h>
#include <net/sock.h>

/* Data Center Bridging (DCB) is a collection of Ethernet enhancements
 * intended to allow network traffic with differing requirements
 * (highly reliable, no drops vs. best effort vs. low latency) to operate
 * and co-exist on Ethernet.  Current DCB features are:
 *
 * Enhanced Transmission Selection (aka Priority Grouping [PG]) - provides a
 *   framework for assigning bandwidth guarantees to traffic classes.
 *
 * Priority-based Flow Control (PFC) - provides a flow control mechanism which
 *   can work independently for each 802.1p priority.
 *
 * Congestion Notification - provides a mechanism for end-to-end congestion
 *   control for protocols which do not have built-in congestion management.
 *
 * More information about the emerging standards for these Ethernet features
 * can be found at: http://www.ieee802.org/1/pages/dcbridges.html
 *
 * This file implements an rtnetlink interface to allow configuration of DCB
 * features for capable devices.
 */

/**************** DCB attribute policies *************************************/

/* DCB netlink attributes policy */
static const struct nla_policy dcbnl_rtnl_policy[DCB_ATTR_MAX + 1] = {
	[DCB_ATTR_IFNAME]      = {.type = NLA_NUL_STRING, .len = IFNAMSIZ - 1},
	[DCB_ATTR_STATE]       = {.type = NLA_U8},
	[DCB_ATTR_PFC_CFG]     = {.type = NLA_NESTED},
	[DCB_ATTR_PG_CFG]      = {.type = NLA_NESTED},
	[DCB_ATTR_SET_ALL]     = {.type = NLA_U8},
	[DCB_ATTR_PERM_HWADDR] = {.type = NLA_FLAG},
	[DCB_ATTR_CAP]         = {.type = NLA_NESTED},
	[DCB_ATTR_PFC_STATE]   = {.type = NLA_U8},
	[DCB_ATTR_BCN]         = {.type = NLA_NESTED},
	[DCB_ATTR_APP]         = {.type = NLA_NESTED},
	[DCB_ATTR_IEEE]	       = {.type = NLA_NESTED},
	[DCB_ATTR_DCBX]        = {.type = NLA_U8},
	[DCB_ATTR_FEATCFG]     = {.type = NLA_NESTED},
};

/* DCB priority flow control to User Priority nested attributes */
static const struct nla_policy dcbnl_pfc_up_nest[DCB_PFC_UP_ATTR_MAX + 1] = {
	[DCB_PFC_UP_ATTR_0]   = {.type = NLA_U8},
	[DCB_PFC_UP_ATTR_1]   = {.type = NLA_U8},
	[DCB_PFC_UP_ATTR_2]   = {.type = NLA_U8},
	[DCB_PFC_UP_ATTR_3]   = {.type = NLA_U8},
	[DCB_PFC_UP_ATTR_4]   = {.type = NLA_U8},
	[DCB_PFC_UP_ATTR_5]   = {.type = NLA_U8},
	[DCB_PFC_UP_ATTR_6]   = {.type = NLA_U8},
	[DCB_PFC_UP_ATTR_7]   = {.type = NLA_U8},
	[DCB_PFC_UP_ATTR_ALL] = {.type = NLA_FLAG},
};

/* DCB priority grouping nested attributes */
static const struct nla_policy dcbnl_pg_nest[DCB_PG_ATTR_MAX + 1] = {
	[DCB_PG_ATTR_TC_0]      = {.type = NLA_NESTED},
	[DCB_PG_ATTR_TC_1]      = {.type = NLA_NESTED},
	[DCB_PG_ATTR_TC_2]      = {.type = NLA_NESTED},
	[DCB_PG_ATTR_TC_3]      = {.type = NLA_NESTED},
	[DCB_PG_ATTR_TC_4]      = {.type = NLA_NESTED},
	[DCB_PG_ATTR_TC_5]      = {.type = NLA_NESTED},
	[DCB_PG_ATTR_TC_6]      = {.type = NLA_NESTED},
	[DCB_PG_ATTR_TC_7]      = {.type = NLA_NESTED},
	[DCB_PG_ATTR_TC_ALL]    = {.type = NLA_NESTED},
	[DCB_PG_ATTR_BW_ID_0]   = {.type = NLA_U8},
	[DCB_PG_ATTR_BW_ID_1]   = {.type = NLA_U8},
	[DCB_PG_ATTR_BW_ID_2]   = {.type = NLA_U8},
	[DCB_PG_ATTR_BW_ID_3]   = {.type = NLA_U8},
	[DCB_PG_ATTR_BW_ID_4]   = {.type = NLA_U8},
	[DCB_PG_ATTR_BW_ID_5]   = {.type = NLA_U8},
	[DCB_PG_ATTR_BW_ID_6]   = {.type = NLA_U8},
	[DCB_PG_ATTR_BW_ID_7]   = {.type = NLA_U8},
	[DCB_PG_ATTR_BW_ID_ALL] = {.type = NLA_FLAG},
};

/* DCB traffic class nested attributes. */
static const struct nla_policy dcbnl_tc_param_nest[DCB_TC_ATTR_PARAM_MAX + 1] = {
	[DCB_TC_ATTR_PARAM_PGID]            = {.type = NLA_U8},
	[DCB_TC_ATTR_PARAM_UP_MAPPING]      = {.type = NLA_U8},
	[DCB_TC_ATTR_PARAM_STRICT_PRIO]     = {.type = NLA_U8},
	[DCB_TC_ATTR_PARAM_BW_PCT]          = {.type = NLA_U8},
	[DCB_TC_ATTR_PARAM_ALL]             = {.type = NLA_FLAG},
};

/* DCB capabilities nested attributes. */
static const struct nla_policy dcbnl_cap_nest[DCB_CAP_ATTR_MAX + 1] = {
	[DCB_CAP_ATTR_ALL]     = {.type = NLA_FLAG},
	[DCB_CAP_ATTR_PG]      = {.type = NLA_U8},
	[DCB_CAP_ATTR_PFC]     = {.type = NLA_U8},
	[DCB_CAP_ATTR_UP2TC]   = {.type = NLA_U8},
	[DCB_CAP_ATTR_PG_TCS]  = {.type = NLA_U8},
	[DCB_CAP_ATTR_PFC_TCS] = {.type = NLA_U8},
	[DCB_CAP_ATTR_GSP]     = {.type = NLA_U8},
	[DCB_CAP_ATTR_BCN]     = {.type = NLA_U8},
	[DCB_CAP_ATTR_DCBX]    = {.type = NLA_U8},
};

/* DCB capabilities nested attributes. */
static const struct nla_policy dcbnl_numtcs_nest[DCB_NUMTCS_ATTR_MAX + 1] = {
	[DCB_NUMTCS_ATTR_ALL]     = {.type = NLA_FLAG},
	[DCB_NUMTCS_ATTR_PG]      = {.type = NLA_U8},
	[DCB_NUMTCS_ATTR_PFC]     = {.type = NLA_U8},
};

/* DCB BCN nested attributes. */
static const struct nla_policy dcbnl_bcn_nest[DCB_BCN_ATTR_MAX + 1] = {
	[DCB_BCN_ATTR_RP_0]         = {.type = NLA_U8},
	[DCB_BCN_ATTR_RP_1]         = {.type = NLA_U8},
	[DCB_BCN_ATTR_RP_2]         = {.type = NLA_U8},
	[DCB_BCN_ATTR_RP_3]         = {.type = NLA_U8},
	[DCB_BCN_ATTR_RP_4]         = {.type = NLA_U8},
	[DCB_BCN_ATTR_RP_5]         = {.type = NLA_U8},
	[DCB_BCN_ATTR_RP_6]         = {.type = NLA_U8},
	[DCB_BCN_ATTR_RP_7]         = {.type = NLA_U8},
	[DCB_BCN_ATTR_RP_ALL]       = {.type = NLA_FLAG},
	[DCB_BCN_ATTR_BCNA_0]       = {.type = NLA_U32},
	[DCB_BCN_ATTR_BCNA_1]       = {.type = NLA_U32},
	[DCB_BCN_ATTR_ALPHA]        = {.type = NLA_U32},
	[DCB_BCN_ATTR_BETA]         = {.type = NLA_U32},
	[DCB_BCN_ATTR_GD]           = {.type = NLA_U32},
	[DCB_BCN_ATTR_GI]           = {.type = NLA_U32},
	[DCB_BCN_ATTR_TMAX]         = {.type = NLA_U32},
	[DCB_BCN_ATTR_TD]           = {.type = NLA_U32},
	[DCB_BCN_ATTR_RMIN]         = {.type = NLA_U32},
	[DCB_BCN_ATTR_W]            = {.type = NLA_U32},
	[DCB_BCN_ATTR_RD]           = {.type = NLA_U32},
	[DCB_BCN_ATTR_RU]           = {.type = NLA_U32},
	[DCB_BCN_ATTR_WRTT]         = {.type = NLA_U32},
	[DCB_BCN_ATTR_RI]           = {.type = NLA_U32},
	[DCB_BCN_ATTR_C]            = {.type = NLA_U32},
	[DCB_BCN_ATTR_ALL]          = {.type = NLA_FLAG},
};

/* DCB APP nested attributes. */
static const struct nla_policy dcbnl_app_nest[DCB_APP_ATTR_MAX + 1] = {
	[DCB_APP_ATTR_IDTYPE]       = {.type = NLA_U8},
	[DCB_APP_ATTR_ID]           = {.type = NLA_U16},
	[DCB_APP_ATTR_PRIORITY]     = {.type = NLA_U8},
};

/* IEEE 802.1Qaz nested attributes. */
static const struct nla_policy dcbnl_ieee_policy[DCB_ATTR_IEEE_MAX + 1] = {
	[DCB_ATTR_IEEE_ETS]	    = {.len = sizeof(struct ieee_ets)},
	[DCB_ATTR_IEEE_PFC]	    = {.len = sizeof(struct ieee_pfc)},
	[DCB_ATTR_IEEE_APP_TABLE]   = {.type = NLA_NESTED},
	[DCB_ATTR_IEEE_MAXRATE]   = {.len = sizeof(struct ieee_maxrate)},
	[DCB_ATTR_IEEE_QCN]         = {.len = sizeof(struct ieee_qcn)},
	[DCB_ATTR_IEEE_QCN_STATS]   = {.len = sizeof(struct ieee_qcn_stats)},
	[DCB_ATTR_DCB_BUFFER]       = {.len = sizeof(struct dcbnl_buffer)},
};

/* DCB number of traffic classes nested attributes. */
static const struct nla_policy dcbnl_featcfg_nest[DCB_FEATCFG_ATTR_MAX + 1] = {
	[DCB_FEATCFG_ATTR_ALL]      = {.type = NLA_FLAG},
	[DCB_FEATCFG_ATTR_PG]       = {.type = NLA_U8},
	[DCB_FEATCFG_ATTR_PFC]      = {.type = NLA_U8},
	[DCB_FEATCFG_ATTR_APP]      = {.type = NLA_U8},
};

static LIST_HEAD(dcb_app_list);
static DEFINE_SPINLOCK(dcb_lock);

static struct sk_buff *dcbnl_newmsg(int type, u8 cmd, u32 port, u32 seq,
				    u32 flags, struct nlmsghdr **nlhp)
{
	struct sk_buff *skb;
	struct dcbmsg *dcb;
	struct nlmsghdr *nlh;

	skb = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!skb)
		return NULL;

	nlh = nlmsg_put(skb, port, seq, type, sizeof(*dcb), flags);
	BUG_ON(!nlh);

	dcb = nlmsg_data(nlh);
	dcb->dcb_family = AF_UNSPEC;
	dcb->cmd = cmd;
	dcb->dcb_pad = 0;

	if (nlhp)
		*nlhp = nlh;

	return skb;
}

static int dcbnl_getstate(struct net_device *netdev, struct nlmsghdr *nlh,
			  u32 seq, struct nlattr **tb, struct sk_buff *skb)
{
	/* if (!tb[DCB_ATTR_STATE] || !netdev->dcbnl_ops->getstate) */
	if (!netdev->dcbnl_ops->getstate)
		return -EOPNOTSUPP;

	return nla_put_u8(skb, DCB_ATTR_STATE,
			  netdev->dcbnl_ops->getstate(netdev));
}

static int dcbnl_getpfccfg(struct net_device *netdev, struct nlmsghdr *nlh,
			   u32 seq, struct nlattr **tb, struct sk_buff *skb)
{
	struct nlattr *data[DCB_PFC_UP_ATTR_MAX + 1], *nest;
	u8 value;
	int ret;
	int i;
	int getall = 0;

	if (!tb[DCB_ATTR_PFC_CFG])
		return -EINVAL;

	if (!netdev->dcbnl_ops->getpfccfg)
		return -EOPNOTSUPP;

	ret = nla_parse_nested_deprecated(data, DCB_PFC_UP_ATTR_MAX,
					  tb[DCB_ATTR_PFC_CFG],
					  dcbnl_pfc_up_nest, NULL);
	if (ret)
		return ret;

	nest = nla_nest_start_noflag(skb, DCB_ATTR_PFC_CFG);
	if (!nest)
		return -EMSGSIZE;

	if (data[DCB_PFC_UP_ATTR_ALL])
		getall = 1;

	for (i = DCB_PFC_UP_ATTR_0; i <= DCB_PFC_UP_ATTR_7; i++) {
		if (!getall && !data[i])
			continue;

		netdev->dcbnl_ops->getpfccfg(netdev, i - DCB_PFC_UP_ATTR_0,
		                             &value);
		ret = nla_put_u8(skb, i, value);
		if (ret) {
			nla_nest_cancel(skb, nest);
			return ret;
		}
	}
	nla_nest_end(skb, nest);

	return 0;
}

static int dcbnl_getperm_hwaddr(struct net_device *netdev, struct nlmsghdr *nlh,
				u32 seq, struct nlattr **tb, struct sk_buff *skb)
{
	u8 perm_addr[MAX_ADDR_LEN];

	if (!netdev->dcbnl_ops->getpermhwaddr)
		return -EOPNOTSUPP;

	memset(perm_addr, 0, sizeof(perm_addr));
	netdev->dcbnl_ops->getpermhwaddr(netdev, perm_addr);

	return nla_put(skb, DCB_ATTR_PERM_HWADDR, sizeof(perm_addr), perm_addr);
}

static int dcbnl_getcap(struct net_device *netdev, struct nlmsghdr *nlh,
			u32 seq, struct nlattr **tb, struct sk_buff *skb)
{
	struct nlattr *data[DCB_CAP_ATTR_MAX + 1], *nest;
	u8 value;
	int ret;
	int i;
	int getall = 0;

	if (!tb[DCB_ATTR_CAP])
		return -EINVAL;

	if (!netdev->dcbnl_ops->getcap)
		return -EOPNOTSUPP;

	ret = nla_parse_nested_deprecated(data, DCB_CAP_ATTR_MAX,
					  tb[DCB_ATTR_CAP], dcbnl_cap_nest,
					  NULL);
	if (ret)
		return ret;

	nest = nla_nest_start_noflag(skb, DCB_ATTR_CAP);
	if (!nest)
		return -EMSGSIZE;

	if (data[DCB_CAP_ATTR_ALL])
		getall = 1;

	for (i = DCB_CAP_ATTR_ALL+1; i <= DCB_CAP_ATTR_MAX; i++) {
		if (!getall && !data[i])
			continue;

		if (!netdev->dcbnl_ops->getcap(netdev, i, &value)) {
			ret = nla_put_u8(skb, i, value);
			if (ret) {
				nla_nest_cancel(skb, nest);
				return ret;
			}
		}
	}
	nla_nest_end(skb, nest);

	return 0;
}

static int dcbnl_getnumtcs(struct net_device *netdev, struct nlmsghdr *nlh,
			   u32 seq, struct nlattr **tb, struct sk_buff *skb)
{
	struct nlattr *data[DCB_NUMTCS_ATTR_MAX + 1], *nest;
	u8 value;
	int ret;
	int i;
	int getall = 0;

	if (!tb[DCB_ATTR_NUMTCS])
		return -EINVAL;

	if (!netdev->dcbnl_ops->getnumtcs)
		return -EOPNOTSUPP;

	ret = nla_parse_nested_deprecated(data, DCB_NUMTCS_ATTR_MAX,
					  tb[DCB_ATTR_NUMTCS],
					  dcbnl_numtcs_nest, NULL);
	if (ret)
		return ret;

	nest = nla_nest_start_noflag(skb, DCB_ATTR_NUMTCS);
	if (!nest)
		return -EMSGSIZE;

	if (data[DCB_NUMTCS_ATTR_ALL])
		getall = 1;

	for (i = DCB_NUMTCS_ATTR_ALL+1; i <= DCB_NUMTCS_ATTR_MAX; i++) {
		if (!getall && !data[i])
			continue;

		ret = netdev->dcbnl_ops->getnumtcs(netdev, i, &value);
		if (!ret) {
			ret = nla_put_u8(skb, i, value);
			if (ret) {
				nla_nest_cancel(skb, nest);
				return ret;
			}
		} else
			return -EINVAL;
	}
	nla_nest_end(skb, nest);

	return 0;
}

static int dcbnl_setnumtcs(struct net_device *netdev, struct nlmsghdr *nlh,
			   u32 seq, struct nlattr **tb, struct sk_buff *skb)
{
	struct nlattr *data[DCB_NUMTCS_ATTR_MAX + 1];
	int ret;
	u8 value;
	int i;

	if (!tb[DCB_ATTR_NUMTCS])
		return -EINVAL;

	if (!netdev->dcbnl_ops->setnumtcs)
		return -EOPNOTSUPP;

	ret = nla_parse_nested_deprecated(data, DCB_NUMTCS_ATTR_MAX,
					  tb[DCB_ATTR_NUMTCS],
					  dcbnl_numtcs_nest, NULL);
	if (ret)
		return ret;

	for (i = DCB_NUMTCS_ATTR_ALL+1; i <= DCB_NUMTCS_ATTR_MAX; i++) {
		if (data[i] == NULL)
			continue;

		value = nla_get_u8(data[i]);

		ret = netdev->dcbnl_ops->setnumtcs(netdev, i, value);
		if (ret)
			break;
	}

	return nla_put_u8(skb, DCB_ATTR_NUMTCS, !!ret);
}

static int dcbnl_getpfcstate(struct net_device *netdev, struct nlmsghdr *nlh,
			     u32 seq, struct nlattr **tb, struct sk_buff *skb)
{
	if (!netdev->dcbnl_ops->getpfcstate)
		return -EOPNOTSUPP;

	return nla_put_u8(skb, DCB_ATTR_PFC_STATE,
			  netdev->dcbnl_ops->getpfcstate(netdev));
}

static int dcbnl_setpfcstate(struct net_device *netdev, struct nlmsghdr *nlh,
			     u32 seq, struct nlattr **tb, struct sk_buff *skb)
{
	u8 value;

	if (!tb[DCB_ATTR_PFC_STATE])
		return -EINVAL;

	if (!netdev->dcbnl_ops->setpfcstate)
		return -EOPNOTSUPP;

	value = nla_get_u8(tb[DCB_ATTR_PFC_STATE]);

	netdev->dcbnl_ops->setpfcstate(netdev, value);

	return nla_put_u8(skb, DCB_ATTR_PFC_STATE, 0);
}

static int dcbnl_getapp(struct net_device *netdev, struct nlmsghdr *nlh,
			u32 seq, struct nlattr **tb, struct sk_buff *skb)
{
	struct nlattr *app_nest;
	struct nlattr *app_tb[DCB_APP_ATTR_MAX + 1];
	u16 id;
	u8 up, idtype;
	int ret;

	if (!tb[DCB_ATTR_APP])
		return -EINVAL;

	ret = nla_parse_nested_deprecated(app_tb, DCB_APP_ATTR_MAX,
					  tb[DCB_ATTR_APP], dcbnl_app_nest,
					  NULL);
	if (ret)
		return ret;

	/* all must be non-null */
	if ((!app_tb[DCB_APP_ATTR_IDTYPE]) ||
	    (!app_tb[DCB_APP_ATTR_ID]))
		return -EINVAL;

	/* either by eth type or by socket number */
	idtype = nla_get_u8(app_tb[DCB_APP_ATTR_IDTYPE]);
	if ((idtype != DCB_APP_IDTYPE_ETHTYPE) &&
	    (idtype != DCB_APP_IDTYPE_PORTNUM))
		return -EINVAL;

	id = nla_get_u16(app_tb[DCB_APP_ATTR_ID]);

	if (netdev->dcbnl_ops->getapp) {
		ret = netdev->dcbnl_ops->getapp(netdev, idtype, id);
		if (ret < 0)
			return ret;
		else
			up = ret;
	} else {
		struct dcb_app app = {
					.selector = idtype,
					.protocol = id,
				     };
		up = dcb_getapp(netdev, &app);
	}

	app_nest = nla_nest_start_noflag(skb, DCB_ATTR_APP);
	if (!app_nest)
		return -EMSGSIZE;

	ret = nla_put_u8(skb, DCB_APP_ATTR_IDTYPE, idtype);
	if (ret)
		goto out_cancel;

	ret = nla_put_u16(skb, DCB_APP_ATTR_ID, id);
	if (ret)
		goto out_cancel;

	ret = nla_put_u8(skb, DCB_APP_ATTR_PRIORITY, up);
	if (ret)
		goto out_cancel;

	nla_nest_end(skb, app_nest);

	return 0;

out_cancel:
	nla_nest_cancel(skb, app_nest);
	return ret;
}

static int dcbnl_setapp(struct net_device *netdev, struct nlmsghdr *nlh,
			u32 seq, struct nlattr **tb, struct sk_buff *skb)
{
	int ret;
	u16 id;
	u8 up, idtype;
	struct nlattr *app_tb[DCB_APP_ATTR_MAX + 1];

	if (!tb[DCB_ATTR_APP])
		return -EINVAL;

	ret = nla_parse_nested_deprecated(app_tb, DCB_APP_ATTR_MAX,
					  tb[DCB_ATTR_APP], dcbnl_app_nest,
					  NULL);
	if (ret)
		return ret;

	/* all must be non-null */
	if ((!app_tb[DCB_APP_ATTR_IDTYPE]) ||
	    (!app_tb[DCB_APP_ATTR_ID]) ||
	    (!app_tb[DCB_APP_ATTR_PRIORITY]))
		return -EINVAL;

	/* either by eth type or by socket number */
	idtype = nla_get_u8(app_tb[DCB_APP_ATTR_IDTYPE]);
	if ((idtype != DCB_APP_IDTYPE_ETHTYPE) &&
	    (idtype != DCB_APP_IDTYPE_PORTNUM))
		return -EINVAL;

	id = nla_get_u16(app_tb[DCB_APP_ATTR_ID]);
	up = nla_get_u8(app_tb[DCB_APP_ATTR_PRIORITY]);

	if (netdev->dcbnl_ops->setapp) {
		ret = netdev->dcbnl_ops->setapp(netdev, idtype, id, up);
		if (ret < 0)
			return ret;
	} else {
		struct dcb_app app;
		app.selector = idtype;
		app.protocol = id;
		app.priority = up;
		ret = dcb_setapp(netdev, &app);
	}

	ret = nla_put_u8(skb, DCB_ATTR_APP, ret);
	dcbnl_cee_notify(netdev, RTM_SETDCB, DCB_CMD_SAPP, seq, 0);

	return ret;
}

static int __dcbnl_pg_getcfg(struct net_device *netdev, struct nlmsghdr *nlh,
			     struct nlattr **tb, struct sk_buff *skb, int dir)
{
	struct nlattr *pg_nest, *param_nest, *data;
	struct nlattr *pg_tb[DCB_PG_ATTR_MAX + 1];
	struct nlattr *param_tb[DCB_TC_ATTR_PARAM_MAX + 1];
	u8 prio, pgid, tc_pct, up_map;
	int ret;
	int getall = 0;
	int i;

	if (!tb[DCB_ATTR_PG_CFG])
		return -EINVAL;

	if (!netdev->dcbnl_ops->getpgtccfgtx ||
	    !netdev->dcbnl_ops->getpgtccfgrx ||
	    !netdev->dcbnl_ops->getpgbwgcfgtx ||
	    !netdev->dcbnl_ops->getpgbwgcfgrx)
		return -EOPNOTSUPP;

	ret = nla_parse_nested_deprecated(pg_tb, DCB_PG_ATTR_MAX,
					  tb[DCB_ATTR_PG_CFG], dcbnl_pg_nest,
					  NULL);
	if (ret)
		return ret;

	pg_nest = nla_nest_start_noflag(skb, DCB_ATTR_PG_CFG);
	if (!pg_nest)
		return -EMSGSIZE;

	if (pg_tb[DCB_PG_ATTR_TC_ALL])
		getall = 1;

	for (i = DCB_PG_ATTR_TC_0; i <= DCB_PG_ATTR_TC_7; i++) {
		if (!getall && !pg_tb[i])
			continue;

		if (pg_tb[DCB_PG_ATTR_TC_ALL])
			data = pg_tb[DCB_PG_ATTR_TC_ALL];
		else
			data = pg_tb[i];
		ret = nla_parse_nested_deprecated(param_tb,
						  DCB_TC_ATTR_PARAM_MAX, data,
						  dcbnl_tc_param_nest, NULL);
		if (ret)
			goto err_pg;

		param_nest = nla_nest_start_noflag(skb, i);
		if (!param_nest)
			goto err_pg;

		pgid = DCB_ATTR_VALUE_UNDEFINED;
		prio = DCB_ATTR_VALUE_UNDEFINED;
		tc_pct = DCB_ATTR_VALUE_UNDEFINED;
		up_map = DCB_ATTR_VALUE_UNDEFINED;

		if (dir) {
			/* Rx */
			netdev->dcbnl_ops->getpgtccfgrx(netdev,
						i - DCB_PG_ATTR_TC_0, &prio,
						&pgid, &tc_pct, &up_map);
		} else {
			/* Tx */
			netdev->dcbnl_ops->getpgtccfgtx(netdev,
						i - DCB_PG_ATTR_TC_0, &prio,
						&pgid, &tc_pct, &up_map);
		}

		if (param_tb[DCB_TC_ATTR_PARAM_PGID] ||
		    param_tb[DCB_TC_ATTR_PARAM_ALL]) {
			ret = nla_put_u8(skb,
			                 DCB_TC_ATTR_PARAM_PGID, pgid);
			if (ret)
				goto err_param;
		}
		if (param_tb[DCB_TC_ATTR_PARAM_UP_MAPPING] ||
		    param_tb[DCB_TC_ATTR_PARAM_ALL]) {
			ret = nla_put_u8(skb,
			                 DCB_TC_ATTR_PARAM_UP_MAPPING, up_map);
			if (ret)
				goto err_param;
		}
		if (param_tb[DCB_TC_ATTR_PARAM_STRICT_PRIO] ||
		    param_tb[DCB_TC_ATTR_PARAM_ALL]) {
			ret = nla_put_u8(skb,
			                 DCB_TC_ATTR_PARAM_STRICT_PRIO, prio);
			if (ret)
				goto err_param;
		}
		if (param_tb[DCB_TC_ATTR_PARAM_BW_PCT] ||
		    param_tb[DCB_TC_ATTR_PARAM_ALL]) {
			ret = nla_put_u8(skb, DCB_TC_ATTR_PARAM_BW_PCT,
			                 tc_pct);
			if (ret)
				goto err_param;
		}
		nla_nest_end(skb, param_nest);
	}

	if (pg_tb[DCB_PG_ATTR_BW_ID_ALL])
		getall = 1;
	else
		getall = 0;

	for (i = DCB_PG_ATTR_BW_ID_0; i <= DCB_PG_ATTR_BW_ID_7; i++) {
		if (!getall && !pg_tb[i])
			continue;

		tc_pct = DCB_ATTR_VALUE_UNDEFINED;

		if (dir) {
			/* Rx */
			netdev->dcbnl_ops->getpgbwgcfgrx(netdev,
					i - DCB_PG_ATTR_BW_ID_0, &tc_pct);
		} else {
			/* Tx */
			netdev->dcbnl_ops->getpgbwgcfgtx(netdev,
					i - DCB_PG_ATTR_BW_ID_0, &tc_pct);
		}
		ret = nla_put_u8(skb, i, tc_pct);
		if (ret)
			goto err_pg;
	}

	nla_nest_end(skb, pg_nest);

	return 0;

err_param:
	nla_nest_cancel(skb, param_nest);
err_pg:
	nla_nest_cancel(skb, pg_nest);

	return -EMSGSIZE;
}

static int dcbnl_pgtx_getcfg(struct net_device *netdev, struct nlmsghdr *nlh,
			     u32 seq, struct nlattr **tb, struct sk_buff *skb)
{
	return __dcbnl_pg_getcfg(netdev, nlh, tb, skb, 0);
}

static int dcbnl_pgrx_getcfg(struct net_device *netdev, struct nlmsghdr *nlh,
			     u32 seq, struct nlattr **tb, struct sk_buff *skb)
{
	return __dcbnl_pg_getcfg(netdev, nlh, tb, skb, 1);
}

static int dcbnl_setstate(struct net_device *netdev, struct nlmsghdr *nlh,
			  u32 seq, struct nlattr **tb, struct sk_buff *skb)
{
	u8 value;

	if (!tb[DCB_ATTR_STATE])
		return -EINVAL;

	if (!netdev->dcbnl_ops->setstate)
		return -EOPNOTSUPP;

	value = nla_get_u8(tb[DCB_ATTR_STATE]);

	return nla_put_u8(skb, DCB_ATTR_STATE,
			  netdev->dcbnl_ops->setstate(netdev, value));
}

static int dcbnl_setpfccfg(struct net_device *netdev, struct nlmsghdr *nlh,
			   u32 seq, struct nlattr **tb, struct sk_buff *skb)
{
	struct nlattr *data[DCB_PFC_UP_ATTR_MAX + 1];
	int i;
	int ret;
	u8 value;

	if (!tb[DCB_ATTR_PFC_CFG])
		return -EINVAL;

	if (!netdev->dcbnl_ops->setpfccfg)
		return -EOPNOTSUPP;

	ret = nla_parse_nested_deprecated(data, DCB_PFC_UP_ATTR_MAX,
					  tb[DCB_ATTR_PFC_CFG],
					  dcbnl_pfc_up_nest, NULL);
	if (ret)
		return ret;

	for (i = DCB_PFC_UP_ATTR_0; i <= DCB_PFC_UP_ATTR_7; i++) {
		if (data[i] == NULL)
			continue;
		value = nla_get_u8(data[i]);
		netdev->dcbnl_ops->setpfccfg(netdev,
			data[i]->nla_type - DCB_PFC_UP_ATTR_0, value);
	}

	return nla_put_u8(skb, DCB_ATTR_PFC_CFG, 0);
}

static int dcbnl_setall(struct net_device *netdev, struct nlmsghdr *nlh,
			u32 seq, struct nlattr **tb, struct sk_buff *skb)
{
	int ret;

	if (!tb[DCB_ATTR_SET_ALL])
		return -EINVAL;

	if (!netdev->dcbnl_ops->setall)
		return -EOPNOTSUPP;

	ret = nla_put_u8(skb, DCB_ATTR_SET_ALL,
			 netdev->dcbnl_ops->setall(netdev));
	dcbnl_cee_notify(netdev, RTM_SETDCB, DCB_CMD_SET_ALL, seq, 0);

	return ret;
}

static int __dcbnl_pg_setcfg(struct net_device *netdev, struct nlmsghdr *nlh,
			     u32 seq, struct nlattr **tb, struct sk_buff *skb,
			     int dir)
{
	struct nlattr *pg_tb[DCB_PG_ATTR_MAX + 1];
	struct nlattr *param_tb[DCB_TC_ATTR_PARAM_MAX + 1];
	int ret;
	int i;
	u8 pgid;
	u8 up_map;
	u8 prio;
	u8 tc_pct;

	if (!tb[DCB_ATTR_PG_CFG])
		return -EINVAL;

	if (!netdev->dcbnl_ops->setpgtccfgtx ||
	    !netdev->dcbnl_ops->setpgtccfgrx ||
	    !netdev->dcbnl_ops->setpgbwgcfgtx ||
	    !netdev->dcbnl_ops->setpgbwgcfgrx)
		return -EOPNOTSUPP;

	ret = nla_parse_nested_deprecated(pg_tb, DCB_PG_ATTR_MAX,
					  tb[DCB_ATTR_PG_CFG], dcbnl_pg_nest,
					  NULL);
	if (ret)
		return ret;

	for (i = DCB_PG_ATTR_TC_0; i <= DCB_PG_ATTR_TC_7; i++) {
		if (!pg_tb[i])
			continue;

		ret = nla_parse_nested_deprecated(param_tb,
						  DCB_TC_ATTR_PARAM_MAX,
						  pg_tb[i],
						  dcbnl_tc_param_nest, NULL);
		if (ret)
			return ret;

		pgid = DCB_ATTR_VALUE_UNDEFINED;
		prio = DCB_ATTR_VALUE_UNDEFINED;
		tc_pct = DCB_ATTR_VALUE_UNDEFINED;
		up_map = DCB_ATTR_VALUE_UNDEFINED;

		if (param_tb[DCB_TC_ATTR_PARAM_STRICT_PRIO])
			prio =
			    nla_get_u8(param_tb[DCB_TC_ATTR_PARAM_STRICT_PRIO]);

		if (param_tb[DCB_TC_ATTR_PARAM_PGID])
			pgid = nla_get_u8(param_tb[DCB_TC_ATTR_PARAM_PGID]);

		if (param_tb[DCB_TC_ATTR_PARAM_BW_PCT])
			tc_pct = nla_get_u8(param_tb[DCB_TC_ATTR_PARAM_BW_PCT]);

		if (param_tb[DCB_TC_ATTR_PARAM_UP_MAPPING])
			up_map =
			     nla_get_u8(param_tb[DCB_TC_ATTR_PARAM_UP_MAPPING]);

		/* dir: Tx = 0, Rx = 1 */
		if (dir) {
			/* Rx */
			netdev->dcbnl_ops->setpgtccfgrx(netdev,
				i - DCB_PG_ATTR_TC_0,
				prio, pgid, tc_pct, up_map);
		} else {
			/* Tx */
			netdev->dcbnl_ops->setpgtccfgtx(netdev,
				i - DCB_PG_ATTR_TC_0,
				prio, pgid, tc_pct, up_map);
		}
	}

	for (i = DCB_PG_ATTR_BW_ID_0; i <= DCB_PG_ATTR_BW_ID_7; i++) {
		if (!pg_tb[i])
			continue;

		tc_pct = nla_get_u8(pg_tb[i]);

		/* dir: Tx = 0, Rx = 1 */
		if (dir) {
			/* Rx */
			netdev->dcbnl_ops->setpgbwgcfgrx(netdev,
					 i - DCB_PG_ATTR_BW_ID_0, tc_pct);
		} else {
			/* Tx */
			netdev->dcbnl_ops->setpgbwgcfgtx(netdev,
					 i - DCB_PG_ATTR_BW_ID_0, tc_pct);
		}
	}

	return nla_put_u8(skb, DCB_ATTR_PG_CFG, 0);
}

static int dcbnl_pgtx_setcfg(struct net_device *netdev, struct nlmsghdr *nlh,
			     u32 seq, struct nlattr **tb, struct sk_buff *skb)
{
	return __dcbnl_pg_setcfg(netdev, nlh, seq, tb, skb, 0);
}

static int dcbnl_pgrx_setcfg(struct net_device *netdev, struct nlmsghdr *nlh,
			     u32 seq, struct nlattr **tb, struct sk_buff *skb)
{
	return __dcbnl_pg_setcfg(netdev, nlh, seq, tb, skb, 1);
}

static int dcbnl_bcn_getcfg(struct net_device *netdev, struct nlmsghdr *nlh,
			    u32 seq, struct nlattr **tb, struct sk_buff *skb)
{
	struct nlattr *bcn_nest;
	struct nlattr *bcn_tb[DCB_BCN_ATTR_MAX + 1];
	u8 value_byte;
	u32 value_integer;
	int ret;
	bool getall = false;
	int i;

	if (!tb[DCB_ATTR_BCN])
		return -EINVAL;

	if (!netdev->dcbnl_ops->getbcnrp ||
	    !netdev->dcbnl_ops->getbcncfg)
		return -EOPNOTSUPP;

	ret = nla_parse_nested_deprecated(bcn_tb, DCB_BCN_ATTR_MAX,
					  tb[DCB_ATTR_BCN], dcbnl_bcn_nest,
					  NULL);
	if (ret)
		return ret;

	bcn_nest = nla_nest_start_noflag(skb, DCB_ATTR_BCN);
	if (!bcn_nest)
		return -EMSGSIZE;

	if (bcn_tb[DCB_BCN_ATTR_ALL])
		getall = true;

	for (i = DCB_BCN_ATTR_RP_0; i <= DCB_BCN_ATTR_RP_7; i++) {
		if (!getall && !bcn_tb[i])
			continue;

		netdev->dcbnl_ops->getbcnrp(netdev, i - DCB_BCN_ATTR_RP_0,
		                            &value_byte);
		ret = nla_put_u8(skb, i, value_byte);
		if (ret)
			goto err_bcn;
	}

	for (i = DCB_BCN_ATTR_BCNA_0; i <= DCB_BCN_ATTR_RI; i++) {
		if (!getall && !bcn_tb[i])
			continue;

		netdev->dcbnl_ops->getbcncfg(netdev, i,
		                             &value_integer);
		ret = nla_put_u32(skb, i, value_integer);
		if (ret)
			goto err_bcn;
	}

	nla_nest_end(skb, bcn_nest);

	return 0;

err_bcn:
	nla_nest_cancel(skb, bcn_nest);
	return ret;
}

static int dcbnl_bcn_setcfg(struct net_device *netdev, struct nlmsghdr *nlh,
			    u32 seq, struct nlattr **tb, struct sk_buff *skb)
{
	struct nlattr *data[DCB_BCN_ATTR_MAX + 1];
	int i;
	int ret;
	u8 value_byte;
	u32 value_int;

	if (!tb[DCB_ATTR_BCN])
		return -EINVAL;

	if (!netdev->dcbnl_ops->setbcncfg ||
	    !netdev->dcbnl_ops->setbcnrp)
		return -EOPNOTSUPP;

	ret = nla_parse_nested_deprecated(data, DCB_BCN_ATTR_MAX,
					  tb[DCB_ATTR_BCN], dcbnl_bcn_nest,
					  NULL);
	if (ret)
		return ret;

	for (i = DCB_BCN_ATTR_RP_0; i <= DCB_BCN_ATTR_RP_7; i++) {
		if (data[i] == NULL)
			continue;
		value_byte = nla_get_u8(data[i]);
		netdev->dcbnl_ops->setbcnrp(netdev,
			data[i]->nla_type - DCB_BCN_ATTR_RP_0, value_byte);
	}

	for (i = DCB_BCN_ATTR_BCNA_0; i <= DCB_BCN_ATTR_RI; i++) {
		if (data[i] == NULL)
			continue;
		value_int = nla_get_u32(data[i]);
		netdev->dcbnl_ops->setbcncfg(netdev,
	                                     i, value_int);
	}

	return nla_put_u8(skb, DCB_ATTR_BCN, 0);
}

static int dcbnl_build_peer_app(struct net_device *netdev, struct sk_buff* skb,
				int app_nested_type, int app_info_type,
				int app_entry_type)
{
	struct dcb_peer_app_info info;
	struct dcb_app *table = NULL;
	const struct dcbnl_rtnl_ops *ops = netdev->dcbnl_ops;
	u16 app_count;
	int err;


	/**
	 * retrieve the peer app configuration form the driver. If the driver
	 * handlers fail exit without doing anything
	 */
	err = ops->peer_getappinfo(netdev, &info, &app_count);
	if (!err && app_count) {
		table = kmalloc_array(app_count, sizeof(struct dcb_app),
				      GFP_KERNEL);
		if (!table)
			return -ENOMEM;

		err = ops->peer_getapptable(netdev, table);
	}

	if (!err) {
		u16 i;
		struct nlattr *app;

		/**
		 * build the message, from here on the only possible failure
		 * is due to the skb size
		 */
		err = -EMSGSIZE;

		app = nla_nest_start_noflag(skb, app_nested_type);
		if (!app)
			goto nla_put_failure;

		if (app_info_type &&
		    nla_put(skb, app_info_type, sizeof(info), &info))
			goto nla_put_failure;

		for (i = 0; i < app_count; i++) {
			if (nla_put(skb, app_entry_type, sizeof(struct dcb_app),
				    &table[i]))
				goto nla_put_failure;
		}
		nla_nest_end(skb, app);
	}
	err = 0;

nla_put_failure:
	kfree(table);
	return err;
}

/* Handle IEEE 802.1Qaz/802.1Qau/802.1Qbb GET commands. */
static int dcbnl_ieee_fill(struct sk_buff *skb, struct net_device *netdev)
{
	struct nlattr *ieee, *app;
	struct dcb_app_type *itr;
	const struct dcbnl_rtnl_ops *ops = netdev->dcbnl_ops;
	int dcbx;
	int err;

	if (nla_put_string(skb, DCB_ATTR_IFNAME, netdev->name))
		return -EMSGSIZE;

	ieee = nla_nest_start_noflag(skb, DCB_ATTR_IEEE);
	if (!ieee)
		return -EMSGSIZE;

	if (ops->ieee_getets) {
		struct ieee_ets ets;
		memset(&ets, 0, sizeof(ets));
		err = ops->ieee_getets(netdev, &ets);
		if (!err &&
		    nla_put(skb, DCB_ATTR_IEEE_ETS, sizeof(ets), &ets))
			return -EMSGSIZE;
	}

	if (ops->ieee_getmaxrate) {
		struct ieee_maxrate maxrate;
		memset(&maxrate, 0, sizeof(maxrate));
		err = ops->ieee_getmaxrate(netdev, &maxrate);
		if (!err) {
			err = nla_put(skb, DCB_ATTR_IEEE_MAXRATE,
				      sizeof(maxrate), &maxrate);
			if (err)
				return -EMSGSIZE;
		}
	}

	if (ops->ieee_getqcn) {
		struct ieee_qcn qcn;

		memset(&qcn, 0, sizeof(qcn));
		err = ops->ieee_getqcn(netdev, &qcn);
		if (!err) {
			err = nla_put(skb, DCB_ATTR_IEEE_QCN,
				      sizeof(qcn), &qcn);
			if (err)
				return -EMSGSIZE;
		}
	}

	if (ops->ieee_getqcnstats) {
		struct ieee_qcn_stats qcn_stats;

		memset(&qcn_stats, 0, sizeof(qcn_stats));
		err = ops->ieee_getqcnstats(netdev, &qcn_stats);
		if (!err) {
			err = nla_put(skb, DCB_ATTR_IEEE_QCN_STATS,
				      sizeof(qcn_stats), &qcn_stats);
			if (err)
				return -EMSGSIZE;
		}
	}

	if (ops->ieee_getpfc) {
		struct ieee_pfc pfc;
		memset(&pfc, 0, sizeof(pfc));
		err = ops->ieee_getpfc(netdev, &pfc);
		if (!err &&
		    nla_put(skb, DCB_ATTR_IEEE_PFC, sizeof(pfc), &pfc))
			return -EMSGSIZE;
	}

	if (ops->dcbnl_getbuffer) {
		struct dcbnl_buffer buffer;

		memset(&buffer, 0, sizeof(buffer));
		err = ops->dcbnl_getbuffer(netdev, &buffer);
		if (!err &&
		    nla_put(skb, DCB_ATTR_DCB_BUFFER, sizeof(buffer), &buffer))
			return -EMSGSIZE;
	}

	app = nla_nest_start_noflag(skb, DCB_ATTR_IEEE_APP_TABLE);
	if (!app)
		return -EMSGSIZE;

	spin_lock_bh(&dcb_lock);
	list_for_each_entry(itr, &dcb_app_list, list) {
		if (itr->ifindex == netdev->ifindex) {
			err = nla_put(skb, DCB_ATTR_IEEE_APP, sizeof(itr->app),
					 &itr->app);
			if (err) {
				spin_unlock_bh(&dcb_lock);
				return -EMSGSIZE;
			}
		}
	}

	if (netdev->dcbnl_ops->getdcbx)
		dcbx = netdev->dcbnl_ops->getdcbx(netdev);
	else
		dcbx = -EOPNOTSUPP;

	spin_unlock_bh(&dcb_lock);
	nla_nest_end(skb, app);

	/* get peer info if available */
	if (ops->ieee_peer_getets) {
		struct ieee_ets ets;
		memset(&ets, 0, sizeof(ets));
		err = ops->ieee_peer_getets(netdev, &ets);
		if (!err &&
		    nla_put(skb, DCB_ATTR_IEEE_PEER_ETS, sizeof(ets), &ets))
			return -EMSGSIZE;
	}

	if (ops->ieee_peer_getpfc) {
		struct ieee_pfc pfc;
		memset(&pfc, 0, sizeof(pfc));
		err = ops->ieee_peer_getpfc(netdev, &pfc);
		if (!err &&
		    nla_put(skb, DCB_ATTR_IEEE_PEER_PFC, sizeof(pfc), &pfc))
			return -EMSGSIZE;
	}

	if (ops->peer_getappinfo && ops->peer_getapptable) {
		err = dcbnl_build_peer_app(netdev, skb,
					   DCB_ATTR_IEEE_PEER_APP,
					   DCB_ATTR_IEEE_APP_UNSPEC,
					   DCB_ATTR_IEEE_APP);
		if (err)
			return -EMSGSIZE;
	}

	nla_nest_end(skb, ieee);
	if (dcbx >= 0) {
		err = nla_put_u8(skb, DCB_ATTR_DCBX, dcbx);
		if (err)
			return -EMSGSIZE;
	}

	return 0;
}

static int dcbnl_cee_pg_fill(struct sk_buff *skb, struct net_device *dev,
			     int dir)
{
	u8 pgid, up_map, prio, tc_pct;
	const struct dcbnl_rtnl_ops *ops = dev->dcbnl_ops;
	int i = dir ? DCB_ATTR_CEE_TX_PG : DCB_ATTR_CEE_RX_PG;
	struct nlattr *pg = nla_nest_start_noflag(skb, i);

	if (!pg)
		return -EMSGSIZE;

	for (i = DCB_PG_ATTR_TC_0; i <= DCB_PG_ATTR_TC_7; i++) {
		struct nlattr *tc_nest = nla_nest_start_noflag(skb, i);

		if (!tc_nest)
			return -EMSGSIZE;

		pgid = DCB_ATTR_VALUE_UNDEFINED;
		prio = DCB_ATTR_VALUE_UNDEFINED;
		tc_pct = DCB_ATTR_VALUE_UNDEFINED;
		up_map = DCB_ATTR_VALUE_UNDEFINED;

		if (!dir)
			ops->getpgtccfgrx(dev, i - DCB_PG_ATTR_TC_0,
					  &prio, &pgid, &tc_pct, &up_map);
		else
			ops->getpgtccfgtx(dev, i - DCB_PG_ATTR_TC_0,
					  &prio, &pgid, &tc_pct, &up_map);

		if (nla_put_u8(skb, DCB_TC_ATTR_PARAM_PGID, pgid) ||
		    nla_put_u8(skb, DCB_TC_ATTR_PARAM_UP_MAPPING, up_map) ||
		    nla_put_u8(skb, DCB_TC_ATTR_PARAM_STRICT_PRIO, prio) ||
		    nla_put_u8(skb, DCB_TC_ATTR_PARAM_BW_PCT, tc_pct))
			return -EMSGSIZE;
		nla_nest_end(skb, tc_nest);
	}

	for (i = DCB_PG_ATTR_BW_ID_0; i <= DCB_PG_ATTR_BW_ID_7; i++) {
		tc_pct = DCB_ATTR_VALUE_UNDEFINED;

		if (!dir)
			ops->getpgbwgcfgrx(dev, i - DCB_PG_ATTR_BW_ID_0,
					   &tc_pct);
		else
			ops->getpgbwgcfgtx(dev, i - DCB_PG_ATTR_BW_ID_0,
					   &tc_pct);
		if (nla_put_u8(skb, i, tc_pct))
			return -EMSGSIZE;
	}
	nla_nest_end(skb, pg);
	return 0;
}

static int dcbnl_cee_fill(struct sk_buff *skb, struct net_device *netdev)
{
	struct nlattr *cee, *app;
	struct dcb_app_type *itr;
	const struct dcbnl_rtnl_ops *ops = netdev->dcbnl_ops;
	int dcbx, i, err = -EMSGSIZE;
	u8 value;

	if (nla_put_string(skb, DCB_ATTR_IFNAME, netdev->name))
		goto nla_put_failure;
	cee = nla_nest_start_noflag(skb, DCB_ATTR_CEE);
	if (!cee)
		goto nla_put_failure;

	/* local pg */
	if (ops->getpgtccfgtx && ops->getpgbwgcfgtx) {
		err = dcbnl_cee_pg_fill(skb, netdev, 1);
		if (err)
			goto nla_put_failure;
	}

	if (ops->getpgtccfgrx && ops->getpgbwgcfgrx) {
		err = dcbnl_cee_pg_fill(skb, netdev, 0);
		if (err)
			goto nla_put_failure;
	}

	/* local pfc */
	if (ops->getpfccfg) {
		struct nlattr *pfc_nest = nla_nest_start_noflag(skb,
								DCB_ATTR_CEE_PFC);

		if (!pfc_nest)
			goto nla_put_failure;

		for (i = DCB_PFC_UP_ATTR_0; i <= DCB_PFC_UP_ATTR_7; i++) {
			ops->getpfccfg(netdev, i - DCB_PFC_UP_ATTR_0, &value);
			if (nla_put_u8(skb, i, value))
				goto nla_put_failure;
		}
		nla_nest_end(skb, pfc_nest);
	}

	/* local app */
	spin_lock_bh(&dcb_lock);
	app = nla_nest_start_noflag(skb, DCB_ATTR_CEE_APP_TABLE);
	if (!app)
		goto dcb_unlock;

	list_for_each_entry(itr, &dcb_app_list, list) {
		if (itr->ifindex == netdev->ifindex) {
			struct nlattr *app_nest = nla_nest_start_noflag(skb,
									DCB_ATTR_APP);
			if (!app_nest)
				goto dcb_unlock;

			err = nla_put_u8(skb, DCB_APP_ATTR_IDTYPE,
					 itr->app.selector);
			if (err)
				goto dcb_unlock;

			err = nla_put_u16(skb, DCB_APP_ATTR_ID,
					  itr->app.protocol);
			if (err)
				goto dcb_unlock;

			err = nla_put_u8(skb, DCB_APP_ATTR_PRIORITY,
					 itr->app.priority);
			if (err)
				goto dcb_unlock;

			nla_nest_end(skb, app_nest);
		}
	}
	nla_nest_end(skb, app);

	if (netdev->dcbnl_ops->getdcbx)
		dcbx = netdev->dcbnl_ops->getdcbx(netdev);
	else
		dcbx = -EOPNOTSUPP;

	spin_unlock_bh(&dcb_lock);

	/* features flags */
	if (ops->getfeatcfg) {
		struct nlattr *feat = nla_nest_start_noflag(skb,
							    DCB_ATTR_CEE_FEAT);
		if (!feat)
			goto nla_put_failure;

		for (i = DCB_FEATCFG_ATTR_ALL + 1; i <= DCB_FEATCFG_ATTR_MAX;
		     i++)
			if (!ops->getfeatcfg(netdev, i, &value) &&
			    nla_put_u8(skb, i, value))
				goto nla_put_failure;

		nla_nest_end(skb, feat);
	}

	/* peer info if available */
	if (ops->cee_peer_getpg) {
		struct cee_pg pg;
		memset(&pg, 0, sizeof(pg));
		err = ops->cee_peer_getpg(netdev, &pg);
		if (!err &&
		    nla_put(skb, DCB_ATTR_CEE_PEER_PG, sizeof(pg), &pg))
			goto nla_put_failure;
	}

	if (ops->cee_peer_getpfc) {
		struct cee_pfc pfc;
		memset(&pfc, 0, sizeof(pfc));
		err = ops->cee_peer_getpfc(netdev, &pfc);
		if (!err &&
		    nla_put(skb, DCB_ATTR_CEE_PEER_PFC, sizeof(pfc), &pfc))
			goto nla_put_failure;
	}

	if (ops->peer_getappinfo && ops->peer_getapptable) {
		err = dcbnl_build_peer_app(netdev, skb,
					   DCB_ATTR_CEE_PEER_APP_TABLE,
					   DCB_ATTR_CEE_PEER_APP_INFO,
					   DCB_ATTR_CEE_PEER_APP);
		if (err)
			goto nla_put_failure;
	}
	nla_nest_end(skb, cee);

	/* DCBX state */
	if (dcbx >= 0) {
		err = nla_put_u8(skb, DCB_ATTR_DCBX, dcbx);
		if (err)
			goto nla_put_failure;
	}
	return 0;

dcb_unlock:
	spin_unlock_bh(&dcb_lock);
nla_put_failure:
	err = -EMSGSIZE;
	return err;
}

static int dcbnl_notify(struct net_device *dev, int event, int cmd,
			u32 seq, u32 portid, int dcbx_ver)
{
	struct net *net = dev_net(dev);
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	const struct dcbnl_rtnl_ops *ops = dev->dcbnl_ops;
	int err;

	if (!ops)
		return -EOPNOTSUPP;

	skb = dcbnl_newmsg(event, cmd, portid, seq, 0, &nlh);
	if (!skb)
		return -ENOMEM;

	if (dcbx_ver == DCB_CAP_DCBX_VER_IEEE)
		err = dcbnl_ieee_fill(skb, dev);
	else
		err = dcbnl_cee_fill(skb, dev);

	if (err < 0) {
		/* Report error to broadcast listeners */
		nlmsg_free(skb);
		rtnl_set_sk_err(net, RTNLGRP_DCB, err);
	} else {
		/* End nlmsg and notify broadcast listeners */
		nlmsg_end(skb, nlh);
		rtnl_notify(skb, net, 0, RTNLGRP_DCB, NULL, GFP_KERNEL);
	}

	return err;
}

int dcbnl_ieee_notify(struct net_device *dev, int event, int cmd,
		      u32 seq, u32 portid)
{
	return dcbnl_notify(dev, event, cmd, seq, portid, DCB_CAP_DCBX_VER_IEEE);
}
EXPORT_SYMBOL(dcbnl_ieee_notify);

int dcbnl_cee_notify(struct net_device *dev, int event, int cmd,
		     u32 seq, u32 portid)
{
	return dcbnl_notify(dev, event, cmd, seq, portid, DCB_CAP_DCBX_VER_CEE);
}
EXPORT_SYMBOL(dcbnl_cee_notify);

/* Handle IEEE 802.1Qaz/802.1Qau/802.1Qbb SET commands.
 * If any requested operation can not be completed
 * the entire msg is aborted and error value is returned.
 * No attempt is made to reconcile the case where only part of the
 * cmd can be completed.
 */
static int dcbnl_ieee_set(struct net_device *netdev, struct nlmsghdr *nlh,
			  u32 seq, struct nlattr **tb, struct sk_buff *skb)
{
	const struct dcbnl_rtnl_ops *ops = netdev->dcbnl_ops;
	struct nlattr *ieee[DCB_ATTR_IEEE_MAX + 1];
	int prio;
	int err;

	if (!ops)
		return -EOPNOTSUPP;

	if (!tb[DCB_ATTR_IEEE])
		return -EINVAL;

	err = nla_parse_nested_deprecated(ieee, DCB_ATTR_IEEE_MAX,
					  tb[DCB_ATTR_IEEE],
					  dcbnl_ieee_policy, NULL);
	if (err)
		return err;

	if (ieee[DCB_ATTR_IEEE_ETS] && ops->ieee_setets) {
		struct ieee_ets *ets = nla_data(ieee[DCB_ATTR_IEEE_ETS]);
		err = ops->ieee_setets(netdev, ets);
		if (err)
			goto err;
	}

	if (ieee[DCB_ATTR_IEEE_MAXRATE] && ops->ieee_setmaxrate) {
		struct ieee_maxrate *maxrate =
			nla_data(ieee[DCB_ATTR_IEEE_MAXRATE]);
		err = ops->ieee_setmaxrate(netdev, maxrate);
		if (err)
			goto err;
	}

	if (ieee[DCB_ATTR_IEEE_QCN] && ops->ieee_setqcn) {
		struct ieee_qcn *qcn =
			nla_data(ieee[DCB_ATTR_IEEE_QCN]);

		err = ops->ieee_setqcn(netdev, qcn);
		if (err)
			goto err;
	}

	if (ieee[DCB_ATTR_IEEE_PFC] && ops->ieee_setpfc) {
		struct ieee_pfc *pfc = nla_data(ieee[DCB_ATTR_IEEE_PFC]);
		err = ops->ieee_setpfc(netdev, pfc);
		if (err)
			goto err;
	}

	if (ieee[DCB_ATTR_DCB_BUFFER] && ops->dcbnl_setbuffer) {
		struct dcbnl_buffer *buffer =
			nla_data(ieee[DCB_ATTR_DCB_BUFFER]);

		for (prio = 0; prio < ARRAY_SIZE(buffer->prio2buffer); prio++) {
			if (buffer->prio2buffer[prio] >= DCBX_MAX_BUFFERS) {
				err = -EINVAL;
				goto err;
			}
		}

		err = ops->dcbnl_setbuffer(netdev, buffer);
		if (err)
			goto err;
	}

	if (ieee[DCB_ATTR_IEEE_APP_TABLE]) {
		struct nlattr *attr;
		int rem;

		nla_for_each_nested(attr, ieee[DCB_ATTR_IEEE_APP_TABLE], rem) {
			struct dcb_app *app_data;

			if (nla_type(attr) != DCB_ATTR_IEEE_APP)
				continue;

			if (nla_len(attr) < sizeof(struct dcb_app)) {
				err = -ERANGE;
				goto err;
			}

			app_data = nla_data(attr);
			if (ops->ieee_setapp)
				err = ops->ieee_setapp(netdev, app_data);
			else
				err = dcb_ieee_setapp(netdev, app_data);
			if (err)
				goto err;
		}
	}

err:
	err = nla_put_u8(skb, DCB_ATTR_IEEE, err);
	dcbnl_ieee_notify(netdev, RTM_SETDCB, DCB_CMD_IEEE_SET, seq, 0);
	return err;
}

static int dcbnl_ieee_get(struct net_device *netdev, struct nlmsghdr *nlh,
			  u32 seq, struct nlattr **tb, struct sk_buff *skb)
{
	const struct dcbnl_rtnl_ops *ops = netdev->dcbnl_ops;

	if (!ops)
		return -EOPNOTSUPP;

	return dcbnl_ieee_fill(skb, netdev);
}

static int dcbnl_ieee_del(struct net_device *netdev, struct nlmsghdr *nlh,
			  u32 seq, struct nlattr **tb, struct sk_buff *skb)
{
	const struct dcbnl_rtnl_ops *ops = netdev->dcbnl_ops;
	struct nlattr *ieee[DCB_ATTR_IEEE_MAX + 1];
	int err;

	if (!ops)
		return -EOPNOTSUPP;

	if (!tb[DCB_ATTR_IEEE])
		return -EINVAL;

	err = nla_parse_nested_deprecated(ieee, DCB_ATTR_IEEE_MAX,
					  tb[DCB_ATTR_IEEE],
					  dcbnl_ieee_policy, NULL);
	if (err)
		return err;

	if (ieee[DCB_ATTR_IEEE_APP_TABLE]) {
		struct nlattr *attr;
		int rem;

		nla_for_each_nested(attr, ieee[DCB_ATTR_IEEE_APP_TABLE], rem) {
			struct dcb_app *app_data;

			if (nla_type(attr) != DCB_ATTR_IEEE_APP)
				continue;
			app_data = nla_data(attr);
			if (ops->ieee_delapp)
				err = ops->ieee_delapp(netdev, app_data);
			else
				err = dcb_ieee_delapp(netdev, app_data);
			if (err)
				goto err;
		}
	}

err:
	err = nla_put_u8(skb, DCB_ATTR_IEEE, err);
	dcbnl_ieee_notify(netdev, RTM_SETDCB, DCB_CMD_IEEE_DEL, seq, 0);
	return err;
}


/* DCBX configuration */
static int dcbnl_getdcbx(struct net_device *netdev, struct nlmsghdr *nlh,
			 u32 seq, struct nlattr **tb, struct sk_buff *skb)
{
	if (!netdev->dcbnl_ops->getdcbx)
		return -EOPNOTSUPP;

	return nla_put_u8(skb, DCB_ATTR_DCBX,
			  netdev->dcbnl_ops->getdcbx(netdev));
}

static int dcbnl_setdcbx(struct net_device *netdev, struct nlmsghdr *nlh,
			 u32 seq, struct nlattr **tb, struct sk_buff *skb)
{
	u8 value;

	if (!netdev->dcbnl_ops->setdcbx)
		return -EOPNOTSUPP;

	if (!tb[DCB_ATTR_DCBX])
		return -EINVAL;

	value = nla_get_u8(tb[DCB_ATTR_DCBX]);

	return nla_put_u8(skb, DCB_ATTR_DCBX,
			  netdev->dcbnl_ops->setdcbx(netdev, value));
}

static int dcbnl_getfeatcfg(struct net_device *netdev, struct nlmsghdr *nlh,
			    u32 seq, struct nlattr **tb, struct sk_buff *skb)
{
	struct nlattr *data[DCB_FEATCFG_ATTR_MAX + 1], *nest;
	u8 value;
	int ret, i;
	int getall = 0;

	if (!netdev->dcbnl_ops->getfeatcfg)
		return -EOPNOTSUPP;

	if (!tb[DCB_ATTR_FEATCFG])
		return -EINVAL;

	ret = nla_parse_nested_deprecated(data, DCB_FEATCFG_ATTR_MAX,
					  tb[DCB_ATTR_FEATCFG],
					  dcbnl_featcfg_nest, NULL);
	if (ret)
		return ret;

	nest = nla_nest_start_noflag(skb, DCB_ATTR_FEATCFG);
	if (!nest)
		return -EMSGSIZE;

	if (data[DCB_FEATCFG_ATTR_ALL])
		getall = 1;

	for (i = DCB_FEATCFG_ATTR_ALL+1; i <= DCB_FEATCFG_ATTR_MAX; i++) {
		if (!getall && !data[i])
			continue;

		ret = netdev->dcbnl_ops->getfeatcfg(netdev, i, &value);
		if (!ret)
			ret = nla_put_u8(skb, i, value);

		if (ret) {
			nla_nest_cancel(skb, nest);
			goto nla_put_failure;
		}
	}
	nla_nest_end(skb, nest);

nla_put_failure:
	return ret;
}

static int dcbnl_setfeatcfg(struct net_device *netdev, struct nlmsghdr *nlh,
			    u32 seq, struct nlattr **tb, struct sk_buff *skb)
{
	struct nlattr *data[DCB_FEATCFG_ATTR_MAX + 1];
	int ret, i;
	u8 value;

	if (!netdev->dcbnl_ops->setfeatcfg)
		return -ENOTSUPP;

	if (!tb[DCB_ATTR_FEATCFG])
		return -EINVAL;

	ret = nla_parse_nested_deprecated(data, DCB_FEATCFG_ATTR_MAX,
					  tb[DCB_ATTR_FEATCFG],
					  dcbnl_featcfg_nest, NULL);

	if (ret)
		goto err;

	for (i = DCB_FEATCFG_ATTR_ALL+1; i <= DCB_FEATCFG_ATTR_MAX; i++) {
		if (data[i] == NULL)
			continue;

		value = nla_get_u8(data[i]);

		ret = netdev->dcbnl_ops->setfeatcfg(netdev, i, value);

		if (ret)
			goto err;
	}
err:
	ret = nla_put_u8(skb, DCB_ATTR_FEATCFG, ret);

	return ret;
}

/* Handle CEE DCBX GET commands. */
static int dcbnl_cee_get(struct net_device *netdev, struct nlmsghdr *nlh,
			 u32 seq, struct nlattr **tb, struct sk_buff *skb)
{
	const struct dcbnl_rtnl_ops *ops = netdev->dcbnl_ops;

	if (!ops)
		return -EOPNOTSUPP;

	return dcbnl_cee_fill(skb, netdev);
}

struct reply_func {
	/* reply netlink message type */
	int	type;

	/* function to fill message contents */
	int   (*cb)(struct net_device *, struct nlmsghdr *, u32,
		    struct nlattr **, struct sk_buff *);
};

static const struct reply_func reply_funcs[DCB_CMD_MAX+1] = {
	[DCB_CMD_GSTATE]	= { RTM_GETDCB, dcbnl_getstate },
	[DCB_CMD_SSTATE]	= { RTM_SETDCB, dcbnl_setstate },
	[DCB_CMD_PFC_GCFG]	= { RTM_GETDCB, dcbnl_getpfccfg },
	[DCB_CMD_PFC_SCFG]	= { RTM_SETDCB, dcbnl_setpfccfg },
	[DCB_CMD_GPERM_HWADDR]	= { RTM_GETDCB, dcbnl_getperm_hwaddr },
	[DCB_CMD_GCAP]		= { RTM_GETDCB, dcbnl_getcap },
	[DCB_CMD_GNUMTCS]	= { RTM_GETDCB, dcbnl_getnumtcs },
	[DCB_CMD_SNUMTCS]	= { RTM_SETDCB, dcbnl_setnumtcs },
	[DCB_CMD_PFC_GSTATE]	= { RTM_GETDCB, dcbnl_getpfcstate },
	[DCB_CMD_PFC_SSTATE]	= { RTM_SETDCB, dcbnl_setpfcstate },
	[DCB_CMD_GAPP]		= { RTM_GETDCB, dcbnl_getapp },
	[DCB_CMD_SAPP]		= { RTM_SETDCB, dcbnl_setapp },
	[DCB_CMD_PGTX_GCFG]	= { RTM_GETDCB, dcbnl_pgtx_getcfg },
	[DCB_CMD_PGTX_SCFG]	= { RTM_SETDCB, dcbnl_pgtx_setcfg },
	[DCB_CMD_PGRX_GCFG]	= { RTM_GETDCB, dcbnl_pgrx_getcfg },
	[DCB_CMD_PGRX_SCFG]	= { RTM_SETDCB, dcbnl_pgrx_setcfg },
	[DCB_CMD_SET_ALL]	= { RTM_SETDCB, dcbnl_setall },
	[DCB_CMD_BCN_GCFG]	= { RTM_GETDCB, dcbnl_bcn_getcfg },
	[DCB_CMD_BCN_SCFG]	= { RTM_SETDCB, dcbnl_bcn_setcfg },
	[DCB_CMD_IEEE_GET]	= { RTM_GETDCB, dcbnl_ieee_get },
	[DCB_CMD_IEEE_SET]	= { RTM_SETDCB, dcbnl_ieee_set },
	[DCB_CMD_IEEE_DEL]	= { RTM_SETDCB, dcbnl_ieee_del },
	[DCB_CMD_GDCBX]		= { RTM_GETDCB, dcbnl_getdcbx },
	[DCB_CMD_SDCBX]		= { RTM_SETDCB, dcbnl_setdcbx },
	[DCB_CMD_GFEATCFG]	= { RTM_GETDCB, dcbnl_getfeatcfg },
	[DCB_CMD_SFEATCFG]	= { RTM_SETDCB, dcbnl_setfeatcfg },
	[DCB_CMD_CEE_GET]	= { RTM_GETDCB, dcbnl_cee_get },
};

static int dcb_doit(struct sk_buff *skb, struct nlmsghdr *nlh,
		    struct netlink_ext_ack *extack)
{
	struct net *net = sock_net(skb->sk);
	struct net_device *netdev;
	struct dcbmsg *dcb = nlmsg_data(nlh);
	struct nlattr *tb[DCB_ATTR_MAX + 1];
	u32 portid = NETLINK_CB(skb).portid;
	int ret = -EINVAL;
	struct sk_buff *reply_skb;
	struct nlmsghdr *reply_nlh = NULL;
	const struct reply_func *fn;

	if ((nlh->nlmsg_type == RTM_SETDCB) && !netlink_capable(skb, CAP_NET_ADMIN))
		return -EPERM;

	ret = nlmsg_parse_deprecated(nlh, sizeof(*dcb), tb, DCB_ATTR_MAX,
				     dcbnl_rtnl_policy, extack);
	if (ret < 0)
		return ret;

	if (dcb->cmd > DCB_CMD_MAX)
		return -EINVAL;

	/* check if a reply function has been defined for the command */
	fn = &reply_funcs[dcb->cmd];
	if (!fn->cb)
		return -EOPNOTSUPP;
	if (fn->type == RTM_SETDCB && !netlink_capable(skb, CAP_NET_ADMIN))
		return -EPERM;

	if (!tb[DCB_ATTR_IFNAME])
		return -EINVAL;

	netdev = __dev_get_by_name(net, nla_data(tb[DCB_ATTR_IFNAME]));
	if (!netdev)
		return -ENODEV;

	if (!netdev->dcbnl_ops)
		return -EOPNOTSUPP;

	reply_skb = dcbnl_newmsg(fn->type, dcb->cmd, portid, nlh->nlmsg_seq,
				 nlh->nlmsg_flags, &reply_nlh);
	if (!reply_skb)
		return -ENOMEM;

	ret = fn->cb(netdev, nlh, nlh->nlmsg_seq, tb, reply_skb);
	if (ret < 0) {
		nlmsg_free(reply_skb);
		goto out;
	}

	nlmsg_end(reply_skb, reply_nlh);

	ret = rtnl_unicast(reply_skb, net, portid);
out:
	return ret;
}

static struct dcb_app_type *dcb_app_lookup(const struct dcb_app *app,
					   int ifindex, int prio)
{
	struct dcb_app_type *itr;

	list_for_each_entry(itr, &dcb_app_list, list) {
		if (itr->app.selector == app->selector &&
		    itr->app.protocol == app->protocol &&
		    itr->ifindex == ifindex &&
		    ((prio == -1) || itr->app.priority == prio))
			return itr;
	}

	return NULL;
}

static int dcb_app_add(const struct dcb_app *app, int ifindex)
{
	struct dcb_app_type *entry;

	entry = kmalloc(sizeof(*entry), GFP_ATOMIC);
	if (!entry)
		return -ENOMEM;

	memcpy(&entry->app, app, sizeof(*app));
	entry->ifindex = ifindex;
	list_add(&entry->list, &dcb_app_list);

	return 0;
}

/**
 * dcb_getapp - retrieve the DCBX application user priority
 * @dev: network interface
 * @app: application to get user priority of
 *
 * On success returns a non-zero 802.1p user priority bitmap
 * otherwise returns 0 as the invalid user priority bitmap to
 * indicate an error.
 */
u8 dcb_getapp(struct net_device *dev, struct dcb_app *app)
{
	struct dcb_app_type *itr;
	u8 prio = 0;

	spin_lock_bh(&dcb_lock);
	itr = dcb_app_lookup(app, dev->ifindex, -1);
	if (itr)
		prio = itr->app.priority;
	spin_unlock_bh(&dcb_lock);

	return prio;
}
EXPORT_SYMBOL(dcb_getapp);

/**
 * dcb_setapp - add CEE dcb application data to app list
 * @dev: network interface
 * @new: application data to add
 *
 * Priority 0 is an invalid priority in CEE spec. This routine
 * removes applications from the app list if the priority is
 * set to zero. Priority is expected to be 8-bit 802.1p user priority bitmap
 */
int dcb_setapp(struct net_device *dev, struct dcb_app *new)
{
	struct dcb_app_type *itr;
	struct dcb_app_type event;
	int err = 0;

	event.ifindex = dev->ifindex;
	memcpy(&event.app, new, sizeof(event.app));
	if (dev->dcbnl_ops->getdcbx)
		event.dcbx = dev->dcbnl_ops->getdcbx(dev);

	spin_lock_bh(&dcb_lock);
	/* Search for existing match and replace */
	itr = dcb_app_lookup(new, dev->ifindex, -1);
	if (itr) {
		if (new->priority)
			itr->app.priority = new->priority;
		else {
			list_del(&itr->list);
			kfree(itr);
		}
		goto out;
	}
	/* App type does not exist add new application type */
	if (new->priority)
		err = dcb_app_add(new, dev->ifindex);
out:
	spin_unlock_bh(&dcb_lock);
	if (!err)
		call_dcbevent_notifiers(DCB_APP_EVENT, &event);
	return err;
}
EXPORT_SYMBOL(dcb_setapp);

/**
 * dcb_ieee_getapp_mask - retrieve the IEEE DCB application priority
 * @dev: network interface
 * @app: where to store the retrieve application data
 *
 * Helper routine which on success returns a non-zero 802.1Qaz user
 * priority bitmap otherwise returns 0 to indicate the dcb_app was
 * not found in APP list.
 */
u8 dcb_ieee_getapp_mask(struct net_device *dev, struct dcb_app *app)
{
	struct dcb_app_type *itr;
	u8 prio = 0;

	spin_lock_bh(&dcb_lock);
	itr = dcb_app_lookup(app, dev->ifindex, -1);
	if (itr)
		prio |= 1 << itr->app.priority;
	spin_unlock_bh(&dcb_lock);

	return prio;
}
EXPORT_SYMBOL(dcb_ieee_getapp_mask);

/**
 * dcb_ieee_setapp - add IEEE dcb application data to app list
 * @dev: network interface
 * @new: application data to add
 *
 * This adds Application data to the list. Multiple application
 * entries may exists for the same selector and protocol as long
 * as the priorities are different. Priority is expected to be a
 * 3-bit unsigned integer
 */
int dcb_ieee_setapp(struct net_device *dev, struct dcb_app *new)
{
	struct dcb_app_type event;
	int err = 0;

	event.ifindex = dev->ifindex;
	memcpy(&event.app, new, sizeof(event.app));
	if (dev->dcbnl_ops->getdcbx)
		event.dcbx = dev->dcbnl_ops->getdcbx(dev);

	spin_lock_bh(&dcb_lock);
	/* Search for existing match and abort if found */
	if (dcb_app_lookup(new, dev->ifindex, new->priority)) {
		err = -EEXIST;
		goto out;
	}

	err = dcb_app_add(new, dev->ifindex);
out:
	spin_unlock_bh(&dcb_lock);
	if (!err)
		call_dcbevent_notifiers(DCB_APP_EVENT, &event);
	return err;
}
EXPORT_SYMBOL(dcb_ieee_setapp);

/**
 * dcb_ieee_delapp - delete IEEE dcb application data from list
 * @dev: network interface
 * @del: application data to delete
 *
 * This removes a matching APP data from the APP list
 */
int dcb_ieee_delapp(struct net_device *dev, struct dcb_app *del)
{
	struct dcb_app_type *itr;
	struct dcb_app_type event;
	int err = -ENOENT;

	event.ifindex = dev->ifindex;
	memcpy(&event.app, del, sizeof(event.app));
	if (dev->dcbnl_ops->getdcbx)
		event.dcbx = dev->dcbnl_ops->getdcbx(dev);

	spin_lock_bh(&dcb_lock);
	/* Search for existing match and remove it. */
	if ((itr = dcb_app_lookup(del, dev->ifindex, del->priority))) {
		list_del(&itr->list);
		kfree(itr);
		err = 0;
	}

	spin_unlock_bh(&dcb_lock);
	if (!err)
		call_dcbevent_notifiers(DCB_APP_EVENT, &event);
	return err;
}
EXPORT_SYMBOL(dcb_ieee_delapp);

/*
 * dcb_ieee_getapp_prio_dscp_mask_map - For a given device, find mapping from
 * priorities to the DSCP values assigned to that priority. Initialize p_map
 * such that each map element holds a bit mask of DSCP values configured for
 * that priority by APP entries.
 */
void dcb_ieee_getapp_prio_dscp_mask_map(const struct net_device *dev,
					struct dcb_ieee_app_prio_map *p_map)
{
	int ifindex = dev->ifindex;
	struct dcb_app_type *itr;
	u8 prio;

	memset(p_map->map, 0, sizeof(p_map->map));

	spin_lock_bh(&dcb_lock);
	list_for_each_entry(itr, &dcb_app_list, list) {
		if (itr->ifindex == ifindex &&
		    itr->app.selector == IEEE_8021QAZ_APP_SEL_DSCP &&
		    itr->app.protocol < 64 &&
		    itr->app.priority < IEEE_8021QAZ_MAX_TCS) {
			prio = itr->app.priority;
			p_map->map[prio] |= 1ULL << itr->app.protocol;
		}
	}
	spin_unlock_bh(&dcb_lock);
}
EXPORT_SYMBOL(dcb_ieee_getapp_prio_dscp_mask_map);

/*
 * dcb_ieee_getapp_dscp_prio_mask_map - For a given device, find mapping from
 * DSCP values to the priorities assigned to that DSCP value. Initialize p_map
 * such that each map element holds a bit mask of priorities configured for a
 * given DSCP value by APP entries.
 */
void
dcb_ieee_getapp_dscp_prio_mask_map(const struct net_device *dev,
				   struct dcb_ieee_app_dscp_map *p_map)
{
	int ifindex = dev->ifindex;
	struct dcb_app_type *itr;

	memset(p_map->map, 0, sizeof(p_map->map));

	spin_lock_bh(&dcb_lock);
	list_for_each_entry(itr, &dcb_app_list, list) {
		if (itr->ifindex == ifindex &&
		    itr->app.selector == IEEE_8021QAZ_APP_SEL_DSCP &&
		    itr->app.protocol < 64 &&
		    itr->app.priority < IEEE_8021QAZ_MAX_TCS)
			p_map->map[itr->app.protocol] |= 1 << itr->app.priority;
	}
	spin_unlock_bh(&dcb_lock);
}
EXPORT_SYMBOL(dcb_ieee_getapp_dscp_prio_mask_map);

/*
 * Per 802.1Q-2014, the selector value of 1 is used for matching on Ethernet
 * type, with valid PID values >= 1536. A special meaning is then assigned to
 * protocol value of 0: "default priority. For use when priority is not
 * otherwise specified".
 *
 * dcb_ieee_getapp_default_prio_mask - For a given device, find all APP entries
 * of the form {$PRIO, ETHERTYPE, 0} and construct a bit mask of all default
 * priorities set by these entries.
 */
u8 dcb_ieee_getapp_default_prio_mask(const struct net_device *dev)
{
	int ifindex = dev->ifindex;
	struct dcb_app_type *itr;
	u8 mask = 0;

	spin_lock_bh(&dcb_lock);
	list_for_each_entry(itr, &dcb_app_list, list) {
		if (itr->ifindex == ifindex &&
		    itr->app.selector == IEEE_8021QAZ_APP_SEL_ETHERTYPE &&
		    itr->app.protocol == 0 &&
		    itr->app.priority < IEEE_8021QAZ_MAX_TCS)
			mask |= 1 << itr->app.priority;
	}
	spin_unlock_bh(&dcb_lock);

	return mask;
}
EXPORT_SYMBOL(dcb_ieee_getapp_default_prio_mask);

static void dcbnl_flush_dev(struct net_device *dev)
{
	struct dcb_app_type *itr, *tmp;

	spin_lock_bh(&dcb_lock);

	list_for_each_entry_safe(itr, tmp, &dcb_app_list, list) {
		if (itr->ifindex == dev->ifindex) {
			list_del(&itr->list);
			kfree(itr);
		}
	}

	spin_unlock_bh(&dcb_lock);
}

static int dcbnl_netdevice_event(struct notifier_block *nb,
				 unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);

	switch (event) {
	case NETDEV_UNREGISTER:
		if (!dev->dcbnl_ops)
			return NOTIFY_DONE;

		dcbnl_flush_dev(dev);

		return NOTIFY_OK;
	default:
		return NOTIFY_DONE;
	}
}

static struct notifier_block dcbnl_nb __read_mostly = {
	.notifier_call  = dcbnl_netdevice_event,
};

static int __init dcbnl_init(void)
{
	int err;

	err = register_netdevice_notifier(&dcbnl_nb);
	if (err)
		return err;

	rtnl_register(PF_UNSPEC, RTM_GETDCB, dcb_doit, NULL, 0);
	rtnl_register(PF_UNSPEC, RTM_SETDCB, dcb_doit, NULL, 0);

	return 0;
}
device_initcall(dcbnl_init);
