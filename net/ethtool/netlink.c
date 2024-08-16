// SPDX-License-Identifier: GPL-2.0-only

#include <net/sock.h>
#include <linux/ethtool_netlink.h>
#include <linux/pm_runtime.h>
#include "netlink.h"
#include "module_fw.h"

static struct genl_family ethtool_genl_family;

static bool ethnl_ok __read_mostly;
static u32 ethnl_bcast_seq;

#define ETHTOOL_FLAGS_BASIC (ETHTOOL_FLAG_COMPACT_BITSETS |	\
			     ETHTOOL_FLAG_OMIT_REPLY)
#define ETHTOOL_FLAGS_STATS (ETHTOOL_FLAGS_BASIC | ETHTOOL_FLAG_STATS)

const struct nla_policy ethnl_header_policy[] = {
	[ETHTOOL_A_HEADER_DEV_INDEX]	= { .type = NLA_U32 },
	[ETHTOOL_A_HEADER_DEV_NAME]	= { .type = NLA_NUL_STRING,
					    .len = ALTIFNAMSIZ - 1 },
	[ETHTOOL_A_HEADER_FLAGS]	= NLA_POLICY_MASK(NLA_U32,
							  ETHTOOL_FLAGS_BASIC),
};

const struct nla_policy ethnl_header_policy_stats[] = {
	[ETHTOOL_A_HEADER_DEV_INDEX]	= { .type = NLA_U32 },
	[ETHTOOL_A_HEADER_DEV_NAME]	= { .type = NLA_NUL_STRING,
					    .len = ALTIFNAMSIZ - 1 },
	[ETHTOOL_A_HEADER_FLAGS]	= NLA_POLICY_MASK(NLA_U32,
							  ETHTOOL_FLAGS_STATS),
};

int ethnl_sock_priv_set(struct sk_buff *skb, struct net_device *dev, u32 portid,
			enum ethnl_sock_type type)
{
	struct ethnl_sock_priv *sk_priv;

	sk_priv = genl_sk_priv_get(&ethtool_genl_family, NETLINK_CB(skb).sk);
	if (IS_ERR(sk_priv))
		return PTR_ERR(sk_priv);

	sk_priv->dev = dev;
	sk_priv->portid = portid;
	sk_priv->type = type;

	return 0;
}

static void ethnl_sock_priv_destroy(void *priv)
{
	struct ethnl_sock_priv *sk_priv = priv;

	switch (sk_priv->type) {
	case ETHTOOL_SOCK_TYPE_MODULE_FW_FLASH:
		ethnl_module_fw_flash_sock_destroy(sk_priv);
		break;
	default:
		break;
	}
}

int ethnl_ops_begin(struct net_device *dev)
{
	int ret;

	if (!dev)
		return -ENODEV;

	if (dev->dev.parent)
		pm_runtime_get_sync(dev->dev.parent);

	if (!netif_device_present(dev) ||
	    dev->reg_state == NETREG_UNREGISTERING) {
		ret = -ENODEV;
		goto err;
	}

	if (dev->ethtool_ops->begin) {
		ret = dev->ethtool_ops->begin(dev);
		if (ret)
			goto err;
	}

	return 0;
err:
	if (dev->dev.parent)
		pm_runtime_put(dev->dev.parent);

	return ret;
}

void ethnl_ops_complete(struct net_device *dev)
{
	if (dev->ethtool_ops->complete)
		dev->ethtool_ops->complete(dev);

	if (dev->dev.parent)
		pm_runtime_put(dev->dev.parent);
}

