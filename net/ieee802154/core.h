#ifndef __IEEE802154_CORE_H
#define __IEEE802154_CORE_H

#include <net/cfg802154.h>

struct cfg802154_registered_device {
	const struct cfg802154_ops *ops;

	/* must be last because of the way we do wpan_phy_priv(),
	 * and it should at least be aligned to NETDEV_ALIGN
	 */
	struct wpan_phy wpan_phy __aligned(NETDEV_ALIGN);
};

static inline struct cfg802154_registered_device *
wpan_phy_to_rdev(struct wpan_phy *wpan_phy)
{
	BUG_ON(!wpan_phy);
	return container_of(wpan_phy, struct cfg802154_registered_device,
			    wpan_phy);
}

/* free object */
void cfg802154_dev_free(struct cfg802154_registered_device *rdev);

#endif /* __IEEE802154_CORE_H */
