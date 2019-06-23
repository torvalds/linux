/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Generic platform data for the NXP NCI NFC chips.
 *
 * Copyright (C) 2014  NXP Semiconductors  All rights reserved.
 *
 * Authors: Clément Perrochaud <clement.perrochaud@nxp.com>
 */

#ifndef _NXP_NCI_H_
#define _NXP_NCI_H_

struct nxp_nci_nfc_platform_data {
	unsigned int gpio_en;
	unsigned int gpio_fw;
	unsigned int irq;
};

#endif /* _NXP_NCI_H_ */