/**
 * ethnl_parse_header_dev_get() - parse request header
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
int ethnl_parse_header_dev_get(struct ethnl_req_info *req_info,
			       const struct nlattr *header, struct net *net,
			       struct netlink_ext_ack *extack, bool require_dev)
{
	struct nlattr *tb[ARRAY_SIZE(ethnl_header_policy)];
	const struct nlattr *devname_attr;
	struct net_device *dev = NULL;
	u32 flags = 0;
	int ret;

	if (!header) {
		if (!require_dev)
			return 0;
		NL_SET_ERR_MSG(extack, "request header missing");
		return -EINVAL;
	}
	/* No validation here, command policy should have a nested policy set
	 * for the header, therefore validation should have already been done.
	 */
	ret = nla_parse_nested(tb, ARRAY_SIZE(ethnl_header_policy) - 1, header,
			       NULL, extack);
	if (ret < 0)
		return ret;
	if (tb[ETHTOOL_A_HEADER_FLAGS])
		flags = nla_get_u32(tb[ETHTOOL_A_HEADER_FLAGS]);

	devname_attr = tb[ETHTOOL_A_HEADER_DEV_NAME];
	if (tb[ETHTOOL_A_HEADER_DEV_INDEX]) {
		u32 ifindex = nla_get_u32(tb[ETHTOOL_A_HEADER_DEV_INDEX]);

		dev = netdev_get_by_index(net, ifindex, &req_info->dev_tracker,
					  GFP_KERNEL);
		if (!dev) {
			NL_SET_ERR_MSG_ATTR(extack,
					    tb[ETHTOOL_A_HEADER_DEV_INDEX],
					    "no device matches ifindex");
			return -ENODEV;
		}
		/* if both ifindex and ifname are passed, they must match */
		if (devname_attr &&
		    strncmp(dev->name, nla_data(devname_attr), IFNAMSIZ)) {
			netdev_put(dev, &req_info->dev_tracker);
			NL_SET_ERR_MSG_ATTR(extack, header,
					    "ifindex and name do not match");
			return -ENODEV;
		}
	} else if (devname_attr) {
		dev = netdev_get_by_name(net, nla_data(devname_attr),
					 &req_info->dev_tracker, GFP_KERNEL);
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

	req_info->dev = dev;
	req_info->flags = flags;
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
 * @payload:      payload length (without netlink and genetlink header)
 * @dev:          device the reply is about (may be null)
 * @cmd:          ETHTOOL_MSG_* message type for reply
 * @hdr_attrtype: attribute type for common header
 * @info:         genetlink info of the received packet we respond to
 * @ehdrp:        place to store payload pointer returned by genlmsg_new()
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

void *ethnl_dump_put(struct sk_buff *skb, struct netlink_callback *cb, u8 cmd)
{
	return genlmsg_put(skb, NETLINK_CB(cb->skb).portid, cb->nlh->nlmsg_seq,
			   &ethtool_genl_family, 0, cmd);
}

void *ethnl_bcastmsg_put(struct sk_buff *skb, u8 cmd)
{
	return genlmsg_put(skb, 0, ++ethnl_bcast_seq, &ethtool_genl_family, 0,
			   cmd);
}

void *ethnl_unicast_put(struct sk_buff *skb, u32 portid, u32 seq, u8 cmd)
{
	return genlmsg_put(skb, portid, seq, &ethtool_genl_family, 0, cmd);
}

int ethnl_multicast(struct sk_buff *skb, struct net_device *dev)
{
	return genlmsg_multicast_netns(&ethtool_genl_family, dev_net(dev), skb,
				       0, ETHNL_MCGRP_MONITOR, GFP_KERNEL);
}

/* GET request helpers */

/**
 * struct ethnl_dump_ctx - context structure for generic dumpit() callback
 * @ops:        request ops of currently processed message type
 * @req_info:   parsed request header of processed request
 * @reply_data: data needed to compose the reply
 * @pos_ifindex: saved iteration position - ifindex
 *
 * These parameters are kept in struct netlink_callback as context preserved
 * between iterations. They are initialized by ethnl_default_start() and used
 * in ethnl_default_dumpit() and ethnl_default_done().
 */
struct ethnl_dump_ctx {
	const struct ethnl_request_ops	*ops;
	struct ethnl_req_info		*req_info;
	struct ethnl_reply_data		*reply_data;
	unsigned long			pos_ifindex;
};

static const struct ethnl_request_ops *
ethnl_default_requests[__ETHTOOL_MSG_USER_CNT] = {
	[ETHTOOL_MSG_STRSET_GET]	= &ethnl_strset_request_ops,
	[ETHTOOL_MSG_LINKINFO_GET]	= &ethnl_linkinfo_request_ops,
	[ETHTOOL_MSG_LINKINFO_SET]	= &ethnl_linkinfo_request_ops,
	[ETHTOOL_MSG_LINKMODES_GET]	= &ethnl_linkmodes_request_ops,
	[ETHTOOL_MSG_LINKMODES_SET]	= &ethnl_linkmodes_request_ops,
	[ETHTOOL_MSG_LINKSTATE_GET]	= &ethnl_linkstate_request_ops,
	[ETHTOOL_MSG_DEBUG_GET]		= &ethnl_debug_request_ops,
	[ETHTOOL_MSG_DEBUG_SET]		= &ethnl_debug_request_ops,
	[ETHTOOL_MSG_WOL_GET]		= &ethnl_wol_request_ops,
	[ETHTOOL_MSG_WOL_SET]		= &ethnl_wol_request_ops,
	[ETHTOOL_MSG_FEATURES_GET]	= &ethnl_features_request_ops,
	[ETHTOOL_MSG_PRIVFLAGS_GET]	= &ethnl_privflags_request_ops,
	[ETHTOOL_MSG_PRIVFLAGS_SET]	= &ethnl_privflags_request_ops,
	[ETHTOOL_MSG_RINGS_GET]		= &ethnl_rings_request_ops,
	[ETHTOOL_MSG_RINGS_SET]		= &ethnl_rings_request_ops,
	[ETHTOOL_MSG_CHANNELS_GET]	= &ethnl_channels_request_ops,
	[ETHTOOL_MSG_CHANNELS_SET]	= &ethnl_channels_request_ops,
	[ETHTOOL_MSG_COALESCE_GET]	= &ethnl_coalesce_request_ops,
	[ETHTOOL_MSG_COALESCE_SET]	= &ethnl_coalesce_request_ops,
	[ETHTOOL_MSG_PAUSE_GET]		= &ethnl_pause_request_ops,
	[ETHTOOL_MSG_PAUSE_SET]		= &ethnl_pause_request_ops,
	[ETHTOOL_MSG_EEE_GET]		= &ethnl_eee_request_ops,
	[ETHTOOL_MSG_EEE_SET]		= &ethnl_eee_request_ops,
	[ETHTOOL_MSG_FEC_GET]		= &ethnl_fec_request_ops,
	[ETHTOOL_MSG_FEC_SET]		= &ethnl_fec_request_ops,
	[ETHTOOL_MSG_TSINFO_GET]	= &ethnl_tsinfo_request_ops,
	[ETHTOOL_MSG_MODULE_EEPROM_GET]	= &ethnl_module_eeprom_request_ops,
	[ETHTOOL_MSG_STATS_GET]		= &ethnl_stats_request_ops,
	[ETHTOOL_MSG_PHC_VCLOCKS_GET]	= &ethnl_phc_vclocks_request_ops,
	[ETHTOOL_MSG_MODULE_GET]	= &ethnl_module_request_ops,
	[ETHTOOL_MSG_MODULE_SET]	= &ethnl_module_request_ops,
	[ETHTOOL_MSG_PSE_GET]		= &ethnl_pse_request_ops,
	[ETHTOOL_MSG_PSE_SET]		= &ethnl_pse_request_ops,
	[ETHTOOL_MSG_RSS_GET]		= &ethnl_rss_request_ops,
	[ETHTOOL_MSG_PLCA_GET_CFG]	= &ethnl_plca_cfg_request_ops,
	[ETHTOOL_MSG_PLCA_SET_CFG]	= &ethnl_plca_cfg_request_ops,
	[ETHTOOL_MSG_PLCA_GET_STATUS]	= &ethnl_plca_status_request_ops,
	[ETHTOOL_MSG_MM_GET]		= &ethnl_mm_request_ops,
	[ETHTOOL_MSG_MM_SET]		= &ethnl_mm_request_ops,
};

static struct ethnl_dump_ctx *ethnl_dump_context(struct netlink_callback *cb)
{
	return (struct ethnl_dump_ctx *)cb->ctx;
}

/**
 * ethnl_default_parse() - Parse request message
 * @req_info:    pointer to structure to put data into
 * @info:	 genl_info from the request
 * @request_ops: struct request_ops for request type
 * @require_dev: fail if no device identified in header
 *
 * Parse universal request header and call request specific ->parse_request()
 * callback (if defined) to parse the rest of the message.
 *
 * Return: 0 on success or negative error code
 */
static int ethnl_default_parse(struct ethnl_req_info *req_info,
			       const struct genl_info *info,
			       const struct ethnl_request_ops *request_ops,
			       bool require_dev)
{
	struct nlattr **tb = info->attrs;
	int ret;

	ret = ethnl_parse_header_dev_get(req_info, tb[request_ops->hdr_attr],
					 genl_info_net(info), info->extack,
					 require_dev);
	if (ret < 0)
		return ret;

	if (request_ops->parse_request) {
		ret = request_ops->parse_request(req_info, tb, info->extack);
		if (ret < 0)
			return ret;
	}

	return 0;
}

/**
 * ethnl_init_reply_data() - Initialize reply data for GET request
 * @reply_data: pointer to embedded struct ethnl_reply_data
 * @ops:        instance of struct ethnl_request_ops describing the layout
 * @dev:        network device to initialize the reply for
 *
 * Fills the reply data part with zeros and sets the dev member. Must be called
 * before calling the ->fill_reply() callback (for each iteration when handling
 * dump requests).
 */
static void ethnl_init_reply_data(struct ethnl_reply_data *reply_data,
				  const struct ethnl_request_ops *ops,
				  struct net_device *dev)
{
	memset(reply_data, 0, ops->reply_data_size);
	reply_data->dev = dev;
}

/* default ->doit() handler for GET type requests */
static int ethnl_default_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct ethnl_reply_data *reply_data = NULL;
	struct ethnl_req_info *req_info = NULL;
	const u8 cmd = info->genlhdr->cmd;
	const struct ethnl_request_ops *ops;
	int hdr_len, reply_len;
	struct sk_buff *rskb;
	void *reply_payload;
	int ret;

	ops = ethnl_default_requests[cmd];
	if (WARN_ONCE(!ops, "cmd %u has no ethnl_request_ops\n", cmd))
		return -EOPNOTSUPP;
	if (GENL_REQ_ATTR_CHECK(info, ops->hdr_attr))
		return -EINVAL;

	req_info = kzalloc(ops->req_info_size, GFP_KERNEL);
	if (!req_info)
		return -ENOMEM;
	reply_data = kmalloc(ops->reply_data_size, GFP_KERNEL);
	if (!reply_data) {
		kfree(req_info);
		return -ENOMEM;
	}

	ret = ethnl_default_parse(req_info, info, ops, !ops->allow_nodev_do);
	if (ret < 0)
		goto err_dev;
	ethnl_init_reply_data(reply_data, ops, req_info->dev);

	rtnl_lock();
	ret = ops->prepare_data(req_info, reply_data, info);
	rtnl_unlock();
	if (ret < 0)
		goto err_cleanup;
	ret = ops->reply_size(req_info, reply_data);
	if (ret < 0)
		goto err_cleanup;
	reply_len = ret;
	ret = -ENOMEM;
	rskb = ethnl_reply_init(reply_len + ethnl_reply_header_size(),
				req_info->dev, ops->reply_cmd,
				ops->hdr_attr, info, &reply_payload);
	if (!rskb)
		goto err_cleanup;
	hdr_len = rskb->len;
	ret = ops->fill_reply(rskb, req_info, reply_data);
	if (ret < 0)
		goto err_msg;
	WARN_ONCE(rskb->len - hdr_len > reply_len,
		  "ethnl cmd %d: calculated reply length %d, but consumed %d\n",
		  cmd, reply_len, rskb->len - hdr_len);
	if (ops->cleanup_data)
		ops->cleanup_data(reply_data);

	genlmsg_end(rskb, reply_payload);
	netdev_put(req_info->dev, &req_info->dev_tracker);
	kfree(reply_data);
	kfree(req_info);
	return genlmsg_reply(rskb, info);

err_msg:
	WARN_ONCE(ret == -EMSGSIZE, "calculated message payload length (%d) not sufficient\n", reply_len);
	nlmsg_free(rskb);
err_cleanup:
	if (ops->cleanup_data)
		ops->cleanup_data(reply_data);
err_dev:
	netdev_put(req_info->dev, &req_info->dev_tracker);
	kfree(reply_data);
	kfree(req_info);
	return ret;
}

