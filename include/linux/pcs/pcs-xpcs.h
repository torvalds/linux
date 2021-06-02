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
	struct mdio_device *mdiodev;
	const struct xpcs_id *id;
	struct phylink_pcs pcs;
};

int xpcs_get_an_mode(struct mdio_xpcs_args *xpcs, phy_interface_t interface);
void xpcs_validate(struct mdio_xpcs_args *xpcs, unsigned long *supported,
		   struct phylink_link_state *state);
int xpcs_config_eee(struct mdio_xpcs_args *xpcs, int mult_fact_100ns,
		    int enable);
struct mdio_xpcs_args *xpcs_create(struct mdio_device *mdiodev,
				   phy_interface_t interface);
void xpcs_destroy(struct mdio_xpcs_args *xpcs);

#endif /* __LINUX_PCS_XPCS_H */
