/*
 * Copyright (c) 2008-2011, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
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
#include <net/sock.h>

/**
 * Data Center Bridging (DCB) is a collection of Ethernet enhancements
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

MODULE_AUTHOR("Lucy Liu, <lucy.liu@intel.com>");
MODULE_DESCRIPTION("Data Center Bridging netlink interface");
MODULE_LICENSE("GPL");

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
};

static const struct nla_policy dcbnl_ieee_app[DCB_ATTR_IEEE_APP_MAX + 1] = {
	[DCB_ATTR_IEEE_APP]	    = {.len = sizeof(struct dcb_app)},
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

/* standard netlink reply call */
static int dcbnl_reply(u8 value, u8 event, u8 cmd, u8 attr, u32 pid,
                       u32 seq, u16 flags)
{
	struct sk_buff *dcbnl_skb;
	struct dcbmsg *dcb;
	struct nlmsghdr *nlh;
	int ret = -EINVAL;

	dcbnl_skb = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!dcbnl_skb)
		return ret;

	nlh = NLMSG_NEW(dcbnl_skb, pid, seq, event, sizeof(*dcb), flags);

	dcb = NLMSG_DATA(nlh);
	dcb->dcb_family = AF_UNSPEC;
	dcb->cmd = cmd;
	dcb->dcb_pad = 0;

	ret = nla_put_u8(dcbnl_skb, attr, value);
	if (ret)
		goto err;

	/* end the message, assign the nlmsg_len. */
	nlmsg_end(dcbnl_skb, nlh);
	ret = rtnl_unicast(dcbnl_skb, &init_net, pid);
	if (ret)
		return -EINVAL;

	return 0;
nlmsg_failure:
err:
	kfree_skb(dcbnl_skb);
	return ret;
}

static int dcbnl_getstate(struct net_device *netdev, struct nlattr **tb,
                          u32 pid, u32 seq, u16 flags)
{
	int ret = -EINVAL;

	/* if (!tb[DCB_ATTR_STATE] || !netdev->dcbnl_ops->getstate) */
	if (!netdev->dcbnl_ops->getstate)
		return ret;

	ret = dcbnl_reply(netdev->dcbnl_ops->getstate(netdev), RTM_GETDCB,
	                  DCB_CMD_GSTATE, DCB_ATTR_STATE, pid, seq, flags);

	return ret;
}

static int dcbnl_getpfccfg(struct net_device *netdev, struct nlattr **tb,
                           u32 pid, u32 seq, u16 flags)
{
	struct sk_buff *dcbnl_skb;
	struct nlmsghdr *nlh;
	struct dcbmsg *dcb;
	struct nlattr *data[DCB_PFC_UP_ATTR_MAX + 1], *nest;
	u8 value;
	int ret = -EINVAL;
	int i;
	int getall = 0;

	if (!tb[DCB_ATTR_PFC_CFG] || !netdev->dcbnl_ops->getpfccfg)
		return ret;

	ret = nla_parse_nested(data, DCB_PFC_UP_ATTR_MAX,
	                       tb[DCB_ATTR_PFC_CFG],
	                       dcbnl_pfc_up_nest);
	if (ret)
		goto err_out;

	dcbnl_skb = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!dcbnl_skb)
		goto err_out;

	nlh = NLMSG_NEW(dcbnl_skb, pid, seq, RTM_GETDCB, sizeof(*dcb), flags);

	dcb = NLMSG_DATA(nlh);
	dcb->dcb_family = AF_UNSPEC;
	dcb->cmd = DCB_CMD_PFC_GCFG;

	nest = nla_nest_start(dcbnl_skb, DCB_ATTR_PFC_CFG);
	if (!nest)
		goto err;

	if (data[DCB_PFC_UP_ATTR_ALL])
		getall = 1;

	for (i = DCB_PFC_UP_ATTR_0; i <= DCB_PFC_UP_ATTR_7; i++) {
		if (!getall && !data[i])
			continue;

		netdev->dcbnl_ops->getpfccfg(netdev, i - DCB_PFC_UP_ATTR_0,
		                             &value);
		ret = nla_put_u8(dcbnl_skb, i, value);

		if (ret) {
			nla_nest_cancel(dcbnl_skb, nest);
			goto err;
		}
	}
	nla_nest_end(dcbnl_skb, nest);

	nlmsg_end(dcbnl_skb, nlh);

	ret = rtnl_unicast(dcbnl_skb, &init_net, pid);
	if (ret)
		goto err_out;

	return 0;
nlmsg_failure:
err:
	kfree_skb(dcbnl_skb);
err_out:
	return -EINVAL;
}

static int dcbnl_getperm_hwaddr(struct net_device *netdev, struct nlattr **tb,
                                u32 pid, u32 seq, u16 flags)
{
	struct sk_buff *dcbnl_skb;
	struct nlmsghdr *nlh;
	struct dcbmsg *dcb;
	u8 perm_addr[MAX_ADDR_LEN];
	int ret = -EINVAL;

	if (!netdev->dcbnl_ops->getpermhwaddr)
		return ret;

	dcbnl_skb = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!dcbnl_skb)
		goto err_out;

	nlh = NLMSG_NEW(dcbnl_skb, pid, seq, RTM_GETDCB, sizeof(*dcb), flags);

	dcb = NLMSG_DATA(nlh);
	dcb->dcb_family = AF_UNSPEC;
	dcb->cmd = DCB_CMD_GPERM_HWADDR;

	netdev->dcbnl_ops->getpermhwaddr(netdev, perm_addr);

	ret = nla_put(dcbnl_skb, DCB_ATTR_PERM_HWADDR, sizeof(perm_addr),
	              perm_addr);

	nlmsg_end(dcbnl_skb, nlh);

	ret = rtnl_unicast(dcbnl_skb, &init_net, pid);
	if (ret)
		goto err_out;

	return 0;

nlmsg_failure:
	kfree_skb(dcbnl_skb);
err_out:
	return -EINVAL;
}

