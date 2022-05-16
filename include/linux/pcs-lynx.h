/* SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause) */
/* Copyright 2020 NXP
 * Lynx PCS helpers
 */

#ifndef __LINUX_PCS_LYNX_H
#define __LINUX_PCS_LYNX_H

#include <linux/mdio.h>
#include <linux/phylink.h>

struct lynx_pcs {
	struct phylink_pcs pcs;
	struct mdio_device *mdio;
};

struct lynx_pcs *lynx_pcs_create(struct mdio_device *mdio);

void lynx_pcs_destroy(struct lynx_pcs *pcs);

#endif /* __LINUX_PCS_LYNX_H */
