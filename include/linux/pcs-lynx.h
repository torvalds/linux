/* SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause) */
/* Copyright 2020 NXP
 * Lynx PCS helpers
 */

#ifndef __LINUX_PCS_LYNX_H
#define __LINUX_PCS_LYNX_H

#include <linux/mdio.h>
#include <linux/phylink.h>

struct phylink_pcs *lynx_pcs_create_mdiodev(struct mii_bus *bus, int addr);
struct phylink_pcs *lynx_pcs_create_fwnode(struct fwnode_handle *node);

void lynx_pcs_destroy(struct phylink_pcs *pcs);

#endif /* __LINUX_PCS_LYNX_H */