static int dcbnl_getcap(struct net_device *netdev, struct nlattr **tb,
                        u32 pid, u32 seq, u16 flags)
{
	struct sk_buff *dcbnl_skb;
	struct nlmsghdr *nlh;
	struct dcbmsg *dcb;
	struct nlattr *data[DCB_CAP_ATTR_MAX + 1], *nest;
	u8 value;
	int ret = -EINVAL;
	int i;
	int getall = 0;

	if (!tb[DCB_ATTR_CAP] || !netdev->dcbnl_ops->getcap)
		return ret;

	ret = nla_parse_nested(data, DCB_CAP_ATTR_MAX, tb[DCB_ATTR_CAP],
	                       dcbnl_cap_nest);
	if (ret)
		goto err_out;

	dcbnl_skb = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!dcbnl_skb)
		goto err_out;

	nlh = NLMSG_NEW(dcbnl_skb, pid, seq, RTM_GETDCB, sizeof(*dcb), flags);

	dcb = NLMSG_DATA(nlh);
	dcb->dcb_family = AF_UNSPEC;
	dcb->cmd = DCB_CMD_GCAP;

	nest = nla_nest_start(dcbnl_skb, DCB_ATTR_CAP);
	if (!nest)
		goto err;

	if (data[DCB_CAP_ATTR_ALL])
		getall = 1;

	for (i = DCB_CAP_ATTR_ALL+1; i <= DCB_CAP_ATTR_MAX; i++) {
		if (!getall && !data[i])
			continue;

		if (!netdev->dcbnl_ops->getcap(netdev, i, &value)) {
			ret = nla_put_u8(dcbnl_skb, i, value);

			if (ret) {
				nla_nest_cancel(dcbnl_skb, nest);
				goto err;
			}
		}
	}
	nla_nest_end(dcbnl_skb, nest);

	nlmsg_end(dcbnl_skb, nlh);

	ret = rtnl_unicast(dcbnl_skb, &init_net, pid);
	if (ret)
		goto err_out;

	return 0;
nlmsg_failure:
err:
	kfree_skb(dcbnl_skb);
err_out:
	return -EINVAL;
}

static int dcbnl_getnumtcs(struct net_device *netdev, struct nlattr **tb,
                           u32 pid, u32 seq, u16 flags)
{
	struct sk_buff *dcbnl_skb;
	struct nlmsghdr *nlh;
	struct dcbmsg *dcb;
	struct nlattr *data[DCB_NUMTCS_ATTR_MAX + 1], *nest;
	u8 value;
	int ret = -EINVAL;
	int i;
	int getall = 0;

	if (!tb[DCB_ATTR_NUMTCS] || !netdev->dcbnl_ops->getnumtcs)
		return ret;

	ret = nla_parse_nested(data, DCB_NUMTCS_ATTR_MAX, tb[DCB_ATTR_NUMTCS],
	                       dcbnl_numtcs_nest);
	if (ret) {
		ret = -EINVAL;
		goto err_out;
	}

	dcbnl_skb = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!dcbnl_skb) {
		ret = -EINVAL;
		goto err_out;
	}

	nlh = NLMSG_NEW(dcbnl_skb, pid, seq, RTM_GETDCB, sizeof(*dcb), flags);

	dcb = NLMSG_DATA(nlh);
	dcb->dcb_family = AF_UNSPEC;
	dcb->cmd = DCB_CMD_GNUMTCS;

	nest = nla_nest_start(dcbnl_skb, DCB_ATTR_NUMTCS);
	if (!nest) {
		ret = -EINVAL;
		goto err;
	}

	if (data[DCB_NUMTCS_ATTR_ALL])
		getall = 1;

	for (i = DCB_NUMTCS_ATTR_ALL+1; i <= DCB_NUMTCS_ATTR_MAX; i++) {
		if (!getall && !data[i])
			continue;

		ret = netdev->dcbnl_ops->getnumtcs(netdev, i, &value);
		if (!ret) {
			ret = nla_put_u8(dcbnl_skb, i, value);

			if (ret) {
				nla_nest_cancel(dcbnl_skb, nest);
				ret = -EINVAL;
				goto err;
			}
		} else {
			goto err;
		}
	}
	nla_nest_end(dcbnl_skb, nest);

	nlmsg_end(dcbnl_skb, nlh);

	ret = rtnl_unicast(dcbnl_skb, &init_net, pid);
	if (ret) {
		ret = -EINVAL;
		goto err_out;
	}

	return 0;
nlmsg_failure:
err:
	kfree_skb(dcbnl_skb);
err_out:
	return ret;
}

static int dcbnl_setnumtcs(struct net_device *netdev, struct nlattr **tb,
                           u32 pid, u32 seq, u16 flags)
{
	struct nlattr *data[DCB_NUMTCS_ATTR_MAX + 1];
	int ret = -EINVAL;
	u8 value;
	int i;

	if (!tb[DCB_ATTR_NUMTCS] || !netdev->dcbnl_ops->setnumtcs)
		return ret;

	ret = nla_parse_nested(data, DCB_NUMTCS_ATTR_MAX, tb[DCB_ATTR_NUMTCS],
	                       dcbnl_numtcs_nest);

	if (ret) {
		ret = -EINVAL;
		goto err;
	}

	for (i = DCB_NUMTCS_ATTR_ALL+1; i <= DCB_NUMTCS_ATTR_MAX; i++) {
		if (data[i] == NULL)
			continue;

		value = nla_get_u8(data[i]);

		ret = netdev->dcbnl_ops->setnumtcs(netdev, i, value);

		if (ret)
			goto operr;
	}

operr:
	ret = dcbnl_reply(!!ret, RTM_SETDCB, DCB_CMD_SNUMTCS,
	                  DCB_ATTR_NUMTCS, pid, seq, flags);

err:
	return ret;
}

static int dcbnl_getpfcstate(struct net_device *netdev, struct nlattr **tb,
                             u32 pid, u32 seq, u16 flags)
{
	int ret = -EINVAL;

	if (!netdev->dcbnl_ops->getpfcstate)
		return ret;

	ret = dcbnl_reply(netdev->dcbnl_ops->getpfcstate(netdev), RTM_GETDCB,
	                  DCB_CMD_PFC_GSTATE, DCB_ATTR_PFC_STATE,
	                  pid, seq, flags);

	return ret;
}

static int dcbnl_setpfcstate(struct net_device *netdev, struct nlattr **tb,
                             u32 pid, u32 seq, u16 flags)
{
	int ret = -EINVAL;
	u8 value;

	if (!tb[DCB_ATTR_PFC_STATE] || !netdev->dcbnl_ops->setpfcstate)
		return ret;

	value = nla_get_u8(tb[DCB_ATTR_PFC_STATE]);

	netdev->dcbnl_ops->setpfcstate(netdev, value);

	ret = dcbnl_reply(0, RTM_SETDCB, DCB_CMD_PFC_SSTATE, DCB_ATTR_PFC_STATE,
	                  pid, seq, flags);

	return ret;
}

