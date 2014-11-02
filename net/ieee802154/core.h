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

/* free object */
void cfg802154_dev_free(struct cfg802154_registered_device *rdev);

#endif /* __IEEE802154_CORE_H */
