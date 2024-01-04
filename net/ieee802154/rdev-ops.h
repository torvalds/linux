/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __CFG802154_RDEV_OPS
#define __CFG802154_RDEV_OPS

#include <net/cfg802154.h>

#include "core.h"
#include "trace.h"

static inline struct net_device *
rdev_add_virtual_intf_deprecated(struct cfg802154_registered_device *rdev,
				 const char *name,
				 unsigned char name_assign_type,
				 int type)
{
	return rdev->ops->add_virtual_intf_deprecated(&rdev->wpan_phy, name,
						      name_assign_type, type);
}

static inline void
rdev_del_virtual_intf_deprecated(struct cfg802154_registered_device *rdev,
				 struct net_device *dev)
{
	rdev->ops->del_virtual_intf_deprecated(&rdev->wpan_phy, dev);
}

static inline int
rdev_suspend(struct cfg802154_registered_device *rdev)
{
	int ret;
	trace_802154_rdev_suspend(&rdev->wpan_phy);
	ret = rdev->ops->suspend(&rdev->wpan_phy);
	trace_802154_rdev_return_int(&rdev->wpan_phy, ret);
	return ret;
}

static inline int
rdev_resume(struct cfg802154_registered_device *rdev)
{
	int ret;
	trace_802154_rdev_resume(&rdev->wpan_phy);
	ret = rdev->ops->resume(&rdev->wpan_phy);
	trace_802154_rdev_return_int(&rdev->wpan_phy, ret);
	return ret;
}

static inline int
rdev_add_virtual_intf(struct cfg802154_registered_device *rdev, char *name,
		      unsigned char name_assign_type,
		      enum nl802154_iftype type, __le64 extended_addr)
{
	int ret;

	trace_802154_rdev_add_virtual_intf(&rdev->wpan_phy, name, type,
					   extended_addr);
	ret = rdev->ops->add_virtual_intf(&rdev->wpan_phy, name,
					  name_assign_type, type,
					  extended_addr);
	trace_802154_rdev_return_int(&rdev->wpan_phy, ret);
	return ret;
}

static inline int
rdev_del_virtual_intf(struct cfg802154_registered_device *rdev,
		      struct wpan_dev *wpan_dev)
{
	int ret;

	trace_802154_rdev_del_virtual_intf(&rdev->wpan_phy, wpan_dev);
	ret = rdev->ops->del_virtual_intf(&rdev->wpan_phy, wpan_dev);
	trace_802154_rdev_return_int(&rdev->wpan_phy, ret);
	return ret;
}

static inline int
rdev_set_channel(struct cfg802154_registered_device *rdev, u8 page, u8 channel)
{
	int ret;

	trace_802154_rdev_set_channel(&rdev->wpan_phy, page, channel);
	ret = rdev->ops->set_channel(&rdev->wpan_phy, page, channel);
	trace_802154_rdev_return_int(&rdev->wpan_phy, ret);
	return ret;
}

static inline int
rdev_set_cca_mode(struct cfg802154_registered_device *rdev,
		  const struct wpan_phy_cca *cca)
{
	int ret;

	trace_802154_rdev_set_cca_mode(&rdev->wpan_phy, cca);
	ret = rdev->ops->set_cca_mode(&rdev->wpan_phy, cca);
	trace_802154_rdev_return_int(&rdev->wpan_phy, ret);
	return ret;
}

static inline int
rdev_set_cca_ed_level(struct cfg802154_registered_device *rdev, s32 ed_level)
{
	int ret;

	trace_802154_rdev_set_cca_ed_level(&rdev->wpan_phy, ed_level);
	ret = rdev->ops->set_cca_ed_level(&rdev->wpan_phy, ed_level);
	trace_802154_rdev_return_int(&rdev->wpan_phy, ret);
	return ret;
}

static inline int
rdev_set_tx_power(struct cfg802154_registered_device *rdev,
		  s32 power)
{
	int ret;

	trace_802154_rdev_set_tx_power(&rdev->wpan_phy, power);
	ret = rdev->ops->set_tx_power(&rdev->wpan_phy, power);
	trace_802154_rdev_return_int(&rdev->wpan_phy, ret);
	return ret;
}

static inline int
rdev_set_pan_id(struct cfg802154_registered_device *rdev,
		struct wpan_dev *wpan_dev, __le16 pan_id)
{
	int ret;

	trace_802154_rdev_set_pan_id(&rdev->wpan_phy, wpan_dev, pan_id);
	ret = rdev->ops->set_pan_id(&rdev->wpan_phy, wpan_dev, pan_id);
	trace_802154_rdev_return_int(&rdev->wpan_phy, ret);
	return ret;
}

static inline int
rdev_set_short_addr(struct cfg802154_registered_device *rdev,
		    struct wpan_dev *wpan_dev, __le16 short_addr)
{
	int ret;

	trace_802154_rdev_set_short_addr(&rdev->wpan_phy, wpan_dev, short_addr);
	ret = rdev->ops->set_short_addr(&rdev->wpan_phy, wpan_dev, short_addr);
	trace_802154_rdev_return_int(&rdev->wpan_phy, ret);
	return ret;
}

