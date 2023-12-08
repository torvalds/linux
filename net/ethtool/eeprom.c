// SPDX-License-Identifier: GPL-2.0-only

#include <linux/ethtool.h>
#include <linux/sfp.h>
#include "netlink.h"
#include "common.h"

struct eeprom_req_info {
	struct ethnl_req_info	base;
	u32			offset;
	u32			length;
	u8			page;
	u8			bank;
	u8			i2c_address;
};

struct eeprom_reply_data {
	struct ethnl_reply_data base;
	u32			length;
	u8			*data;
};

#define MODULE_EEPROM_REQINFO(__req_base) \
	container_of(__req_base, struct eeprom_req_info, base)

#define MODULE_EEPROM_REPDATA(__reply_base) \
	container_of(__reply_base, struct eeprom_reply_data, base)

static int fallback_set_params(struct eeprom_req_info *request,
			       struct ethtool_modinfo *modinfo,
			       struct ethtool_eeprom *eeprom)
{
	u32 offset = request->offset;
	u32 length = request->length;

	if (request->page)
		offset = request->page * ETH_MODULE_EEPROM_PAGE_LEN + offset;

	if (modinfo->type == ETH_MODULE_SFF_8472 &&
	    request->i2c_address == 0x51)
		offset += ETH_MODULE_EEPROM_PAGE_LEN * 2;

	if (offset >= modinfo->eeprom_len)
		return -EINVAL;

	eeprom->cmd = ETHTOOL_GMODULEEEPROM;
	eeprom->len = length;
	eeprom->offset = offset;

	return 0;
}

static int eeprom_fallback(struct eeprom_req_info *request,
			   struct eeprom_reply_data *reply,
			   struct genl_info *info)
{
	struct net_device *dev = reply->base.dev;
	struct ethtool_modinfo modinfo = {0};
	struct ethtool_eeprom eeprom = {0};
	u8 *data;
	int err;

	modinfo.cmd = ETHTOOL_GMODULEINFO;
	err = ethtool_get_module_info_call(dev, &modinfo);
	if (err < 0)
		return err;

	err = fallback_set_params(request, &modinfo, &eeprom);
	if (err < 0)
		return err;

	data = kmalloc(eeprom.len, GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	err = ethtool_get_module_eeprom_call(dev, &eeprom, data);
	if (err < 0)
		goto err_out;

	reply->data = data;
	reply->length = eeprom.len;

	return 0;

err_out:
	kfree(data);
	return err;
}

static int get_module_eeprom_by_page(struct net_device *dev,
				     struct ethtool_module_eeprom *page_data,
				     struct netlink_ext_ack *extack)
{
	const struct ethtool_ops *ops = dev->ethtool_ops;

	if (dev->sfp_bus)
		return sfp_get_module_eeprom_by_page(dev->sfp_bus, page_data, extack);

	if (ops->get_module_eeprom_by_page)
		return ops->get_module_eeprom_by_page(dev, page_data, extack);

	return -EOPNOTSUPP;
}

static int eeprom_prepare_data(const struct ethnl_req_info *req_base,
			       struct ethnl_reply_data *reply_base,
			       struct genl_info *info)
{
	struct eeprom_reply_data *reply = MODULE_EEPROM_REPDATA(reply_base);
	struct eeprom_req_info *request = MODULE_EEPROM_REQINFO(req_base);
	struct ethtool_module_eeprom page_data = {0};
	struct net_device *dev = reply_base->dev;
	int ret;

	page_data.offset = request->offset;
	page_data.length = request->length;
	page_data.i2c_address = request->i2c_address;
	page_data.page = request->page;
	page_data.bank = request->bank;
	page_data.data = kmalloc(page_data.length, GFP_KERNEL);
	if (!page_data.data)
		return -ENOMEM;

	ret = ethnl_ops_begin(dev);
	if (ret)
		goto err_free;

	ret = get_module_eeprom_by_page(dev, &page_data, info ? info->extack : NULL);
	if (ret < 0)
		goto err_ops;

	reply->length = ret;
	reply->data = page_data.data;

	ethnl_ops_complete(dev);
	return 0;

err_ops:
	ethnl_ops_complete(dev);
err_free:
	kfree(page_data.data);

	if (ret == -EOPNOTSUPP)
		return eeprom_fallback(request, reply, info);
	return ret;
}

static int eeprom_parse_request(struct ethnl_req_info *req_info, struct nlattr **tb,
				struct netlink_ext_ack *extack)
{
	struct eeprom_req_info *request = MODULE_EEPROM_REQINFO(req_info);

	if (!tb[ETHTOOL_A_MODULE_EEPROM_OFFSET] ||
	    !tb[ETHTOOL_A_MODULE_EEPROM_LENGTH] ||
	    !tb[ETHTOOL_A_MODULE_EEPROM_PAGE] ||
	    !tb[ETHTOOL_A_MODULE_EEPROM_I2C_ADDRESS])
		return -EINVAL;