static int ethnl_default_dump_one(struct sk_buff *skb, struct net_device *dev,
				  const struct ethnl_dump_ctx *ctx,
				  const struct genl_info *info)
{
	void *ehdr;
	int ret;

	ehdr = genlmsg_put(skb, info->snd_portid, info->snd_seq,
			   &ethtool_genl_family, NLM_F_MULTI,
			   ctx->ops->reply_cmd);
	if (!ehdr)
		return -EMSGSIZE;

	ethnl_init_reply_data(ctx->reply_data, ctx->ops, dev);
	rtnl_lock();
	ret = ctx->ops->prepare_data(ctx->req_info, ctx->reply_data, info);
	rtnl_unlock();
	if (ret < 0)
		goto out;
	ret = ethnl_fill_reply_header(skb, dev, ctx->ops->hdr_attr);
	if (ret < 0)
		goto out;
	ret = ctx->ops->fill_reply(skb, ctx->req_info, ctx->reply_data);

out:
	if (ctx->ops->cleanup_data)
		ctx->ops->cleanup_data(ctx->reply_data);
	ctx->reply_data->dev = NULL;
	if (ret < 0)
		genlmsg_cancel(skb, ehdr);
	else
		genlmsg_end(skb, ehdr);
	return ret;
}

/* Default ->dumpit() handler for GET requests. */
static int ethnl_default_dumpit(struct sk_buff *skb,
				struct netlink_callback *cb)
{
	struct ethnl_dump_ctx *ctx = ethnl_dump_context(cb);
	struct net *net = sock_net(skb->sk);
	struct net_device *dev;
	int ret = 0;

