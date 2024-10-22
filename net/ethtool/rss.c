// SPDX-License-Identifier: GPL-2.0-only

#include "netlink.h"
#include "common.h"

struct rss_req_info {
	struct ethnl_req_info		base;
	u32				rss_context;
};

struct rss_reply_data {
	struct ethnl_reply_data		base;
	bool				no_key_fields;
	u32				indir_size;
	u32				hkey_size;
	u32				hfunc;
	u32				input_xfrm;
	u32				*indir_table;
	u8				*hkey;
};

#define RSS_REQINFO(__req_base) \
	container_of(__req_base, struct rss_req_info, base)

#define RSS_REPDATA(__reply_base) \
	container_of(__reply_base, struct rss_reply_data, base)

const struct nla_policy ethnl_rss_get_policy[] = {
	[ETHTOOL_A_RSS_HEADER] = NLA_POLICY_NESTED(ethnl_header_policy),
	[ETHTOOL_A_RSS_CONTEXT] = { .type = NLA_U32 },
	[ETHTOOL_A_RSS_START_CONTEXT] = { .type = NLA_U32 },
};

static int
rss_parse_request(struct ethnl_req_info *req_info, struct nlattr **tb,
		  struct netlink_ext_ack *extack)
{
	struct rss_req_info *request = RSS_REQINFO(req_info);

	if (tb[ETHTOOL_A_RSS_CONTEXT])
		request->rss_context = nla_get_u32(tb[ETHTOOL_A_RSS_CONTEXT]);
	if (tb[ETHTOOL_A_RSS_START_CONTEXT]) {
		NL_SET_BAD_ATTR(extack, tb[ETHTOOL_A_RSS_START_CONTEXT]);
		return -EINVAL;
	}

	return 0;
}

static int
rss_prepare_get(const struct rss_req_info *request, struct net_device *dev,
		struct rss_reply_data *data, const struct genl_info *info)
{
	struct ethtool_rxfh_param rxfh = {};
	const struct ethtool_ops *ops;
	u32 total_size, indir_bytes;
	u8 *rss_config;
	int ret;

	ops = dev->ethtool_ops;

	ret = ethnl_ops_begin(dev);
	if (ret < 0)
		return ret;

	data->indir_size = 0;
	data->hkey_size = 0;
	if (ops->get_rxfh_indir_size)
		data->indir_size = ops->get_rxfh_indir_size(dev);
	if (ops->get_rxfh_key_size)
		data->hkey_size = ops->get_rxfh_key_size(dev);

	indir_bytes = data->indir_size * sizeof(u32);
	total_size = indir_bytes + data->hkey_size;
	rss_config = kzalloc(total_size, GFP_KERNEL);
	if (!rss_config) {
		ret = -ENOMEM;
		goto out_ops;
	}

	if (data->indir_size)
		data->indir_table = (u32 *)rss_config;
	if (data->hkey_size)
		data->hkey = rss_config + indir_bytes;

	rxfh.indir_size = data->indir_size;
	rxfh.indir = data->indir_table;
	rxfh.key_size = data->hkey_size;
	rxfh.key = data->hkey;

	ret = ops->get_rxfh(dev, &rxfh);
	if (ret)
		goto out_ops;

	data->hfunc = rxfh.hfunc;
	data->input_xfrm = rxfh.input_xfrm;
out_ops:
	ethnl_ops_complete(dev);
	return ret;
}

static int
rss_prepare_ctx(const struct rss_req_info *request, struct net_device *dev,
		struct rss_reply_data *data, const struct genl_info *info)
{
	struct ethtool_rxfh_context *ctx;
	u32 total_size, indir_bytes;
	u8 *rss_config;

	ctx = xa_load(&dev->ethtool->rss_ctx, request->rss_context);
	if (!ctx)
		return -ENOENT;

	data->indir_size = ctx->indir_size;
	data->hkey_size = ctx->key_size;
	data->hfunc = ctx->hfunc;
	data->input_xfrm = ctx->input_xfrm;

	indir_bytes = data->indir_size * sizeof(u32);
	total_size = indir_bytes + data->hkey_size;
	rss_config = kzalloc(total_size, GFP_KERNEL);
	if (!rss_config)
		return -ENOMEM;