static int dcbnl_getapp(struct net_device *netdev, struct nlattr **tb,
                        u32 pid, u32 seq, u16 flags)
{
	struct sk_buff *dcbnl_skb;
	struct nlmsghdr *nlh;
	struct dcbmsg *dcb;
	struct nlattr *app_nest;
	struct nlattr *app_tb[DCB_APP_ATTR_MAX + 1];
	u16 id;
	u8 up, idtype;
	int ret = -EINVAL;

	if (!tb[DCB_ATTR_APP])
		goto out;

	ret = nla_parse_nested(app_tb, DCB_APP_ATTR_MAX, tb[DCB_ATTR_APP],
	                       dcbnl_app_nest);
	if (ret)
		goto out;

	ret = -EINVAL;
	/* all must be non-null */
	if ((!app_tb[DCB_APP_ATTR_IDTYPE]) ||
	    (!app_tb[DCB_APP_ATTR_ID]))
		goto out;

	/* either by eth type or by socket number */
	idtype = nla_get_u8(app_tb[DCB_APP_ATTR_IDTYPE]);
	if ((idtype != DCB_APP_IDTYPE_ETHTYPE) &&
	    (idtype != DCB_APP_IDTYPE_PORTNUM))
		goto out;

	id = nla_get_u16(app_tb[DCB_APP_ATTR_ID]);

	if (netdev->dcbnl_ops->getapp) {
		up = netdev->dcbnl_ops->getapp(netdev, idtype, id);
	} else {
		struct dcb_app app = {
					.selector = idtype,
					.protocol = id,
				     };
		up = dcb_getapp(netdev, &app);
	}

	/* send this back */
	dcbnl_skb = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!dcbnl_skb)
		goto out;

	nlh = NLMSG_NEW(dcbnl_skb, pid, seq, RTM_GETDCB, sizeof(*dcb), flags);
	dcb = NLMSG_DATA(nlh);
	dcb->dcb_family = AF_UNSPEC;
	dcb->cmd = DCB_CMD_GAPP;

	app_nest = nla_nest_start(dcbnl_skb, DCB_ATTR_APP);
	if (!app_nest)
		goto out_cancel;

	ret = nla_put_u8(dcbnl_skb, DCB_APP_ATTR_IDTYPE, idtype);
	if (ret)
		goto out_cancel;

	ret = nla_put_u16(dcbnl_skb, DCB_APP_ATTR_ID, id);
	if (ret)
		goto out_cancel;

	ret = nla_put_u8(dcbnl_skb, DCB_APP_ATTR_PRIORITY, up);
	if (ret)
		goto out_cancel;

	nla_nest_end(dcbnl_skb, app_nest);
	nlmsg_end(dcbnl_skb, nlh);

	ret = rtnl_unicast(dcbnl_skb, &init_net, pid);
	if (ret)
		goto nlmsg_failure;

	goto out;

out_cancel:
	nla_nest_cancel(dcbnl_skb, app_nest);
nlmsg_failure:
	kfree_skb(dcbnl_skb);
out:
	return ret;
}

static int dcbnl_setapp(struct net_device *netdev, struct nlattr **tb,
                        u32 pid, u32 seq, u16 flags)
{
	int err, ret = -EINVAL;
	u16 id;
	u8 up, idtype;
	struct nlattr *app_tb[DCB_APP_ATTR_MAX + 1];

	if (!tb[DCB_ATTR_APP])
		goto out;

	ret = nla_parse_nested(app_tb, DCB_APP_ATTR_MAX, tb[DCB_ATTR_APP],
	                       dcbnl_app_nest);
	if (ret)
		goto out;

	ret = -EINVAL;
	/* all must be non-null */
	if ((!app_tb[DCB_APP_ATTR_IDTYPE]) ||
	    (!app_tb[DCB_APP_ATTR_ID]) ||
	    (!app_tb[DCB_APP_ATTR_PRIORITY]))
		goto out;

	/* either by eth type or by socket number */
	idtype = nla_get_u8(app_tb[DCB_APP_ATTR_IDTYPE]);
	if ((idtype != DCB_APP_IDTYPE_ETHTYPE) &&
	    (idtype != DCB_APP_IDTYPE_PORTNUM))
		goto out;

	id = nla_get_u16(app_tb[DCB_APP_ATTR_ID]);
	up = nla_get_u8(app_tb[DCB_APP_ATTR_PRIORITY]);

	if (netdev->dcbnl_ops->setapp) {
		err = netdev->dcbnl_ops->setapp(netdev, idtype, id, up);
	} else {
		struct dcb_app app;
		app.selector = idtype;
		app.protocol = id;
		app.priority = up;
		err = dcb_setapp(netdev, &app);
	}

	ret = dcbnl_reply(err, RTM_SETDCB, DCB_CMD_SAPP, DCB_ATTR_APP,
			  pid, seq, flags);
out:
	return ret;
}

static int __dcbnl_pg_getcfg(struct net_device *netdev, struct nlattr **tb,
                             u32 pid, u32 seq, u16 flags, int dir)
{
	struct sk_buff *dcbnl_skb;
	struct nlmsghdr *nlh;
	struct dcbmsg *dcb;
	struct nlattr *pg_nest, *param_nest, *data;
	struct nlattr *pg_tb[DCB_PG_ATTR_MAX + 1];
	struct nlattr *param_tb[DCB_TC_ATTR_PARAM_MAX + 1];
	u8 prio, pgid, tc_pct, up_map;
	int ret  = -EINVAL;
	int getall = 0;
	int i;

	if (!tb[DCB_ATTR_PG_CFG] ||
	    !netdev->dcbnl_ops->getpgtccfgtx ||
	    !netdev->dcbnl_ops->getpgtccfgrx ||
	    !netdev->dcbnl_ops->getpgbwgcfgtx ||
	    !netdev->dcbnl_ops->getpgbwgcfgrx)
		return ret;

	ret = nla_parse_nested(pg_tb, DCB_PG_ATTR_MAX,
	                       tb[DCB_ATTR_PG_CFG], dcbnl_pg_nest);

	if (ret)
		goto err_out;

	dcbnl_skb = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!dcbnl_skb)
		goto err_out;

	nlh = NLMSG_NEW(dcbnl_skb, pid, seq, RTM_GETDCB, sizeof(*dcb), flags);

	dcb = NLMSG_DATA(nlh);
	dcb->dcb_family = AF_UNSPEC;
	dcb->cmd = (dir) ? DCB_CMD_PGRX_GCFG : DCB_CMD_PGTX_GCFG;

	pg_nest = nla_nest_start(dcbnl_skb, DCB_ATTR_PG_CFG);
	if (!pg_nest)
		goto err;

	if (pg_tb[DCB_PG_ATTR_TC_ALL])
		getall = 1;

	for (i = DCB_PG_ATTR_TC_0; i <= DCB_PG_ATTR_TC_7; i++) {
		if (!getall && !pg_tb[i])
			continue;

		if (pg_tb[DCB_PG_ATTR_TC_ALL])
			data = pg_tb[DCB_PG_ATTR_TC_ALL];
		else
			data = pg_tb[i];
		ret = nla_parse_nested(param_tb, DCB_TC_ATTR_PARAM_MAX,
				       data, dcbnl_tc_param_nest);
		if (ret)
			goto err_pg;

		param_nest = nla_nest_start(dcbnl_skb, i);
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
			ret = nla_put_u8(dcbnl_skb,
			                 DCB_TC_ATTR_PARAM_PGID, pgid);
			if (ret)
				goto err_param;
		}
		if (param_tb[DCB_TC_ATTR_PARAM_UP_MAPPING] ||
		    param_tb[DCB_TC_ATTR_PARAM_ALL]) {
			ret = nla_put_u8(dcbnl_skb,
			                 DCB_TC_ATTR_PARAM_UP_MAPPING, up_map);
			if (ret)
				goto err_param;
		}
		if (param_tb[DCB_TC_ATTR_PARAM_STRICT_PRIO] ||
		    param_tb[DCB_TC_ATTR_PARAM_ALL]) {
			ret = nla_put_u8(dcbnl_skb,
			                 DCB_TC_ATTR_PARAM_STRICT_PRIO, prio);
			if (ret)
				goto err_param;
		}
		if (param_tb[DCB_TC_ATTR_PARAM_BW_PCT] ||
		    param_tb[DCB_TC_ATTR_PARAM_ALL]) {
			ret = nla_put_u8(dcbnl_skb, DCB_TC_ATTR_PARAM_BW_PCT,
			                 tc_pct);
			if (ret)
				goto err_param;
		}
		nla_nest_end(dcbnl_skb, param_nest);
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
		ret = nla_put_u8(dcbnl_skb, i, tc_pct);

		if (ret)
			goto err_pg;
	}

	nla_nest_end(dcbnl_skb, pg_nest);

	nlmsg_end(dcbnl_skb, nlh);

	ret = rtnl_unicast(dcbnl_skb, &init_net, pid);
	if (ret)
		goto err_out;

	return 0;

