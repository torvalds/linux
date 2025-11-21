// SPDX-License-Identifier: GPL-2.0-only

#include <net/netdev_lock.h>

#include "netlink.h"
#include "common.h"

struct rss_req_info {
	struct ethnl_req_info		base;
	u32				rss_context;
};

struct rss_reply_data {
	struct ethnl_reply_data		base;
	bool				has_flow_hash;
	bool				no_key_fields;
	u32				indir_size;
	u32				hkey_size;
	u32				hfunc;
	u32				input_xfrm;
	u32				*indir_table;
	u8				*hkey;
	int				flow_hash[__ETHTOOL_A_FLOW_CNT];
};

static const u8 ethtool_rxfh_ft_nl2ioctl[] = {
	[ETHTOOL_A_FLOW_ETHER]		= ETHER_FLOW,
	[ETHTOOL_A_FLOW_IP4]		= IPV4_FLOW,
	[ETHTOOL_A_FLOW_IP6]		= IPV6_FLOW,
	[ETHTOOL_A_FLOW_TCP4]		= TCP_V4_FLOW,
	[ETHTOOL_A_FLOW_UDP4]		= UDP_V4_FLOW,
	[ETHTOOL_A_FLOW_SCTP4]		= SCTP_V4_FLOW,
	[ETHTOOL_A_FLOW_AH_ESP4]	= AH_ESP_V4_FLOW,
	[ETHTOOL_A_FLOW_TCP6]		= TCP_V6_FLOW,
	[ETHTOOL_A_FLOW_UDP6]		= UDP_V6_FLOW,
	[ETHTOOL_A_FLOW_SCTP6]		= SCTP_V6_FLOW,
	[ETHTOOL_A_FLOW_AH_ESP6]	= AH_ESP_V6_FLOW,
	[ETHTOOL_A_FLOW_AH4]		= AH_V4_FLOW,
	[ETHTOOL_A_FLOW_ESP4]		= ESP_V4_FLOW,
	[ETHTOOL_A_FLOW_AH6]		= AH_V6_FLOW,
	[ETHTOOL_A_FLOW_ESP6]		= ESP_V6_FLOW,
	[ETHTOOL_A_FLOW_GTPU4]		= GTPU_V4_FLOW,
	[ETHTOOL_A_FLOW_GTPU6]		= GTPU_V6_FLOW,
	[ETHTOOL_A_FLOW_GTPC4]		= GTPC_V4_FLOW,
	[ETHTOOL_A_FLOW_GTPC6]		= GTPC_V6_FLOW,
	[ETHTOOL_A_FLOW_GTPC_TEID4]	= GTPC_TEID_V4_FLOW,
	[ETHTOOL_A_FLOW_GTPC_TEID6]	= GTPC_TEID_V6_FLOW,
	[ETHTOOL_A_FLOW_GTPU_EH4]	= GTPU_EH_V4_FLOW,
	[ETHTOOL_A_FLOW_GTPU_EH6]	= GTPU_EH_V6_FLOW,
	[ETHTOOL_A_FLOW_GTPU_UL4]	= GTPU_UL_V4_FLOW,
	[ETHTOOL_A_FLOW_GTPU_UL6]	= GTPU_UL_V6_FLOW,
	[ETHTOOL_A_FLOW_GTPU_DL4]	= GTPU_DL_V4_FLOW,
	[ETHTOOL_A_FLOW_GTPU_DL6]	= GTPU_DL_V6_FLOW,
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

static void
rss_prepare_flow_hash(const struct rss_req_info *req, struct net_device *dev,
		      struct rss_reply_data *data, const struct genl_info *info)
{
	int i;

	data->has_flow_hash = false;

	if (!dev->ethtool_ops->get_rxfh_fields)
		return;
	if (req->rss_context && !dev->ethtool_ops->rxfh_per_ctx_fields)
		return;

	mutex_lock(&dev->ethtool->rss_lock);
	for (i = 1; i < __ETHTOOL_A_FLOW_CNT; i++) {
		struct ethtool_rxfh_fields fields = {
			.flow_type	= ethtool_rxfh_ft_nl2ioctl[i],
			.rss_context	= req->rss_context,
		};

		if (dev->ethtool_ops->get_rxfh_fields(dev, &fields)) {
			data->flow_hash[i] = -1; /* Unsupported */
			continue;
		}

		data->flow_hash[i] = fields.data;
		data->has_flow_hash = true;
	}
	mutex_unlock(&dev->ethtool->rss_lock);
}

static int
rss_get_data_alloc(struct net_device *dev, struct rss_reply_data *data)
{
	const struct ethtool_ops *ops = dev->ethtool_ops;
	u32 total_size, indir_bytes;
	u8 *rss_config;

	data->indir_size = 0;
	data->hkey_size = 0;
	if (ops->get_rxfh_indir_size)
		data->indir_size = ops->get_rxfh_indir_size(dev);
	if (ops->get_rxfh_key_size)
		data->hkey_size = ops->get_rxfh_key_size(dev);

	indir_bytes = data->indir_size * sizeof(u32);
	total_size = indir_bytes + data->hkey_size;
	rss_config = kzalloc(total_size, GFP_KERNEL);
	if (!rss_config)
		return -ENOMEM;

	if (data->indir_size)
		data->indir_table = (u32 *)rss_config;
	if (data->hkey_size)
		data->hkey = rss_config + indir_bytes;

	return 0;
}

static void rss_get_data_free(const struct rss_reply_data *data)
{
	kfree(data->indir_table);
}

static int
rss_prepare_get(const struct rss_req_info *request, struct net_device *dev,
		struct rss_reply_data *data, const struct genl_info *info)
{
	const struct ethtool_ops *ops = dev->ethtool_ops;
	struct ethtool_rxfh_param rxfh = {};
	int ret;

	ret = ethnl_ops_begin(dev);
	if (ret < 0)
		return ret;
	mutex_lock(&dev->ethtool->rss_lock);

	ret = rss_get_data_alloc(dev, data);
	if (ret)
		goto out_unlock;

	rxfh.indir_size = data->indir_size;
	rxfh.indir = data->indir_table;
	rxfh.key_size = data->hkey_size;
	rxfh.key = data->hkey;

	ret = ops->get_rxfh(dev, &rxfh);
	if (ret)
		goto out_unlock;

	data->hfunc = rxfh.hfunc;
	data->input_xfrm = rxfh.input_xfrm;
out_unlock:
	mutex_unlock(&dev->ethtool->rss_lock);
	ethnl_ops_complete(dev);
	return ret;
}

static void
__rss_prepare_ctx(struct net_device *dev, struct rss_reply_data *data,
		  struct ethtool_rxfh_context *ctx)
{
	if (WARN_ON_ONCE(data->indir_size != ctx->indir_size ||
			 data->hkey_size != ctx->key_size))
		return;

	data->no_key_fields = !dev->ethtool_ops->rxfh_per_ctx_key;

	data->hfunc = ctx->hfunc;
	data->input_xfrm = ctx->input_xfrm;
	memcpy(data->indir_table, ethtool_rxfh_context_indir(ctx),
	       data->indir_size * sizeof(u32));
	if (data->hkey_size)
		memcpy(data->hkey, ethtool_rxfh_context_key(ctx),
		       data->hkey_size);
}

static int
rss_prepare_ctx(const struct rss_req_info *request, struct net_device *dev,
		struct rss_reply_data *data, const struct genl_info *info)
{
	struct ethtool_rxfh_context *ctx;
	u32 total_size, indir_bytes;
	u8 *rss_config;
	int ret;

	mutex_lock(&dev->ethtool->rss_lock);
	ctx = xa_load(&dev->ethtool->rss_ctx, request->rss_context);
	if (!ctx) {
		ret = -ENOENT;
		goto out_unlock;
	}

	data->indir_size = ctx->indir_size;
	data->hkey_size = ctx->key_size;

	indir_bytes = data->indir_size * sizeof(u32);
	total_size = indir_bytes + data->hkey_size;
	rss_config = kzalloc(total_size, GFP_KERNEL);
	if (!rss_config) {
		ret = -ENOMEM;
		goto out_unlock;
	}

	data->indir_table = (u32 *)rss_config;
	if (data->hkey_size)
		data->hkey = rss_config + indir_bytes;

	__rss_prepare_ctx(dev, data, ctx);

	ret = 0;
out_unlock:
	mutex_unlock(&dev->ethtool->rss_lock);
	return ret;
}

static int
rss_prepare(const struct rss_req_info *request, struct net_device *dev,
	    struct rss_reply_data *data, const struct genl_info *info)
{
	rss_prepare_flow_hash(request, dev, data, info);

	/* Coming from RSS_SET, driver may only have flow_hash_fields ops */
	if (!dev->ethtool_ops->get_rxfh)
		return 0;

	if (request->rss_context)
		return rss_prepare_ctx(request, dev, data, info);
	return rss_prepare_get(request, dev, data, info);
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
	if (request->rss_context && !ops->create_rxfh_context)
		return -EOPNOTSUPP;

	return rss_prepare(request, dev, data, info);
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
	      nla_total_size(data->hkey_size) + /* _RSS_HKEY */
	      nla_total_size(0) +		/* _RSS_FLOW_HASH */
		nla_total_size(sizeof(u32)) * ETHTOOL_A_FLOW_MAX +
	      0;

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

	if (!data->no_key_fields &&
	    ((data->hfunc &&
	      nla_put_u32(skb, ETHTOOL_A_RSS_HFUNC, data->hfunc)) ||
	     (data->input_xfrm &&
	      nla_put_u32(skb, ETHTOOL_A_RSS_INPUT_XFRM, data->input_xfrm)) ||
	     (data->hkey_size &&
	      nla_put(skb, ETHTOOL_A_RSS_HKEY, data->hkey_size, data->hkey))))
		return -EMSGSIZE;

	if (data->has_flow_hash) {
		struct nlattr *nest;
		int i;

		nest = nla_nest_start(skb, ETHTOOL_A_RSS_FLOW_HASH);
		if (!nest)
			return -EMSGSIZE;

		for (i = 1; i < __ETHTOOL_A_FLOW_CNT; i++) {
			if (data->flow_hash[i] >= 0 &&
			    nla_put_uint(skb, i, data->flow_hash[i])) {
				nla_nest_cancel(skb, nest);
				return -EMSGSIZE;
			}
		}

		nla_nest_end(skb, nest);
	}

	return 0;
}

static void rss_cleanup_data(struct ethnl_reply_data *reply_base)
{
	const struct rss_reply_data *data = RSS_REPDATA(reply_base);

	rss_get_data_free(data);
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
	NL_ASSERT_CTX_FITS(struct rss_nl_dump_ctx);

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

	ret = rss_prepare(&req, dev, &data, info);
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

		netdev_lock_ops(dev);
		ret = rss_dump_one_dev(skb, cb, dev);
		netdev_unlock_ops(dev);
		if (ret)
			break;
	}
	rtnl_unlock();

	return ret;
}

