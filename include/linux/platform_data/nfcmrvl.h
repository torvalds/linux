/*
 * Copyright (C) 2015, Marvell International Ltd.
 *
 * This software file (the "File") is distributed by Marvell International
 * Ltd. under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available on the worldwide web at
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 */

#ifndef _NFCMRVL_PTF_H_
#define _NFCMRVL_PTF_H_

struct nfcmrvl_platform_data {
	/*
	 * Generic
	 */

	/* GPIO that is wired to RESET_N signal */
	int reset_n_io;
	/* Tell if transport is muxed in HCI one */
	unsigned int hci_muxed;

	/*
	 * UART specific
	 */

	/* Tell if UART needs flow control at init */
	unsigned int flow_control;
	/* Tell if firmware supports break control for power management */
	unsigned int break_control;


	/*
	 * I2C specific
	 */

	unsigned int irq;
	unsigned int irq_polarity;
};

#endif /* _NFCMRVL_PTF_H_ */
