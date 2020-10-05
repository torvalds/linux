/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _NET_ETHTOOL_NETLINK_H
#define _NET_ETHTOOL_NETLINK_H

#include <linux/ethtool_netlink.h>
#include <linux/netdevice.h>
#include <net/genetlink.h>
#include <net/sock.h>

struct ethnl_req_info;

int ethnl_parse_header_dev_get(struct ethnl_req_info *req_info,
			       const struct nlattr *nest, struct net *net,
			       struct netlink_ext_ack *extack,
			       bool require_dev);
int ethnl_fill_reply_header(struct sk_buff *skb, struct net_device *dev,
			    u16 attrtype);
struct sk_buff *ethnl_reply_init(size_t payload, struct net_device *dev, u8 cmd,
				 u16 hdr_attrtype, struct genl_info *info,
				 void **ehdrp);
void *ethnl_dump_put(struct sk_buff *skb, struct netlink_callback *cb, u8 cmd);
void *ethnl_bcastmsg_put(struct sk_buff *skb, u8 cmd);
int ethnl_multicast(struct sk_buff *skb, struct net_device *dev);

/**
 * ethnl_strz_size() - calculate attribute length for fixed size string
 * @s: ETH_GSTRING_LEN sized string (may not be null terminated)
 *
 * Return: total length of an attribute with null terminated string from @s
 */
static inline int ethnl_strz_size(const char *s)
{
	return nla_total_size(strnlen(s, ETH_GSTRING_LEN) + 1);
}

/**
 * ethnl_put_strz() - put string attribute with fixed size string
 * @skb:     skb with the message
 * @attrype: attribute type
 * @s:       ETH_GSTRING_LEN sized string (may not be null terminated)
 *
 * Puts an attribute with null terminated string from @s into the message.
 *
 * Return: 0 on success, negative error code on failure
 */
static inline int ethnl_put_strz(struct sk_buff *skb, u16 attrtype,
				 const char *s)
{
	unsigned int len = strnlen(s, ETH_GSTRING_LEN);
	struct nlattr *attr;

	attr = nla_reserve(skb, attrtype, len + 1);
	if (!attr)
		return -EMSGSIZE;

	memcpy(nla_data(attr), s, len);
	((char *)nla_data(attr))[len] = '\0';
	return 0;
}

/**
 * ethnl_update_u32() - update u32 value from NLA_U32 attribute
 * @dst:  value to update
 * @attr: netlink attribute with new value or null
 * @mod:  pointer to bool for modification tracking
 *
 * Copy the u32 value from NLA_U32 netlink attribute @attr into variable
 * pointed to by @dst; do nothing if @attr is null. Bool pointed to by @mod
 * is set to true if this function changed the value of *dst, otherwise it
 * is left as is.
 */
static inline void ethnl_update_u32(u32 *dst, const struct nlattr *attr,
				    bool *mod)
{
	u32 val;

	if (!attr)
		return;
	val = nla_get_u32(attr);
	if (*dst == val)
		return;

	*dst = val;
	*mod = true;
}

/**
 * ethnl_update_u8() - update u8 value from NLA_U8 attribute
 * @dst:  value to update
 * @attr: netlink attribute with new value or null
 * @mod:  pointer to bool for modification tracking
 *
 * Copy the u8 value from NLA_U8 netlink attribute @attr into variable
 * pointed to by @dst; do nothing if @attr is null. Bool pointed to by @mod
 * is set to true if this function changed the value of *dst, otherwise it
 * is left as is.
 */
static inline void ethnl_update_u8(u8 *dst, const struct nlattr *attr,
				   bool *mod)
{
	u8 val;

	if (!attr)
		return;
	val = nla_get_u8(attr);
	if (*dst == val)
		return;

	*dst = val;
	*mod = true;
}

/**
 * ethnl_update_bool32() - update u32 used as bool from NLA_U8 attribute
 * @dst:  value to update
 * @attr: netlink attribute with new value or null
 * @mod:  pointer to bool for modification tracking
 *
 * Use the u8 value from NLA_U8 netlink attribute @attr to set u32 variable
 * pointed to by @dst to 0 (if zero) or 1 (if not); do nothing if @attr is
 * null. Bool pointed to by @mod is set to true if this function changed the
 * logical value of *dst, otherwise it is left as is.
 */
