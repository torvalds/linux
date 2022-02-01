/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2007, 2008, 2009 Siemens AG
 *
 * Written by:
 * Dmitry Eremin-Solenikov <dbaryshkov@gmail.com>
 */

#ifndef __NET_CFG802154_H
#define __NET_CFG802154_H

#include <linux/ieee802154.h>
#include <linux/netdevice.h>
#include <linux/mutex.h>
#include <linux/bug.h>

#include <net/nl802154.h>

struct wpan_phy;
struct wpan_phy_cca;

#ifdef CONFIG_IEEE802154_NL802154_EXPERIMENTAL
struct ieee802154_llsec_device_key;
struct ieee802154_llsec_seclevel;
struct ieee802154_llsec_params;
struct ieee802154_llsec_device;
struct ieee802154_llsec_table;
struct ieee802154_llsec_key_id;
struct ieee802154_llsec_key;
#endif /* CONFIG_IEEE802154_NL802154_EXPERIMENTAL */

struct cfg802154_ops {
	struct net_device * (*add_virtual_intf_deprecated)(struct wpan_phy *wpan_phy,
							   const char *name,
							   unsigned char name_assign_type,
							   int type);
	void	(*del_virtual_intf_deprecated)(struct wpan_phy *wpan_phy,
					       struct net_device *dev);
	int	(*suspend)(struct wpan_phy *wpan_phy);
	int	(*resume)(struct wpan_phy *wpan_phy);
	int	(*add_virtual_intf)(struct wpan_phy *wpan_phy,
				    const char *name,
				    unsigned char name_assign_type,
				    enum nl802154_iftype type,
				    __le64 extended_addr);
	int	(*del_virtual_intf)(struct wpan_phy *wpan_phy,
				    struct wpan_dev *wpan_dev);
	int	(*set_channel)(struct wpan_phy *wpan_phy, u8 page, u8 channel);
	int	(*set_cca_mode)(struct wpan_phy *wpan_phy,
				const struct wpan_phy_cca *cca);
	int     (*set_cca_ed_level)(struct wpan_phy *wpan_phy, s32 ed_level);
	int     (*set_tx_power)(struct wpan_phy *wpan_phy, s32 power);
	int	(*set_pan_id)(struct wpan_phy *wpan_phy,
			      struct wpan_dev *wpan_dev, __le16 pan_id);
	int	(*set_short_addr)(struct wpan_phy *wpan_phy,
				  struct wpan_dev *wpan_dev, __le16 short_addr);
	int	(*set_backoff_exponent)(struct wpan_phy *wpan_phy,
					struct wpan_dev *wpan_dev, u8 min_be,
					u8 max_be);
	int	(*set_max_csma_backoffs)(struct wpan_phy *wpan_phy,
					 struct wpan_dev *wpan_dev,
					 u8 max_csma_backoffs);
	int	(*set_max_frame_retries)(struct wpan_phy *wpan_phy,
					 struct wpan_dev *wpan_dev,
					 s8 max_frame_retries);
	int	(*set_lbt_mode)(struct wpan_phy *wpan_phy,
				struct wpan_dev *wpan_dev, bool mode);
	int	(*set_ackreq_default)(struct wpan_phy *wpan_phy,
				      struct wpan_dev *wpan_dev, bool ackreq);
#ifdef CONFIG_IEEE802154_NL802154_EXPERIMENTAL
	void	(*get_llsec_table)(struct wpan_phy *wpan_phy,
				   struct wpan_dev *wpan_dev,
				   struct ieee802154_llsec_table **table);
	void	(*lock_llsec_table)(struct wpan_phy *wpan_phy,
				    struct wpan_dev *wpan_dev);
	void	(*unlock_llsec_table)(struct wpan_phy *wpan_phy,
				      struct wpan_dev *wpan_dev);
	/* TODO remove locking/get table callbacks, this is part of the
	 * nl802154 interface and should be accessible from ieee802154 layer.
	 */
	int	(*get_llsec_params)(struct wpan_phy *wpan_phy,
				    struct wpan_dev *wpan_dev,
				    struct ieee802154_llsec_params *params);
	int	(*set_llsec_params)(struct wpan_phy *wpan_phy,
				    struct wpan_dev *wpan_dev,
				    const struct ieee802154_llsec_params *params,
				    int changed);
	int	(*add_llsec_key)(struct wpan_phy *wpan_phy,
				 struct wpan_dev *wpan_dev,
				 const struct ieee802154_llsec_key_id *id,
				 const struct ieee802154_llsec_key *key);
	int	(*del_llsec_key)(struct wpan_phy *wpan_phy,
				 struct wpan_dev *wpan_dev,
				 const struct ieee802154_llsec_key_id *id);
	int	(*add_seclevel)(struct wpan_phy *wpan_phy,
				 struct wpan_dev *wpan_dev,
				 const struct ieee802154_llsec_seclevel *sl);
	int	(*del_seclevel)(struct wpan_phy *wpan_phy,
				 struct wpan_dev *wpan_dev,
				 const struct ieee802154_llsec_seclevel *sl);
	int	(*add_device)(struct wpan_phy *wpan_phy,
			      struct wpan_dev *wpan_dev,
			      const struct ieee802154_llsec_device *dev);
	int	(*del_device)(struct wpan_phy *wpan_phy,
			      struct wpan_dev *wpan_dev, __le64 extended_addr);
	int	(*add_devkey)(struct wpan_phy *wpan_phy,
			      struct wpan_dev *wpan_dev,
			      __le64 extended_addr,
			      const struct ieee802154_llsec_device_key *key);
	int	(*del_devkey)(struct wpan_phy *wpan_phy,
			      struct wpan_dev *wpan_dev,
			      __le64 extended_addr,
			      const struct ieee802154_llsec_device_key *key);
#endif /* CONFIG_IEEE802154_NL802154_EXPERIMENTAL */
};

