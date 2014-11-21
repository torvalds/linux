#ifndef __CFG802154_RDEV_OPS
#define __CFG802154_RDEV_OPS

#include <net/cfg802154.h>

#include "core.h"

static inline struct net_device *
rdev_add_virtual_intf_deprecated(struct cfg802154_registered_device *rdev,
				 const char *name, int type)
{
	return rdev->ops->add_virtual_intf_deprecated(&rdev->wpan_phy, name,
						      type);
}

static inline void
rdev_del_virtual_intf_deprecated(struct cfg802154_registered_device *rdev,
				 struct net_device *dev)
{
	rdev->ops->del_virtual_intf_deprecated(&rdev->wpan_phy, dev);
}

static inline int
rdev_add_virtual_intf(struct cfg802154_registered_device *rdev, char *name,
		      enum nl802154_iftype type, __le64 extended_addr)
{
	return rdev->ops->add_virtual_intf(&rdev->wpan_phy, name, type,
					   extended_addr);
}

static inline int
rdev_del_virtual_intf(struct cfg802154_registered_device *rdev,
		      struct wpan_dev *wpan_dev)
{
	return rdev->ops->del_virtual_intf(&rdev->wpan_phy, wpan_dev);
}

static inline int
rdev_set_channel(struct cfg802154_registered_device *rdev, u8 page, u8 channel)
{
	return rdev->ops->set_channel(&rdev->wpan_phy, page, channel);
}

static inline int
rdev_set_pan_id(struct cfg802154_registered_device *rdev,
		struct wpan_dev *wpan_dev, __le16 pan_id)
{
	return rdev->ops->set_pan_id(&rdev->wpan_phy, wpan_dev, pan_id);
}

static inline int
rdev_set_short_addr(struct cfg802154_registered_device *rdev,
		    struct wpan_dev *wpan_dev, __le16 short_addr)
{
	return rdev->ops->set_short_addr(&rdev->wpan_phy, wpan_dev, short_addr);
}

static inline int
rdev_set_backoff_exponent(struct cfg802154_registered_device *rdev,
			  struct wpan_dev *wpan_dev, u8 min_be, u8 max_be)
{
	return rdev->ops->set_backoff_exponent(&rdev->wpan_phy, wpan_dev,
					       min_be, max_be);
}

static inline int
rdev_set_max_csma_backoffs(struct cfg802154_registered_device *rdev,
			   struct wpan_dev *wpan_dev, u8 max_csma_backoffs)
{
	return rdev->ops->set_max_csma_backoffs(&rdev->wpan_phy, wpan_dev,
						max_csma_backoffs);
}

static inline int
rdev_set_max_frame_retries(struct cfg802154_registered_device *rdev,
			   struct wpan_dev *wpan_dev, s8 max_frame_retries)
{
	return rdev->ops->set_max_frame_retries(&rdev->wpan_phy, wpan_dev,
						max_frame_retries);
}

static inline int
rdev_set_lbt_mode(struct cfg802154_registered_device *rdev,
		  struct wpan_dev *wpan_dev, bool mode)
{
	return rdev->ops->set_lbt_mode(&rdev->wpan_phy, wpan_dev, mode);
}

#endif /* __CFG802154_RDEV_OPS */
