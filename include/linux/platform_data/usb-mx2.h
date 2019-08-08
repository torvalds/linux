/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *	Copyright (C) 2009 Martin Fuzzey <mfuzzey@gmail.com>
 */

#ifndef __ASM_ARCH_MX21_USBH
#define __ASM_ARCH_MX21_USBH

enum mx21_usbh_xcvr {
	/* Values below as used by hardware (HWMODE register) */
	MX21_USBXCVR_TXDIF_RXDIF = 0,
	MX21_USBXCVR_TXDIF_RXSE = 1,
	MX21_USBXCVR_TXSE_RXDIF = 2,
	MX21_USBXCVR_TXSE_RXSE = 3,
};

struct mx21_usbh_platform_data {
	enum mx21_usbh_xcvr host_xcvr; /* tranceiver mode host 1,2 ports */
	enum mx21_usbh_xcvr otg_xcvr; /* tranceiver mode otg (as host) port */
	u16 	enable_host1:1,
		enable_host2:1,
		enable_otg_host:1, /* enable "OTG" port (as host) */
		host1_xcverless:1, /* traceiverless host1 port */
		host1_txenoe:1, /* output enable host1 transmit enable */
		otg_ext_xcvr:1, /* external tranceiver for OTG port */
		unused:10;
};

#endif /* __ASM_ARCH_MX21_USBH */