err_param:
	nla_nest_cancel(dcbnl_skb, param_nest);
err_pg:
	nla_nest_cancel(dcbnl_skb, pg_nest);
nlmsg_failure:
err:
	kfree_skb(dcbnl_skb);
err_out:
	ret  = -EINVAL;
	return ret;
}

static int dcbnl_pgtx_getcfg(struct net_device *netdev, struct nlattr **tb,
                             u32 pid, u32 seq, u16 flags)
{
	return __dcbnl_pg_getcfg(netdev, tb, pid, seq, flags, 0);
}

static int dcbnl_pgrx_getcfg(struct net_device *netdev, struct nlattr **tb,
                             u32 pid, u32 seq, u16 flags)
{
	return __dcbnl_pg_getcfg(netdev, tb, pid, seq, flags, 1);
}

static int dcbnl_setstate(struct net_device *netdev, struct nlattr **tb,
                          u32 pid, u32 seq, u16 flags)
{
	int ret = -EINVAL;
	u8 value;

	if (!tb[DCB_ATTR_STATE] || !netdev->dcbnl_ops->setstate)
		return ret;

	value = nla_get_u8(tb[DCB_ATTR_STATE]);

	ret = dcbnl_reply(netdev->dcbnl_ops->setstate(netdev, value),
	                  RTM_SETDCB, DCB_CMD_SSTATE, DCB_ATTR_STATE,
	                  pid, seq, flags);

	return ret;
}

static int dcbnl_setpfccfg(struct net_device *netdev, struct nlattr **tb,
                           u32 pid, u32 seq, u16 flags)
{
	struct nlattr *data[DCB_PFC_UP_ATTR_MAX + 1];
	int i;
	int ret = -EINVAL;
	u8 value;

	if (!tb[DCB_ATTR_PFC_CFG] || !netdev->dcbnl_ops->setpfccfg)
		return ret;

	ret = nla_parse_nested(data, DCB_PFC_UP_ATTR_MAX,
	                       tb[DCB_ATTR_PFC_CFG],
	                       dcbnl_pfc_up_nest);
	if (ret)
		goto err;

	for (i = DCB_PFC_UP_ATTR_0; i <= DCB_PFC_UP_ATTR_7; i++) {
		if (data[i] == NULL)
			continue;
		value = nla_get_u8(data[i]);
		netdev->dcbnl_ops->setpfccfg(netdev,
			data[i]->nla_type - DCB_PFC_UP_ATTR_0, value);
	}

	ret = dcbnl_reply(0, RTM_SETDCB, DCB_CMD_PFC_SCFG, DCB_ATTR_PFC_CFG,
	                  pid, seq, flags);
err:
	return ret;
}

static int dcbnl_setall(struct net_device *netdev, struct nlattr **tb,
                        u32 pid, u32 seq, u16 flags)
{
	int ret = -EINVAL;

	if (!tb[DCB_ATTR_SET_ALL] || !netdev->dcbnl_ops->setall)
		return ret;

	ret = dcbnl_reply(netdev->dcbnl_ops->setall(netdev), RTM_SETDCB,
	                  DCB_CMD_SET_ALL, DCB_ATTR_SET_ALL, pid, seq, flags);

	return ret;
}