	data->indir_table = (u32 *)rss_config;
	memcpy(data->indir_table, ethtool_rxfh_context_indir(ctx), indir_bytes);

	if (data->hkey_size) {
		data->hkey = rss_config + indir_bytes;
		memcpy(data->hkey, ethtool_rxfh_context_key(ctx),
		       data->hkey_size);
	}

	return 0;
}

static int
rss_prepare_data(const struct ethnl_req_info *req_base,
		 struct ethnl_reply_data *reply_base,
		 const struct genl_info *info)
{
	struct rss_reply_data *data = RSS_REPDATA(reply_base);
	struct rss_req_info *request = RSS_REQINFO(req_base);
	struct net_device *dev = reply_base->dev;
	const struct ethtool_ops *ops;

	ops = dev->ethtool_ops;
	if (!ops->get_rxfh)
		return -EOPNOTSUPP;

	/* Some drivers don't handle rss_context */
	if (request->rss_context) {
		if (!ops->cap_rss_ctx_supported && !ops->create_rxfh_context)
			return -EOPNOTSUPP;

		data->no_key_fields = !ops->rxfh_per_ctx_key;
		return rss_prepare_ctx(request, dev, data, info);
	}

	return rss_prepare_get(request, dev, data, info);
}

static int
rss_reply_size(const struct ethnl_req_info *req_base,
	       const struct ethnl_reply_data *reply_base)
{
	const struct rss_reply_data *data = RSS_REPDATA(reply_base);
	int len;

	len = nla_total_size(sizeof(u32)) +	/* _RSS_CONTEXT */
	      nla_total_size(sizeof(u32)) +	/* _RSS_HFUNC */
	      nla_total_size(sizeof(u32)) +	/* _RSS_INPUT_XFRM */
	      nla_total_size(sizeof(u32) * data->indir_size) + /* _RSS_INDIR */
	      nla_total_size(data->hkey_size);	/* _RSS_HKEY */

	return len;
}

static int
rss_fill_reply(struct sk_buff *skb, const struct ethnl_req_info *req_base,
	       const struct ethnl_reply_data *reply_base)
{
	const struct rss_reply_data *data = RSS_REPDATA(reply_base);
	struct rss_req_info *request = RSS_REQINFO(req_base);

	if (request->rss_context &&
	    nla_put_u32(skb, ETHTOOL_A_RSS_CONTEXT, request->rss_context))
		return -EMSGSIZE;

	if ((data->indir_size &&
	     nla_put(skb, ETHTOOL_A_RSS_INDIR,
		     sizeof(u32) * data->indir_size, data->indir_table)))
		return -EMSGSIZE;

	if (data->no_key_fields)
		return 0;

	if ((data->hfunc &&
	     nla_put_u32(skb, ETHTOOL_A_RSS_HFUNC, data->hfunc)) ||
	    (data->input_xfrm &&
	     nla_put_u32(skb, ETHTOOL_A_RSS_INPUT_XFRM, data->input_xfrm)) ||
	    (data->hkey_size &&
	     nla_put(skb, ETHTOOL_A_RSS_HKEY, data->hkey_size, data->hkey)))
		return -EMSGSIZE;

	return 0;
}

static void rss_cleanup_data(struct ethnl_reply_data *reply_base)
{
	const struct rss_reply_data *data = RSS_REPDATA(reply_base);

	kfree(data->indir_table);
}

struct rss_nl_dump_ctx {
	unsigned long		ifindex;
	unsigned long		ctx_idx;

	/* User wants to only dump contexts from given ifindex */
	unsigned int		match_ifindex;
	unsigned int		start_ctx;
};

static struct rss_nl_dump_ctx *rss_dump_ctx(struct netlink_callback *cb)
{
	NL_ASSERT_DUMP_CTX_FITS(struct rss_nl_dump_ctx);

	return (struct rss_nl_dump_ctx *)cb->ctx;
}

