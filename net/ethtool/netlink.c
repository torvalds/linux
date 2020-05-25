// SPDX-License-Identifier: GPL-2.0-only

#include <net/sock.h>
#include <linux/ethtool_netlink.h>
#include "netlink.h"

static struct genl_family ethtool_genl_family;

static bool ethnl_ok __read_mostly;
static u32 ethnl_bcast_seq;

static const struct nla_policy ethnl_header_policy[ETHTOOL_A_HEADER_MAX + 1] = {
	[ETHTOOL_A_HEADER_UNSPEC]	= { .type = NLA_REJECT },
	[ETHTOOL_A_HEADER_DEV_INDEX]	= { .type = NLA_U32 },
	[ETHTOOL_A_HEADER_DEV_NAME]	= { .type = NLA_NUL_STRING,
					    .len = ALTIFNAMSIZ - 1 },
	[ETHTOOL_A_HEADER_FLAGS]	= { .type = NLA_U32 },
};

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
	struct nlattr *tb[ETHTOOL_A_HEADER_MAX + 1];
	const struct nlattr *devname_attr;
	struct net_device *dev = NULL;
	u32 flags = 0;
	int ret;

	if (!header) {
		NL_SET_ERR_MSG(extack, "request header missing");
		return -EINVAL;
	}
	ret = nla_parse_nested(tb, ETHTOOL_A_HEADER_MAX, header,
			       ethnl_header_policy, extack);
	if (ret < 0)
		return ret;
	if (tb[ETHTOOL_A_HEADER_FLAGS]) {
		flags = nla_get_u32(tb[ETHTOOL_A_HEADER_FLAGS]);
		if (flags & ~ETHTOOL_FLAG_ALL) {
			NL_SET_ERR_MSG_ATTR(extack, tb[ETHTOOL_A_HEADER_FLAGS],
					    "unrecognized request flags");
			nl_set_extack_cookie_u32(extack, ETHTOOL_FLAG_ALL);
			return -EOPNOTSUPP;
		}
	}

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

static void *ethnl_bcastmsg_put(struct sk_buff *skb, u8 cmd)
{
	return genlmsg_put(skb, 0, ++ethnl_bcast_seq, &ethtool_genl_family, 0,
			   cmd);
}

static int ethnl_multicast(struct sk_buff *skb, struct net_device *dev)
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
 * @pos_hash:   saved iteration position - hashbucket
 * @pos_idx:    saved iteration position - index
 *
 * These parameters are kept in struct netlink_callback as context preserved
 * between iterations. They are initialized by ethnl_default_start() and used
 * in ethnl_default_dumpit() and ethnl_default_done().
 */
struct ethnl_dump_ctx {
	const struct ethnl_request_ops	*ops;
	struct ethnl_req_info		*req_info;
	struct ethnl_reply_data		*reply_data;
	int				pos_hash;
	int				pos_idx;
};

static const struct ethnl_request_ops *
ethnl_default_requests[__ETHTOOL_MSG_USER_CNT] = {
	[ETHTOOL_MSG_STRSET_GET]	= &ethnl_strset_request_ops,
	[ETHTOOL_MSG_LINKINFO_GET]	= &ethnl_linkinfo_request_ops,
	[ETHTOOL_MSG_LINKMODES_GET]	= &ethnl_linkmodes_request_ops,
	[ETHTOOL_MSG_LINKSTATE_GET]	= &ethnl_linkstate_request_ops,
	[ETHTOOL_MSG_DEBUG_GET]		= &ethnl_debug_request_ops,
	[ETHTOOL_MSG_WOL_GET]		= &ethnl_wol_request_ops,
	[ETHTOOL_MSG_FEATURES_GET]	= &ethnl_features_request_ops,
	[ETHTOOL_MSG_PRIVFLAGS_GET]	= &ethnl_privflags_request_ops,
	[ETHTOOL_MSG_RINGS_GET]		= &ethnl_rings_request_ops,
	[ETHTOOL_MSG_CHANNELS_GET]	= &ethnl_channels_request_ops,
	[ETHTOOL_MSG_COALESCE_GET]	= &ethnl_coalesce_request_ops,
	[ETHTOOL_MSG_PAUSE_GET]		= &ethnl_pause_request_ops,
	[ETHTOOL_MSG_EEE_GET]		= &ethnl_eee_request_ops,
	[ETHTOOL_MSG_TSINFO_GET]	= &ethnl_tsinfo_request_ops,
};

