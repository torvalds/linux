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
void *ethnl_unicast_put(struct sk_buff *skb, u32 portid, u32 seq, u8 cmd);
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
 * @skb:      skb with the message
 * @attrtype: attribute type
 * @s:        ETH_GSTRING_LEN sized string (may not be null terminated)
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
 * ethnl_update_bool() - updateb bool used as bool from NLA_U8 attribute
 * @dst:  value to update
 * @attr: netlink attribute with new value or null
 * @mod:  pointer to bool for modification tracking
 *
 * Use the bool value from NLA_U8 netlink attribute @attr to set bool variable
 * pointed to by @dst to 0 (if zero) or 1 (if not); do nothing if @attr is
 * null. Bool pointed to by @mod is set to true if this function changed the
 * logical value of *dst, otherwise it is left as is.
 */
static inline void ethnl_update_bool(bool *dst, const struct nlattr *attr,
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
 * ethnl_update_binary() - update binary data from NLA_BINARY attribute
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
 * @dev_tracker: refcount tracker for @dev reference
 * @flags: request flags common for all request types
 * @phy_index: phy_device index connected to @dev this request is for. Can be
 *	       0 if the request doesn't target a phy, or if the @dev's attached
 *	       phy is targeted.
 *
 * This is a common base for request specific structures holding data from
 * parsed userspace request. These always embed struct ethnl_req_info at
 * zero offset.
 */
struct ethnl_req_info {
	struct net_device	*dev;
	netdevice_tracker	dev_tracker;
	u32			flags;
	u32			phy_index;
};

static inline void ethnl_parse_header_dev_put(struct ethnl_req_info *req_info)
{
	netdev_put(req_info->dev, &req_info->dev_tracker);
}

/**
 * ethnl_req_get_phydev() - Gets the phy_device targeted by this request,
 *			    if any. Must be called under rntl_lock().
 * @req_info:	The ethnl request to get the phy from.
 * @tb:		The netlink attributes array, for error reporting.
 * @header:	The netlink header index, used for error reporting.
 * @extack:	The netlink extended ACK, for error reporting.
 *
 * The caller must hold RTNL, until it's done interacting with the returned
 * phy_device.
 *
 * Return: A phy_device pointer corresponding either to the passed phy_index
 *	   if one is provided. If not, the phy_device attached to the
 *	   net_device targeted by this request is returned. If there's no
 *	   targeted net_device, or no phy_device is attached, NULL is
 *	   returned. If the provided phy_index is invalid, an error pointer
 *	   is returned.
 */
struct phy_device *ethnl_req_get_phydev(const struct ethnl_req_info *req_info,
					struct nlattr **tb, unsigned int header,
					struct netlink_ext_ack *extack);

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

int ethnl_ops_begin(struct net_device *dev);
void ethnl_ops_complete(struct net_device *dev);

enum ethnl_sock_type {
	ETHTOOL_SOCK_TYPE_MODULE_FW_FLASH,
};

struct ethnl_sock_priv {
	struct net_device *dev;
	u32 portid;
	enum ethnl_sock_type type;
};

int ethnl_sock_priv_set(struct sk_buff *skb, struct net_device *dev, u32 portid,
			enum ethnl_sock_type type);

/**
 * struct ethnl_request_ops - unified handling of GET and SET requests
 * @request_cmd:      command id for request (GET)
 * @reply_cmd:        command id for reply (GET_REPLY)
 * @hdr_attr:         attribute type for request header
 * @req_info_size:    size of request info
 * @reply_data_size:  size of reply data
 * @allow_nodev_do:   allow non-dump request with no device identification
 * @set_ntf_cmd:      notification to generate on changes (SET)
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
 * @set_validate:
 *	Check if set operation is supported for a given device, and perform
 *	extra input checks. Expected return values:
 *	 - 0 if the operation is a noop for the device (rare)
 *	 - 1 if operation should proceed to calling @set
 *	 - negative errno on errors
 *	Called without any locks, just a reference on the netdev.
 * @set:
 *	Execute the set operation. The implementation should return
 *	 - 0 if no configuration has changed
 *	 - 1 if configuration changed and notification should be generated
 *	 - negative errno on errors
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
	u8			set_ntf_cmd;

	int (*parse_request)(struct ethnl_req_info *req_info,
			     struct nlattr **tb,
			     struct netlink_ext_ack *extack);
	int (*prepare_data)(const struct ethnl_req_info *req_info,
			    struct ethnl_reply_data *reply_data,
			    const struct genl_info *info);
	int (*reply_size)(const struct ethnl_req_info *req_info,
			  const struct ethnl_reply_data *reply_data);
	int (*fill_reply)(struct sk_buff *skb,
			  const struct ethnl_req_info *req_info,
			  const struct ethnl_reply_data *reply_data);
	void (*cleanup_data)(struct ethnl_reply_data *reply_data);

	int (*set_validate)(struct ethnl_req_info *req_info,
			    struct genl_info *info);
	int (*set)(struct ethnl_req_info *req_info,
		   struct genl_info *info);
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
extern const struct ethnl_request_ops ethnl_fec_request_ops;
extern const struct ethnl_request_ops ethnl_module_eeprom_request_ops;
extern const struct ethnl_request_ops ethnl_stats_request_ops;
extern const struct ethnl_request_ops ethnl_phc_vclocks_request_ops;
extern const struct ethnl_request_ops ethnl_module_request_ops;
extern const struct ethnl_request_ops ethnl_pse_request_ops;
extern const struct ethnl_request_ops ethnl_rss_request_ops;
extern const struct ethnl_request_ops ethnl_plca_cfg_request_ops;
extern const struct ethnl_request_ops ethnl_plca_status_request_ops;
extern const struct ethnl_request_ops ethnl_mm_request_ops;
extern const struct ethnl_request_ops ethnl_phy_request_ops;
extern const struct ethnl_request_ops ethnl_tsconfig_request_ops;

extern const struct nla_policy ethnl_header_policy[ETHTOOL_A_HEADER_FLAGS + 1];
extern const struct nla_policy ethnl_header_policy_stats[ETHTOOL_A_HEADER_FLAGS + 1];
extern const struct nla_policy ethnl_header_policy_phy[ETHTOOL_A_HEADER_PHY_INDEX + 1];
extern const struct nla_policy ethnl_header_policy_phy_stats[ETHTOOL_A_HEADER_PHY_INDEX + 1];
extern const struct nla_policy ethnl_strset_get_policy[ETHTOOL_A_STRSET_COUNTS_ONLY + 1];
extern const struct nla_policy ethnl_linkinfo_get_policy[ETHTOOL_A_LINKINFO_HEADER + 1];
extern const struct nla_policy ethnl_linkinfo_set_policy[ETHTOOL_A_LINKINFO_TP_MDIX_CTRL + 1];
extern const struct nla_policy ethnl_linkmodes_get_policy[ETHTOOL_A_LINKMODES_HEADER + 1];
extern const struct nla_policy ethnl_linkmodes_set_policy[ETHTOOL_A_LINKMODES_LANES + 1];
extern const struct nla_policy ethnl_linkstate_get_policy[ETHTOOL_A_LINKSTATE_HEADER + 1];
extern const struct nla_policy ethnl_debug_get_policy[ETHTOOL_A_DEBUG_HEADER + 1];
extern const struct nla_policy ethnl_debug_set_policy[ETHTOOL_A_DEBUG_MSGMASK + 1];
extern const struct nla_policy ethnl_wol_get_policy[ETHTOOL_A_WOL_HEADER + 1];
extern const struct nla_policy ethnl_wol_set_policy[ETHTOOL_A_WOL_SOPASS + 1];
extern const struct nla_policy ethnl_features_get_policy[ETHTOOL_A_FEATURES_HEADER + 1];
extern const struct nla_policy ethnl_features_set_policy[ETHTOOL_A_FEATURES_WANTED + 1];
extern const struct nla_policy ethnl_privflags_get_policy[ETHTOOL_A_PRIVFLAGS_HEADER + 1];
extern const struct nla_policy ethnl_privflags_set_policy[ETHTOOL_A_PRIVFLAGS_FLAGS + 1];
extern const struct nla_policy ethnl_rings_get_policy[ETHTOOL_A_RINGS_HEADER + 1];
extern const struct nla_policy ethnl_rings_set_policy[ETHTOOL_A_RINGS_HDS_THRESH_MAX + 1];
extern const struct nla_policy ethnl_channels_get_policy[ETHTOOL_A_CHANNELS_HEADER + 1];
extern const struct nla_policy ethnl_channels_set_policy[ETHTOOL_A_CHANNELS_COMBINED_COUNT + 1];
extern const struct nla_policy ethnl_coalesce_get_policy[ETHTOOL_A_COALESCE_HEADER + 1];
extern const struct nla_policy ethnl_coalesce_set_policy[ETHTOOL_A_COALESCE_MAX + 1];
extern const struct nla_policy ethnl_pause_get_policy[ETHTOOL_A_PAUSE_STATS_SRC + 1];
extern const struct nla_policy ethnl_pause_set_policy[ETHTOOL_A_PAUSE_TX + 1];
extern const struct nla_policy ethnl_eee_get_policy[ETHTOOL_A_EEE_HEADER + 1];
extern const struct nla_policy ethnl_eee_set_policy[ETHTOOL_A_EEE_TX_LPI_TIMER + 1];
extern const struct nla_policy ethnl_tsinfo_get_policy[ETHTOOL_A_TSINFO_MAX + 1];
extern const struct nla_policy ethnl_cable_test_act_policy[ETHTOOL_A_CABLE_TEST_HEADER + 1];
extern const struct nla_policy ethnl_cable_test_tdr_act_policy[ETHTOOL_A_CABLE_TEST_TDR_CFG + 1];
extern const struct nla_policy ethnl_tunnel_info_get_policy[ETHTOOL_A_TUNNEL_INFO_HEADER + 1];
extern const struct nla_policy ethnl_fec_get_policy[ETHTOOL_A_FEC_HEADER + 1];
extern const struct nla_policy ethnl_fec_set_policy[ETHTOOL_A_FEC_AUTO + 1];
extern const struct nla_policy ethnl_module_eeprom_get_policy[ETHTOOL_A_MODULE_EEPROM_I2C_ADDRESS + 1];
extern const struct nla_policy ethnl_stats_get_policy[ETHTOOL_A_STATS_SRC + 1];
extern const struct nla_policy ethnl_phc_vclocks_get_policy[ETHTOOL_A_PHC_VCLOCKS_HEADER + 1];
extern const struct nla_policy ethnl_module_get_policy[ETHTOOL_A_MODULE_HEADER + 1];
extern const struct nla_policy ethnl_module_set_policy[ETHTOOL_A_MODULE_POWER_MODE_POLICY + 1];
extern const struct nla_policy ethnl_pse_get_policy[ETHTOOL_A_PSE_HEADER + 1];
extern const struct nla_policy ethnl_pse_set_policy[ETHTOOL_A_PSE_MAX + 1];
extern const struct nla_policy ethnl_rss_get_policy[ETHTOOL_A_RSS_START_CONTEXT + 1];
extern const struct nla_policy ethnl_plca_get_cfg_policy[ETHTOOL_A_PLCA_HEADER + 1];
extern const struct nla_policy ethnl_plca_set_cfg_policy[ETHTOOL_A_PLCA_MAX + 1];
extern const struct nla_policy ethnl_plca_get_status_policy[ETHTOOL_A_PLCA_HEADER + 1];
extern const struct nla_policy ethnl_mm_get_policy[ETHTOOL_A_MM_HEADER + 1];
extern const struct nla_policy ethnl_mm_set_policy[ETHTOOL_A_MM_MAX + 1];
extern const struct nla_policy ethnl_module_fw_flash_act_policy[ETHTOOL_A_MODULE_FW_FLASH_PASSWORD + 1];
extern const struct nla_policy ethnl_phy_get_policy[ETHTOOL_A_PHY_HEADER + 1];
extern const struct nla_policy ethnl_tsconfig_get_policy[ETHTOOL_A_TSCONFIG_HEADER + 1];
extern const struct nla_policy ethnl_tsconfig_set_policy[ETHTOOL_A_TSCONFIG_MAX + 1];

int ethnl_set_features(struct sk_buff *skb, struct genl_info *info);
int ethnl_act_cable_test(struct sk_buff *skb, struct genl_info *info);
int ethnl_act_cable_test_tdr(struct sk_buff *skb, struct genl_info *info);
int ethnl_tunnel_info_doit(struct sk_buff *skb, struct genl_info *info);
int ethnl_tunnel_info_start(struct netlink_callback *cb);
int ethnl_tunnel_info_dumpit(struct sk_buff *skb, struct netlink_callback *cb);
int ethnl_act_module_fw_flash(struct sk_buff *skb, struct genl_info *info);
int ethnl_rss_dump_start(struct netlink_callback *cb);
int ethnl_rss_dumpit(struct sk_buff *skb, struct netlink_callback *cb);
int ethnl_phy_start(struct netlink_callback *cb);
int ethnl_phy_doit(struct sk_buff *skb, struct genl_info *info);
int ethnl_phy_dumpit(struct sk_buff *skb, struct netlink_callback *cb);
int ethnl_phy_done(struct netlink_callback *cb);
int ethnl_tsinfo_start(struct netlink_callback *cb);
int ethnl_tsinfo_dumpit(struct sk_buff *skb, struct netlink_callback *cb);
int ethnl_tsinfo_done(struct netlink_callback *cb);

extern const char stats_std_names[__ETHTOOL_STATS_CNT][ETH_GSTRING_LEN];
extern const char stats_eth_phy_names[__ETHTOOL_A_STATS_ETH_PHY_CNT][ETH_GSTRING_LEN];
extern const char stats_eth_mac_names[__ETHTOOL_A_STATS_ETH_MAC_CNT][ETH_GSTRING_LEN];
extern const char stats_eth_ctrl_names[__ETHTOOL_A_STATS_ETH_CTRL_CNT][ETH_GSTRING_LEN];
extern const char stats_rmon_names[__ETHTOOL_A_STATS_RMON_CNT][ETH_GSTRING_LEN];
extern const char stats_phy_names[__ETHTOOL_A_STATS_PHY_CNT][ETH_GSTRING_LEN];

#endif /* _NET_ETHTOOL_NETLINK_H */