static inline bool
wpan_phy_supported_bool(bool b, enum nl802154_supported_bool_states st)
{
	switch (st) {
	case NL802154_SUPPORTED_BOOL_TRUE:
		return b;
	case NL802154_SUPPORTED_BOOL_FALSE:
		return !b;
	case NL802154_SUPPORTED_BOOL_BOTH:
		return true;
	default:
		WARN_ON(1);
	}

	return false;
}

struct wpan_phy_supported {
	u32 channels[IEEE802154_MAX_PAGE + 1],
	    cca_modes, cca_opts, iftypes;
	enum nl802154_supported_bool_states lbt;
	u8 min_minbe, max_minbe, min_maxbe, max_maxbe,
	   min_csma_backoffs, max_csma_backoffs;
	s8 min_frame_retries, max_frame_retries;
	size_t tx_powers_size, cca_ed_levels_size;
	const s32 *tx_powers, *cca_ed_levels;
};

struct wpan_phy_cca {
	enum nl802154_cca_modes mode;
	enum nl802154_cca_opts opt;
};

static inline bool
wpan_phy_cca_cmp(const struct wpan_phy_cca *a, const struct wpan_phy_cca *b)
{
	if (a->mode != b->mode)
		return false;

	if (a->mode == NL802154_CCA_ENERGY_CARRIER)
		return a->opt == b->opt;

	return true;
}

/**
 * @WPAN_PHY_FLAG_TRANSMIT_POWER: Indicates that transceiver will support
 *	transmit power setting.
 * @WPAN_PHY_FLAG_CCA_ED_LEVEL: Indicates that transceiver will support cca ed
 *	level setting.
 * @WPAN_PHY_FLAG_CCA_MODE: Indicates that transceiver will support cca mode
 *	setting.
 */
enum wpan_phy_flags {
	WPAN_PHY_FLAG_TXPOWER		= BIT(1),
	WPAN_PHY_FLAG_CCA_ED_LEVEL	= BIT(2),
	WPAN_PHY_FLAG_CCA_MODE		= BIT(3),
};

struct wpan_phy {
	/* If multiple wpan_phys are registered and you're handed e.g.
	 * a regular netdev with assigned ieee802154_ptr, you won't
	 * know whether it points to a wpan_phy your driver has registered
	 * or not. Assign this to something global to your driver to
	 * help determine whether you own this wpan_phy or not.
	 */
	const void *privid;

	u32 flags;

	/*
	 * This is a PIB according to 802.15.4-2011.
	 * We do not provide timing-related variables, as they
	 * aren't used outside of driver
	 */
	u8 current_channel;
	u8 current_page;
	struct wpan_phy_supported supported;
	/* current transmit_power in mBm */
	s32 transmit_power;
	struct wpan_phy_cca cca;

	__le64 perm_extended_addr;

	/* current cca ed threshold in mBm */
	s32 cca_ed_level;

	/* PHY depended MAC PIB values */

	/* 802.15.4 acronym: Tdsym in nsec */
	u32 symbol_duration;
	/* lifs and sifs periods timing */
	u16 lifs_period;
	u16 sifs_period;

	struct device dev;

	/* the network namespace this phy lives in currently */
	possible_net_t _net;

	char priv[] __aligned(NETDEV_ALIGN);
};

static inline struct net *wpan_phy_net(struct wpan_phy *wpan_phy)
{
	return read_pnet(&wpan_phy->_net);
}

static inline void wpan_phy_net_set(struct wpan_phy *wpan_phy, struct net *net)
{
	write_pnet(&wpan_phy->_net, net);
}

/**
 * struct ieee802154_addr - IEEE802.15.4 device address
 * @mode: Address mode from frame header. Can be one of:
 *        - @IEEE802154_ADDR_NONE
 *        - @IEEE802154_ADDR_SHORT
 *        - @IEEE802154_ADDR_LONG
 * @pan_id: The PAN ID this address belongs to
 * @short_addr: address if @mode is @IEEE802154_ADDR_SHORT
 * @extended_addr: address if @mode is @IEEE802154_ADDR_LONG
 */
