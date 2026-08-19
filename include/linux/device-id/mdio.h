/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_DEVICE_ID_MDIO_H
#define LINUX_DEVICE_ID_MDIO_H

#ifdef __KERNEL__
#include <linux/types.h>
#endif

#define MDIO_MODULE_PREFIX	"mdio:"

#define MDIO_ID_FMT "%u%u%u%u%u%u%u%u%u%u%u%u%u%u%u%u%u%u%u%u%u%u%u%u%u%u%u%u%u%u%u%u"
#define MDIO_ID_ARGS(_id) \
	((_id)>>31) & 1, ((_id)>>30) & 1, ((_id)>>29) & 1, ((_id)>>28) & 1, \
	((_id)>>27) & 1, ((_id)>>26) & 1, ((_id)>>25) & 1, ((_id)>>24) & 1, \
	((_id)>>23) & 1, ((_id)>>22) & 1, ((_id)>>21) & 1, ((_id)>>20) & 1, \
	((_id)>>19) & 1, ((_id)>>18) & 1, ((_id)>>17) & 1, ((_id)>>16) & 1, \
	((_id)>>15) & 1, ((_id)>>14) & 1, ((_id)>>13) & 1, ((_id)>>12) & 1, \
	((_id)>>11) & 1, ((_id)>>10) & 1, ((_id)>>9) & 1, ((_id)>>8) & 1, \
	((_id)>>7) & 1, ((_id)>>6) & 1, ((_id)>>5) & 1, ((_id)>>4) & 1, \
	((_id)>>3) & 1, ((_id)>>2) & 1, ((_id)>>1) & 1, (_id) & 1

/**
 * struct mdio_device_id - identifies PHY devices on an MDIO/MII bus
 * @phy_id: The result of
 *     (mdio_read(&MII_PHYSID1) << 16 | mdio_read(&MII_PHYSID2)) & @phy_id_mask
 *     for this PHY type
 * @phy_id_mask: Defines the significant bits of @phy_id.  A value of 0
 *     is used to terminate an array of struct mdio_device_id.
 */
struct mdio_device_id {
	__u32 phy_id;
	__u32 phy_id_mask;
};

#endif /* ifndef LINUX_DEVICE_ID_MDIO_H */