static inline void ethnl_update_bool32(u32 *dst, const struct nlattr *attr,
				       bool *mod)
{
	u8 val;

	if (!attr)
		return;
	val = !!nla_get_u8(attr);
	if (!!*dst == val)
		return;

	*dst = val;
	*mod = true;
}

/**
 * ethnl_update_binary() - update binary data from NLA_BINARY atribute
 * @dst:  value to update
 * @len:  destination buffer length
 * @attr: netlink attribute with new value or null
 * @mod:  pointer to bool for modification tracking
 *
 * Use the u8 value from NLA_U8 netlink attribute @attr to rewrite data block
 * of length @len at @dst by attribute payload; do nothing if @attr is null.
 * Bool pointed to by @mod is set to true if this function changed the logical
 * value of *dst, otherwise it is left as is.
 */
static inline void ethnl_update_binary(void *dst, unsigned int len,
				       const struct nlattr *attr, bool *mod)
{
	if (!attr)
		return;
	if (nla_len(attr) < len)
		len = nla_len(attr);
	if (!memcmp(dst, nla_data(attr), len))
		return;

	memcpy(dst, nla_data(attr), len);
	*mod = true;
}

/**
 * ethnl_update_bitfield32() - update u32 value from NLA_BITFIELD32 attribute
 * @dst:  value to update
 * @attr: netlink attribute with new value or null
 * @mod:  pointer to bool for modification tracking
 *
 * Update bits in u32 value which are set in attribute's mask to values from
 * attribute's value. Do nothing if @attr is null or the value wouldn't change;
 * otherwise, set bool pointed to by @mod to true.
 */
static inline void ethnl_update_bitfield32(u32 *dst, const struct nlattr *attr,
					   bool *mod)
{
	struct nla_bitfield32 change;
	u32 newval;

	if (!attr)
		return;
	change = nla_get_bitfield32(attr);
	newval = (*dst & ~change.selector) | (change.value & change.selector);
	if (*dst == newval)
		return;

	*dst = newval;
	*mod = true;
}

/**
 * ethnl_reply_header_size() - total size of reply header
 *
 * This is an upper estimate so that we do not need to hold RTNL lock longer
 * than necessary (to prevent rename between size estimate and composing the
 * message). Accounts only for device ifindex and name as those are the only
 * attributes ethnl_fill_reply_header() puts into the reply header.
 */
static inline unsigned int ethnl_reply_header_size(void)
{
	return nla_total_size(nla_total_size(sizeof(u32)) +
			      nla_total_size(IFNAMSIZ));
}

/* GET request handling */

/* Unified processing of GET requests uses two data structures: request info
 * and reply data. Request info holds information parsed from client request
 * and its stays constant through all request processing. Reply data holds data
 * retrieved from ethtool_ops callbacks or other internal sources which is used
 * to compose the reply. When processing a dump request, request info is filled
 * only once (when the request message is parsed) but reply data is filled for
 * each reply message.
 *
 * Both structures consist of part common for all request types (struct
 * ethnl_req_info and struct ethnl_reply_data defined below) and optional
 * parts specific for each request type. Common part always starts at offset 0.
 */

/**
 * struct ethnl_req_info - base type of request information for GET requests
 * @dev:   network device the request is for (may be null)
 * @flags: request flags common for all request types
 *
 * This is a common base for request specific structures holding data from
 * parsed userspace request. These always embed struct ethnl_req_info at
 * zero offset.
 */
struct ethnl_req_info {
	struct net_device	*dev;
	u32			flags;
};

/**
 * struct ethnl_reply_data - base type of reply data for GET requests
 * @dev:       device for current reply message; in single shot requests it is
 *             equal to &ethnl_req_info.dev; in dumps it's different for each
 *             reply message
 *
 * This is a common base for request specific structures holding data for
 * kernel reply message. These always embed struct ethnl_reply_data at zero
 * offset.
 */
struct ethnl_reply_data {
	struct net_device		*dev;
};

static inline int ethnl_ops_begin(struct net_device *dev)
{
	if (dev && dev->ethtool_ops->begin)
		return dev->ethtool_ops->begin(dev);
	else
		return 0;
}

static inline void ethnl_ops_complete(struct net_device *dev)
{
	if (dev && dev->ethtool_ops->complete)
		dev->ethtool_ops->complete(dev);
}

