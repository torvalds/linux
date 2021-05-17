/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2020 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare XPCS helpers
 */

#ifndef __LINUX_PCS_XPCS_H
#define __LINUX_PCS_XPCS_H

#include <linux/phy.h>
#include <linux/phylink.h>

/* AN mode */
#define DW_AN_C73			1
#define DW_AN_C37_SGMII			2

struct mdio_xpcs_args {
	__ETHTOOL_DECLARE_LINK_MODE_MASK(supported);
	struct mii_bus *bus;
	int addr;
	int an_mode;
};

struct mdio_xpcs_ops {
	int (*validate)(struct mdio_xpcs_args *xpcs,
			unsigned long *supported,
			struct phylink_link_state *state);
	int (*config)(struct mdio_xpcs_args *xpcs,
		      const struct phylink_link_state *state);
	int (*get_state)(struct mdio_xpcs_args *xpcs,
			 struct phylink_link_state *state);
	int (*link_up)(struct mdio_xpcs_args *xpcs, int speed,
		       phy_interface_t interface);
	int (*probe)(struct mdio_xpcs_args *xpcs, phy_interface_t interface);
	int (*config_eee)(struct mdio_xpcs_args *xpcs, int mult_fact_100ns,
			  int enable);
};

#if IS_ENABLED(CONFIG_PCS_XPCS)
struct mdio_xpcs_ops *mdio_xpcs_get_ops(void);
#else
static inline struct mdio_xpcs_ops *mdio_xpcs_get_ops(void)
{
	return NULL;
}
#endif

#endif /* __LINUX_PCS_XPCS_H */