/* RSS_NTF */

static void ethnl_rss_delete_notify(struct net_device *dev, u32 rss_context)
{
	struct sk_buff *ntf;
	size_t ntf_size;
	void *hdr;

	ntf_size = ethnl_reply_header_size() +
		nla_total_size(sizeof(u32));	/* _RSS_CONTEXT */

	ntf = genlmsg_new(ntf_size, GFP_KERNEL);
	if (!ntf)
		goto out_warn;

	hdr = ethnl_bcastmsg_put(ntf, ETHTOOL_MSG_RSS_DELETE_NTF);
	if (!hdr)
		goto out_free_ntf;

	if (ethnl_fill_reply_header(ntf, dev, ETHTOOL_A_RSS_HEADER) ||
	    nla_put_u32(ntf, ETHTOOL_A_RSS_CONTEXT, rss_context))
		goto out_free_ntf;

	genlmsg_end(ntf, hdr);
	if (ethnl_multicast(ntf, dev))
		goto out_warn;

	return;

out_free_ntf:
	nlmsg_free(ntf);
out_warn:
	pr_warn_once("Failed to send a RSS delete notification");
}

void ethtool_rss_notify(struct net_device *dev, u32 type, u32 rss_context)
{
	struct rss_req_info req_info = {
		.rss_context = rss_context,
	};

	if (type == ETHTOOL_MSG_RSS_DELETE_NTF)
		ethnl_rss_delete_notify(dev, rss_context);
	else
		ethnl_notify(dev, type, &req_info.base);
}

