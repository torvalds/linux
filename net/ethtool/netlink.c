// SPDX-License-Identifier: GPL-2.0-only

#include <net/sock.h>
#include <linux/ethtool_netlink.h>
#include "netlink.h"

static struct genl_family ethtool_genl_family;

static bool ethnl_ok __read_mostly;

static const struct nla_policy ethnl_header_policy[ETHTOOL_A_HEADER_MAX + 1] = {
	[ETHTOOL_A_HEADER_UNSPEC]	= { .type = NLA_REJECT },
	[ETHTOOL_A_HEADER_DEV_INDEX]	= { .type = NLA_U32 },
	[ETHTOOL_A_HEADER_DEV_NAME]	= { .type = NLA_NUL_STRING,
					    .len = ALTIFNAMSIZ - 1 },
	[ETHTOOL_A_HEADER_FLAGS]	= { .type = NLA_U32 },
};

/**
 * ethnl_parse_header() - parse request header
 * @req_info:    structure to put results into
 * @header:      nest attribute with request header
 * @net:         request netns
 * @extack:      netlink extack for error reporting
 * @require_dev: fail if no device identified in header
 *
 * Parse request header in nested attribute @nest and puts results into
 * the structure pointed to by @req_info. Extack from @info is used for error
 * reporting. If req_info->dev is not null on return, reference to it has
 * been taken. If error is returned, *req_info is null initialized and no
 * reference is held.
 *
 * Return: 0 on success or negative error code
 */
int ethnl_parse_header(struct ethnl_req_info *req_info,
		       const struct nlattr *header, struct net *net,
		       struct netlink_ext_ack *extack, bool require_dev)
{
	struct nlattr *tb[ETHTOOL_A_HEADER_MAX + 1];
	const struct nlattr *devname_attr;
	struct net_device *dev = NULL;
	int ret;

	if (!header) {
		NL_SET_ERR_MSG(extack, "request header missing");
		return -EINVAL;
	}
	ret = nla_parse_nested(tb, ETHTOOL_A_HEADER_MAX, header,
			       ethnl_header_policy, extack);
	if (ret < 0)
		return ret;
	devname_attr = tb[ETHTOOL_A_HEADER_DEV_NAME];

	if (tb[ETHTOOL_A_HEADER_DEV_INDEX]) {
		u32 ifindex = nla_get_u32(tb[ETHTOOL_A_HEADER_DEV_INDEX]);

		dev = dev_get_by_index(net, ifindex);
		if (!dev) {
			NL_SET_ERR_MSG_ATTR(extack,
					    tb[ETHTOOL_A_HEADER_DEV_INDEX],
					    "no device matches ifindex");
			return -ENODEV;
		}
		/* if both ifindex and ifname are passed, they must match */
		if (devname_attr &&
		    strncmp(dev->name, nla_data(devname_attr), IFNAMSIZ)) {
			dev_put(dev);
			NL_SET_ERR_MSG_ATTR(extack, header,
					    "ifindex and name do not match");
			return -ENODEV;
		}
	} else if (devname_attr) {
		dev = dev_get_by_name(net, nla_data(devname_attr));
		if (!dev) {
			NL_SET_ERR_MSG_ATTR(extack, devname_attr,
					    "no device matches name");
			return -ENODEV;
		}
	} else if (require_dev) {
		NL_SET_ERR_MSG_ATTR(extack, header,
				    "neither ifindex nor name specified");
		return -EINVAL;
	}

	if (dev && !netif_device_present(dev)) {
		dev_put(dev);
		NL_SET_ERR_MSG(extack, "device not present");
		return -ENODEV;
	}

	req_info->dev = dev;
	if (tb[ETHTOOL_A_HEADER_FLAGS])
		req_info->flags = nla_get_u32(tb[ETHTOOL_A_HEADER_FLAGS]);

	return 0;
}

/**
 * ethnl_fill_reply_header() - Put common header into a reply message
 * @skb:      skb with the message
 * @dev:      network device to describe in header
 * @attrtype: attribute type to use for the nest
 *
 * Create a nested attribute with attributes describing given network device.
 *
 * Return: 0 on success, error value (-EMSGSIZE only) on error
 */