/**
 * struct ethnl_request_ops - unified handling of GET requests
 * @request_cmd:      command id for request (GET)
 * @reply_cmd:        command id for reply (GET_REPLY)
 * @hdr_attr:         attribute type for request header
 * @req_info_size:    size of request info
 * @reply_data_size:  size of reply data
 * @allow_nodev_do:   allow non-dump request with no device identification
 * @parse_request:
 *	Parse request except common header (struct ethnl_req_info). Common
 *	header is already filled on entry, the rest up to @repdata_offset
 *	is zero initialized. This callback should only modify type specific
 *	request info by parsed attributes from request message.
 * @prepare_data:
 *	Retrieve and prepare data needed to compose a reply message. Calls to
 *	ethtool_ops handlers are limited to this callback. Common reply data
 *	(struct ethnl_reply_data) is filled on entry, type specific part after
 *	it is zero initialized. This callback should only modify the type
 *	specific part of reply data. Device identification from struct
 *	ethnl_reply_data is to be used as for dump requests, it iterates
 *	through network devices while dev member of struct ethnl_req_info
 *	points to the device from client request.
 * @reply_size:
 *	Estimate reply message size. Returned value must be sufficient for
 *	message payload without common reply header. The callback may returned
 *	estimate higher than actual message size if exact calculation would
 *	not be worth the saved memory space.
 * @fill_reply:
 *	Fill reply message payload (except for common header) from reply data.
 *	The callback must not generate more payload than previously called
 *	->reply_size() estimated.
 * @cleanup_data:
 *	Optional cleanup called when reply data is no longer needed. Can be
 *	used e.g. to free any additional data structures outside the main
 *	structure which were allocated by ->prepare_data(). When processing
 *	dump requests, ->cleanup() is called for each message.
 *
 * Description of variable parts of GET request handling when using the
 * unified infrastructure. When used, a pointer to an instance of this
 * structure is to be added to &ethnl_default_requests array and generic
 * handlers ethnl_default_doit(), ethnl_default_dumpit(),
 * ethnl_default_start() and ethnl_default_done() used in @ethtool_genl_ops;
 * ethnl_default_notify() can be used in @ethnl_notify_handlers to send
 * notifications of the corresponding type.
 */
struct ethnl_request_ops {
	u8			request_cmd;
	u8			reply_cmd;
	u16			hdr_attr;
	unsigned int		req_info_size;
	unsigned int		reply_data_size;
	bool			allow_nodev_do;

	int (*parse_request)(struct ethnl_req_info *req_info,
			     struct nlattr **tb,
			     struct netlink_ext_ack *extack);
	int (*prepare_data)(const struct ethnl_req_info *req_info,
			    struct ethnl_reply_data *reply_data,
			    struct genl_info *info);
	int (*reply_size)(const struct ethnl_req_info *req_info,
			  const struct ethnl_reply_data *reply_data);
	int (*fill_reply)(struct sk_buff *skb,
			  const struct ethnl_req_info *req_info,
			  const struct ethnl_reply_data *reply_data);
	void (*cleanup_data)(struct ethnl_reply_data *reply_data);
};

/* request handlers */

extern const struct ethnl_request_ops ethnl_strset_request_ops;
extern const struct ethnl_request_ops ethnl_linkinfo_request_ops;
extern const struct ethnl_request_ops ethnl_linkmodes_request_ops;
extern const struct ethnl_request_ops ethnl_linkstate_request_ops;
extern const struct ethnl_request_ops ethnl_debug_request_ops;
extern const struct ethnl_request_ops ethnl_wol_request_ops;
extern const struct ethnl_request_ops ethnl_features_request_ops;
extern const struct ethnl_request_ops ethnl_privflags_request_ops;
extern const struct ethnl_request_ops ethnl_rings_request_ops;
extern const struct ethnl_request_ops ethnl_channels_request_ops;
extern const struct ethnl_request_ops ethnl_coalesce_request_ops;
extern const struct ethnl_request_ops ethnl_pause_request_ops;
extern const struct ethnl_request_ops ethnl_eee_request_ops;
extern const struct ethnl_request_ops ethnl_tsinfo_request_ops;