	rcu_read_lock();
	for_each_netdev_dump(net, dev, ctx->pos_ifindex) {
		dev_hold(dev);
		rcu_read_unlock();

		ret = ethnl_default_dump_one(skb, dev, ctx, genl_info_dump(cb));

		rcu_read_lock();
		dev_put(dev);

		if (ret < 0 && ret != -EOPNOTSUPP) {
			if (likely(skb->len))
				ret = skb->len;
			break;
		}
		ret = 0;
	}
	rcu_read_unlock();

	return ret;
}

/* generic ->start() handler for GET requests */
static int ethnl_default_start(struct netlink_callback *cb)
{
	const struct genl_dumpit_info *info = genl_dumpit_info(cb);
	struct ethnl_dump_ctx *ctx = ethnl_dump_context(cb);
	struct ethnl_reply_data *reply_data;
	const struct ethnl_request_ops *ops;
	struct ethnl_req_info *req_info;
	struct genlmsghdr *ghdr;
	int ret;

	BUILD_BUG_ON(sizeof(*ctx) > sizeof(cb->ctx));

	ghdr = nlmsg_data(cb->nlh);
	ops = ethnl_default_requests[ghdr->cmd];
	if (WARN_ONCE(!ops, "cmd %u has no ethnl_request_ops\n", ghdr->cmd))
		return -EOPNOTSUPP;
	req_info = kzalloc(ops->req_info_size, GFP_KERNEL);
	if (!req_info)
		return -ENOMEM;
	reply_data = kmalloc(ops->reply_data_size, GFP_KERNEL);
	if (!reply_data) {
		ret = -ENOMEM;
		goto free_req_info;
	}

	ret = ethnl_default_parse(req_info, &info->info, ops, false);
	if (req_info->dev) {
		/* We ignore device specification in dump requests but as the
		 * same parser as for non-dump (doit) requests is used, it
		 * would take reference to the device if it finds one
		 */
		netdev_put(req_info->dev, &req_info->dev_tracker);
		req_info->dev = NULL;
	}
	if (ret < 0)
		goto free_reply_data;

	ctx->ops = ops;
	ctx->req_info = req_info;
	ctx->reply_data = reply_data;
	ctx->pos_ifindex = 0;

	return 0;

free_reply_data:
	kfree(reply_data);
free_req_info:
	kfree(req_info);

	return ret;
}

/* default ->done() handler for GET requests */
static int ethnl_default_done(struct netlink_callback *cb)
{
	struct ethnl_dump_ctx *ctx = ethnl_dump_context(cb);

	kfree(ctx->reply_data);
	kfree(ctx->req_info);

	return 0;
}

static int ethnl_default_set_doit(struct sk_buff *skb, struct genl_info *info)
{
	const struct ethnl_request_ops *ops;
	struct ethnl_req_info req_info = {};
	const u8 cmd = info->genlhdr->cmd;
	int ret;

	ops = ethnl_default_requests[cmd];
	if (WARN_ONCE(!ops, "cmd %u has no ethnl_request_ops\n", cmd))
		return -EOPNOTSUPP;
	if (GENL_REQ_ATTR_CHECK(info, ops->hdr_attr))
		return -EINVAL;

	ret = ethnl_parse_header_dev_get(&req_info, info->attrs[ops->hdr_attr],
					 genl_info_net(info), info->extack,
					 true);
	if (ret < 0)
		return ret;

	if (ops->set_validate) {
		ret = ops->set_validate(&req_info, info);
		/* 0 means nothing to do */
		if (ret <= 0)
			goto out_dev;
	}

	rtnl_lock();
	ret = ethnl_ops_begin(req_info.dev);
	if (ret < 0)
		goto out_rtnl;

	ret = ops->set(&req_info, info);
	if (ret <= 0)
		goto out_ops;
	ethtool_notify(req_info.dev, ops->set_ntf_cmd, NULL);

	ret = 0;
out_ops:
	ethnl_ops_complete(req_info.dev);
out_rtnl:
	rtnl_unlock();
out_dev:
	ethnl_parse_header_dev_put(&req_info);
	return ret;
}

static const struct ethnl_request_ops *
ethnl_default_notify_ops[ETHTOOL_MSG_KERNEL_MAX + 1] = {
	[ETHTOOL_MSG_LINKINFO_NTF]	= &ethnl_linkinfo_request_ops,
	[ETHTOOL_MSG_LINKMODES_NTF]	= &ethnl_linkmodes_request_ops,
	[ETHTOOL_MSG_DEBUG_NTF]		= &ethnl_debug_request_ops,
	[ETHTOOL_MSG_WOL_NTF]		= &ethnl_wol_request_ops,
	[ETHTOOL_MSG_FEATURES_NTF]	= &ethnl_features_request_ops,
	[ETHTOOL_MSG_PRIVFLAGS_NTF]	= &ethnl_privflags_request_ops,
	[ETHTOOL_MSG_RINGS_NTF]		= &ethnl_rings_request_ops,
	[ETHTOOL_MSG_CHANNELS_NTF]	= &ethnl_channels_request_ops,
	[ETHTOOL_MSG_COALESCE_NTF]	= &ethnl_coalesce_request_ops,
	[ETHTOOL_MSG_PAUSE_NTF]		= &ethnl_pause_request_ops,
	[ETHTOOL_MSG_EEE_NTF]		= &ethnl_eee_request_ops,
	[ETHTOOL_MSG_FEC_NTF]		= &ethnl_fec_request_ops,
	[ETHTOOL_MSG_MODULE_NTF]	= &ethnl_module_request_ops,
	[ETHTOOL_MSG_PLCA_NTF]		= &ethnl_plca_cfg_request_ops,
	[ETHTOOL_MSG_MM_NTF]		= &ethnl_mm_request_ops,
};

