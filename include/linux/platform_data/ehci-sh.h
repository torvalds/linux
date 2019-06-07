/* SPDX-License-Identifier: GPL-2.0
 *
 * EHCI SuperH driver platform data
 *
 * Copyright (C) 2012  Nobuhiro Iwamatsu <nobuhiro.iwamatsu.yj@renesas.com>
 * Copyright (C) 2012  Renesas Solutions Corp.
 */

#ifndef __USB_EHCI_SH_H
#define __USB_EHCI_SH_H

struct ehci_sh_platdata {
	void (*phy_init)(void); /* Phy init function */
};

#endif /* __USB_EHCI_SH_H */
