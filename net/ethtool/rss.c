// SPDX-License-Identifier: GPL-2.0-only

#include "netlink.h"
#include "common.h"

struct rss_req_info {
	struct ethnl_req_info		base;
	u32				rss_context;
};

struct rss_reply_data {
	struct ethnl_reply_data		base;
	u32				indir_size;
	u32				hkey_size;
	u32				hfunc;
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
};

static int
rss_parse_request(struct ethnl_req_info *req_info, struct nlattr **tb,
		  struct netlink_ext_ack *extack)
{
	struct rss_req_info *request = RSS_REQINFO(req_info);

	if (tb[ETHTOOL_A_RSS_CONTEXT])
		request->rss_context = nla_get_u32(tb[ETHTOOL_A_RSS_CONTEXT]);

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
	u32 total_size, indir_bytes;
	u8 dev_hfunc = 0;
	u8 *rss_config;
	int ret;

	ops = dev->ethtool_ops;
	if (!ops->get_rxfh)
		return -EOPNOTSUPP;

	/* Some drivers don't handle rss_context */
	if (request->rss_context && !ops->get_rxfh_context)
		return -EOPNOTSUPP;

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

	if (request->rss_context)
		ret = ops->get_rxfh_context(dev, data->indir_table, data->hkey,
					    &dev_hfunc, request->rss_context);
	else
		ret = ops->get_rxfh(dev, data->indir_table, data->hkey,
				    &dev_hfunc);

	if (ret)
		goto out_ops;

	data->hfunc = dev_hfunc;
out_ops:
	ethnl_ops_complete(dev);
	return ret;
}

static int
rss_reply_size(const struct ethnl_req_info *req_base,
	       const struct ethnl_reply_data *reply_base)
{
	const struct rss_reply_data *data = RSS_REPDATA(reply_base);
	int len;

	len = nla_total_size(sizeof(u32)) +	/* _RSS_HFUNC */
	      nla_total_size(sizeof(u32) * data->indir_size) + /* _RSS_INDIR */
	      nla_total_size(data->hkey_size);	/* _RSS_HKEY */

	return len;
}

static int
rss_fill_reply(struct sk_buff *skb, const struct ethnl_req_info *req_base,
	       const struct ethnl_reply_data *reply_base)
{
	const struct rss_reply_data *data = RSS_REPDATA(reply_base);

	if ((data->hfunc &&
	     nla_put_u32(skb, ETHTOOL_A_RSS_HFUNC, data->hfunc)) ||
	    (data->indir_size &&
	     nla_put(skb, ETHTOOL_A_RSS_INDIR,
		     sizeof(u32) * data->indir_size, data->indir_table)) ||
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
