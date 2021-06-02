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

struct xpcs_id;

struct mdio_xpcs_args {
	struct mii_bus *bus;
	const struct xpcs_id *id;
	int addr;
};

struct mdio_xpcs_ops {
	int (*config)(struct mdio_xpcs_args *xpcs,
		      const struct phylink_link_state *state);
	int (*get_state)(struct mdio_xpcs_args *xpcs,
			 struct phylink_link_state *state);
	int (*link_up)(struct mdio_xpcs_args *xpcs, int speed,
		       phy_interface_t interface);
};

int xpcs_get_an_mode(struct mdio_xpcs_args *xpcs, phy_interface_t interface);
struct mdio_xpcs_ops *mdio_xpcs_get_ops(void);
void xpcs_validate(struct mdio_xpcs_args *xpcs, unsigned long *supported,
		   struct phylink_link_state *state);
int xpcs_config_eee(struct mdio_xpcs_args *xpcs, int mult_fact_100ns,
		    int enable);
int xpcs_probe(struct mdio_xpcs_args *xpcs, phy_interface_t interface);

#endif /* __LINUX_PCS_XPCS_H */
