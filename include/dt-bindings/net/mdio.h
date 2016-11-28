/*
 * This header provides generic constants for ethernet MDIO bindings
 */

#ifndef _DT_BINDINGS_NET_MDIO_H
#define _DT_BINDINGS_NET_MDIO_H

/*
 * EEE capability Advertisement
 */

#define MDIO_EEE_100TX		0x0002	/* 100TX EEE cap */
#define MDIO_EEE_1000T		0x0004	/* 1000T EEE cap */
#define MDIO_EEE_10GT		0x0008	/* 10GT EEE cap */
#define MDIO_EEE_1000KX		0x0010	/* 1000KX EEE cap */
#define MDIO_EEE_10GKX4		0x0020	/* 10G KX4 EEE cap */
#define MDIO_EEE_10GKR		0x0040	/* 10G KR EEE cap */

#endif
