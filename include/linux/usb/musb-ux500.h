// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2013 ST-Ericsson AB
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __MUSB_UX500_H__
#define __MUSB_UX500_H__

enum ux500_musb_vbus_id_status {
	UX500_MUSB_NONE = 0,
	UX500_MUSB_VBUS,
	UX500_MUSB_ID,
	UX500_MUSB_CHARGER,
	UX500_MUSB_ENUMERATED,
	UX500_MUSB_RIDA,
	UX500_MUSB_RIDB,
	UX500_MUSB_RIDC,
	UX500_MUSB_PREPARE,
	UX500_MUSB_CLEAN,
};

#endif	/* __MUSB_UX500_H__ */