static int __dcbnl_pg_setcfg(struct net_device *netdev, struct nlattr **tb,
                             u32 pid, u32 seq, u16 flags, int dir)
{
	struct nlattr *pg_tb[DCB_PG_ATTR_MAX + 1];
	struct nlattr *param_tb[DCB_TC_ATTR_PARAM_MAX + 1];
	int ret = -EINVAL;
	int i;
	u8 pgid;
	u8 up_map;
	u8 prio;
	u8 tc_pct;

	if (!tb[DCB_ATTR_PG_CFG] ||
	    !netdev->dcbnl_ops->setpgtccfgtx ||
	    !netdev->dcbnl_ops->setpgtccfgrx ||
	    !netdev->dcbnl_ops->setpgbwgcfgtx ||
	    !netdev->dcbnl_ops->setpgbwgcfgrx)
		return ret;

	ret = nla_parse_nested(pg_tb, DCB_PG_ATTR_MAX,
	                       tb[DCB_ATTR_PG_CFG], dcbnl_pg_nest);
	if (ret)
		goto err;

	for (i = DCB_PG_ATTR_TC_0; i <= DCB_PG_ATTR_TC_7; i++) {
		if (!pg_tb[i])
			continue;

		ret = nla_parse_nested(param_tb, DCB_TC_ATTR_PARAM_MAX,
		                       pg_tb[i], dcbnl_tc_param_nest);
		if (ret)
			goto err;

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

	ret = dcbnl_reply(0, RTM_SETDCB,
			  (dir ? DCB_CMD_PGRX_SCFG : DCB_CMD_PGTX_SCFG),
			  DCB_ATTR_PG_CFG, pid, seq, flags);

err:
	return ret;
}

static int dcbnl_pgtx_setcfg(struct net_device *netdev, struct nlattr **tb,
                             u32 pid, u32 seq, u16 flags)
{
	return __dcbnl_pg_setcfg(netdev, tb, pid, seq, flags, 0);
}

static int dcbnl_pgrx_setcfg(struct net_device *netdev, struct nlattr **tb,
                             u32 pid, u32 seq, u16 flags)
{
	return __dcbnl_pg_setcfg(netdev, tb, pid, seq, flags, 1);
}

static int dcbnl_bcn_getcfg(struct net_device *netdev, struct nlattr **tb,
                            u32 pid, u32 seq, u16 flags)
{
	struct sk_buff *dcbnl_skb;
	struct nlmsghdr *nlh;
	struct dcbmsg *dcb;
	struct nlattr *bcn_nest;
	struct nlattr *bcn_tb[DCB_BCN_ATTR_MAX + 1];
	u8 value_byte;
	u32 value_integer;
	int ret  = -EINVAL;
	bool getall = false;
	int i;

	if (!tb[DCB_ATTR_BCN] || !netdev->dcbnl_ops->getbcnrp ||
	    !netdev->dcbnl_ops->getbcncfg)
		return ret;

	ret = nla_parse_nested(bcn_tb, DCB_BCN_ATTR_MAX,
	                       tb[DCB_ATTR_BCN], dcbnl_bcn_nest);

	if (ret)
		goto err_out;

	dcbnl_skb = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!dcbnl_skb)
		goto err_out;

	nlh = NLMSG_NEW(dcbnl_skb, pid, seq, RTM_GETDCB, sizeof(*dcb), flags);

	dcb = NLMSG_DATA(nlh);
	dcb->dcb_family = AF_UNSPEC;
	dcb->cmd = DCB_CMD_BCN_GCFG;

	bcn_nest = nla_nest_start(dcbnl_skb, DCB_ATTR_BCN);
	if (!bcn_nest)
		goto err;

	if (bcn_tb[DCB_BCN_ATTR_ALL])
		getall = true;

	for (i = DCB_BCN_ATTR_RP_0; i <= DCB_BCN_ATTR_RP_7; i++) {
		if (!getall && !bcn_tb[i])
			continue;

		netdev->dcbnl_ops->getbcnrp(netdev, i - DCB_BCN_ATTR_RP_0,
		                            &value_byte);
		ret = nla_put_u8(dcbnl_skb, i, value_byte);
		if (ret)
			goto err_bcn;
	}

	for (i = DCB_BCN_ATTR_BCNA_0; i <= DCB_BCN_ATTR_RI; i++) {
		if (!getall && !bcn_tb[i])
			continue;

		netdev->dcbnl_ops->getbcncfg(netdev, i,
		                             &value_integer);
		ret = nla_put_u32(dcbnl_skb, i, value_integer);
		if (ret)
			goto err_bcn;
	}

	nla_nest_end(dcbnl_skb, bcn_nest);

	nlmsg_end(dcbnl_skb, nlh);

	ret = rtnl_unicast(dcbnl_skb, &init_net, pid);
	if (ret)
		goto err_out;

	return 0;

err_bcn:
	nla_nest_cancel(dcbnl_skb, bcn_nest);
nlmsg_failure:
err:
	kfree_skb(dcbnl_skb);
err_out:
	ret  = -EINVAL;
	return ret;
}

static int dcbnl_bcn_setcfg(struct net_device *netdev, struct nlattr **tb,
                            u32 pid, u32 seq, u16 flags)
{
	struct nlattr *data[DCB_BCN_ATTR_MAX + 1];
	int i;
	int ret = -EINVAL;
	u8 value_byte;
	u32 value_int;

	if (!tb[DCB_ATTR_BCN] || !netdev->dcbnl_ops->setbcncfg ||
	    !netdev->dcbnl_ops->setbcnrp)
		return ret;

	ret = nla_parse_nested(data, DCB_BCN_ATTR_MAX,
	                       tb[DCB_ATTR_BCN],
	                       dcbnl_pfc_up_nest);
	if (ret)
		goto err;

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

	ret = dcbnl_reply(0, RTM_SETDCB, DCB_CMD_BCN_SCFG, DCB_ATTR_BCN,
	                  pid, seq, flags);
err:
	return ret;
}

/* Handle IEEE 802.1Qaz SET commands. If any requested operation can not
 * be completed the entire msg is aborted and error value is returned.
 * No attempt is made to reconcile the case where only part of the
 * cmd can be completed.
 */
static int dcbnl_ieee_set(struct net_device *netdev, struct nlattr **tb,
			  u32 pid, u32 seq, u16 flags)
{
	const struct dcbnl_rtnl_ops *ops = netdev->dcbnl_ops;
	struct nlattr *ieee[DCB_ATTR_IEEE_MAX + 1];
	int err = -EOPNOTSUPP;

	if (!ops)
		goto err;

	err = nla_parse_nested(ieee, DCB_ATTR_IEEE_MAX,
			       tb[DCB_ATTR_IEEE], dcbnl_ieee_policy);
	if (err)
		goto err;

	if (ieee[DCB_ATTR_IEEE_ETS] && ops->ieee_setets) {
		struct ieee_ets *ets = nla_data(ieee[DCB_ATTR_IEEE_ETS]);
		err = ops->ieee_setets(netdev, ets);
		if (err)
			goto err;
	}

	if (ieee[DCB_ATTR_IEEE_PFC] && ops->ieee_setpfc) {
		struct ieee_pfc *pfc = nla_data(ieee[DCB_ATTR_IEEE_PFC]);
		err = ops->ieee_setpfc(netdev, pfc);
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
			app_data = nla_data(attr);
			if (ops->ieee_setapp)
				err = ops->ieee_setapp(netdev, app_data);
			else
				err = dcb_setapp(netdev, app_data);
			if (err)
				goto err;
		}
	}

err:
	dcbnl_reply(err, RTM_SETDCB, DCB_CMD_IEEE_SET, DCB_ATTR_IEEE,
		    pid, seq, flags);
	return err;
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
		table = kmalloc(sizeof(struct dcb_app) * app_count, GFP_KERNEL);
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

		app = nla_nest_start(skb, app_nested_type);
		if (!app)
			goto nla_put_failure;

		if (app_info_type)
			NLA_PUT(skb, app_info_type, sizeof(info), &info);

		for (i = 0; i < app_count; i++)
			NLA_PUT(skb, app_entry_type, sizeof(struct dcb_app),
				&table[i]);

		nla_nest_end(skb, app);
	}
	err = 0;

