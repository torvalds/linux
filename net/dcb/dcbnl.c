/*
 * Copyright (c) 2008, Intel Corporation.
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
#include <net/netlink.h>
#include <net/rtnetlink.h>
#include <linux/dcbnl.h>
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

	if (!tb[DCB_ATTR_APP] || !netdev->dcbnl_ops->getapp)
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
	up = netdev->dcbnl_ops->getapp(netdev, idtype, id);

	/* send this back */
	dcbnl_skb = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!dcbnl_skb)
		goto out;

	nlh = NLMSG_NEW(dcbnl_skb, pid, seq, RTM_GETDCB, sizeof(*dcb), flags);
	dcb = NLMSG_DATA(nlh);
	dcb->dcb_family = AF_UNSPEC;
	dcb->cmd = DCB_CMD_GAPP;

	app_nest = nla_nest_start(dcbnl_skb, DCB_ATTR_APP);
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
	int ret = -EINVAL;
	u16 id;
	u8 up, idtype;
	struct nlattr *app_tb[DCB_APP_ATTR_MAX + 1];

	if (!tb[DCB_ATTR_APP] || !netdev->dcbnl_ops->setapp)
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

	ret = dcbnl_reply(netdev->dcbnl_ops->setapp(netdev, idtype, id, up),
	                  RTM_SETDCB, DCB_CMD_SAPP, DCB_ATTR_APP,
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
	default:
		goto errout;
	}
errout:
	ret = -EINVAL;
out:
	dev_put(netdev);
	return ret;
}

static int __init dcbnl_init(void)
{
	rtnl_register(PF_UNSPEC, RTM_GETDCB, dcb_doit, NULL);
	rtnl_register(PF_UNSPEC, RTM_SETDCB, dcb_doit, NULL);

	return 0;
}
module_init(dcbnl_init);

static void __exit dcbnl_exit(void)
{
	rtnl_unregister(PF_UNSPEC, RTM_GETDCB);
	rtnl_unregister(PF_UNSPEC, RTM_SETDCB);
}
module_exit(dcbnl_exit);