static inline int
rdev_set_backoff_exponent(struct cfg802154_registered_device *rdev,
			  struct wpan_dev *wpan_dev, u8 min_be, u8 max_be)
{
	int ret;

	trace_802154_rdev_set_backoff_exponent(&rdev->wpan_phy, wpan_dev,
					       min_be, max_be);
	ret = rdev->ops->set_backoff_exponent(&rdev->wpan_phy, wpan_dev,
					      min_be, max_be);
	trace_802154_rdev_return_int(&rdev->wpan_phy, ret);
	return ret;
}

static inline int
rdev_set_max_csma_backoffs(struct cfg802154_registered_device *rdev,
			   struct wpan_dev *wpan_dev, u8 max_csma_backoffs)
{
	int ret;

	trace_802154_rdev_set_csma_backoffs(&rdev->wpan_phy, wpan_dev,
					    max_csma_backoffs);
	ret = rdev->ops->set_max_csma_backoffs(&rdev->wpan_phy, wpan_dev,
					       max_csma_backoffs);
	trace_802154_rdev_return_int(&rdev->wpan_phy, ret);
	return ret;
}

static inline int
rdev_set_max_frame_retries(struct cfg802154_registered_device *rdev,
			   struct wpan_dev *wpan_dev, s8 max_frame_retries)
{
	int ret;

	trace_802154_rdev_set_max_frame_retries(&rdev->wpan_phy, wpan_dev,
						max_frame_retries);
	ret = rdev->ops->set_max_frame_retries(&rdev->wpan_phy, wpan_dev,
					       max_frame_retries);
	trace_802154_rdev_return_int(&rdev->wpan_phy, ret);
	return ret;
}

static inline int
rdev_set_lbt_mode(struct cfg802154_registered_device *rdev,
		  struct wpan_dev *wpan_dev, bool mode)
{
	int ret;

	trace_802154_rdev_set_lbt_mode(&rdev->wpan_phy, wpan_dev, mode);
	ret = rdev->ops->set_lbt_mode(&rdev->wpan_phy, wpan_dev, mode);
	trace_802154_rdev_return_int(&rdev->wpan_phy, ret);
	return ret;
}

static inline int
rdev_set_ackreq_default(struct cfg802154_registered_device *rdev,
			struct wpan_dev *wpan_dev, bool ackreq)
{
	int ret;

	trace_802154_rdev_set_ackreq_default(&rdev->wpan_phy, wpan_dev,
					     ackreq);
	ret = rdev->ops->set_ackreq_default(&rdev->wpan_phy, wpan_dev, ackreq);
	trace_802154_rdev_return_int(&rdev->wpan_phy, ret);
	return ret;
}

static inline int rdev_trigger_scan(struct cfg802154_registered_device *rdev,
				    struct cfg802154_scan_request *request)
{
	int ret;

	if (!rdev->ops->trigger_scan)
		return -EOPNOTSUPP;

	trace_802154_rdev_trigger_scan(&rdev->wpan_phy, request);
	ret = rdev->ops->trigger_scan(&rdev->wpan_phy, request);
	trace_802154_rdev_return_int(&rdev->wpan_phy, ret);
	return ret;
}

static inline int rdev_abort_scan(struct cfg802154_registered_device *rdev,
				  struct wpan_dev *wpan_dev)
{
	int ret;

	if (!rdev->ops->abort_scan)
		return -EOPNOTSUPP;

	trace_802154_rdev_abort_scan(&rdev->wpan_phy, wpan_dev);
	ret = rdev->ops->abort_scan(&rdev->wpan_phy, wpan_dev);
	trace_802154_rdev_return_int(&rdev->wpan_phy, ret);
	return ret;
}

static inline int rdev_send_beacons(struct cfg802154_registered_device *rdev,
				    struct cfg802154_beacon_request *request)
{
	int ret;

	if (!rdev->ops->send_beacons)
		return -EOPNOTSUPP;

	trace_802154_rdev_send_beacons(&rdev->wpan_phy, request);
	ret = rdev->ops->send_beacons(&rdev->wpan_phy, request);
	trace_802154_rdev_return_int(&rdev->wpan_phy, ret);
	return ret;
}

static inline int rdev_stop_beacons(struct cfg802154_registered_device *rdev,
				    struct wpan_dev *wpan_dev)
{
	int ret;

	if (!rdev->ops->stop_beacons)
		return -EOPNOTSUPP;

	trace_802154_rdev_stop_beacons(&rdev->wpan_phy, wpan_dev);
	ret = rdev->ops->stop_beacons(&rdev->wpan_phy, wpan_dev);
	trace_802154_rdev_return_int(&rdev->wpan_phy, ret);
	return ret;
}

static inline int rdev_associate(struct cfg802154_registered_device *rdev,
				 struct wpan_dev *wpan_dev,
				 struct ieee802154_addr *coord)
{
	int ret;

	if (!rdev->ops->associate)
		return -EOPNOTSUPP;