/* RSS_SET */

#define RFH_MASK (RXH_L2DA | RXH_VLAN | RXH_IP_SRC | RXH_IP_DST | \
		  RXH_L3_PROTO | RXH_L4_B_0_1 | RXH_L4_B_2_3 |	  \
		  RXH_GTP_TEID | RXH_DISCARD)
#define RFH_MASKv6 (RFH_MASK | RXH_IP6_FL)

static const struct nla_policy ethnl_rss_flows_policy[] = {
	[ETHTOOL_A_FLOW_ETHER]		= NLA_POLICY_MASK(NLA_UINT, RFH_MASK),
	[ETHTOOL_A_FLOW_IP4]		= NLA_POLICY_MASK(NLA_UINT, RFH_MASK),
	[ETHTOOL_A_FLOW_IP6]		= NLA_POLICY_MASK(NLA_UINT, RFH_MASKv6),
	[ETHTOOL_A_FLOW_TCP4]		= NLA_POLICY_MASK(NLA_UINT, RFH_MASK),
	[ETHTOOL_A_FLOW_UDP4]		= NLA_POLICY_MASK(NLA_UINT, RFH_MASK),
	[ETHTOOL_A_FLOW_SCTP4]		= NLA_POLICY_MASK(NLA_UINT, RFH_MASK),
	[ETHTOOL_A_FLOW_AH_ESP4]	= NLA_POLICY_MASK(NLA_UINT, RFH_MASK),
	[ETHTOOL_A_FLOW_TCP6]		= NLA_POLICY_MASK(NLA_UINT, RFH_MASKv6),
	[ETHTOOL_A_FLOW_UDP6]		= NLA_POLICY_MASK(NLA_UINT, RFH_MASKv6),
	[ETHTOOL_A_FLOW_SCTP6]		= NLA_POLICY_MASK(NLA_UINT, RFH_MASKv6),
	[ETHTOOL_A_FLOW_AH_ESP6]	= NLA_POLICY_MASK(NLA_UINT, RFH_MASKv6),
	[ETHTOOL_A_FLOW_AH4]		= NLA_POLICY_MASK(NLA_UINT, RFH_MASK),
	[ETHTOOL_A_FLOW_ESP4]		= NLA_POLICY_MASK(NLA_UINT, RFH_MASK),
	[ETHTOOL_A_FLOW_AH6]		= NLA_POLICY_MASK(NLA_UINT, RFH_MASKv6),
	[ETHTOOL_A_FLOW_ESP6]		= NLA_POLICY_MASK(NLA_UINT, RFH_MASKv6),
	[ETHTOOL_A_FLOW_GTPU4]		= NLA_POLICY_MASK(NLA_UINT, RFH_MASK),
	[ETHTOOL_A_FLOW_GTPU6]		= NLA_POLICY_MASK(NLA_UINT, RFH_MASKv6),
	[ETHTOOL_A_FLOW_GTPC4]		= NLA_POLICY_MASK(NLA_UINT, RFH_MASK),
	[ETHTOOL_A_FLOW_GTPC6]		= NLA_POLICY_MASK(NLA_UINT, RFH_MASKv6),
	[ETHTOOL_A_FLOW_GTPC_TEID4]	= NLA_POLICY_MASK(NLA_UINT, RFH_MASK),
	[ETHTOOL_A_FLOW_GTPC_TEID6]	= NLA_POLICY_MASK(NLA_UINT, RFH_MASKv6),
	[ETHTOOL_A_FLOW_GTPU_EH4]	= NLA_POLICY_MASK(NLA_UINT, RFH_MASK),
	[ETHTOOL_A_FLOW_GTPU_EH6]	= NLA_POLICY_MASK(NLA_UINT, RFH_MASKv6),
	[ETHTOOL_A_FLOW_GTPU_UL4]	= NLA_POLICY_MASK(NLA_UINT, RFH_MASK),
	[ETHTOOL_A_FLOW_GTPU_UL6]	= NLA_POLICY_MASK(NLA_UINT, RFH_MASKv6),
	[ETHTOOL_A_FLOW_GTPU_DL4]	= NLA_POLICY_MASK(NLA_UINT, RFH_MASK),
	[ETHTOOL_A_FLOW_GTPU_DL6]	= NLA_POLICY_MASK(NLA_UINT, RFH_MASKv6),
};