int ethnl_rss_dump_start(struct netlink_callback *cb)
{
	const struct genl_info *info = genl_info_dump(cb);
	struct rss_nl_dump_ctx *ctx = rss_dump_ctx(cb);
	struct ethnl_req_info req_info = {};
	struct nlattr **tb = info->attrs;
	int ret;

	/* Filtering by context not supported */
	if (tb[ETHTOOL_A_RSS_CONTEXT]) {
		NL_SET_BAD_ATTR(info->extack, tb[ETHTOOL_A_RSS_CONTEXT]);
		return -EINVAL;
	}
	if (tb[ETHTOOL_A_RSS_START_CONTEXT]) {
		ctx->start_ctx = nla_get_u32(tb[ETHTOOL_A_RSS_START_CONTEXT]);
		ctx->ctx_idx = ctx->start_ctx;
	}

	ret = ethnl_parse_header_dev_get(&req_info,
					 tb[ETHTOOL_A_RSS_HEADER],
					 sock_net(cb->skb->sk), cb->extack,
					 false);
	if (req_info.dev) {
		ctx->match_ifindex = req_info.dev->ifindex;
		ctx->ifindex = ctx->match_ifindex;
		ethnl_parse_header_dev_put(&req_info);
		req_info.dev = NULL;
	}

	return ret;
}

static int
rss_dump_one_ctx(struct sk_buff *skb, struct netlink_callback *cb,
		 struct net_device *dev, u32 rss_context)
{
	const struct genl_info *info = genl_info_dump(cb);
	struct rss_reply_data data = {};
	struct rss_req_info req = {};
	void *ehdr;
	int ret;

	req.rss_context = rss_context;

	ehdr = ethnl_dump_put(skb, cb, ETHTOOL_MSG_RSS_GET_REPLY);
	if (!ehdr)
		return -EMSGSIZE;

	ret = ethnl_fill_reply_header(skb, dev, ETHTOOL_A_RSS_HEADER);
	if (ret < 0)
		goto err_cancel;

	/* Context 0 is not currently storred or cached in the XArray */
	if (!rss_context)
		ret = rss_prepare_get(&req, dev, &data, info);
	else
		ret = rss_prepare_ctx(&req, dev, &data, info);
	if (ret)
		goto err_cancel;

	ret = rss_fill_reply(skb, &req.base, &data.base);
	if (ret)
		goto err_cleanup;
	genlmsg_end(skb, ehdr);

	rss_cleanup_data(&data.base);
	return 0;

err_cleanup:
	rss_cleanup_data(&data.base);
err_cancel:
	genlmsg_cancel(skb, ehdr);
	return ret;
}

static int
rss_dump_one_dev(struct sk_buff *skb, struct netlink_callback *cb,
		 struct net_device *dev)
{
	struct rss_nl_dump_ctx *ctx = rss_dump_ctx(cb);
	int ret;

	if (!dev->ethtool_ops->get_rxfh)
		return 0;

	if (!ctx->ctx_idx) {
		ret = rss_dump_one_ctx(skb, cb, dev, 0);
		if (ret)
			return ret;
		ctx->ctx_idx++;
	}

	for (; xa_find(&dev->ethtool->rss_ctx, &ctx->ctx_idx,
		       ULONG_MAX, XA_PRESENT); ctx->ctx_idx++) {
		ret = rss_dump_one_ctx(skb, cb, dev, ctx->ctx_idx);
		if (ret)
			return ret;
	}
	ctx->ctx_idx = ctx->start_ctx;

	return 0;
}

int ethnl_rss_dumpit(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct rss_nl_dump_ctx *ctx = rss_dump_ctx(cb);
	struct net *net = sock_net(skb->sk);
	struct net_device *dev;
	int ret = 0;

	rtnl_lock();
	for_each_netdev_dump(net, dev, ctx->ifindex) {
		if (ctx->match_ifindex && ctx->match_ifindex != ctx->ifindex)
			break;

		ret = rss_dump_one_dev(skb, cb, dev);
		if (ret)
			break;
	}
	rtnl_unlock();

	return ret;
}

const struct ethnl_request_ops ethnl_rss_request_ops = {
	.request_cmd		= ETHTOOL_MSG_RSS_GET,
	.reply_cmd		= ETHTOOL_MSG_RSS_GET_REPLY,
	.hdr_attr		= ETHTOOL_A_RSS_HEADER,
	.req_info_size		= sizeof(struct rss_req_info),
	.reply_data_size	= sizeof(struct rss_reply_data),

	.parse_request		= rss_parse_request,
	.prepare_data		= rss_prepare_data,
	.reply_size		= rss_reply_size,
	.fill_reply		= rss_fill_reply,
	.cleanup_data		= rss_cleanup_data,
};