	trace_802154_rdev_associate(&rdev->wpan_phy, wpan_dev, coord);
	ret = rdev->ops->associate(&rdev->wpan_phy, wpan_dev, coord);
	trace_802154_rdev_return_int(&rdev->wpan_phy, ret);
	return ret;
}

static inline int rdev_disassociate(struct cfg802154_registered_device *rdev,
				    struct wpan_dev *wpan_dev,
				    struct ieee802154_addr *target)
{
	int ret;

	if (!rdev->ops->disassociate)
		return -EOPNOTSUPP;

	trace_802154_rdev_disassociate(&rdev->wpan_phy, wpan_dev, target);
	ret = rdev->ops->disassociate(&rdev->wpan_phy, wpan_dev, target);
	trace_802154_rdev_return_int(&rdev->wpan_phy, ret);
	return ret;
}

#ifdef CONFIG_IEEE802154_NL802154_EXPERIMENTAL
/* TODO this is already a nl802154, so move into ieee802154 */
static inline void
rdev_get_llsec_table(struct cfg802154_registered_device *rdev,
		     struct wpan_dev *wpan_dev,
		     struct ieee802154_llsec_table **table)
{
	rdev->ops->get_llsec_table(&rdev->wpan_phy, wpan_dev, table);
}

static inline void
rdev_lock_llsec_table(struct cfg802154_registered_device *rdev,
		      struct wpan_dev *wpan_dev)
{
	rdev->ops->lock_llsec_table(&rdev->wpan_phy, wpan_dev);
}

static inline void
rdev_unlock_llsec_table(struct cfg802154_registered_device *rdev,
			struct wpan_dev *wpan_dev)
{
	rdev->ops->unlock_llsec_table(&rdev->wpan_phy, wpan_dev);
}

static inline int
rdev_get_llsec_params(struct cfg802154_registered_device *rdev,
		      struct wpan_dev *wpan_dev,
		      struct ieee802154_llsec_params *params)
{
	return rdev->ops->get_llsec_params(&rdev->wpan_phy, wpan_dev, params);
}

static inline int
rdev_set_llsec_params(struct cfg802154_registered_device *rdev,
		      struct wpan_dev *wpan_dev,
		      const struct ieee802154_llsec_params *params,
		      u32 changed)
{
	return rdev->ops->set_llsec_params(&rdev->wpan_phy, wpan_dev, params,
					   changed);
}

static inline int
rdev_add_llsec_key(struct cfg802154_registered_device *rdev,
		   struct wpan_dev *wpan_dev,
		   const struct ieee802154_llsec_key_id *id,
		   const struct ieee802154_llsec_key *key)
{
	return rdev->ops->add_llsec_key(&rdev->wpan_phy, wpan_dev, id, key);
}

static inline int
rdev_del_llsec_key(struct cfg802154_registered_device *rdev,
		   struct wpan_dev *wpan_dev,
		   const struct ieee802154_llsec_key_id *id)
{
	return rdev->ops->del_llsec_key(&rdev->wpan_phy, wpan_dev, id);
}

static inline int
rdev_add_seclevel(struct cfg802154_registered_device *rdev,
		  struct wpan_dev *wpan_dev,
		  const struct ieee802154_llsec_seclevel *sl)
{
	return rdev->ops->add_seclevel(&rdev->wpan_phy, wpan_dev, sl);
}

static inline int
rdev_del_seclevel(struct cfg802154_registered_device *rdev,
		  struct wpan_dev *wpan_dev,
		  const struct ieee802154_llsec_seclevel *sl)
{
	return rdev->ops->del_seclevel(&rdev->wpan_phy, wpan_dev, sl);
}

static inline int
rdev_add_device(struct cfg802154_registered_device *rdev,
		struct wpan_dev *wpan_dev,
		const struct ieee802154_llsec_device *dev_desc)
{
	return rdev->ops->add_device(&rdev->wpan_phy, wpan_dev, dev_desc);
}

static inline int
rdev_del_device(struct cfg802154_registered_device *rdev,
		struct wpan_dev *wpan_dev, __le64 extended_addr)
{
	return rdev->ops->del_device(&rdev->wpan_phy, wpan_dev, extended_addr);
}

static inline int
rdev_add_devkey(struct cfg802154_registered_device *rdev,
		struct wpan_dev *wpan_dev, __le64 extended_addr,
		const struct ieee802154_llsec_device_key *devkey)
{
	return rdev->ops->add_devkey(&rdev->wpan_phy, wpan_dev, extended_addr,
				     devkey);
}

static inline int
rdev_del_devkey(struct cfg802154_registered_device *rdev,
		struct wpan_dev *wpan_dev, __le64 extended_addr,
		const struct ieee802154_llsec_device_key *devkey)
{
	return rdev->ops->del_devkey(&rdev->wpan_phy, wpan_dev, extended_addr,
				     devkey);
}
#endif /* CONFIG_IEEE802154_NL802154_EXPERIMENTAL */

#endif /* __CFG802154_RDEV_OPS */