const struct nla_policy ethnl_rss_set_policy[ETHTOOL_A_RSS_FLOW_HASH + 1] = {
	[ETHTOOL_A_RSS_HEADER] = NLA_POLICY_NESTED(ethnl_header_policy),
	[ETHTOOL_A_RSS_CONTEXT] = { .type = NLA_U32, },
	[ETHTOOL_A_RSS_HFUNC] = NLA_POLICY_MIN(NLA_U32, 1),
	[ETHTOOL_A_RSS_INDIR] = { .type = NLA_BINARY, },
	[ETHTOOL_A_RSS_HKEY] = NLA_POLICY_MIN(NLA_BINARY, 1),
	[ETHTOOL_A_RSS_INPUT_XFRM] =
		NLA_POLICY_MAX(NLA_U32, RXH_XFRM_SYM_OR_XOR),
	[ETHTOOL_A_RSS_FLOW_HASH] = NLA_POLICY_NESTED(ethnl_rss_flows_policy),
};

static int
ethnl_rss_set_validate(struct ethnl_req_info *req_info, struct genl_info *info)
{
	const struct ethtool_ops *ops = req_info->dev->ethtool_ops;
	struct rss_req_info *request = RSS_REQINFO(req_info);
	struct nlattr **tb = info->attrs;
	struct nlattr *bad_attr = NULL;
	u32 input_xfrm;

	if (request->rss_context && !ops->create_rxfh_context)
		bad_attr = bad_attr ?: tb[ETHTOOL_A_RSS_CONTEXT];

	if (request->rss_context && !ops->rxfh_per_ctx_key) {
		bad_attr = bad_attr ?: tb[ETHTOOL_A_RSS_HFUNC];
		bad_attr = bad_attr ?: tb[ETHTOOL_A_RSS_HKEY];
		bad_attr = bad_attr ?: tb[ETHTOOL_A_RSS_INPUT_XFRM];
	}

	input_xfrm = nla_get_u32_default(tb[ETHTOOL_A_RSS_INPUT_XFRM], 0);
	if (input_xfrm & ~ops->supported_input_xfrm)
		bad_attr = bad_attr ?: tb[ETHTOOL_A_RSS_INPUT_XFRM];

	if (tb[ETHTOOL_A_RSS_FLOW_HASH] && !ops->set_rxfh_fields)
		bad_attr = bad_attr ?: tb[ETHTOOL_A_RSS_FLOW_HASH];
	if (request->rss_context &&
	    tb[ETHTOOL_A_RSS_FLOW_HASH] && !ops->rxfh_per_ctx_fields)
		bad_attr = bad_attr ?: tb[ETHTOOL_A_RSS_FLOW_HASH];

	if (bad_attr) {
		NL_SET_BAD_ATTR(info->extack, bad_attr);
		return -EOPNOTSUPP;
	}

	return 1;
}

