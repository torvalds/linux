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

#endif /* __CFG802154_RDEV_OPS */