extern const struct nla_policy ethnl_strset_get_policy[ETHTOOL_A_STRSET_MAX + 1];
extern const struct nla_policy ethnl_linkinfo_get_policy[ETHTOOL_A_LINKINFO_MAX + 1];
extern const struct nla_policy ethnl_linkinfo_set_policy[ETHTOOL_A_LINKINFO_MAX + 1];
extern const struct nla_policy ethnl_linkmodes_get_policy[ETHTOOL_A_LINKMODES_MAX + 1];
extern const struct nla_policy ethnl_linkmodes_set_policy[ETHTOOL_A_LINKMODES_MAX + 1];
extern const struct nla_policy ethnl_linkstate_get_policy[ETHTOOL_A_LINKSTATE_MAX + 1];
extern const struct nla_policy ethnl_debug_get_policy[ETHTOOL_A_DEBUG_MAX + 1];
extern const struct nla_policy ethnl_debug_set_policy[ETHTOOL_A_DEBUG_MAX + 1];
extern const struct nla_policy ethnl_wol_get_policy[ETHTOOL_A_WOL_MAX + 1];
extern const struct nla_policy ethnl_wol_set_policy[ETHTOOL_A_WOL_MAX + 1];
extern const struct nla_policy ethnl_features_get_policy[ETHTOOL_A_FEATURES_MAX + 1];
extern const struct nla_policy ethnl_features_set_policy[ETHTOOL_A_FEATURES_MAX + 1];
extern const struct nla_policy ethnl_privflags_get_policy[ETHTOOL_A_PRIVFLAGS_MAX + 1];
extern const struct nla_policy ethnl_privflags_set_policy[ETHTOOL_A_PRIVFLAGS_MAX + 1];
extern const struct nla_policy ethnl_rings_get_policy[ETHTOOL_A_RINGS_MAX + 1];
extern const struct nla_policy ethnl_rings_set_policy[ETHTOOL_A_RINGS_MAX + 1];
extern const struct nla_policy ethnl_channels_get_policy[ETHTOOL_A_CHANNELS_MAX + 1];
extern const struct nla_policy ethnl_channels_set_policy[ETHTOOL_A_CHANNELS_MAX + 1];
extern const struct nla_policy ethnl_coalesce_get_policy[ETHTOOL_A_COALESCE_MAX + 1];
extern const struct nla_policy ethnl_coalesce_set_policy[ETHTOOL_A_COALESCE_MAX + 1];
extern const struct nla_policy ethnl_pause_get_policy[ETHTOOL_A_PAUSE_MAX + 1];
extern const struct nla_policy ethnl_pause_set_policy[ETHTOOL_A_PAUSE_MAX + 1];
extern const struct nla_policy ethnl_eee_get_policy[ETHTOOL_A_EEE_MAX + 1];
extern const struct nla_policy ethnl_eee_set_policy[ETHTOOL_A_EEE_MAX + 1];
extern const struct nla_policy ethnl_tsinfo_get_policy[ETHTOOL_A_TSINFO_MAX + 1];
extern const struct nla_policy ethnl_cable_test_act_policy[ETHTOOL_A_CABLE_TEST_MAX + 1];
extern const struct nla_policy ethnl_cable_test_tdr_act_policy[ETHTOOL_A_CABLE_TEST_TDR_MAX + 1];
extern const struct nla_policy ethnl_tunnel_info_get_policy[ETHTOOL_A_TUNNEL_INFO_MAX + 1];

int ethnl_set_linkinfo(struct sk_buff *skb, struct genl_info *info);
int ethnl_set_linkmodes(struct sk_buff *skb, struct genl_info *info);
int ethnl_set_debug(struct sk_buff *skb, struct genl_info *info);
int ethnl_set_wol(struct sk_buff *skb, struct genl_info *info);
int ethnl_set_features(struct sk_buff *skb, struct genl_info *info);
int ethnl_set_privflags(struct sk_buff *skb, struct genl_info *info);
int ethnl_set_rings(struct sk_buff *skb, struct genl_info *info);
int ethnl_set_channels(struct sk_buff *skb, struct genl_info *info);
int ethnl_set_coalesce(struct sk_buff *skb, struct genl_info *info);
int ethnl_set_pause(struct sk_buff *skb, struct genl_info *info);
int ethnl_set_eee(struct sk_buff *skb, struct genl_info *info);
int ethnl_act_cable_test(struct sk_buff *skb, struct genl_info *info);
int ethnl_act_cable_test_tdr(struct sk_buff *skb, struct genl_info *info);
int ethnl_tunnel_info_doit(struct sk_buff *skb, struct genl_info *info);
int ethnl_tunnel_info_start(struct netlink_callback *cb);
int ethnl_tunnel_info_dumpit(struct sk_buff *skb, struct netlink_callback *cb);

#endif /* _NET_ETHTOOL_NETLINK_H */
