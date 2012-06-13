#ifndef _MICREL_PHY_H
#define _MICREL_PHY_H

#define MICREL_PHY_ID_MASK	0x00fffff0

#define PHY_ID_KSZ9021		0x00221610
#define PHY_ID_KS8737		0x00221720
#define PHY_ID_KS8041		0x00221510
#define PHY_ID_KS8051		0x00221550
/* both for ks8001 Rev. A/B, and for ks8721 Rev 3. */
#define PHY_ID_KS8001		0x0022161A

/* struct phy_device dev_flags definitions */
#define MICREL_PHY_50MHZ_CLK	0x00000001

#endif /* _MICREL_PHY_H */