static int
rss_set_prep_indir(struct net_device *dev, struct genl_info *info,
		   struct rss_reply_data *data, struct ethtool_rxfh_param *rxfh,
		   bool *reset, bool *mod)
{
	struct netlink_ext_ack *extack = info->extack;
	struct nlattr **tb = info->attrs;
	size_t alloc_size;
	int num_rx_rings;
	u32 user_size;
	int i, err;

	if (!tb[ETHTOOL_A_RSS_INDIR])
		return 0;
	if (!data->indir_size)
		return -EOPNOTSUPP;

	err = ethtool_get_rx_ring_count(dev);
	if (err < 0)
		return err;
	num_rx_rings = err;

	if (nla_len(tb[ETHTOOL_A_RSS_INDIR]) % 4) {
		NL_SET_BAD_ATTR(info->extack, tb[ETHTOOL_A_RSS_INDIR]);
		return -EINVAL;
	}
	user_size = nla_len(tb[ETHTOOL_A_RSS_INDIR]) / 4;
	if (!user_size) {
		if (rxfh->rss_context) {
			NL_SET_ERR_MSG_ATTR(extack, tb[ETHTOOL_A_RSS_INDIR],
					    "can't reset table for a context");
			return -EINVAL;
		}
		*reset = true;
	} else if (data->indir_size % user_size) {
		NL_SET_ERR_MSG_ATTR_FMT(extack, tb[ETHTOOL_A_RSS_INDIR],
					"size (%d) mismatch with device indir table (%d)",
					user_size, data->indir_size);
		return -EINVAL;
	}

	rxfh->indir_size = data->indir_size;
	alloc_size = array_size(data->indir_size, sizeof(rxfh->indir[0]));
	rxfh->indir = kzalloc(alloc_size, GFP_KERNEL);
	if (!rxfh->indir)
		return -ENOMEM;

	nla_memcpy(rxfh->indir, tb[ETHTOOL_A_RSS_INDIR], alloc_size);
	for (i = 0; i < user_size; i++) {
		if (rxfh->indir[i] < num_rx_rings)
			continue;

		NL_SET_ERR_MSG_ATTR_FMT(extack, tb[ETHTOOL_A_RSS_INDIR],
					"entry %d: queue out of range (%d)",
					i, rxfh->indir[i]);
		err = -EINVAL;
		goto err_free;
	}

	if (user_size) {
		/* Replicate the user-provided table to fill the device table */
		for (i = user_size; i < data->indir_size; i++)
			rxfh->indir[i] = rxfh->indir[i % user_size];
	} else {
		for (i = 0; i < data->indir_size; i++)
			rxfh->indir[i] =
				ethtool_rxfh_indir_default(i, num_rx_rings);
	}

	*mod |= memcmp(rxfh->indir, data->indir_table, data->indir_size);

	return 0;

err_free:
	kfree(rxfh->indir);
	rxfh->indir = NULL;
	return err;
}

static int
rss_set_prep_hkey(struct net_device *dev, struct genl_info *info,
		  struct rss_reply_data *data, struct ethtool_rxfh_param *rxfh,
		  bool *mod)
{
	struct nlattr **tb = info->attrs;

	if (!tb[ETHTOOL_A_RSS_HKEY])
		return 0;

	if (nla_len(tb[ETHTOOL_A_RSS_HKEY]) != data->hkey_size) {
		NL_SET_BAD_ATTR(info->extack, tb[ETHTOOL_A_RSS_HKEY]);
		return -EINVAL;
	}

	rxfh->key_size = data->hkey_size;
	rxfh->key = kmemdup(data->hkey, data->hkey_size, GFP_KERNEL);
	if (!rxfh->key)
		return -ENOMEM;

	ethnl_update_binary(rxfh->key, rxfh->key_size, tb[ETHTOOL_A_RSS_HKEY],
			    mod);
	return 0;
}

static int
rss_check_rxfh_fields_sym(struct net_device *dev, struct genl_info *info,
			  struct rss_reply_data *data, bool xfrm_sym)
{
	struct nlattr **tb = info->attrs;
	int i;

	if (!xfrm_sym)
		return 0;
	if (!data->has_flow_hash) {
		NL_SET_ERR_MSG_ATTR(info->extack, tb[ETHTOOL_A_RSS_INPUT_XFRM],
				    "hash field config not reported");
		return -EINVAL;
	}

	for (i = 1; i < __ETHTOOL_A_FLOW_CNT; i++)
		if (data->flow_hash[i] >= 0 &&
		    !ethtool_rxfh_config_is_sym(data->flow_hash[i])) {
			NL_SET_ERR_MSG_ATTR(info->extack,
					    tb[ETHTOOL_A_RSS_INPUT_XFRM],
					    "hash field config is not symmetric");
			return -EINVAL;
		}

	return 0;
}

static int
ethnl_set_rss_fields(struct net_device *dev, struct genl_info *info,
		     u32 rss_context, struct rss_reply_data *data,
		     bool xfrm_sym, bool *mod)
{
	struct nlattr *flow_nest = info->attrs[ETHTOOL_A_RSS_FLOW_HASH];
	struct nlattr *flows[ETHTOOL_A_FLOW_MAX + 1];
	const struct ethtool_ops *ops;
	int i, ret;

	ops = dev->ethtool_ops;

	ret = rss_check_rxfh_fields_sym(dev, info, data, xfrm_sym);
	if (ret)
		return ret;

	if (!flow_nest)
		return 0;

	ret = nla_parse_nested(flows, ARRAY_SIZE(ethnl_rss_flows_policy) - 1,
			       flow_nest, ethnl_rss_flows_policy, info->extack);
	if (ret < 0)
		return ret;

	for (i = 1; i < __ETHTOOL_A_FLOW_CNT; i++) {
		struct ethtool_rxfh_fields fields = {
			.flow_type	= ethtool_rxfh_ft_nl2ioctl[i],
			.rss_context	= rss_context,
		};

		if (!flows[i])
			continue;

		fields.data = nla_get_u32(flows[i]);
		if (data->has_flow_hash && data->flow_hash[i] == fields.data)
			continue;

		if (xfrm_sym && !ethtool_rxfh_config_is_sym(fields.data)) {
			NL_SET_ERR_MSG_ATTR(info->extack, flows[i],
					    "conflict with xfrm-input");
			return -EINVAL;
		}

		ret = ops->set_rxfh_fields(dev, &fields, info->extack);
		if (ret)
			return ret;

		*mod = true;
	}

	return 0;
}

