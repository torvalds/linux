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
rdev_set_channel(struct cfg802154_registered_device *rdev, const u8 page,
		 const u8 channel)
{
	return rdev->ops->set_channel(&rdev->wpan_phy, page, channel);
}

static inline int
rdev_set_pan_id(struct cfg802154_registered_device *rdev,
		struct wpan_dev *wpan_dev, u16 pan_id)
{
	return rdev->ops->set_pan_id(&rdev->wpan_phy, wpan_dev, pan_id);
}

static inline int
rdev_set_short_addr(struct cfg802154_registered_device *rdev,
		    struct wpan_dev *wpan_dev, u16 short_addr)
{
	return rdev->ops->set_short_addr(&rdev->wpan_phy, wpan_dev, short_addr);
}

static inline int
rdev_set_backoff_exponent(struct cfg802154_registered_device *rdev,
			  struct wpan_dev *wpan_dev, const u8 min_be,
			  const u8 max_be)
{
	return rdev->ops->set_backoff_exponent(&rdev->wpan_phy, wpan_dev,
					       min_be, max_be);
}

#endif /* __CFG802154_RDEV_OPS */