nla_put_failure:
	kfree(table);
	return err;
}

/* Handle IEEE 802.1Qaz GET commands. */
static int dcbnl_ieee_get(struct net_device *netdev, struct nlattr **tb,
			  u32 pid, u32 seq, u16 flags)
{
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	struct dcbmsg *dcb;
	struct nlattr *ieee, *app;
	struct dcb_app_type *itr;
	const struct dcbnl_rtnl_ops *ops = netdev->dcbnl_ops;
	int err;

	if (!ops)
		return -EOPNOTSUPP;

	skb = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!skb)
		return -ENOBUFS;

	nlh = NLMSG_NEW(skb, pid, seq, RTM_GETDCB, sizeof(*dcb), flags);

	dcb = NLMSG_DATA(nlh);
	dcb->dcb_family = AF_UNSPEC;
	dcb->cmd = DCB_CMD_IEEE_GET;

	NLA_PUT_STRING(skb, DCB_ATTR_IFNAME, netdev->name);

	ieee = nla_nest_start(skb, DCB_ATTR_IEEE);
	if (!ieee)
		goto nla_put_failure;

	if (ops->ieee_getets) {
		struct ieee_ets ets;
		err = ops->ieee_getets(netdev, &ets);
		if (!err)
			NLA_PUT(skb, DCB_ATTR_IEEE_ETS, sizeof(ets), &ets);
	}

	if (ops->ieee_getpfc) {
		struct ieee_pfc pfc;
		err = ops->ieee_getpfc(netdev, &pfc);
		if (!err)
			NLA_PUT(skb, DCB_ATTR_IEEE_PFC, sizeof(pfc), &pfc);
	}

	app = nla_nest_start(skb, DCB_ATTR_IEEE_APP_TABLE);
	if (!app)
		goto nla_put_failure;

	spin_lock(&dcb_lock);
	list_for_each_entry(itr, &dcb_app_list, list) {
		if (strncmp(itr->name, netdev->name, IFNAMSIZ) == 0) {
			err = nla_put(skb, DCB_ATTR_IEEE_APP, sizeof(itr->app),
					 &itr->app);
			if (err) {
				spin_unlock(&dcb_lock);
				goto nla_put_failure;
			}
		}
	}
	spin_unlock(&dcb_lock);
	nla_nest_end(skb, app);

	/* get peer info if available */
	if (ops->ieee_peer_getets) {
		struct ieee_ets ets;
		err = ops->ieee_peer_getets(netdev, &ets);
		if (!err)
			NLA_PUT(skb, DCB_ATTR_IEEE_PEER_ETS, sizeof(ets), &ets);
	}

	if (ops->ieee_peer_getpfc) {
		struct ieee_pfc pfc;
		err = ops->ieee_peer_getpfc(netdev, &pfc);
		if (!err)
			NLA_PUT(skb, DCB_ATTR_IEEE_PEER_PFC, sizeof(pfc), &pfc);
	}

	if (ops->peer_getappinfo && ops->peer_getapptable) {
		err = dcbnl_build_peer_app(netdev, skb,
					   DCB_ATTR_IEEE_PEER_APP,
					   DCB_ATTR_IEEE_APP_UNSPEC,
					   DCB_ATTR_IEEE_APP);
		if (err)
			goto nla_put_failure;
	}

	nla_nest_end(skb, ieee);
	nlmsg_end(skb, nlh);

	return rtnl_unicast(skb, &init_net, pid);
nla_put_failure:
	nlmsg_cancel(skb, nlh);
nlmsg_failure:
	kfree_skb(skb);
	return -1;
}

/* DCBX configuration */
static int dcbnl_getdcbx(struct net_device *netdev, struct nlattr **tb,
			 u32 pid, u32 seq, u16 flags)
{
	int ret;

	if (!netdev->dcbnl_ops->getdcbx)
		return -EOPNOTSUPP;

	ret = dcbnl_reply(netdev->dcbnl_ops->getdcbx(netdev), RTM_GETDCB,
			  DCB_CMD_GDCBX, DCB_ATTR_DCBX, pid, seq, flags);

	return ret;
}

static int dcbnl_setdcbx(struct net_device *netdev, struct nlattr **tb,
			 u32 pid, u32 seq, u16 flags)
{
	int ret;
	u8 value;

	if (!netdev->dcbnl_ops->setdcbx)
		return -EOPNOTSUPP;

	if (!tb[DCB_ATTR_DCBX])
		return -EINVAL;

	value = nla_get_u8(tb[DCB_ATTR_DCBX]);

	ret = dcbnl_reply(netdev->dcbnl_ops->setdcbx(netdev, value),
			  RTM_SETDCB, DCB_CMD_SDCBX, DCB_ATTR_DCBX,
			  pid, seq, flags);

	return ret;
}

static int dcbnl_getfeatcfg(struct net_device *netdev, struct nlattr **tb,
			    u32 pid, u32 seq, u16 flags)
{
	struct sk_buff *dcbnl_skb;
	struct nlmsghdr *nlh;
	struct dcbmsg *dcb;
	struct nlattr *data[DCB_FEATCFG_ATTR_MAX + 1], *nest;
	u8 value;
	int ret, i;
	int getall = 0;

	if (!netdev->dcbnl_ops->getfeatcfg)
		return -EOPNOTSUPP;

	if (!tb[DCB_ATTR_FEATCFG])
		return -EINVAL;

	ret = nla_parse_nested(data, DCB_FEATCFG_ATTR_MAX, tb[DCB_ATTR_FEATCFG],
			       dcbnl_featcfg_nest);
	if (ret)
		goto err_out;

	dcbnl_skb = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!dcbnl_skb) {
		ret = -ENOBUFS;
		goto err_out;
	}

	nlh = NLMSG_NEW(dcbnl_skb, pid, seq, RTM_GETDCB, sizeof(*dcb), flags);

	dcb = NLMSG_DATA(nlh);
	dcb->dcb_family = AF_UNSPEC;
	dcb->cmd = DCB_CMD_GFEATCFG;

	nest = nla_nest_start(dcbnl_skb, DCB_ATTR_FEATCFG);
	if (!nest) {
		ret = -EMSGSIZE;
		goto nla_put_failure;
	}

	if (data[DCB_FEATCFG_ATTR_ALL])
		getall = 1;

	for (i = DCB_FEATCFG_ATTR_ALL+1; i <= DCB_FEATCFG_ATTR_MAX; i++) {
		if (!getall && !data[i])
			continue;

		ret = netdev->dcbnl_ops->getfeatcfg(netdev, i, &value);
		if (!ret)
			ret = nla_put_u8(dcbnl_skb, i, value);

		if (ret) {
			nla_nest_cancel(dcbnl_skb, nest);
			goto nla_put_failure;
		}
	}
	nla_nest_end(dcbnl_skb, nest);

	nlmsg_end(dcbnl_skb, nlh);

	return rtnl_unicast(dcbnl_skb, &init_net, pid);