static void
rss_set_ctx_update(struct ethtool_rxfh_context *ctx, struct nlattr **tb,
		   struct rss_reply_data *data, struct ethtool_rxfh_param *rxfh)
{
	int i;

	if (rxfh->indir) {
		for (i = 0; i < data->indir_size; i++)
			ethtool_rxfh_context_indir(ctx)[i] = rxfh->indir[i];
		ctx->indir_configured = !!nla_len(tb[ETHTOOL_A_RSS_INDIR]);
	}
	if (rxfh->key) {
		memcpy(ethtool_rxfh_context_key(ctx), rxfh->key,
		       data->hkey_size);
		ctx->key_configured = !!rxfh->key_size;
	}
	if (rxfh->hfunc != ETH_RSS_HASH_NO_CHANGE)
		ctx->hfunc = rxfh->hfunc;
	if (rxfh->input_xfrm != RXH_XFRM_NO_CHANGE)
		ctx->input_xfrm = rxfh->input_xfrm;
}

static int
ethnl_rss_set(struct ethnl_req_info *req_info, struct genl_info *info)
{
	bool indir_reset = false, indir_mod, xfrm_sym = false;
	struct rss_req_info *request = RSS_REQINFO(req_info);
	struct ethtool_rxfh_context *ctx = NULL;
	struct net_device *dev = req_info->dev;
	bool mod = false, fields_mod = false;
	struct ethtool_rxfh_param rxfh = {};
	struct nlattr **tb = info->attrs;
	struct rss_reply_data data = {};
	const struct ethtool_ops *ops;
	int ret;

	ops = dev->ethtool_ops;
	data.base.dev = dev;

	ret = rss_prepare(request, dev, &data, info);
	if (ret)
		return ret;

	rxfh.rss_context = request->rss_context;

	ret = rss_set_prep_indir(dev, info, &data, &rxfh, &indir_reset, &mod);
	if (ret)
		goto exit_clean_data;
	indir_mod = !!tb[ETHTOOL_A_RSS_INDIR];

	rxfh.hfunc = data.hfunc;
	ethnl_update_u8(&rxfh.hfunc, tb[ETHTOOL_A_RSS_HFUNC], &mod);
	if (rxfh.hfunc == data.hfunc)
		rxfh.hfunc = ETH_RSS_HASH_NO_CHANGE;

	ret = rss_set_prep_hkey(dev, info, &data, &rxfh, &mod);
	if (ret)
		goto exit_free_indir;

	rxfh.input_xfrm = data.input_xfrm;
	ethnl_update_u8(&rxfh.input_xfrm, tb[ETHTOOL_A_RSS_INPUT_XFRM], &mod);
	/* For drivers which don't support input_xfrm it will be set to 0xff
	 * in the RSS context info. In all other case input_xfrm != 0 means
	 * symmetric hashing is requested.
	 */
	if (!request->rss_context || ops->rxfh_per_ctx_key)
		xfrm_sym = rxfh.input_xfrm || data.input_xfrm;
	if (rxfh.input_xfrm == data.input_xfrm)
		rxfh.input_xfrm = RXH_XFRM_NO_CHANGE;

	mutex_lock(&dev->ethtool->rss_lock);
	if (request->rss_context) {
		ctx = xa_load(&dev->ethtool->rss_ctx, request->rss_context);
		if (!ctx) {
			ret = -ENOENT;
			goto exit_unlock;
		}
	}

	ret = ethnl_set_rss_fields(dev, info, request->rss_context,
				   &data, xfrm_sym, &fields_mod);
	if (ret)
		goto exit_unlock;

	if (!mod)
		ret = 0; /* nothing to tell the driver */
	else if (!ops->set_rxfh)
		ret = -EOPNOTSUPP;
	else if (!rxfh.rss_context)
		ret = ops->set_rxfh(dev, &rxfh, info->extack);
	else
		ret = ops->modify_rxfh_context(dev, ctx, &rxfh, info->extack);
	if (ret)
		goto exit_unlock;

	if (ctx)
		rss_set_ctx_update(ctx, tb, &data, &rxfh);
	else if (indir_reset)
		dev->priv_flags &= ~IFF_RXFH_CONFIGURED;
	else if (indir_mod)
		dev->priv_flags |= IFF_RXFH_CONFIGURED;

exit_unlock:
	mutex_unlock(&dev->ethtool->rss_lock);
	kfree(rxfh.key);
exit_free_indir:
	kfree(rxfh.indir);
exit_clean_data:
	rss_cleanup_data(&data.base);

	return ret ?: mod || fields_mod;
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

	.set_validate		= ethnl_rss_set_validate,
	.set			= ethnl_rss_set,
	.set_ntf_cmd		= ETHTOOL_MSG_RSS_NTF,
};

/* RSS_CREATE */