static struct ethnl_dump_ctx *ethnl_dump_context(struct netlink_callback *cb)
{
	return (struct ethnl_dump_ctx *)cb->ctx;
}

/**
 * ethnl_default_parse() - Parse request message
 * @req_info:    pointer to structure to put data into
 * @nlhdr:       pointer to request message header
 * @net:         request netns
 * @request_ops: struct request_ops for request type
 * @extack:      netlink extack for error reporting
 * @require_dev: fail if no device identified in header
 *
 * Parse universal request header and call request specific ->parse_request()
 * callback (if defined) to parse the rest of the message.
 *
 * Return: 0 on success or negative error code
 */
static int ethnl_default_parse(struct ethnl_req_info *req_info,
			       const struct nlmsghdr *nlhdr, struct net *net,
			       const struct ethnl_request_ops *request_ops,
			       struct netlink_ext_ack *extack, bool require_dev)
{
	struct nlattr **tb;
	int ret;

	tb = kmalloc_array(request_ops->max_attr + 1, sizeof(tb[0]),
			   GFP_KERNEL);
	if (!tb)
		return -ENOMEM;

	ret = nlmsg_parse(nlhdr, GENL_HDRLEN, tb, request_ops->max_attr,
			  request_ops->request_policy, extack);
	if (ret < 0)
		goto out;
	ret = ethnl_parse_header_dev_get(req_info, tb[request_ops->hdr_attr],
					 net, extack, require_dev);
	if (ret < 0)
		goto out;

	if (request_ops->parse_request) {
		ret = request_ops->parse_request(req_info, tb, extack);
		if (ret < 0)
			goto out;
	}

	ret = 0;
out:
	kfree(tb);
	return ret;
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
	struct sk_buff *rskb;
	void *reply_payload;
	int reply_len;
	int ret;

	ops = ethnl_default_requests[cmd];
	if (WARN_ONCE(!ops, "cmd %u has no ethnl_request_ops\n", cmd))
		return -EOPNOTSUPP;
	req_info = kzalloc(ops->req_info_size, GFP_KERNEL);
	if (!req_info)
		return -ENOMEM;
	reply_data = kmalloc(ops->reply_data_size, GFP_KERNEL);
	if (!reply_data) {
		kfree(req_info);
		return -ENOMEM;
	}

	ret = ethnl_default_parse(req_info, info->nlhdr, genl_info_net(info), ops,
				  info->extack, !ops->allow_nodev_do);
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
	reply_len = ret + ethnl_reply_header_size();
	ret = -ENOMEM;
	rskb = ethnl_reply_init(reply_len, req_info->dev, ops->reply_cmd,
				ops->hdr_attr, info, &reply_payload);
	if (!rskb)
		goto err_cleanup;
	ret = ops->fill_reply(rskb, req_info, reply_data);
	if (ret < 0)
		goto err_msg;
	if (ops->cleanup_data)
		ops->cleanup_data(reply_data);

	genlmsg_end(rskb, reply_payload);
	if (req_info->dev)
		dev_put(req_info->dev);
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
	if (req_info->dev)
		dev_put(req_info->dev);
	kfree(reply_data);
	kfree(req_info);
	return ret;
}

static int ethnl_default_dump_one(struct sk_buff *skb, struct net_device *dev,
				  const struct ethnl_dump_ctx *ctx)
{
	int ret;

	ethnl_init_reply_data(ctx->reply_data, ctx->ops, dev);
	rtnl_lock();
	ret = ctx->ops->prepare_data(ctx->req_info, ctx->reply_data, NULL);
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
	return ret;
}

/* Default ->dumpit() handler for GET requests. Device iteration copied from
 * rtnl_dump_ifinfo(); we have to be more careful about device hashtable
 * persistence as we cannot guarantee to hold RTNL lock through the whole
 * function as rtnetnlink does.
 */
static int ethnl_default_dumpit(struct sk_buff *skb,
				struct netlink_callback *cb)
{
	struct ethnl_dump_ctx *ctx = ethnl_dump_context(cb);
	struct net *net = sock_net(skb->sk);
	int s_idx = ctx->pos_idx;
	int h, idx = 0;
	int ret = 0;
	void *ehdr;

