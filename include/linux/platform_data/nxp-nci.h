/*
 * Generic platform data for the NXP NCI NFC chips.
 *
 * Copyright (C) 2014  NXP Semiconductors  All rights reserved.
 *
 * Authors: Clément Perrochaud <clement.perrochaud@nxp.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef _NXP_NCI_H_
#define _NXP_NCI_H_

struct nxp_nci_nfc_platform_data {
	unsigned int gpio_en;
	unsigned int gpio_fw;
	unsigned int irq;
};

#endif /* _NXP_NCI_H_ */