	request->i2c_address = nla_get_u8(tb[ETHTOOL_A_MODULE_EEPROM_I2C_ADDRESS]);
	request->offset = nla_get_u32(tb[ETHTOOL_A_MODULE_EEPROM_OFFSET]);
	request->length = nla_get_u32(tb[ETHTOOL_A_MODULE_EEPROM_LENGTH]);

	/* The following set of conditions limit the API to only dump 1/2
	 * EEPROM page without crossing low page boundary located at offset 128.
	 * This means user may only request dumps of length limited to 128 from
	 * either low 128 bytes or high 128 bytes.
	 * For pages higher than 0 only high 128 bytes are accessible.
	 */
	request->page = nla_get_u8(tb[ETHTOOL_A_MODULE_EEPROM_PAGE]);
	if (request->page && request->offset < ETH_MODULE_EEPROM_PAGE_LEN) {
		NL_SET_ERR_MSG_ATTR(extack, tb[ETHTOOL_A_MODULE_EEPROM_PAGE],
				    "reading from lower half page is allowed for page 0 only");
		return -EINVAL;
	}

	if (request->offset < ETH_MODULE_EEPROM_PAGE_LEN &&
	    request->offset + request->length > ETH_MODULE_EEPROM_PAGE_LEN) {
		NL_SET_ERR_MSG_ATTR(extack, tb[ETHTOOL_A_MODULE_EEPROM_LENGTH],
				    "reading cross half page boundary is illegal");
		return -EINVAL;
	} else if (request->offset + request->length > ETH_MODULE_EEPROM_PAGE_LEN * 2) {
		NL_SET_ERR_MSG_ATTR(extack, tb[ETHTOOL_A_MODULE_EEPROM_LENGTH],
				    "reading cross page boundary is illegal");
		return -EINVAL;
	}

	if (tb[ETHTOOL_A_MODULE_EEPROM_BANK])
		request->bank = nla_get_u8(tb[ETHTOOL_A_MODULE_EEPROM_BANK]);

	return 0;
}

static int eeprom_reply_size(const struct ethnl_req_info *req_base,
			     const struct ethnl_reply_data *reply_base)
{
	const struct eeprom_req_info *request = MODULE_EEPROM_REQINFO(req_base);

	return nla_total_size(sizeof(u8) * request->length); /* _EEPROM_DATA */
}

static int eeprom_fill_reply(struct sk_buff *skb,
			     const struct ethnl_req_info *req_base,
			     const struct ethnl_reply_data *reply_base)
{
	struct eeprom_reply_data *reply = MODULE_EEPROM_REPDATA(reply_base);

	return nla_put(skb, ETHTOOL_A_MODULE_EEPROM_DATA, reply->length, reply->data);
}

static void eeprom_cleanup_data(struct ethnl_reply_data *reply_base)
{
	struct eeprom_reply_data *reply = MODULE_EEPROM_REPDATA(reply_base);

	kfree(reply->data);
}

const struct ethnl_request_ops ethnl_module_eeprom_request_ops = {
	.request_cmd		= ETHTOOL_MSG_MODULE_EEPROM_GET,
	.reply_cmd		= ETHTOOL_MSG_MODULE_EEPROM_GET_REPLY,
	.hdr_attr		= ETHTOOL_A_MODULE_EEPROM_HEADER,
	.req_info_size		= sizeof(struct eeprom_req_info),
	.reply_data_size	= sizeof(struct eeprom_reply_data),

	.parse_request		= eeprom_parse_request,
	.prepare_data		= eeprom_prepare_data,
	.reply_size		= eeprom_reply_size,
	.fill_reply		= eeprom_fill_reply,
	.cleanup_data		= eeprom_cleanup_data,
};

const struct nla_policy ethnl_module_eeprom_get_policy[] = {
	[ETHTOOL_A_MODULE_EEPROM_HEADER]	= NLA_POLICY_NESTED(ethnl_header_policy),
	[ETHTOOL_A_MODULE_EEPROM_OFFSET]	=
		NLA_POLICY_MAX(NLA_U32, ETH_MODULE_EEPROM_PAGE_LEN * 2 - 1),
	[ETHTOOL_A_MODULE_EEPROM_LENGTH]	=
		NLA_POLICY_RANGE(NLA_U32, 1, ETH_MODULE_EEPROM_PAGE_LEN),
	[ETHTOOL_A_MODULE_EEPROM_PAGE]		= { .type = NLA_U8 },
	[ETHTOOL_A_MODULE_EEPROM_BANK]		= { .type = NLA_U8 },
	[ETHTOOL_A_MODULE_EEPROM_I2C_ADDRESS]	=
		NLA_POLICY_RANGE(NLA_U8, 0, ETH_MODULE_MAX_I2C_ADDRESS),
};