const struct nla_policy ethnl_rss_create_policy[ETHTOOL_A_RSS_INPUT_XFRM + 1] = {
	[ETHTOOL_A_RSS_HEADER]	= NLA_POLICY_NESTED(ethnl_header_policy),
	[ETHTOOL_A_RSS_CONTEXT]	= NLA_POLICY_MIN(NLA_U32, 1),
	[ETHTOOL_A_RSS_HFUNC]	= NLA_POLICY_MIN(NLA_U32, 1),
	[ETHTOOL_A_RSS_INDIR]	= NLA_POLICY_MIN(NLA_BINARY, 1),
	[ETHTOOL_A_RSS_HKEY]	= NLA_POLICY_MIN(NLA_BINARY, 1),
	[ETHTOOL_A_RSS_INPUT_XFRM] =
		NLA_POLICY_MAX(NLA_U32, RXH_XFRM_SYM_OR_XOR),
};

static int
ethnl_rss_create_validate(struct net_device *dev, struct genl_info *info)
{
	const struct ethtool_ops *ops = dev->ethtool_ops;
	struct nlattr **tb = info->attrs;
	struct nlattr *bad_attr = NULL;
	u32 rss_context, input_xfrm;

	if (!ops->create_rxfh_context)
		return -EOPNOTSUPP;

	rss_context = nla_get_u32_default(tb[ETHTOOL_A_RSS_CONTEXT], 0);
	if (ops->rxfh_max_num_contexts &&
	    ops->rxfh_max_num_contexts <= rss_context) {
		NL_SET_BAD_ATTR(info->extack, tb[ETHTOOL_A_RSS_CONTEXT]);
		return -ERANGE;
	}

	if (!ops->rxfh_per_ctx_key) {
		bad_attr = bad_attr ?: tb[ETHTOOL_A_RSS_HFUNC];
		bad_attr = bad_attr ?: tb[ETHTOOL_A_RSS_HKEY];
		bad_attr = bad_attr ?: tb[ETHTOOL_A_RSS_INPUT_XFRM];
	}

	input_xfrm = nla_get_u32_default(tb[ETHTOOL_A_RSS_INPUT_XFRM], 0);
	if (input_xfrm & ~ops->supported_input_xfrm)
		bad_attr = bad_attr ?: tb[ETHTOOL_A_RSS_INPUT_XFRM];

	if (bad_attr) {
		NL_SET_BAD_ATTR(info->extack, bad_attr);
		return -EOPNOTSUPP;
	}

	return 0;
}

static void
ethnl_rss_create_send_ntf(struct sk_buff *rsp, struct net_device *dev)
{
	struct nlmsghdr *nlh = (void *)rsp->data;
	struct genlmsghdr *genl_hdr;

	/* Convert the reply into a notification */
	nlh->nlmsg_pid = 0;
	nlh->nlmsg_seq = ethnl_bcast_seq_next();

	genl_hdr = nlmsg_data(nlh);
	genl_hdr->cmd =	ETHTOOL_MSG_RSS_CREATE_NTF;

	ethnl_multicast(rsp, dev);
}

