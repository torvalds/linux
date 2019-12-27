/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _NET_ETHTOOL_NETLINK_H
#define _NET_ETHTOOL_NETLINK_H

#include <linux/ethtool_netlink.h>
#include <linux/netdevice.h>
#include <net/genetlink.h>
#include <net/sock.h>

struct ethnl_req_info;

int ethnl_parse_header(struct ethnl_req_info *req_info,
		       const struct nlattr *nest, struct net *net,
		       struct netlink_ext_ack *extack, bool require_dev);
int ethnl_fill_reply_header(struct sk_buff *skb, struct net_device *dev,
			    u16 attrtype);
struct sk_buff *ethnl_reply_init(size_t payload, struct net_device *dev, u8 cmd,
				 u16 hdr_attrtype, struct genl_info *info,
				 void **ehdrp);

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

/**
 * struct ethnl_req_info - base type of request information for GET requests
 * @dev:   network device the request is for (may be null)
 * @flags: request flags common for all request types
 *
 * This is a common base, additional members may follow after this structure.
 */
struct ethnl_req_info {
	struct net_device	*dev;
	u32			flags;
};

#endif /* _NET_ETHTOOL_NETLINK_H */