nla_put_failure:
	nlmsg_cancel(dcbnl_skb, nlh);
nlmsg_failure:
	kfree_skb(dcbnl_skb);
err_out:
	return ret;
}

static int dcbnl_setfeatcfg(struct net_device *netdev, struct nlattr **tb,
			    u32 pid, u32 seq, u16 flags)
{
	struct nlattr *data[DCB_FEATCFG_ATTR_MAX + 1];
	int ret, i;
	u8 value;

	if (!netdev->dcbnl_ops->setfeatcfg)
		return -ENOTSUPP;

	if (!tb[DCB_ATTR_FEATCFG])
		return -EINVAL;

	ret = nla_parse_nested(data, DCB_FEATCFG_ATTR_MAX, tb[DCB_ATTR_FEATCFG],
			       dcbnl_featcfg_nest);

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
	dcbnl_reply(ret, RTM_SETDCB, DCB_CMD_SFEATCFG, DCB_ATTR_FEATCFG,
		    pid, seq, flags);

	return ret;
}

/* Handle CEE DCBX GET commands. */
static int dcbnl_cee_get(struct net_device *netdev, struct nlattr **tb,
			 u32 pid, u32 seq, u16 flags)
{
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	struct dcbmsg *dcb;
	struct nlattr *cee;
	const struct dcbnl_rtnl_ops *ops = netdev->dcbnl_ops;
	int err;

	if (!ops)
		return -EOPNOTSUPP;

	skb = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!skb)
		return -ENOBUFS;

	nlh = NLMSG_NEW(skb, pid, seq, RTM_GETDCB, sizeof(*dcb), flags);

	dcb = NLMSG_DATA(nlh);
	dcb->dcb_family = AF_UNSPEC;
	dcb->cmd = DCB_CMD_CEE_GET;

	NLA_PUT_STRING(skb, DCB_ATTR_IFNAME, netdev->name);

	cee = nla_nest_start(skb, DCB_ATTR_CEE);
	if (!cee)
		goto nla_put_failure;

	/* get peer info if available */
	if (ops->cee_peer_getpg) {
		struct cee_pg pg;
		err = ops->cee_peer_getpg(netdev, &pg);
		if (!err)
			NLA_PUT(skb, DCB_ATTR_CEE_PEER_PG, sizeof(pg), &pg);
	}

	if (ops->cee_peer_getpfc) {
		struct cee_pfc pfc;
		err = ops->cee_peer_getpfc(netdev, &pfc);
		if (!err)
			NLA_PUT(skb, DCB_ATTR_CEE_PEER_PFC, sizeof(pfc), &pfc);
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
	nlmsg_end(skb, nlh);

	return rtnl_unicast(skb, &init_net, pid);
nla_put_failure:
	nlmsg_cancel(skb, nlh);
nlmsg_failure:
	kfree_skb(skb);
	return -1;
}

static int dcb_doit(struct sk_buff *skb, struct nlmsghdr *nlh, void *arg)
{
	struct net *net = sock_net(skb->sk);
	struct net_device *netdev;
	struct dcbmsg  *dcb = (struct dcbmsg *)NLMSG_DATA(nlh);
	struct nlattr *tb[DCB_ATTR_MAX + 1];
	u32 pid = skb ? NETLINK_CB(skb).pid : 0;
	int ret = -EINVAL;

	if (!net_eq(net, &init_net))
		return -EINVAL;

	ret = nlmsg_parse(nlh, sizeof(*dcb), tb, DCB_ATTR_MAX,
			  dcbnl_rtnl_policy);
	if (ret < 0)
		return ret;

	if (!tb[DCB_ATTR_IFNAME])
		return -EINVAL;

	netdev = dev_get_by_name(&init_net, nla_data(tb[DCB_ATTR_IFNAME]));
	if (!netdev)
		return -EINVAL;

	if (!netdev->dcbnl_ops)
		goto errout;

	switch (dcb->cmd) {
	case DCB_CMD_GSTATE:
		ret = dcbnl_getstate(netdev, tb, pid, nlh->nlmsg_seq,
		                     nlh->nlmsg_flags);
		goto out;
	case DCB_CMD_PFC_GCFG:
		ret = dcbnl_getpfccfg(netdev, tb, pid, nlh->nlmsg_seq,
		                      nlh->nlmsg_flags);
		goto out;
	case DCB_CMD_GPERM_HWADDR:
		ret = dcbnl_getperm_hwaddr(netdev, tb, pid, nlh->nlmsg_seq,
		                           nlh->nlmsg_flags);
		goto out;
	case DCB_CMD_PGTX_GCFG:
		ret = dcbnl_pgtx_getcfg(netdev, tb, pid, nlh->nlmsg_seq,
		                        nlh->nlmsg_flags);
		goto out;
	case DCB_CMD_PGRX_GCFG:
		ret = dcbnl_pgrx_getcfg(netdev, tb, pid, nlh->nlmsg_seq,
		                        nlh->nlmsg_flags);
		goto out;
	case DCB_CMD_BCN_GCFG:
		ret = dcbnl_bcn_getcfg(netdev, tb, pid, nlh->nlmsg_seq,
		                       nlh->nlmsg_flags);
		goto out;
	case DCB_CMD_SSTATE:
		ret = dcbnl_setstate(netdev, tb, pid, nlh->nlmsg_seq,
		                     nlh->nlmsg_flags);
		goto out;
	case DCB_CMD_PFC_SCFG:
		ret = dcbnl_setpfccfg(netdev, tb, pid, nlh->nlmsg_seq,
		                      nlh->nlmsg_flags);
		goto out;

	case DCB_CMD_SET_ALL:
		ret = dcbnl_setall(netdev, tb, pid, nlh->nlmsg_seq,
		                   nlh->nlmsg_flags);
		goto out;
	case DCB_CMD_PGTX_SCFG:
		ret = dcbnl_pgtx_setcfg(netdev, tb, pid, nlh->nlmsg_seq,
		                        nlh->nlmsg_flags);
		goto out;
	case DCB_CMD_PGRX_SCFG:
		ret = dcbnl_pgrx_setcfg(netdev, tb, pid, nlh->nlmsg_seq,
		                        nlh->nlmsg_flags);
		goto out;
	case DCB_CMD_GCAP:
		ret = dcbnl_getcap(netdev, tb, pid, nlh->nlmsg_seq,
		                   nlh->nlmsg_flags);
		goto out;
	case DCB_CMD_GNUMTCS:
		ret = dcbnl_getnumtcs(netdev, tb, pid, nlh->nlmsg_seq,
		                      nlh->nlmsg_flags);
		goto out;
	case DCB_CMD_SNUMTCS:
		ret = dcbnl_setnumtcs(netdev, tb, pid, nlh->nlmsg_seq,
		                      nlh->nlmsg_flags);
		goto out;
	case DCB_CMD_PFC_GSTATE:
		ret = dcbnl_getpfcstate(netdev, tb, pid, nlh->nlmsg_seq,
		                        nlh->nlmsg_flags);
		goto out;
	case DCB_CMD_PFC_SSTATE:
		ret = dcbnl_setpfcstate(netdev, tb, pid, nlh->nlmsg_seq,
		                        nlh->nlmsg_flags);
		goto out;
	case DCB_CMD_BCN_SCFG:
		ret = dcbnl_bcn_setcfg(netdev, tb, pid, nlh->nlmsg_seq,
		                       nlh->nlmsg_flags);
		goto out;
	case DCB_CMD_GAPP:
		ret = dcbnl_getapp(netdev, tb, pid, nlh->nlmsg_seq,
		                   nlh->nlmsg_flags);
		goto out;
	case DCB_CMD_SAPP:
		ret = dcbnl_setapp(netdev, tb, pid, nlh->nlmsg_seq,
		                   nlh->nlmsg_flags);
		goto out;
	case DCB_CMD_IEEE_SET:
		ret = dcbnl_ieee_set(netdev, tb, pid, nlh->nlmsg_seq,
				 nlh->nlmsg_flags);
		goto out;
	case DCB_CMD_IEEE_GET:
		ret = dcbnl_ieee_get(netdev, tb, pid, nlh->nlmsg_seq,
				 nlh->nlmsg_flags);
		goto out;
	case DCB_CMD_GDCBX:
		ret = dcbnl_getdcbx(netdev, tb, pid, nlh->nlmsg_seq,
				    nlh->nlmsg_flags);
		goto out;
	case DCB_CMD_SDCBX:
		ret = dcbnl_setdcbx(netdev, tb, pid, nlh->nlmsg_seq,
				    nlh->nlmsg_flags);
		goto out;
	case DCB_CMD_GFEATCFG:
		ret = dcbnl_getfeatcfg(netdev, tb, pid, nlh->nlmsg_seq,
				       nlh->nlmsg_flags);
		goto out;
	case DCB_CMD_SFEATCFG:
		ret = dcbnl_setfeatcfg(netdev, tb, pid, nlh->nlmsg_seq,
				       nlh->nlmsg_flags);
		goto out;
	case DCB_CMD_CEE_GET:
		ret = dcbnl_cee_get(netdev, tb, pid, nlh->nlmsg_seq,
				    nlh->nlmsg_flags);
		goto out;
	default:
		goto errout;
	}
errout:
	ret = -EINVAL;
out:
	dev_put(netdev);
	return ret;
}