/* default notification handler */
static void ethnl_default_notify(struct net_device *dev, unsigned int cmd,
				 const void *data)
{
	struct ethnl_reply_data *reply_data;
	const struct ethnl_request_ops *ops;
	struct ethnl_req_info *req_info;
	struct genl_info info;
	struct sk_buff *skb;
	void *reply_payload;
	int reply_len;
	int ret;

	genl_info_init_ntf(&info, &ethtool_genl_family, cmd);

	if (WARN_ONCE(cmd > ETHTOOL_MSG_KERNEL_MAX ||
		      !ethnl_default_notify_ops[cmd],
		      "unexpected notification type %u\n", cmd))
		return;
	ops = ethnl_default_notify_ops[cmd];
	req_info = kzalloc(ops->req_info_size, GFP_KERNEL);
	if (!req_info)
		return;
	reply_data = kmalloc(ops->reply_data_size, GFP_KERNEL);
	if (!reply_data) {
		kfree(req_info);
		return;
	}

	req_info->dev = dev;
	req_info->flags |= ETHTOOL_FLAG_COMPACT_BITSETS;

	ethnl_init_reply_data(reply_data, ops, dev);
	ret = ops->prepare_data(req_info, reply_data, &info);
	if (ret < 0)
		goto err_cleanup;
	ret = ops->reply_size(req_info, reply_data);
	if (ret < 0)
		goto err_cleanup;
	reply_len = ret + ethnl_reply_header_size();
	skb = genlmsg_new(reply_len, GFP_KERNEL);
	if (!skb)
		goto err_cleanup;
	reply_payload = ethnl_bcastmsg_put(skb, cmd);
	if (!reply_payload)
		goto err_skb;
	ret = ethnl_fill_reply_header(skb, dev, ops->hdr_attr);
	if (ret < 0)
		goto err_msg;
	ret = ops->fill_reply(skb, req_info, reply_data);
	if (ret < 0)
		goto err_msg;
	if (ops->cleanup_data)
		ops->cleanup_data(reply_data);

	genlmsg_end(skb, reply_payload);
	kfree(reply_data);
	kfree(req_info);
	ethnl_multicast(skb, dev);
	return;

err_msg:
	WARN_ONCE(ret == -EMSGSIZE,
		  "calculated message payload length (%d) not sufficient\n",
		  reply_len);
err_skb:
	nlmsg_free(skb);
err_cleanup:
	if (ops->cleanup_data)
		ops->cleanup_data(reply_data);
	kfree(reply_data);
	kfree(req_info);
	return;
}

/* notifications */

typedef void (*ethnl_notify_handler_t)(struct net_device *dev, unsigned int cmd,
				       const void *data);

