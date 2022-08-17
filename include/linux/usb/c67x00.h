// SPDX-License-Identifier: GPL-2.0+
/*
 * usb_c67x00.h: platform definitions for the Cypress C67X00 USB chip
 *
 * Copyright (C) 2006-2008 Barco N.V.
 */

#ifndef _LINUX_USB_C67X00_H
#define _LINUX_USB_C67X00_H

/* SIE configuration */
#define C67X00_SIE_UNUSED	0
#define C67X00_SIE_HOST		1
#define C67X00_SIE_PERIPHERAL_A	2	/* peripheral on A port */
#define C67X00_SIE_PERIPHERAL_B	3	/* peripheral on B port */

#define c67x00_sie_config(config, n)  (((config)>>(4*(n)))&0x3)

#define C67X00_SIE1_UNUSED	        (C67X00_SIE_UNUSED		<< 0)
#define C67X00_SIE1_HOST	        (C67X00_SIE_HOST		<< 0)
#define C67X00_SIE1_PERIPHERAL_A	(C67X00_SIE_PERIPHERAL_A	<< 0)
#define C67X00_SIE1_PERIPHERAL_B	(C67X00_SIE_PERIPHERAL_B	<< 0)

#define C67X00_SIE2_UNUSED		(C67X00_SIE_UNUSED		<< 4)
#define C67X00_SIE2_HOST		(C67X00_SIE_HOST		<< 4)
#define C67X00_SIE2_PERIPHERAL_A	(C67X00_SIE_PERIPHERAL_A	<< 4)
#define C67X00_SIE2_PERIPHERAL_B	(C67X00_SIE_PERIPHERAL_B	<< 4)

struct c67x00_platform_data {
	int sie_config;			/* SIEs config (C67X00_SIEx_*) */
	unsigned long hpi_regstep;	/* Step between HPI registers  */
};

#endif /* _LINUX_USB_C67X00_H */