/**
 * dcb_getapp - retrieve the DCBX application user priority
 *
 * On success returns a non-zero 802.1p user priority bitmap
 * otherwise returns 0 as the invalid user priority bitmap to
 * indicate an error.
 */
u8 dcb_getapp(struct net_device *dev, struct dcb_app *app)
{
	struct dcb_app_type *itr;
	u8 prio = 0;

	spin_lock(&dcb_lock);
	list_for_each_entry(itr, &dcb_app_list, list) {
		if (itr->app.selector == app->selector &&
		    itr->app.protocol == app->protocol &&
		    (strncmp(itr->name, dev->name, IFNAMSIZ) == 0)) {
			prio = itr->app.priority;
			break;
		}
	}
	spin_unlock(&dcb_lock);

	return prio;
}
EXPORT_SYMBOL(dcb_getapp);

/**
 * ixgbe_dcbnl_setapp - add dcb application data to app list
 *
 * Priority 0 is the default priority this removes applications
 * from the app list if the priority is set to zero.
 */
u8 dcb_setapp(struct net_device *dev, struct dcb_app *new)
{
	struct dcb_app_type *itr;
	struct dcb_app_type event;

	memcpy(&event.name, dev->name, sizeof(event.name));
	memcpy(&event.app, new, sizeof(event.app));

	spin_lock(&dcb_lock);
	/* Search for existing match and replace */
	list_for_each_entry(itr, &dcb_app_list, list) {
		if (itr->app.selector == new->selector &&
		    itr->app.protocol == new->protocol &&
		    (strncmp(itr->name, dev->name, IFNAMSIZ) == 0)) {
			if (new->priority)
				itr->app.priority = new->priority;
			else {
				list_del(&itr->list);
				kfree(itr);
			}
			goto out;
		}
	}
	/* App type does not exist add new application type */
	if (new->priority) {
		struct dcb_app_type *entry;
		entry = kmalloc(sizeof(struct dcb_app_type), GFP_ATOMIC);
		if (!entry) {
			spin_unlock(&dcb_lock);
			return -ENOMEM;
		}

		memcpy(&entry->app, new, sizeof(*new));
		strncpy(entry->name, dev->name, IFNAMSIZ);
		list_add(&entry->list, &dcb_app_list);
	}
out:
	spin_unlock(&dcb_lock);
	call_dcbevent_notifiers(DCB_APP_EVENT, &event);
	return 0;
}
EXPORT_SYMBOL(dcb_setapp);

static void dcb_flushapp(void)
{
	struct dcb_app_type *app;
	struct dcb_app_type *tmp;

	spin_lock(&dcb_lock);
	list_for_each_entry_safe(app, tmp, &dcb_app_list, list) {
		list_del(&app->list);
		kfree(app);
	}
	spin_unlock(&dcb_lock);
}

static int __init dcbnl_init(void)
{
	INIT_LIST_HEAD(&dcb_app_list);

	rtnl_register(PF_UNSPEC, RTM_GETDCB, dcb_doit, NULL, NULL);
	rtnl_register(PF_UNSPEC, RTM_SETDCB, dcb_doit, NULL, NULL);

	return 0;
}
module_init(dcbnl_init);

static void __exit dcbnl_exit(void)
{
	rtnl_unregister(PF_UNSPEC, RTM_GETDCB);
	rtnl_unregister(PF_UNSPEC, RTM_SETDCB);
	dcb_flushapp();
}
module_exit(dcbnl_exit);