struct ieee802154_addr {
	u8 mode;
	__le16 pan_id;
	union {
		__le16 short_addr;
		__le64 extended_addr;
	};
};

struct ieee802154_llsec_key_id {
	u8 mode;
	u8 id;
	union {
		struct ieee802154_addr device_addr;
		__le32 short_source;
		__le64 extended_source;
	};
};

#define IEEE802154_LLSEC_KEY_SIZE 16

struct ieee802154_llsec_key {
	u8 frame_types;
	u32 cmd_frame_ids;
	/* TODO replace with NL802154_KEY_SIZE */
	u8 key[IEEE802154_LLSEC_KEY_SIZE];
};

struct ieee802154_llsec_key_entry {
	struct list_head list;

	struct ieee802154_llsec_key_id id;
	struct ieee802154_llsec_key *key;
};

struct ieee802154_llsec_params {
	bool enabled;

	__be32 frame_counter;
	u8 out_level;
	struct ieee802154_llsec_key_id out_key;

	__le64 default_key_source;

	__le16 pan_id;
	__le64 hwaddr;
	__le64 coord_hwaddr;
	__le16 coord_shortaddr;
};

struct ieee802154_llsec_table {
	struct list_head keys;
	struct list_head devices;
	struct list_head security_levels;
};

struct ieee802154_llsec_seclevel {
	struct list_head list;

	u8 frame_type;
	u8 cmd_frame_id;
	bool device_override;
	u32 sec_levels;
};

struct ieee802154_llsec_device {
	struct list_head list;

	__le16 pan_id;
	__le16 short_addr;
	__le64 hwaddr;
	u32 frame_counter;
	bool seclevel_exempt;

	u8 key_mode;
	struct list_head keys;
};

struct ieee802154_llsec_device_key {
	struct list_head list;

	struct ieee802154_llsec_key_id key_id;
	u32 frame_counter;
};

struct wpan_dev_header_ops {
	/* TODO create callback currently assumes ieee802154_mac_cb inside
	 * skb->cb. This should be changed to give these information as
	 * parameter.
	 */
	int	(*create)(struct sk_buff *skb, struct net_device *dev,
			  const struct ieee802154_addr *daddr,
			  const struct ieee802154_addr *saddr,
			  unsigned int len);
};

struct wpan_dev {
	struct wpan_phy *wpan_phy;
	int iftype;

	/* the remainder of this struct should be private to cfg802154 */
	struct list_head list;
	struct net_device *netdev;

	const struct wpan_dev_header_ops *header_ops;

	/* lowpan interface, set when the wpan_dev belongs to one lowpan_dev */
	struct net_device *lowpan_dev;

	u32 identifier;

	/* MAC PIB */
	__le16 pan_id;
	__le16 short_addr;
	__le64 extended_addr;

	/* MAC BSN field */
	atomic_t bsn;
	/* MAC DSN field */
	atomic_t dsn;

	u8 min_be;
	u8 max_be;
	u8 csma_retries;
	s8 frame_retries;

	bool lbt;

	bool promiscuous_mode;

	/* fallback for acknowledgment bit setting */
	bool ackreq;
};

#define to_phy(_dev)	container_of(_dev, struct wpan_phy, dev)

static inline int
wpan_dev_hard_header(struct sk_buff *skb, struct net_device *dev,
		     const struct ieee802154_addr *daddr,
		     const struct ieee802154_addr *saddr,
		     unsigned int len)
{
	struct wpan_dev *wpan_dev = dev->ieee802154_ptr;

	return wpan_dev->header_ops->create(skb, dev, daddr, saddr, len);
}

struct wpan_phy *
wpan_phy_new(const struct cfg802154_ops *ops, size_t priv_size);
static inline void wpan_phy_set_dev(struct wpan_phy *phy, struct device *dev)
{
	phy->dev.parent = dev;
}

int wpan_phy_register(struct wpan_phy *phy);
void wpan_phy_unregister(struct wpan_phy *phy);
void wpan_phy_free(struct wpan_phy *phy);
/* Same semantics as for class_for_each_device */
int wpan_phy_for_each(int (*fn)(struct wpan_phy *phy, void *data), void *data);

static inline void *wpan_phy_priv(struct wpan_phy *phy)
{
	BUG_ON(!phy);
	return &phy->priv;
}

struct wpan_phy *wpan_phy_find(const char *str);

static inline void wpan_phy_put(struct wpan_phy *phy)
{
	put_device(&phy->dev);
}

static inline const char *wpan_phy_name(struct wpan_phy *phy)
{
	return dev_name(&phy->dev);
}

void ieee802154_configure_durations(struct wpan_phy *phy);

#endif /* __NET_CFG802154_H */
