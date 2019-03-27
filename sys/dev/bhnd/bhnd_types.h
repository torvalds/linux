/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2015-2016 Landon Fuller <landon@landonf.org>
 * Copyright (c) 2017 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by Landon Fuller
 * under sponsorship from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 * 
 * $FreeBSD$
 */

#ifndef _BHND_BHND_TYPES_H_
#define _BHND_BHND_TYPES_H_

#include <sys/types.h>

#include "nvram/bhnd_nvram.h"

/** bhnd(4) device classes. */
typedef enum {
	BHND_DEVCLASS_CC,		/**< chipcommon i/o controller */
	BHND_DEVCLASS_CC_B,		/**< chipcommon auxiliary controller */
	BHND_DEVCLASS_PMU,		/**< pmu controller */
	BHND_DEVCLASS_PCI,		/**< pci host/device bridge */
	BHND_DEVCLASS_PCIE,		/**< pcie host/device bridge */
	BHND_DEVCLASS_PCCARD,		/**< pcmcia host/device bridge */
	BHND_DEVCLASS_RAM,		/**< internal RAM/SRAM */
	BHND_DEVCLASS_MEMC,		/**< memory controller */
	BHND_DEVCLASS_ENET,		/**< 802.3 MAC/PHY */
	BHND_DEVCLASS_ENET_MAC,		/**< 802.3 MAC */
	BHND_DEVCLASS_ENET_PHY,		/**< 802.3 PHY */
	BHND_DEVCLASS_WLAN,		/**< 802.11 MAC/PHY/Radio */
	BHND_DEVCLASS_WLAN_MAC,		/**< 802.11 MAC */
	BHND_DEVCLASS_WLAN_PHY,		/**< 802.11 PHY */
	BHND_DEVCLASS_CPU,		/**< cpu core */
	BHND_DEVCLASS_SOC_ROUTER,	/**< interconnect router */
	BHND_DEVCLASS_SOC_BRIDGE,	/**< interconnect host bridge */
	BHND_DEVCLASS_EROM,		/**< bus device enumeration ROM */
	BHND_DEVCLASS_NVRAM,		/**< nvram/flash controller */
	BHND_DEVCLASS_USB_HOST,		/**< USB host controller */
	BHND_DEVCLASS_USB_DEV,		/**< USB device controller */
	BHND_DEVCLASS_USB_DUAL,		/**< USB host/device controller */
	BHND_DEVCLASS_SOFTMODEM,	/**< analog/PSTN softmodem codec */

	BHND_DEVCLASS_OTHER	= 1000,	/**< other / unknown */
	BHND_DEVCLASS_INVALID		/**< no/invalid class */
} bhnd_devclass_t;

/** bhnd(4) platform services. */
typedef enum {
	BHND_SERVICE_CHIPC,		/**< chipcommon service; implements the bhnd_chipc interface */
	BHND_SERVICE_PWRCTL,		/**< legacy pwrctl service; implements the bhnd_pwrctl interface */
	BHND_SERVICE_PMU,		/**< pmu service; implements the bhnd_pmu interface */
	BHND_SERVICE_NVRAM,		/**< nvram service; implements the bhnd_nvram interface */
	BHND_SERVICE_GPIO,		/**< gpio service; implements the standard gpio interface */

	BHND_SERVICE_ANY = 1000,	/**< match on any service type */
} bhnd_service_t;

/**
 * bhnd(4) port types.
 * 
 * Only BHND_PORT_DEVICE is guaranteed to be supported by all bhnd(4) bus
 * implementations.
 */
typedef enum {
	BHND_PORT_DEVICE	= 0,	/**< device memory */
	BHND_PORT_BRIDGE	= 1,	/**< bridge memory */
	BHND_PORT_AGENT		= 2,	/**< interconnect agent/wrapper */
} bhnd_port_type;

/**
 * bhnd(4) attachment types.
 */
typedef enum {
	BHND_ATTACH_ADAPTER	= 0,	/**< A bridged card, such as a PCI WiFi chipset  */
	BHND_ATTACH_NATIVE	= 1	/**< A bus resident on the native host, such as
					  *  the primary or secondary bus of an embedded
					  *  SoC */
} bhnd_attach_type;

/**
 * bhnd(4) clock types.
 */
typedef enum {
	/**
	 * Dynamically select an appropriate clock source based on all
	 * outstanding clock requests.
	 */
	BHND_CLOCK_DYN		= (1 << 0),

	/**
	 * Idle Low-Power (ILP).
	 * 
	 * No register access is required, or long request latency is
	 * acceptable.
	 */
	BHND_CLOCK_ILP		= (1 << 1),
	
	/**
	 * Active Low-Power (ALP).
	 * 
	 * Low-latency register access and low-rate DMA.
	 */
	BHND_CLOCK_ALP		= (1 << 2),
	
	/**
	 * High Throughput (HT).
	 * 
	 * High bus throughput and lowest-latency register access.
	 */
	BHND_CLOCK_HT		= (1 << 3)
} bhnd_clock;

/**
 * Given two clock types, return the type with the highest precedence. 
 */
static inline bhnd_clock
bhnd_clock_max(bhnd_clock a, bhnd_clock b) {
	return (a > b ? a : b);
}

/**
 * bhnd(4) clock sources.
 */
typedef enum {
	/**
	 * Clock is provided by the PCI bus clock
	 */
	BHND_CLKSRC_PCI		= 0,

	/** Clock is provided by a crystal. */
	BHND_CLKSRC_XTAL	= 1,

	/** Clock is provided by a low power oscillator. */
	BHND_CLKSRC_LPO		= 2,

	/** Clock source is unknown */
	BHND_CLKSRC_UNKNOWN	= 3
} bhnd_clksrc;

/** Evaluates to true if @p cls is a device class that can be configured
 *  as a host bridge device. */
#define	BHND_DEVCLASS_SUPPORTS_HOSTB(cls)					\
	((cls) == BHND_DEVCLASS_PCI || (cls) == BHND_DEVCLASS_PCIE ||	\
	 (cls) == BHND_DEVCLASS_PCCARD)

/**
 * BHND bus address.
 * 
 * @note While the interconnect may support 64-bit addressing, not
 * all bridges and SoC CPUs will.
 */
typedef uint64_t	bhnd_addr_t;
#define	BHND_ADDR_MAX	UINT64_MAX	/**< Maximum bhnd_addr_t value */

/** BHND bus size. */
typedef uint64_t	bhnd_size_t;
#define	BHND_SIZE_MAX	UINT64_MAX	/**< Maximum bhnd_size_t value */


#endif /* _BHND_BHND_TYPES_H_ */