	rtnl_lock();
	for (h = ctx->pos_hash; h < NETDEV_HASHENTRIES; h++, s_idx = 0) {
		struct hlist_head *head;
		struct net_device *dev;
		unsigned int seq;

		head = &net->dev_index_head[h];

restart_chain:
		seq = net->dev_base_seq;
		cb->seq = seq;
		idx = 0;
		hlist_for_each_entry(dev, head, index_hlist) {
			if (idx < s_idx)
				goto cont;
			dev_hold(dev);
			rtnl_unlock();

			ehdr = genlmsg_put(skb, NETLINK_CB(cb->skb).portid,
					   cb->nlh->nlmsg_seq,
					   &ethtool_genl_family, 0,
					   ctx->ops->reply_cmd);
			if (!ehdr) {
				dev_put(dev);
				ret = -EMSGSIZE;
				goto out;
			}
			ret = ethnl_default_dump_one(skb, dev, ctx);
			dev_put(dev);
			if (ret < 0) {
				genlmsg_cancel(skb, ehdr);
				if (ret == -EOPNOTSUPP)
					goto lock_and_cont;
				if (likely(skb->len))
					ret = skb->len;
				goto out;
			}
			genlmsg_end(skb, ehdr);
lock_and_cont:
			rtnl_lock();
			if (net->dev_base_seq != seq) {
				s_idx = idx + 1;
				goto restart_chain;
			}
cont:
			idx++;
		}

	}
	rtnl_unlock();

out:
	ctx->pos_hash = h;
	ctx->pos_idx = idx;
	nl_dump_check_consistent(cb, nlmsg_hdr(skb));

	return ret;
}

/* generic ->start() handler for GET requests */
static int ethnl_default_start(struct netlink_callback *cb)
{
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

	ret = ethnl_default_parse(req_info, cb->nlh, sock_net(cb->skb->sk), ops,
				  cb->extack, false);
	if (req_info->dev) {
		/* We ignore device specification in dump requests but as the
		 * same parser as for non-dump (doit) requests is used, it
		 * would take reference to the device if it finds one
		 */
		dev_put(req_info->dev);
		req_info->dev = NULL;
	}
	if (ret < 0)
		goto free_reply_data;

	ctx->ops = ops;
	ctx->req_info = req_info;
	ctx->reply_data = reply_data;
	ctx->pos_hash = 0;
	ctx->pos_idx = 0;

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
};