int ethnl_rss_create_doit(struct sk_buff *skb, struct genl_info *info)
{
	bool indir_dflt = false, mod = false, ntf_fail = false;
	struct ethtool_rxfh_param rxfh = {};
	struct ethtool_rxfh_context *ctx;
	struct nlattr **tb = info->attrs;
	struct rss_reply_data data = {};
	const struct ethtool_ops *ops;
	struct rss_req_info req = {};
	struct net_device *dev;
	struct sk_buff *rsp;
	void *hdr;
	u32 limit;
	int ret;

	rsp = genlmsg_new(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!rsp)
		return -ENOMEM;

	ret = ethnl_parse_header_dev_get(&req.base, tb[ETHTOOL_A_RSS_HEADER],
					 genl_info_net(info), info->extack,
					 true);
	if (ret < 0)
		goto exit_free_rsp;

	dev = req.base.dev;
	ops = dev->ethtool_ops;

	req.rss_context = nla_get_u32_default(tb[ETHTOOL_A_RSS_CONTEXT], 0);

	ret = ethnl_rss_create_validate(dev, info);
	if (ret)
		goto exit_free_dev;

	rtnl_lock();
	netdev_lock_ops(dev);

	ret = ethnl_ops_begin(dev);
	if (ret < 0)
		goto exit_dev_unlock;

	ret = rss_get_data_alloc(dev, &data);
	if (ret)
		goto exit_ops;

	ret = rss_set_prep_indir(dev, info, &data, &rxfh, &indir_dflt, &mod);
	if (ret)
		goto exit_clean_data;

	ethnl_update_u8(&rxfh.hfunc, tb[ETHTOOL_A_RSS_HFUNC], &mod);

	ret = rss_set_prep_hkey(dev, info, &data, &rxfh, &mod);
	if (ret)
		goto exit_free_indir;

	rxfh.input_xfrm = RXH_XFRM_NO_CHANGE;
	ethnl_update_u8(&rxfh.input_xfrm, tb[ETHTOOL_A_RSS_INPUT_XFRM], &mod);

	ctx = ethtool_rxfh_ctx_alloc(ops, data.indir_size, data.hkey_size);
	if (!ctx) {
		ret = -ENOMEM;
		goto exit_free_hkey;
	}

	mutex_lock(&dev->ethtool->rss_lock);
	if (!req.rss_context) {
		limit = ops->rxfh_max_num_contexts ?: U32_MAX;
		ret = xa_alloc(&dev->ethtool->rss_ctx, &req.rss_context, ctx,
			       XA_LIMIT(1, limit - 1), GFP_KERNEL_ACCOUNT);
	} else {
		ret = xa_insert(&dev->ethtool->rss_ctx,
				req.rss_context, ctx, GFP_KERNEL_ACCOUNT);
	}
	if (ret < 0) {
		NL_SET_ERR_MSG_ATTR(info->extack, tb[ETHTOOL_A_RSS_CONTEXT],
				    "error allocating context ID");
		goto err_unlock_free_ctx;
	}
	rxfh.rss_context = req.rss_context;

	ret = ops->create_rxfh_context(dev, ctx, &rxfh, info->extack);
	if (ret)
		goto err_ctx_id_free;

	/* Make sure driver populates defaults */
	WARN_ON_ONCE(!rxfh.key && ops->rxfh_per_ctx_key &&
		     !memchr_inv(ethtool_rxfh_context_key(ctx), 0,
				 ctx->key_size));

	/* Store the config from rxfh to Xarray.. */
	rss_set_ctx_update(ctx, tb, &data, &rxfh);
	/* .. copy from Xarray to data. */
	__rss_prepare_ctx(dev, &data, ctx);

	hdr = ethnl_unicast_put(rsp, info->snd_portid, info->snd_seq,
				ETHTOOL_MSG_RSS_CREATE_ACT_REPLY);
	ntf_fail = ethnl_fill_reply_header(rsp, dev, ETHTOOL_A_RSS_HEADER);
	ntf_fail |= rss_fill_reply(rsp, &req.base, &data.base);
	if (WARN_ON(!hdr || ntf_fail)) {
		ret = -EMSGSIZE;
		goto exit_unlock;
	}

	genlmsg_end(rsp, hdr);

	/* Use the same skb for the response and the notification,
	 * genlmsg_reply() will copy the skb if it has elevated user count.
	 */
	skb_get(rsp);
	ret = genlmsg_reply(rsp, info);
	ethnl_rss_create_send_ntf(rsp, dev);
	rsp = NULL;

exit_unlock:
	mutex_unlock(&dev->ethtool->rss_lock);
exit_free_hkey:
	kfree(rxfh.key);
exit_free_indir:
	kfree(rxfh.indir);
exit_clean_data:
	rss_get_data_free(&data);
exit_ops:
	ethnl_ops_complete(dev);
exit_dev_unlock:
	netdev_unlock_ops(dev);
	rtnl_unlock();
exit_free_dev:
	ethnl_parse_header_dev_put(&req.base);
exit_free_rsp:
	nlmsg_free(rsp);
	return ret;

err_ctx_id_free:
	xa_erase(&dev->ethtool->rss_ctx, req.rss_context);
err_unlock_free_ctx:
	kfree(ctx);
	goto exit_unlock;
}

/* RSS_DELETE */

const struct nla_policy ethnl_rss_delete_policy[ETHTOOL_A_RSS_CONTEXT + 1] = {
	[ETHTOOL_A_RSS_HEADER]	= NLA_POLICY_NESTED(ethnl_header_policy),
	[ETHTOOL_A_RSS_CONTEXT]	= NLA_POLICY_MIN(NLA_U32, 1),
};

int ethnl_rss_delete_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct ethtool_rxfh_context *ctx;
	struct nlattr **tb = info->attrs;
	struct ethnl_req_info req = {};
	const struct ethtool_ops *ops;
	struct net_device *dev;
	u32 rss_context;
	int ret;

	if (GENL_REQ_ATTR_CHECK(info, ETHTOOL_A_RSS_CONTEXT))
		return -EINVAL;
	rss_context = nla_get_u32(tb[ETHTOOL_A_RSS_CONTEXT]);

	ret = ethnl_parse_header_dev_get(&req, tb[ETHTOOL_A_RSS_HEADER],
					 genl_info_net(info), info->extack,
					 true);
	if (ret < 0)
		return ret;

	dev = req.dev;
	ops = dev->ethtool_ops;

	if (!ops->create_rxfh_context)
		goto exit_free_dev;

	rtnl_lock();
	netdev_lock_ops(dev);

	ret = ethnl_ops_begin(dev);
	if (ret < 0)
		goto exit_dev_unlock;

	mutex_lock(&dev->ethtool->rss_lock);
	ret = ethtool_check_rss_ctx_busy(dev, rss_context);
	if (ret)
		goto exit_unlock;

	ctx = xa_load(&dev->ethtool->rss_ctx, rss_context);
	if (!ctx) {
		ret = -ENOENT;
		goto exit_unlock;
	}

	ret = ops->remove_rxfh_context(dev, ctx, rss_context, info->extack);
	if (ret)
		goto exit_unlock;

	WARN_ON(xa_erase(&dev->ethtool->rss_ctx, rss_context) != ctx);
	kfree(ctx);

	ethnl_rss_delete_notify(dev, rss_context);

exit_unlock:
	mutex_unlock(&dev->ethtool->rss_lock);
	ethnl_ops_complete(dev);
exit_dev_unlock:
	netdev_unlock_ops(dev);
	rtnl_unlock();
exit_free_dev:
	ethnl_parse_header_dev_put(&req);
	return ret;
}
