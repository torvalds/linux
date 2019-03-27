/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2015-2016 Landon Fuller <landonf@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * $FreeBSD$
 */

#ifndef _IF_BWN_PCIVAR_H_
#define _IF_BWN_PCIVAR_H_

struct bwn_pci_devcfg;

/** bwn_pci per-instance state. */
struct bwn_pci_softc {
	device_t			 dev;		/**< device */
	device_t			 bhndb_dev;	/**< bhnd bridge device */
	const struct bwn_pci_devcfg	*devcfg;	/**< bwn device config */
	uint32_t			 quirks;	/**< quirk flags */
};

/* bwn device quirks */
enum {
	/** No quirks */
	BWN_QUIRK_NONE			= 0,

	/**
	 * This model/revision has not been tested and may not work.
	 */
	BWN_QUIRK_UNTESTED		= 1<<0,

	/**
	 * Early dual-band devices did not support accessing multiple PHYs
	 * from a single WLAN core, and instead used separate 2GHz and 5GHz
	 * WLAN cores.
	 * 
	 * However, not all cards with two WLAN cores are fully populated;
	 * we must whitelist the boards on which a second WLAN core is actually
	 * usable.
	 */
	BWN_QUIRK_WLAN_DUALCORE		= 1<<1,

	/**
	 * Some early devices shipped with unconnected ethernet cores; set
	 * this quirk to treat these cores as unpopulated.
	 */
	BWN_QUIRK_ENET_HW_UNPOPULATED	= 1<<2,

	/**
	 * Some PCI/PCIe "Intensi-fi" chipsets shipped with floating USB
	 * host controller cores; set this quirk to treat these cores as
	 * unpopulated.
	 */
	BWN_QUIRK_USBH_UNPOPULATED	= 1<<3,

	/**
	 * Some early devices (including all BCM4306 chipsets) shipped with
	 * floating analog softmodem codec cores; set this quirk to treat these
	 * cores as unpopulated.
	 */
	BWN_QUIRK_SOFTMODEM_UNPOPULATED	= 1<<4,
};

/* PCI device descriptor */
struct bwn_pci_device {
	uint16_t	vendor;
	uint16_t	device;
	const char	*desc;
	uint32_t	quirks;
};


#define	BWN_BCM_DEV(_devid, _desc, _quirks)		\
    { PCI_VENDOR_BROADCOM, PCI_DEVID_ ## _devid,	\
        "Broadcom " _desc " Wireless", _quirks }

/* Supported device table */
struct bwn_pci_devcfg {
	const struct bhndb_hwcfg	*bridge_hwcfg;
	const struct bhndb_hw		*bridge_hwtable;
	const struct bhndb_hw_priority	*bridge_hwprio;
	const struct bwn_pci_device	*devices;
};

#endif /* _IF_BWN_PCIVAR_H_ */