int ethnl_fill_reply_header(struct sk_buff *skb, struct net_device *dev,
			    u16 attrtype)
{
	struct nlattr *nest;

	if (!dev)
		return 0;
	nest = nla_nest_start(skb, attrtype);
	if (!nest)
		return -EMSGSIZE;

	if (nla_put_u32(skb, ETHTOOL_A_HEADER_DEV_INDEX, (u32)dev->ifindex) ||
	    nla_put_string(skb, ETHTOOL_A_HEADER_DEV_NAME, dev->name))
		goto nla_put_failure;
	/* If more attributes are put into reply header, ethnl_header_size()
	 * must be updated to account for them.
	 */

	nla_nest_end(skb, nest);
	return 0;

nla_put_failure:
	nla_nest_cancel(skb, nest);
	return -EMSGSIZE;
}

/**
 * ethnl_reply_init() - Create skb for a reply and fill device identification
 * @payload: payload length (without netlink and genetlink header)
 * @dev:     device the reply is about (may be null)
 * @cmd:     ETHTOOL_MSG_* message type for reply
 * @info:    genetlink info of the received packet we respond to
 * @ehdrp:   place to store payload pointer returned by genlmsg_new()
 *
 * Return: pointer to allocated skb on success, NULL on error
 */
struct sk_buff *ethnl_reply_init(size_t payload, struct net_device *dev, u8 cmd,
				 u16 hdr_attrtype, struct genl_info *info,
				 void **ehdrp)
{
	struct sk_buff *skb;

	skb = genlmsg_new(payload, GFP_KERNEL);
	if (!skb)
		goto err;
	*ehdrp = genlmsg_put_reply(skb, info, &ethtool_genl_family, 0, cmd);
	if (!*ehdrp)
		goto err_free;

	if (dev) {
		int ret;

		ret = ethnl_fill_reply_header(skb, dev, hdr_attrtype);
		if (ret < 0)
			goto err_free;
	}
	return skb;

err_free:
	nlmsg_free(skb);
err:
	if (info)
		GENL_SET_ERR_MSG(info, "failed to setup reply message");
	return NULL;
}

/* notifications */

typedef void (*ethnl_notify_handler_t)(struct net_device *dev, unsigned int cmd,
				       const void *data);

static const ethnl_notify_handler_t ethnl_notify_handlers[] = {
};

void ethtool_notify(struct net_device *dev, unsigned int cmd, const void *data)
{
	if (unlikely(!ethnl_ok))
		return;
	ASSERT_RTNL();

	if (likely(cmd < ARRAY_SIZE(ethnl_notify_handlers) &&
		   ethnl_notify_handlers[cmd]))
		ethnl_notify_handlers[cmd](dev, cmd, data);
	else
		WARN_ONCE(1, "notification %u not implemented (dev=%s)\n",
			  cmd, netdev_name(dev));
}
EXPORT_SYMBOL(ethtool_notify);

/* genetlink setup */

static const struct genl_ops ethtool_genl_ops[] = {
};

static const struct genl_multicast_group ethtool_nl_mcgrps[] = {
	[ETHNL_MCGRP_MONITOR] = { .name = ETHTOOL_MCGRP_MONITOR_NAME },
};

static struct genl_family ethtool_genl_family = {
	.name		= ETHTOOL_GENL_NAME,
	.version	= ETHTOOL_GENL_VERSION,
	.netnsok	= true,
	.parallel_ops	= true,
	.ops		= ethtool_genl_ops,
	.n_ops		= ARRAY_SIZE(ethtool_genl_ops),
	.mcgrps		= ethtool_nl_mcgrps,
	.n_mcgrps	= ARRAY_SIZE(ethtool_nl_mcgrps),
};

/* module setup */

static int __init ethnl_init(void)
{
	int ret;

	ret = genl_register_family(&ethtool_genl_family);
	if (WARN(ret < 0, "ethtool: genetlink family registration failed"))
		return ret;
	ethnl_ok = true;

	return 0;
}

subsys_initcall(ethnl_init);