/* default notification handler */
static void ethnl_default_notify(struct net_device *dev, unsigned int cmd,
				 const void *data)
{
	struct ethnl_reply_data *reply_data;
	const struct ethnl_request_ops *ops;
	struct ethnl_req_info *req_info;
	struct sk_buff *skb;
	void *reply_payload;
	int reply_len;
	int ret;

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
	ret = ops->prepare_data(req_info, reply_data, NULL);
	if (ret < 0)
		goto err_cleanup;
	ret = ops->reply_size(req_info, reply_data);
	if (ret < 0)
		goto err_cleanup;
	reply_len = ret + ethnl_reply_header_size();
	ret = -ENOMEM;
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
	switch (event) {
	case NETDEV_FEAT_CHANGE:
		ethnl_notify_features(ptr);
		break;
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
	},
	{
		.cmd	= ETHTOOL_MSG_LINKINFO_GET,
		.doit	= ethnl_default_doit,
		.start	= ethnl_default_start,
		.dumpit	= ethnl_default_dumpit,
		.done	= ethnl_default_done,
	},
	{
		.cmd	= ETHTOOL_MSG_LINKINFO_SET,
		.flags	= GENL_UNS_ADMIN_PERM,
		.doit	= ethnl_set_linkinfo,
	},
	{
		.cmd	= ETHTOOL_MSG_LINKMODES_GET,
		.doit	= ethnl_default_doit,
		.start	= ethnl_default_start,
		.dumpit	= ethnl_default_dumpit,
		.done	= ethnl_default_done,
	},
	{
		.cmd	= ETHTOOL_MSG_LINKMODES_SET,
		.flags	= GENL_UNS_ADMIN_PERM,
		.doit	= ethnl_set_linkmodes,
	},
	{
		.cmd	= ETHTOOL_MSG_LINKSTATE_GET,
		.doit	= ethnl_default_doit,
		.start	= ethnl_default_start,
		.dumpit	= ethnl_default_dumpit,
		.done	= ethnl_default_done,
	},
	{
		.cmd	= ETHTOOL_MSG_DEBUG_GET,
		.doit	= ethnl_default_doit,
		.start	= ethnl_default_start,
		.dumpit	= ethnl_default_dumpit,
		.done	= ethnl_default_done,
	},
	{
		.cmd	= ETHTOOL_MSG_DEBUG_SET,
		.flags	= GENL_UNS_ADMIN_PERM,
		.doit	= ethnl_set_debug,
	},
	{
		.cmd	= ETHTOOL_MSG_WOL_GET,
		.flags	= GENL_UNS_ADMIN_PERM,
		.doit	= ethnl_default_doit,
		.start	= ethnl_default_start,
		.dumpit	= ethnl_default_dumpit,
		.done	= ethnl_default_done,
	},
	{
		.cmd	= ETHTOOL_MSG_WOL_SET,
		.flags	= GENL_UNS_ADMIN_PERM,
		.doit	= ethnl_set_wol,
	},
	{
		.cmd	= ETHTOOL_MSG_FEATURES_GET,
		.doit	= ethnl_default_doit,
		.start	= ethnl_default_start,
		.dumpit	= ethnl_default_dumpit,
		.done	= ethnl_default_done,
	},
	{
		.cmd	= ETHTOOL_MSG_FEATURES_SET,
		.flags	= GENL_UNS_ADMIN_PERM,
		.doit	= ethnl_set_features,
	},
	{
		.cmd	= ETHTOOL_MSG_PRIVFLAGS_GET,
		.doit	= ethnl_default_doit,
		.start	= ethnl_default_start,
		.dumpit	= ethnl_default_dumpit,
		.done	= ethnl_default_done,
	},
	{
		.cmd	= ETHTOOL_MSG_PRIVFLAGS_SET,
		.flags	= GENL_UNS_ADMIN_PERM,
		.doit	= ethnl_set_privflags,
	},
	{
		.cmd	= ETHTOOL_MSG_RINGS_GET,
		.doit	= ethnl_default_doit,
		.start	= ethnl_default_start,
		.dumpit	= ethnl_default_dumpit,
		.done	= ethnl_default_done,
	},
	{
		.cmd	= ETHTOOL_MSG_RINGS_SET,
		.flags	= GENL_UNS_ADMIN_PERM,
		.doit	= ethnl_set_rings,
	},
	{
		.cmd	= ETHTOOL_MSG_CHANNELS_GET,
		.doit	= ethnl_default_doit,
		.start	= ethnl_default_start,
		.dumpit	= ethnl_default_dumpit,
		.done	= ethnl_default_done,
	},
	{
		.cmd	= ETHTOOL_MSG_CHANNELS_SET,
		.flags	= GENL_UNS_ADMIN_PERM,
		.doit	= ethnl_set_channels,
	},
	{
		.cmd	= ETHTOOL_MSG_COALESCE_GET,
		.doit	= ethnl_default_doit,
		.start	= ethnl_default_start,
		.dumpit	= ethnl_default_dumpit,
		.done	= ethnl_default_done,
	},
	{
		.cmd	= ETHTOOL_MSG_COALESCE_SET,
		.flags	= GENL_UNS_ADMIN_PERM,
		.doit	= ethnl_set_coalesce,
	},
	{
		.cmd	= ETHTOOL_MSG_PAUSE_GET,
		.doit	= ethnl_default_doit,
		.start	= ethnl_default_start,
		.dumpit	= ethnl_default_dumpit,
		.done	= ethnl_default_done,
	},
	{
		.cmd	= ETHTOOL_MSG_PAUSE_SET,
		.flags	= GENL_UNS_ADMIN_PERM,
		.doit	= ethnl_set_pause,
	},
	{
		.cmd	= ETHTOOL_MSG_EEE_GET,
		.doit	= ethnl_default_doit,
		.start	= ethnl_default_start,
		.dumpit	= ethnl_default_dumpit,
		.done	= ethnl_default_done,
	},
	{
		.cmd	= ETHTOOL_MSG_EEE_SET,
		.flags	= GENL_UNS_ADMIN_PERM,
		.doit	= ethnl_set_eee,
	},
	{
		.cmd	= ETHTOOL_MSG_TSINFO_GET,
		.doit	= ethnl_default_doit,
		.start	= ethnl_default_start,
		.dumpit	= ethnl_default_dumpit,
		.done	= ethnl_default_done,
	},
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

	ret = register_netdevice_notifier(&ethnl_netdev_notifier);
	WARN(ret < 0, "ethtool: net device notifier registration failed");
	return ret;
}

subsys_initcall(ethnl_init);