static const ethnl_notify_handler_t ethnl_notify_handlers[] = {
	[ETHTOOL_MSG_LINKINFO_NTF]	= ethnl_default_notify,
	[ETHTOOL_MSG_LINKMODES_NTF]	= ethnl_default_notify,
	[ETHTOOL_MSG_DEBUG_NTF]		= ethnl_default_notify,
	[ETHTOOL_MSG_WOL_NTF]		= ethnl_default_notify,
	[ETHTOOL_MSG_FEATURES_NTF]	= ethnl_default_notify,
	[ETHTOOL_MSG_PRIVFLAGS_NTF]	= ethnl_default_notify,
	[ETHTOOL_MSG_RINGS_NTF]		= ethnl_default_notify,
	[ETHTOOL_MSG_CHANNELS_NTF]	= ethnl_default_notify,
	[ETHTOOL_MSG_COALESCE_NTF]	= ethnl_default_notify,
	[ETHTOOL_MSG_PAUSE_NTF]		= ethnl_default_notify,
	[ETHTOOL_MSG_EEE_NTF]		= ethnl_default_notify,
	[ETHTOOL_MSG_FEC_NTF]		= ethnl_default_notify,
	[ETHTOOL_MSG_MODULE_NTF]	= ethnl_default_notify,
	[ETHTOOL_MSG_PLCA_NTF]		= ethnl_default_notify,
	[ETHTOOL_MSG_MM_NTF]		= ethnl_default_notify,
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

static void ethnl_notify_features(struct netdev_notifier_info *info)
{
	struct net_device *dev = netdev_notifier_info_to_dev(info);

	ethtool_notify(dev, ETHTOOL_MSG_FEATURES_NTF, NULL);
}

static int ethnl_netdev_event(struct notifier_block *this, unsigned long event,
			      void *ptr)
{
	struct netdev_notifier_info *info = ptr;
	struct netlink_ext_ack *extack;
	struct net_device *dev;

	dev = netdev_notifier_info_to_dev(info);
	extack = netdev_notifier_info_to_extack(info);

	switch (event) {
	case NETDEV_FEAT_CHANGE:
		ethnl_notify_features(ptr);
		break;
	case NETDEV_PRE_UP:
		if (dev->ethtool->module_fw_flash_in_progress) {
			NL_SET_ERR_MSG(extack, "Can't set port up while flashing module firmware");
			return NOTIFY_BAD;
		}
	}

	return NOTIFY_DONE;
}

static struct notifier_block ethnl_netdev_notifier = {
	.notifier_call = ethnl_netdev_event,
};

/* genetlink setup */

static const struct genl_ops ethtool_genl_ops[] = {
	{
		.cmd	= ETHTOOL_MSG_STRSET_GET,
		.doit	= ethnl_default_doit,
		.start	= ethnl_default_start,
		.dumpit	= ethnl_default_dumpit,
		.done	= ethnl_default_done,
		.policy = ethnl_strset_get_policy,
		.maxattr = ARRAY_SIZE(ethnl_strset_get_policy) - 1,
	},
	{
		.cmd	= ETHTOOL_MSG_LINKINFO_GET,
		.doit	= ethnl_default_doit,
		.start	= ethnl_default_start,
		.dumpit	= ethnl_default_dumpit,
		.done	= ethnl_default_done,
		.policy = ethnl_linkinfo_get_policy,
		.maxattr = ARRAY_SIZE(ethnl_linkinfo_get_policy) - 1,
	},
	{
		.cmd	= ETHTOOL_MSG_LINKINFO_SET,
		.flags	= GENL_UNS_ADMIN_PERM,
		.doit	= ethnl_default_set_doit,
		.policy = ethnl_linkinfo_set_policy,
		.maxattr = ARRAY_SIZE(ethnl_linkinfo_set_policy) - 1,
	},
	{
		.cmd	= ETHTOOL_MSG_LINKMODES_GET,
		.doit	= ethnl_default_doit,
		.start	= ethnl_default_start,
		.dumpit	= ethnl_default_dumpit,
		.done	= ethnl_default_done,
		.policy = ethnl_linkmodes_get_policy,
		.maxattr = ARRAY_SIZE(ethnl_linkmodes_get_policy) - 1,
	},
	{
		.cmd	= ETHTOOL_MSG_LINKMODES_SET,
		.flags	= GENL_UNS_ADMIN_PERM,
		.doit	= ethnl_default_set_doit,
		.policy = ethnl_linkmodes_set_policy,
		.maxattr = ARRAY_SIZE(ethnl_linkmodes_set_policy) - 1,
	},
	{
		.cmd	= ETHTOOL_MSG_LINKSTATE_GET,
		.doit	= ethnl_default_doit,
		.start	= ethnl_default_start,
		.dumpit	= ethnl_default_dumpit,
		.done	= ethnl_default_done,
		.policy = ethnl_linkstate_get_policy,
		.maxattr = ARRAY_SIZE(ethnl_linkstate_get_policy) - 1,
	},
	{
		.cmd	= ETHTOOL_MSG_DEBUG_GET,
		.doit	= ethnl_default_doit,
		.start	= ethnl_default_start,
		.dumpit	= ethnl_default_dumpit,
		.done	= ethnl_default_done,
		.policy = ethnl_debug_get_policy,
		.maxattr = ARRAY_SIZE(ethnl_debug_get_policy) - 1,
	},
	{
		.cmd	= ETHTOOL_MSG_DEBUG_SET,
		.flags	= GENL_UNS_ADMIN_PERM,
		.doit	= ethnl_default_set_doit,
		.policy = ethnl_debug_set_policy,
		.maxattr = ARRAY_SIZE(ethnl_debug_set_policy) - 1,
	},
	{
		.cmd	= ETHTOOL_MSG_WOL_GET,
		.flags	= GENL_UNS_ADMIN_PERM,
		.doit	= ethnl_default_doit,
		.start	= ethnl_default_start,
		.dumpit	= ethnl_default_dumpit,
		.done	= ethnl_default_done,
		.policy = ethnl_wol_get_policy,
		.maxattr = ARRAY_SIZE(ethnl_wol_get_policy) - 1,
	},
	{
		.cmd	= ETHTOOL_MSG_WOL_SET,
		.flags	= GENL_UNS_ADMIN_PERM,
		.doit	= ethnl_default_set_doit,
		.policy = ethnl_wol_set_policy,
		.maxattr = ARRAY_SIZE(ethnl_wol_set_policy) - 1,
	},
	{
		.cmd	= ETHTOOL_MSG_FEATURES_GET,
		.doit	= ethnl_default_doit,
		.start	= ethnl_default_start,
		.dumpit	= ethnl_default_dumpit,
		.done	= ethnl_default_done,
		.policy = ethnl_features_get_policy,
		.maxattr = ARRAY_SIZE(ethnl_features_get_policy) - 1,
	},
	{
		.cmd	= ETHTOOL_MSG_FEATURES_SET,
		.flags	= GENL_UNS_ADMIN_PERM,
		.doit	= ethnl_set_features,
		.policy = ethnl_features_set_policy,
		.maxattr = ARRAY_SIZE(ethnl_features_set_policy) - 1,
	},
	{
		.cmd	= ETHTOOL_MSG_PRIVFLAGS_GET,
		.doit	= ethnl_default_doit,
		.start	= ethnl_default_start,
		.dumpit	= ethnl_default_dumpit,
		.done	= ethnl_default_done,
		.policy = ethnl_privflags_get_policy,
		.maxattr = ARRAY_SIZE(ethnl_privflags_get_policy) - 1,
	},
	{
		.cmd	= ETHTOOL_MSG_PRIVFLAGS_SET,
		.flags	= GENL_UNS_ADMIN_PERM,
		.doit	= ethnl_default_set_doit,
		.policy = ethnl_privflags_set_policy,
		.maxattr = ARRAY_SIZE(ethnl_privflags_set_policy) - 1,
	},
	{
		.cmd	= ETHTOOL_MSG_RINGS_GET,
		.doit	= ethnl_default_doit,
		.start	= ethnl_default_start,
		.dumpit	= ethnl_default_dumpit,
		.done	= ethnl_default_done,
		.policy = ethnl_rings_get_policy,
		.maxattr = ARRAY_SIZE(ethnl_rings_get_policy) - 1,
	},
	{
		.cmd	= ETHTOOL_MSG_RINGS_SET,
		.flags	= GENL_UNS_ADMIN_PERM,
		.doit	= ethnl_default_set_doit,
		.policy = ethnl_rings_set_policy,
		.maxattr = ARRAY_SIZE(ethnl_rings_set_policy) - 1,
	},
	{
		.cmd	= ETHTOOL_MSG_CHANNELS_GET,
		.doit	= ethnl_default_doit,
		.start	= ethnl_default_start,
		.dumpit	= ethnl_default_dumpit,
		.done	= ethnl_default_done,
		.policy = ethnl_channels_get_policy,
		.maxattr = ARRAY_SIZE(ethnl_channels_get_policy) - 1,
	},
	{
		.cmd	= ETHTOOL_MSG_CHANNELS_SET,
		.flags	= GENL_UNS_ADMIN_PERM,
		.doit	= ethnl_default_set_doit,
		.policy = ethnl_channels_set_policy,
		.maxattr = ARRAY_SIZE(ethnl_channels_set_policy) - 1,
	},
	{
		.cmd	= ETHTOOL_MSG_COALESCE_GET,
		.doit	= ethnl_default_doit,
		.start	= ethnl_default_start,
		.dumpit	= ethnl_default_dumpit,
		.done	= ethnl_default_done,
		.policy = ethnl_coalesce_get_policy,
		.maxattr = ARRAY_SIZE(ethnl_coalesce_get_policy) - 1,
	},
	{
		.cmd	= ETHTOOL_MSG_COALESCE_SET,
		.flags	= GENL_UNS_ADMIN_PERM,
		.doit	= ethnl_default_set_doit,
		.policy = ethnl_coalesce_set_policy,
		.maxattr = ARRAY_SIZE(ethnl_coalesce_set_policy) - 1,
	},
	{
		.cmd	= ETHTOOL_MSG_PAUSE_GET,
		.doit	= ethnl_default_doit,
		.start	= ethnl_default_start,
		.dumpit	= ethnl_default_dumpit,
		.done	= ethnl_default_done,
		.policy = ethnl_pause_get_policy,
		.maxattr = ARRAY_SIZE(ethnl_pause_get_policy) - 1,
	},
	{
		.cmd	= ETHTOOL_MSG_PAUSE_SET,
		.flags	= GENL_UNS_ADMIN_PERM,
		.doit	= ethnl_default_set_doit,
		.policy = ethnl_pause_set_policy,
		.maxattr = ARRAY_SIZE(ethnl_pause_set_policy) - 1,
	},
	{
		.cmd	= ETHTOOL_MSG_EEE_GET,
		.doit	= ethnl_default_doit,
		.start	= ethnl_default_start,
		.dumpit	= ethnl_default_dumpit,
		.done	= ethnl_default_done,
		.policy = ethnl_eee_get_policy,
		.maxattr = ARRAY_SIZE(ethnl_eee_get_policy) - 1,
	},
	{
		.cmd	= ETHTOOL_MSG_EEE_SET,
		.flags	= GENL_UNS_ADMIN_PERM,
		.doit	= ethnl_default_set_doit,
		.policy = ethnl_eee_set_policy,
		.maxattr = ARRAY_SIZE(ethnl_eee_set_policy) - 1,
	},
	{
		.cmd	= ETHTOOL_MSG_TSINFO_GET,
		.doit	= ethnl_default_doit,
		.start	= ethnl_default_start,
		.dumpit	= ethnl_default_dumpit,
		.done	= ethnl_default_done,
		.policy = ethnl_tsinfo_get_policy,
		.maxattr = ARRAY_SIZE(ethnl_tsinfo_get_policy) - 1,
	},
	{
		.cmd	= ETHTOOL_MSG_CABLE_TEST_ACT,
		.flags	= GENL_UNS_ADMIN_PERM,
		.doit	= ethnl_act_cable_test,
		.policy = ethnl_cable_test_act_policy,
		.maxattr = ARRAY_SIZE(ethnl_cable_test_act_policy) - 1,
	},
	{
		.cmd	= ETHTOOL_MSG_CABLE_TEST_TDR_ACT,
		.flags	= GENL_UNS_ADMIN_PERM,
		.doit	= ethnl_act_cable_test_tdr,
		.policy = ethnl_cable_test_tdr_act_policy,
		.maxattr = ARRAY_SIZE(ethnl_cable_test_tdr_act_policy) - 1,
	},
	{
		.cmd	= ETHTOOL_MSG_TUNNEL_INFO_GET,
		.doit	= ethnl_tunnel_info_doit,
		.start	= ethnl_tunnel_info_start,
		.dumpit	= ethnl_tunnel_info_dumpit,
		.policy = ethnl_tunnel_info_get_policy,
		.maxattr = ARRAY_SIZE(ethnl_tunnel_info_get_policy) - 1,
	},
	{
		.cmd	= ETHTOOL_MSG_FEC_GET,
		.doit	= ethnl_default_doit,
		.start	= ethnl_default_start,
		.dumpit	= ethnl_default_dumpit,
		.done	= ethnl_default_done,
		.policy = ethnl_fec_get_policy,
		.maxattr = ARRAY_SIZE(ethnl_fec_get_policy) - 1,
	},
	{
		.cmd	= ETHTOOL_MSG_FEC_SET,
		.flags	= GENL_UNS_ADMIN_PERM,
		.doit	= ethnl_default_set_doit,
		.policy = ethnl_fec_set_policy,
		.maxattr = ARRAY_SIZE(ethnl_fec_set_policy) - 1,
	},
	{
		.cmd	= ETHTOOL_MSG_MODULE_EEPROM_GET,
		.flags  = GENL_UNS_ADMIN_PERM,
		.doit	= ethnl_default_doit,
		.start	= ethnl_default_start,
		.dumpit	= ethnl_default_dumpit,
		.done	= ethnl_default_done,
		.policy = ethnl_module_eeprom_get_policy,
		.maxattr = ARRAY_SIZE(ethnl_module_eeprom_get_policy) - 1,
	},
	{
		.cmd	= ETHTOOL_MSG_STATS_GET,
		.doit	= ethnl_default_doit,
		.start	= ethnl_default_start,
		.dumpit	= ethnl_default_dumpit,
		.done	= ethnl_default_done,
		.policy = ethnl_stats_get_policy,
		.maxattr = ARRAY_SIZE(ethnl_stats_get_policy) - 1,
	},
	{
		.cmd	= ETHTOOL_MSG_PHC_VCLOCKS_GET,
		.doit	= ethnl_default_doit,
		.start	= ethnl_default_start,
		.dumpit	= ethnl_default_dumpit,
		.done	= ethnl_default_done,
		.policy = ethnl_phc_vclocks_get_policy,
		.maxattr = ARRAY_SIZE(ethnl_phc_vclocks_get_policy) - 1,
	},
	{
		.cmd	= ETHTOOL_MSG_MODULE_GET,
		.doit	= ethnl_default_doit,
		.start	= ethnl_default_start,
		.dumpit	= ethnl_default_dumpit,
		.done	= ethnl_default_done,
		.policy = ethnl_module_get_policy,
		.maxattr = ARRAY_SIZE(ethnl_module_get_policy) - 1,
	},
	{
		.cmd	= ETHTOOL_MSG_MODULE_SET,
		.flags	= GENL_UNS_ADMIN_PERM,
		.doit	= ethnl_default_set_doit,
		.policy = ethnl_module_set_policy,
		.maxattr = ARRAY_SIZE(ethnl_module_set_policy) - 1,
	},
	{
		.cmd	= ETHTOOL_MSG_PSE_GET,
		.doit	= ethnl_default_doit,
		.start	= ethnl_default_start,
		.dumpit	= ethnl_default_dumpit,
		.done	= ethnl_default_done,
		.policy = ethnl_pse_get_policy,
		.maxattr = ARRAY_SIZE(ethnl_pse_get_policy) - 1,
	},
	{
		.cmd	= ETHTOOL_MSG_PSE_SET,
		.flags	= GENL_UNS_ADMIN_PERM,
		.doit	= ethnl_default_set_doit,
		.policy = ethnl_pse_set_policy,
		.maxattr = ARRAY_SIZE(ethnl_pse_set_policy) - 1,
	},
	{
		.cmd	= ETHTOOL_MSG_RSS_GET,
		.doit	= ethnl_default_doit,
		.policy = ethnl_rss_get_policy,
		.maxattr = ARRAY_SIZE(ethnl_rss_get_policy) - 1,
	},
	{
		.cmd	= ETHTOOL_MSG_PLCA_GET_CFG,
		.doit	= ethnl_default_doit,
		.start	= ethnl_default_start,
		.dumpit	= ethnl_default_dumpit,
		.done	= ethnl_default_done,
		.policy = ethnl_plca_get_cfg_policy,
		.maxattr = ARRAY_SIZE(ethnl_plca_get_cfg_policy) - 1,
	},
	{
		.cmd	= ETHTOOL_MSG_PLCA_SET_CFG,
		.flags	= GENL_UNS_ADMIN_PERM,
		.doit	= ethnl_default_set_doit,
		.policy = ethnl_plca_set_cfg_policy,
		.maxattr = ARRAY_SIZE(ethnl_plca_set_cfg_policy) - 1,
	},
	{
		.cmd	= ETHTOOL_MSG_PLCA_GET_STATUS,
		.doit	= ethnl_default_doit,
		.start	= ethnl_default_start,
		.dumpit	= ethnl_default_dumpit,
		.done	= ethnl_default_done,
		.policy = ethnl_plca_get_status_policy,
		.maxattr = ARRAY_SIZE(ethnl_plca_get_status_policy) - 1,
	},
	{
		.cmd	= ETHTOOL_MSG_MM_GET,
		.doit	= ethnl_default_doit,
		.start	= ethnl_default_start,
		.dumpit	= ethnl_default_dumpit,
		.done	= ethnl_default_done,
		.policy = ethnl_mm_get_policy,
		.maxattr = ARRAY_SIZE(ethnl_mm_get_policy) - 1,
	},
	{
		.cmd	= ETHTOOL_MSG_MM_SET,
		.flags	= GENL_UNS_ADMIN_PERM,
		.doit	= ethnl_default_set_doit,
		.policy = ethnl_mm_set_policy,
		.maxattr = ARRAY_SIZE(ethnl_mm_set_policy) - 1,
	},
	{
		.cmd	= ETHTOOL_MSG_MODULE_FW_FLASH_ACT,
		.flags	= GENL_UNS_ADMIN_PERM,
		.doit	= ethnl_act_module_fw_flash,
		.policy	= ethnl_module_fw_flash_act_policy,
		.maxattr = ARRAY_SIZE(ethnl_module_fw_flash_act_policy) - 1,
	},
};

static const struct genl_multicast_group ethtool_nl_mcgrps[] = {
	[ETHNL_MCGRP_MONITOR] = { .name = ETHTOOL_MCGRP_MONITOR_NAME },
};

static struct genl_family ethtool_genl_family __ro_after_init = {
	.name		= ETHTOOL_GENL_NAME,
	.version	= ETHTOOL_GENL_VERSION,
	.netnsok	= true,
	.parallel_ops	= true,
	.ops		= ethtool_genl_ops,
	.n_ops		= ARRAY_SIZE(ethtool_genl_ops),
	.resv_start_op	= ETHTOOL_MSG_MODULE_GET + 1,
	.mcgrps		= ethtool_nl_mcgrps,
	.n_mcgrps	= ARRAY_SIZE(ethtool_nl_mcgrps),
	.sock_priv_size		= sizeof(struct ethnl_sock_priv),
	.sock_priv_destroy	= ethnl_sock_priv_destroy,
};

/* module setup */

static int __init ethnl_init(void)
{
	int ret;

	ret = genl_register_family(&ethtool_genl_family);
	if (WARN(ret < 0, "ethtool: genetlink family registration failed"))
		return ret;
	ethnl_ok = true;

	ret = register_netdevice_notifier(&ethnl_netdev_notifier);
	WARN(ret < 0, "ethtool: net device notifier registration failed");
	return ret;
}

subsys_initcall(ethnl_init);
