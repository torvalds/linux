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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/refcount.h>
#include <sys/systm.h>

#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>

#include <dev/bhnd/siba/sibareg.h>

#include <dev/bhnd/cores/chipc/chipcreg.h>

#include "nvram/bhnd_nvram.h"

#include "bhnd_chipc_if.h"

#include "bhnd_nvram_if.h"
#include "bhnd_nvram_map.h"

#include "bhndreg.h"
#include "bhndvar.h"
#include "bhnd_private.h"

static void	bhnd_service_registry_free_entry(
		    struct bhnd_service_entry *entry);

static int	compare_ascending_probe_order(const void *lhs, const void *rhs);
static int	compare_descending_probe_order(const void *lhs,
		    const void *rhs);

/* BHND core device description table. */
static const struct bhnd_core_desc {
	uint16_t	 vendor;
	uint16_t	 device;
	bhnd_devclass_t	 class;
	const char	*desc;
} bhnd_core_descs[] = {
	#define	BHND_CDESC(_mfg, _cid, _cls, _desc)		\
	    { BHND_MFGID_ ## _mfg, BHND_COREID_ ## _cid,	\
		BHND_DEVCLASS_ ## _cls, _desc }

	BHND_CDESC(BCM, CC,		CC,		"ChipCommon I/O Controller"),
	BHND_CDESC(BCM, ILINE20,	OTHER,		"iLine20 HPNA"),
	BHND_CDESC(BCM, SRAM,		RAM,		"SRAM"),
	BHND_CDESC(BCM, SDRAM,		RAM,		"SDRAM"),
	BHND_CDESC(BCM, PCI,		PCI,		"PCI Bridge"),
	BHND_CDESC(BCM, MIPS,		CPU,		"BMIPS CPU"),
	BHND_CDESC(BCM, ENET,		ENET_MAC,	"Fast Ethernet MAC"),
	BHND_CDESC(BCM, V90_CODEC,	SOFTMODEM,	"V.90 SoftModem Codec"),
	BHND_CDESC(BCM, USB,		USB_DUAL,	"USB 1.1 Device/Host Controller"),
	BHND_CDESC(BCM, ADSL,		OTHER,		"ADSL Core"),
	BHND_CDESC(BCM, ILINE100,	OTHER,		"iLine100 HPNA"),
	BHND_CDESC(BCM, IPSEC,		OTHER,		"IPsec Accelerator"),
	BHND_CDESC(BCM, UTOPIA,		OTHER,		"UTOPIA ATM Core"),
	BHND_CDESC(BCM, PCMCIA,		PCCARD,		"PCMCIA Bridge"),
	BHND_CDESC(BCM, SOCRAM,		RAM,		"Internal Memory"),
	BHND_CDESC(BCM, MEMC,		MEMC,		"MEMC SDRAM Controller"),
	BHND_CDESC(BCM, OFDM,		OTHER,		"OFDM PHY"),
	BHND_CDESC(BCM, EXTIF,		OTHER,		"External Interface"),
	BHND_CDESC(BCM, D11,		WLAN,		"802.11 MAC/PHY/Radio"),
	BHND_CDESC(BCM, APHY,		WLAN_PHY,	"802.11a PHY"),
	BHND_CDESC(BCM, BPHY,		WLAN_PHY,	"802.11b PHY"),
	BHND_CDESC(BCM, GPHY,		WLAN_PHY,	"802.11g PHY"),
	BHND_CDESC(BCM, MIPS33,		CPU,		"BMIPS33 CPU"),
	BHND_CDESC(BCM, USB11H,		USB_HOST,	"USB 1.1 Host Controller"),
	BHND_CDESC(BCM, USB11D,		USB_DEV,	"USB 1.1 Device Controller"),
	BHND_CDESC(BCM, USB20H,		USB_HOST,	"USB 2.0 Host Controller"),
	BHND_CDESC(BCM, USB20D,		USB_DEV,	"USB 2.0 Device Controller"),
	BHND_CDESC(BCM, SDIOH,		OTHER,		"SDIO Host Controller"),
	BHND_CDESC(BCM, ROBO,		OTHER,		"RoboSwitch"),
	BHND_CDESC(BCM, ATA100,		OTHER,		"Parallel ATA Controller"),
	BHND_CDESC(BCM, SATAXOR,	OTHER,		"SATA DMA/XOR Controller"),
	BHND_CDESC(BCM, GIGETH,		ENET_MAC,	"Gigabit Ethernet MAC"),
	BHND_CDESC(BCM, PCIE,		PCIE,		"PCIe Bridge"),
	BHND_CDESC(BCM, NPHY,		WLAN_PHY,	"802.11n 2x2 PHY"),
	BHND_CDESC(BCM, SRAMC,		MEMC,		"SRAM Controller"),
	BHND_CDESC(BCM, MINIMAC,	OTHER,		"MINI MAC/PHY"),
	BHND_CDESC(BCM, ARM11,		CPU,		"ARM1176 CPU"),
	BHND_CDESC(BCM, ARM7S,		CPU,		"ARM7TDMI-S CPU"),
	BHND_CDESC(BCM, LPPHY,		WLAN_PHY,	"802.11a/b/g PHY"),
	BHND_CDESC(BCM, PMU,		PMU,		"PMU"),
	BHND_CDESC(BCM, SSNPHY,		WLAN_PHY,	"802.11n Single-Stream PHY"),
	BHND_CDESC(BCM, SDIOD,		OTHER,		"SDIO Device Core"),
	BHND_CDESC(BCM, ARMCM3,		CPU,		"ARM Cortex-M3 CPU"),
	BHND_CDESC(BCM, HTPHY,		WLAN_PHY,	"802.11n 4x4 PHY"),
	BHND_CDESC(MIPS,MIPS74K,	CPU,		"MIPS74k CPU"),
	BHND_CDESC(BCM, GMAC,		ENET_MAC,	"Gigabit MAC core"),
	BHND_CDESC(BCM, DMEMC,		MEMC,		"DDR1/DDR2 Memory Controller"),
	BHND_CDESC(BCM, PCIERC,		OTHER,		"PCIe Root Complex"),
	BHND_CDESC(BCM, OCP,		SOC_BRIDGE,	"OCP to OCP Bridge"),
	BHND_CDESC(BCM, SC,		OTHER,		"Shared Common Core"),
	BHND_CDESC(BCM, AHB,		SOC_BRIDGE,	"OCP to AHB Bridge"),
	BHND_CDESC(BCM, SPIH,		OTHER,		"SPI Host Controller"),
	BHND_CDESC(BCM, I2S,		OTHER,		"I2S Digital Audio Interface"),
	BHND_CDESC(BCM, DMEMS,		MEMC,		"SDR/DDR1 Memory Controller"),
	BHND_CDESC(BCM, UBUS_SHIM,	OTHER,		"BCM6362/UBUS WLAN SHIM"),
	BHND_CDESC(BCM, PCIE2,		PCIE,		"PCIe Bridge (Gen2)"),

	BHND_CDESC(ARM, APB_BRIDGE,	SOC_BRIDGE,	"BP135 AMBA3 AXI to APB Bridge"),
	BHND_CDESC(ARM, PL301,		SOC_ROUTER,	"PL301 AMBA3 Interconnect"),
	BHND_CDESC(ARM, EROM,		EROM,		"PL366 Device Enumeration ROM"),
	BHND_CDESC(ARM, OOB_ROUTER,	OTHER,		"PL367 OOB Interrupt Router"),
	BHND_CDESC(ARM, AXI_UNMAPPED,	OTHER,		"Unmapped Address Ranges"),

	BHND_CDESC(BCM, 4706_CC,	CC,		"ChipCommon I/O Controller"),
	BHND_CDESC(BCM, NS_PCIE2,	PCIE,		"PCIe Bridge (Gen2)"),
	BHND_CDESC(BCM, NS_DMA,		OTHER,		"DMA engine"),
	BHND_CDESC(BCM, NS_SDIO,	OTHER,		"SDIO 3.0 Host Controller"),
	BHND_CDESC(BCM, NS_USB20H,	USB_HOST,	"USB 2.0 Host Controller"),
	BHND_CDESC(BCM, NS_USB30H,	USB_HOST,	"USB 3.0 Host Controller"),
	BHND_CDESC(BCM, NS_A9JTAG,	OTHER,		"ARM Cortex A9 JTAG Interface"),
	BHND_CDESC(BCM, NS_DDR23_MEMC,	MEMC,		"Denali DDR2/DD3 Memory Controller"),
	BHND_CDESC(BCM, NS_ROM,		NVRAM,		"System ROM"),
	BHND_CDESC(BCM, NS_NAND,	NVRAM,		"NAND Flash Controller"),
	BHND_CDESC(BCM, NS_QSPI,	NVRAM,		"QSPI Flash Controller"),
	BHND_CDESC(BCM, NS_CC_B,	CC_B,		"ChipCommon B Auxiliary I/O Controller"),
	BHND_CDESC(BCM, 4706_SOCRAM,	RAM,		"Internal Memory"),
	BHND_CDESC(BCM, IHOST_ARMCA9,	CPU,		"ARM Cortex A9 CPU"),
	BHND_CDESC(BCM, 4706_GMAC_CMN,	ENET,		"Gigabit MAC (Common)"),
	BHND_CDESC(BCM, 4706_GMAC,	ENET_MAC,	"Gigabit MAC"),
	BHND_CDESC(BCM, AMEMC,		MEMC,		"Denali DDR1/DDR2 Memory Controller"),
#undef	BHND_CDESC

	/* Derived from inspection of the BCM4331 cores that provide PrimeCell
	 * IDs. Due to lack of documentation, the surmised device name/purpose
	 * provided here may be incorrect. */
	{ BHND_MFGID_ARM,	BHND_PRIMEID_EROM,	BHND_DEVCLASS_OTHER,
	    "PL364 Device Enumeration ROM" },
	{ BHND_MFGID_ARM,	BHND_PRIMEID_SWRAP,	BHND_DEVCLASS_OTHER,
	    "PL368 Device Management Interface" },
	{ BHND_MFGID_ARM,	BHND_PRIMEID_MWRAP,	BHND_DEVCLASS_OTHER,
	    "PL369 Device Management Interface" },

	{ 0, 0, 0, NULL }
};

static const struct bhnd_device_quirk bhnd_chipc_clkctl_quirks[];
static const struct bhnd_device_quirk bhnd_pcmcia_clkctl_quirks[];

/**
 * Device table entries for core-specific CLKCTL quirk lookup.
 */
static const struct bhnd_device bhnd_clkctl_devices[] = {
	BHND_DEVICE(BCM, CC,		NULL,	bhnd_chipc_clkctl_quirks),
	BHND_DEVICE(BCM, PCMCIA,	NULL,	bhnd_pcmcia_clkctl_quirks),
	BHND_DEVICE_END,
};

/** ChipCommon CLKCTL quirks */
static const struct bhnd_device_quirk bhnd_chipc_clkctl_quirks[] = {
	/* HTAVAIL/ALPAVAIL are bitswapped in chipc's CLKCTL */
	BHND_CHIP_QUIRK(4328,	HWREV_ANY,	BHND_CLKCTL_QUIRK_CCS0),
	BHND_CHIP_QUIRK(5354,	HWREV_ANY,	BHND_CLKCTL_QUIRK_CCS0),
	BHND_DEVICE_QUIRK_END
};

/** PCMCIA CLKCTL quirks */
static const struct bhnd_device_quirk bhnd_pcmcia_clkctl_quirks[] = {
	/* HTAVAIL/ALPAVAIL are bitswapped in pcmcia's CLKCTL */
	BHND_CHIP_QUIRK(4328,	HWREV_ANY,	BHND_CLKCTL_QUIRK_CCS0),
	BHND_CHIP_QUIRK(5354,	HWREV_ANY,	BHND_CLKCTL_QUIRK_CCS0),
	BHND_DEVICE_QUIRK_END
};

/**
 * Return the name for a given JEP106 manufacturer ID.
 * 
 * @param vendor A JEP106 Manufacturer ID, including the non-standard ARM 4-bit
 * JEP106 continuation code.
 */
const char *
bhnd_vendor_name(uint16_t vendor)
{
	switch (vendor) {
	case BHND_MFGID_ARM:
		return "ARM";
	case BHND_MFGID_BCM:
		return "Broadcom";
	case BHND_MFGID_MIPS:
		return "MIPS";
	default:
		return "unknown";
	}
}

/**
 * Return the name of a port type.
 * 
 * @param port_type The port type to look up.
 */
const char *
bhnd_port_type_name(bhnd_port_type port_type)
{
	switch (port_type) {
	case BHND_PORT_DEVICE:
		return ("device");
	case BHND_PORT_BRIDGE:
		return ("bridge");
	case BHND_PORT_AGENT:
		return ("agent");
	default:
		return "unknown";
	}
}

/**
 * Return the name of an NVRAM source.
 * 
 * @param nvram_src The NVRAM source type to look up.
 */
const char *
bhnd_nvram_src_name(bhnd_nvram_src nvram_src)
{
	switch (nvram_src) {
	case BHND_NVRAM_SRC_FLASH:
		return ("flash");
	case BHND_NVRAM_SRC_OTP:
		return ("OTP");
	case BHND_NVRAM_SRC_SPROM:
		return ("SPROM");
	case BHND_NVRAM_SRC_UNKNOWN:
		return ("none");
	default:
		return ("unknown");
	}
}

static const struct bhnd_core_desc *
bhnd_find_core_desc(uint16_t vendor, uint16_t device)
{
	for (u_int i = 0; bhnd_core_descs[i].desc != NULL; i++) {
		if (bhnd_core_descs[i].vendor != vendor)
			continue;
		
		if (bhnd_core_descs[i].device != device)
			continue;
		
		return (&bhnd_core_descs[i]);
	}
	
	return (NULL);
}

/**
 * Return a human-readable name for a BHND core.
 * 
 * @param vendor The core designer's JEDEC-106 Manufacturer ID.
 * @param device The core identifier.
 */
const char *
bhnd_find_core_name(uint16_t vendor, uint16_t device)
{
	const struct bhnd_core_desc *desc;
	
	if ((desc = bhnd_find_core_desc(vendor, device)) == NULL)
		return ("unknown");

	return desc->desc;
}

/**
 * Return the device class for a BHND core.
 * 
 * @param vendor The core designer's JEDEC-106 Manufacturer ID.
 * @param device The core identifier.
 */
bhnd_devclass_t
bhnd_find_core_class(uint16_t vendor, uint16_t device)
{
	const struct bhnd_core_desc *desc;
	
	if ((desc = bhnd_find_core_desc(vendor, device)) == NULL)
		return (BHND_DEVCLASS_OTHER);

	return desc->class;
}

/**
 * Return a human-readable name for a BHND core.
 * 
 * @param ci The core's info record.
 */
const char *
bhnd_core_name(const struct bhnd_core_info *ci)
{
	return bhnd_find_core_name(ci->vendor, ci->device);
}

/**
 * Return the device class for a BHND core.
 * 
 * @param ci The core's info record.
 */
bhnd_devclass_t
bhnd_core_class(const struct bhnd_core_info *ci)
{
	return bhnd_find_core_class(ci->vendor, ci->device);
}

/**
 * Write a human readable name representation of the given
 * BHND_CHIPID_* constant to @p buffer.
 * 
 * @param buffer Output buffer, or NULL to compute the required size.
 * @param size Capacity of @p buffer, in bytes.
 * @param chip_id Chip ID to be formatted.
 * 
 * @return The required number of bytes on success, or a negative integer on
 * failure. No more than @p size-1 characters be written, with the @p size'th
 * set to '\0'.
 * 
 * @sa BHND_CHIPID_MAX_NAMELEN
 */
int
bhnd_format_chip_id(char *buffer, size_t size, uint16_t chip_id)
{
	/* All hex formatted IDs are within the range of 0x4000-0x9C3F (40000-1) */
	if (chip_id >= 0x4000 && chip_id <= 0x9C3F)
		return (snprintf(buffer, size, "BCM%hX", chip_id));
	else
		return (snprintf(buffer, size, "BCM%hu", chip_id));
}

/**
 * Return a core info record populated from a bhnd-attached @p dev.
 * 
 * @param dev A bhnd device.
 * 
 * @return A core info record for @p dev.
 */
struct bhnd_core_info
bhnd_get_core_info(device_t dev) {
	return (struct bhnd_core_info) {
		.vendor		= bhnd_get_vendor(dev),
		.device		= bhnd_get_device(dev),
		.hwrev		= bhnd_get_hwrev(dev),
		.core_idx	= bhnd_get_core_index(dev),
		.unit		= bhnd_get_core_unit(dev)
	};
}

/**
 * Find a @p class child device with @p unit on @p bus.
 * 
 * @param bus The bhnd-compatible bus to be searched.
 * @param class The device class to match on.
 * @param unit The core unit number; specify -1 to return the first match
 * regardless of unit number.
 * 
 * @retval device_t if a matching child device is found.
 * @retval NULL if no matching child device is found.
 */
device_t
bhnd_bus_find_child(device_t bus, bhnd_devclass_t class, int unit)
{
	struct bhnd_core_match md = {
		BHND_MATCH_CORE_CLASS(class),
		BHND_MATCH_CORE_UNIT(unit)
	};

	if (unit == -1)
		md.m.match.core_unit = 0;

	return bhnd_bus_match_child(bus, &md);
}

/**
 * Find the first child device on @p bus that matches @p desc.
 * 
 * @param bus The bhnd-compatible bus to be searched.
 * @param desc A match descriptor.
 * 
 * @retval device_t if a matching child device is found.
 * @retval NULL if no matching child device is found.
 */
device_t
bhnd_bus_match_child(device_t bus, const struct bhnd_core_match *desc)
{
	device_t	*devlistp;
	device_t	 match;
	int		 devcnt;
	int		 error;

	error = device_get_children(bus, &devlistp, &devcnt);
	if (error != 0)
		return (NULL);

	match = NULL;
	for (int i = 0; i < devcnt; i++) {
		struct bhnd_core_info ci = bhnd_get_core_info(devlistp[i]);

		if (bhnd_core_matches(&ci, desc)) {
			match = devlistp[i];
			goto done;
		}
	}

done:
	free(devlistp, M_TEMP);
	return match;
}

/**
 * Retrieve an ordered list of all device instances currently connected to
 * @p bus, returning a pointer to the array in @p devlistp and the count
 * in @p ndevs.
 * 
 * The memory allocated for the table must be freed via
 * bhnd_bus_free_children().
 * 
 * @param	bus		The bhnd-compatible bus to be queried.
 * @param[out]	devlist		The array of devices.
 * @param[out]	devcount	The number of devices in @p devlistp
 * @param	order		The order in which devices will be returned
 *				in @p devlist.
 * 
 * @retval 0		success
 * @retval non-zero	if an error occurs, a regular unix error code will
 *			be returned.
 */
int
bhnd_bus_get_children(device_t bus, device_t **devlist, int *devcount,
    bhnd_device_order order)
{
	int error;

	/* Fetch device array */
	if ((error = device_get_children(bus, devlist, devcount)))
		return (error);

	/* Perform requested sorting */
	if ((error = bhnd_sort_devices(*devlist, *devcount, order))) {
		bhnd_bus_free_children(*devlist);
		return (error);
	}

	return (0);
}

/**
 * Free any memory allocated in a previous call to bhnd_bus_get_children().
 *
 * @param devlist The device array returned by bhnd_bus_get_children().
 */
void
bhnd_bus_free_children(device_t *devlist)
{
	free(devlist, M_TEMP);
}

/**
 * Perform in-place sorting of an array of bhnd device instances.
 * 
 * @param devlist	An array of bhnd devices.
 * @param devcount	The number of devices in @p devs.
 * @param order		The sort order to be used.
 * 
 * @retval 0		success
 * @retval EINVAL	if the sort order is unknown.
 */
int
bhnd_sort_devices(device_t *devlist, size_t devcount, bhnd_device_order order)
{
	int (*compare)(const void *, const void *);

	switch (order) {
	case BHND_DEVICE_ORDER_ATTACH:
		compare = compare_ascending_probe_order;
		break;
	case BHND_DEVICE_ORDER_DETACH:
		compare = compare_descending_probe_order;
		break;
	default:
		printf("unknown sort order: %d\n", order);
		return (EINVAL);
	}

	qsort(devlist, devcount, sizeof(*devlist), compare);
	return (0);
}

/*
 * Ascending comparison of bhnd device's probe order.
 */
static int
compare_ascending_probe_order(const void *lhs, const void *rhs)
{
	device_t	ldev, rdev;
	int		lorder, rorder;

	ldev = (*(const device_t *) lhs);
	rdev = (*(const device_t *) rhs);

	lorder = BHND_BUS_GET_PROBE_ORDER(device_get_parent(ldev), ldev);
	rorder = BHND_BUS_GET_PROBE_ORDER(device_get_parent(rdev), rdev);

	if (lorder < rorder) {
		return (-1);
	} else if (lorder > rorder) {
		return (1);
	} else {
		return (0);
	}
}

/*
 * Descending comparison of bhnd device's probe order.
 */
static int
compare_descending_probe_order(const void *lhs, const void *rhs)
{
	return (compare_ascending_probe_order(rhs, lhs));
}

/**
 * Call device_probe_and_attach() for each of the bhnd bus device's
 * children, in bhnd attach order.
 * 
 * @param bus The bhnd-compatible bus for which all children should be probed
 * and attached.
 */
int
bhnd_bus_probe_children(device_t bus)
{
	device_t	*devs;
	int		 ndevs;
	int		 error;

	/* Fetch children in attach order */
	error = bhnd_bus_get_children(bus, &devs, &ndevs,
	    BHND_DEVICE_ORDER_ATTACH);
	if (error)
		return (error);

	/* Probe and attach all children */
	for (int i = 0; i < ndevs; i++) {
		device_t child = devs[i];
		device_probe_and_attach(child);
	}

	bhnd_bus_free_children(devs);

	return (0);
}

/**
 * Walk up the bhnd device hierarchy to locate the root device
 * to which the bhndb bridge is attached.
 * 
 * This can be used from within bhnd host bridge drivers to locate the
 * actual upstream host device.
 * 
 * @param dev A bhnd device.
 * @param bus_class The expected bus (e.g. "pci") to which the bridge root
 * should be attached.
 * 
 * @retval device_t if a matching parent device is found.
 * @retval NULL if @p dev is not attached via a bhndb bus.
 * @retval NULL if no parent device is attached via @p bus_class.
 */
device_t
bhnd_find_bridge_root(device_t dev, devclass_t bus_class)
{
	devclass_t	bhndb_class;
	device_t	parent;

	KASSERT(device_get_devclass(device_get_parent(dev)) == bhnd_devclass,
	   ("%s not a bhnd device", device_get_nameunit(dev)));

	bhndb_class = devclass_find("bhndb");

	/* Walk the device tree until we hit a bridge */
	parent = dev;
	while ((parent = device_get_parent(parent)) != NULL) {
		if (device_get_devclass(parent) == bhndb_class)
			break;
	}

	/* No bridge? */
	if (parent == NULL)
		return (NULL);

	/* Search for a parent attached to the expected bus class */
	while ((parent = device_get_parent(parent)) != NULL) {
		device_t bus;

		bus = device_get_parent(parent);
		if (bus != NULL && device_get_devclass(bus) == bus_class)
			return (parent);
	}

	/* Not found */
	return (NULL);
}

/**
 * Find the first core in @p cores that matches @p desc.
 * 
 * @param cores The table to search.
 * @param num_cores The length of @p cores.
 * @param desc A match descriptor.
 * 
 * @retval bhnd_core_info if a matching core is found.
 * @retval NULL if no matching core is found.
 */
const struct bhnd_core_info *
bhnd_match_core(const struct bhnd_core_info *cores, u_int num_cores,
    const struct bhnd_core_match *desc)
{
	for (u_int i = 0; i < num_cores; i++) {
		if (bhnd_core_matches(&cores[i], desc))
			return &cores[i];
	}

	return (NULL);
}


/**
 * Find the first core in @p cores with the given @p class.
 * 
 * @param cores The table to search.
 * @param num_cores The length of @p cores.
 * @param class The device class to match on.
 * 
 * @retval non-NULL if a matching core is found.
 * @retval NULL if no matching core is found.
 */
const struct bhnd_core_info *
bhnd_find_core(const struct bhnd_core_info *cores, u_int num_cores,
    bhnd_devclass_t class)
{
	struct bhnd_core_match md = {
		BHND_MATCH_CORE_CLASS(class)
	};

	return bhnd_match_core(cores, num_cores, &md);
}


/**
 * Create an equality match descriptor for @p core.
 * 
 * @param core The core info to be matched on.
 * 
 * @return an equality match descriptor for @p core.
 */
struct bhnd_core_match
bhnd_core_get_match_desc(const struct bhnd_core_info *core)
{
	return ((struct bhnd_core_match) {
		BHND_MATCH_CORE_VENDOR(core->vendor),
		BHND_MATCH_CORE_ID(core->device),
		BHND_MATCH_CORE_REV(HWREV_EQ(core->hwrev)),
		BHND_MATCH_CORE_CLASS(bhnd_core_class(core)),
		BHND_MATCH_CORE_IDX(core->core_idx),
		BHND_MATCH_CORE_UNIT(core->unit)
	});
}


/**
 * Return true if the @p lhs is equal to @p rhs.
 * 
 * @param lhs The first bhnd core descriptor to compare.
 * @param rhs The second bhnd core descriptor to compare.
 * 
 * @retval true if @p lhs is equal to @p rhs
 * @retval false if @p lhs is not equal to @p rhs
 */
bool
bhnd_cores_equal(const struct bhnd_core_info *lhs,
    const struct bhnd_core_info *rhs)
{
	struct bhnd_core_match md;

	/* Use an equality match descriptor to perform the comparison */
	md = bhnd_core_get_match_desc(rhs);
	return (bhnd_core_matches(lhs, &md));
}

/**
 * Return true if the @p core matches @p desc.
 * 
 * @param core A bhnd core descriptor.
 * @param desc A match descriptor to compare against @p core.
 * 
 * @retval true if @p core matches @p match.
 * @retval false if @p core does not match @p match.
 */
bool
bhnd_core_matches(const struct bhnd_core_info *core,
    const struct bhnd_core_match *desc)
{
	if (desc->m.match.core_vendor && desc->core_vendor != core->vendor)
		return (false);

	if (desc->m.match.core_id && desc->core_id != core->device)
		return (false);

	if (desc->m.match.core_unit && desc->core_unit != core->unit)
		return (false);

	if (desc->m.match.core_rev && 
	    !bhnd_hwrev_matches(core->hwrev, &desc->core_rev))
		return (false);

	if (desc->m.match.core_idx && desc->core_idx != core->core_idx)
		return (false);

	if (desc->m.match.core_class &&
	    desc->core_class != bhnd_core_class(core))
		return (false);

	return true;
}

/**
 * Return true if the @p chip matches @p desc.
 * 
 * @param chip A bhnd chip identifier.
 * @param desc A match descriptor to compare against @p chip.
 * 
 * @retval true if @p chip matches @p match.
 * @retval false if @p chip does not match @p match.
 */
bool
bhnd_chip_matches(const struct bhnd_chipid *chip,
    const struct bhnd_chip_match *desc)
{
	if (desc->m.match.chip_id && chip->chip_id != desc->chip_id)
		return (false);

	if (desc->m.match.chip_pkg && chip->chip_pkg != desc->chip_pkg)
		return (false);

	if (desc->m.match.chip_rev &&
	    !bhnd_hwrev_matches(chip->chip_rev, &desc->chip_rev))
		return (false);

	if (desc->m.match.chip_type && chip->chip_type != desc->chip_type)
		return (false);

	return (true);
}

/**
 * Return true if the @p board matches @p desc.
 * 
 * @param board The bhnd board info.
 * @param desc A match descriptor to compare against @p board.
 * 
 * @retval true if @p chip matches @p match.
 * @retval false if @p chip does not match @p match.
 */
bool
bhnd_board_matches(const struct bhnd_board_info *board,
    const struct bhnd_board_match *desc)
{
	if (desc->m.match.board_srom_rev &&
	    !bhnd_hwrev_matches(board->board_srom_rev, &desc->board_srom_rev))
		return (false);

	if (desc->m.match.board_vendor &&
	    board->board_vendor != desc->board_vendor)
		return (false);

	if (desc->m.match.board_type && board->board_type != desc->board_type)
		return (false);

	if (desc->m.match.board_devid &&
	    board->board_devid != desc->board_devid)
		return (false);

	if (desc->m.match.board_rev &&
	    !bhnd_hwrev_matches(board->board_rev, &desc->board_rev))
		return (false);

	return (true);
}

/**
 * Return true if the @p hwrev matches @p desc.
 * 
 * @param hwrev A bhnd hardware revision.
 * @param desc A match descriptor to compare against @p core.
 * 
 * @retval true if @p hwrev matches @p match.
 * @retval false if @p hwrev does not match @p match.
 */
bool
bhnd_hwrev_matches(uint16_t hwrev, const struct bhnd_hwrev_match *desc)
{
	if (desc->start != BHND_HWREV_INVALID &&
	    desc->start > hwrev)
		return false;
		
	if (desc->end != BHND_HWREV_INVALID &&
	    desc->end < hwrev)
		return false;

	return true;
}

/**
 * Return true if the @p dev matches @p desc.
 * 
 * @param dev A bhnd device.
 * @param desc A match descriptor to compare against @p dev.
 * 
 * @retval true if @p dev matches @p match.
 * @retval false if @p dev does not match @p match.
 */
bool
bhnd_device_matches(device_t dev, const struct bhnd_device_match *desc)
{
	struct bhnd_core_info		 core;
	const struct bhnd_chipid	*chip;
	struct bhnd_board_info		 board;
	device_t			 parent;
	int				 error;

	/* Construct individual match descriptors */
	struct bhnd_core_match	m_core	= { _BHND_CORE_MATCH_COPY(desc) };
	struct bhnd_chip_match	m_chip	= { _BHND_CHIP_MATCH_COPY(desc) };
	struct bhnd_board_match	m_board	= { _BHND_BOARD_MATCH_COPY(desc) };

	/* Fetch and match core info */
	if (m_core.m.match_flags) {
		/* Only applicable to bhnd-attached cores */
		parent = device_get_parent(dev);
		if (device_get_devclass(parent) != bhnd_devclass) {
			device_printf(dev, "attempting to match core "
			    "attributes against non-core device\n");
			return (false);
		}

		core = bhnd_get_core_info(dev);
		if (!bhnd_core_matches(&core, &m_core))
			return (false);
	}

	/* Fetch and match chip info */
	if (m_chip.m.match_flags) {
		chip = bhnd_get_chipid(dev);

		if (!bhnd_chip_matches(chip, &m_chip))
			return (false);
	}

	/* Fetch and match board info.
	 *
	 * This is not available until  after NVRAM is up; earlier device
	 * matches should not include board requirements */
	if (m_board.m.match_flags) {
		if ((error = bhnd_read_board_info(dev, &board))) {
			device_printf(dev, "failed to read required board info "
			    "during device matching: %d\n", error);
			return (false);
		}

		if (!bhnd_board_matches(&board, &m_board))
			return (false);
	}

	/* All matched */
	return (true);
}

/**
 * Search @p table for an entry matching @p dev.
 * 
 * @param dev A bhnd device to match against @p table.
 * @param table The device table to search.
 * @param entry_size The @p table entry size, in bytes.
 * 
 * @retval non-NULL the first matching device, if any.
 * @retval NULL if no matching device is found in @p table.
 */
const struct bhnd_device *
bhnd_device_lookup(device_t dev, const struct bhnd_device *table,
    size_t entry_size)
{
	const struct bhnd_device	*entry;
	device_t			 hostb, parent;
	bhnd_attach_type		 attach_type;
	uint32_t			 dflags;

	parent = device_get_parent(dev);
	hostb = bhnd_bus_find_hostb_device(parent);
	attach_type = bhnd_get_attach_type(dev);

	for (entry = table; !BHND_DEVICE_IS_END(entry); entry =
	    (const struct bhnd_device *) ((const char *) entry + entry_size))
	{
		/* match core info */
		if (!bhnd_device_matches(dev, &entry->core))
			continue;

		/* match device flags */
		dflags = entry->device_flags;

		/* hostb implies BHND_ATTACH_ADAPTER requirement */
		if (dflags & BHND_DF_HOSTB)
			dflags |= BHND_DF_ADAPTER;
	
		if (dflags & BHND_DF_ADAPTER)
			if (attach_type != BHND_ATTACH_ADAPTER)
				continue;

		if (dflags & BHND_DF_HOSTB)
			if (dev != hostb)
				continue;

		if (dflags & BHND_DF_SOC)
			if (attach_type != BHND_ATTACH_NATIVE)
				continue;

		/* device found */
		return (entry);
	}

	/* not found */
	return (NULL);
}

/**
 * Scan the device @p table for all quirk flags applicable to @p dev.
 * 
 * @param dev A bhnd device to match against @p table.
 * @param table The device table to search.
 * @param entry_size The @p table entry size, in bytes.
 * 
 * @return all matching quirk flags.
 */
uint32_t
bhnd_device_quirks(device_t dev, const struct bhnd_device *table,
    size_t entry_size)
{
	const struct bhnd_device	*dent;
	const struct bhnd_device_quirk	*qent, *qtable;
	uint32_t			 quirks;

	/* Locate the device entry */
	if ((dent = bhnd_device_lookup(dev, table, entry_size)) == NULL)
		return (0);

	/* Quirks table is optional */
	qtable = dent->quirks_table;
	if (qtable == NULL)
		return (0);

	/* Collect matching device quirk entries */
	quirks = 0;
	for (qent = qtable; !BHND_DEVICE_QUIRK_IS_END(qent); qent++) {
		if (bhnd_device_matches(dev, &qent->desc))
			quirks |= qent->quirks;
	}

	return (quirks);
}


/**
 * Allocate bhnd(4) resources defined in @p rs from a parent bus.
 * 
 * @param dev The device requesting ownership of the resources.
 * @param rs A standard bus resource specification. This will be updated
 * with the allocated resource's RIDs.
 * @param res On success, the allocated bhnd resources.
 * 
 * @retval 0 success
 * @retval non-zero if allocation of any non-RF_OPTIONAL resource fails,
 * 		    all allocated resources will be released and a regular
 * 		    unix error code will be returned.
 */
int
bhnd_alloc_resources(device_t dev, struct resource_spec *rs,
    struct bhnd_resource **res)
{
	/* Initialize output array */
	for (u_int i = 0; rs[i].type != -1; i++)
		res[i] = NULL;

	for (u_int i = 0; rs[i].type != -1; i++) {
		res[i] = bhnd_alloc_resource_any(dev, rs[i].type, &rs[i].rid,
		    rs[i].flags);

		/* Clean up all allocations on failure */
		if (res[i] == NULL && !(rs[i].flags & RF_OPTIONAL)) {
			bhnd_release_resources(dev, rs, res);
			return (ENXIO);
		}
	}

	return (0);
}

/**
 * Release bhnd(4) resources defined in @p rs from a parent bus.
 * 
 * @param dev The device that owns the resources.
 * @param rs A standard bus resource specification previously initialized
 * by @p bhnd_alloc_resources.
 * @param res The bhnd resources to be released.
 */
void
bhnd_release_resources(device_t dev, const struct resource_spec *rs,
    struct bhnd_resource **res)
{
	for (u_int i = 0; rs[i].type != -1; i++) {
		if (res[i] == NULL)
			continue;

		bhnd_release_resource(dev, rs[i].type, rs[i].rid, res[i]);
		res[i] = NULL;
	}
}

/**
 * Allocate and return a new per-core PMU clock control/status (clkctl)
 * instance for @p dev.
 * 
 * @param dev		The bhnd(4) core device mapped by @p r.
 * @param pmu_dev	The bhnd(4) PMU device, implmenting the bhnd_pmu_if
 *			interface. The caller is responsible for ensuring that
 *			this reference remains valid for the lifetime of the
 *			returned clkctl instance.
 * @param r		A resource mapping the core's clock control register
 * 			(see BHND_CLK_CTL_ST). The caller is responsible for
 *			ensuring that this resource remains valid for the
 *			lifetime of the returned clkctl instance.
 * @param offset	The offset to the clock control register within @p r.
 * @param max_latency	The PMU's maximum state transition latency in
 *			microseconds; this upper bound will be used to busy-wait
 *			on PMU state transitions.
 * 
 * @retval non-NULL	success
 * @retval NULL		if allocation fails.
 * 
 */
struct bhnd_core_clkctl *
bhnd_alloc_core_clkctl(device_t dev, device_t pmu_dev, struct bhnd_resource *r,
    bus_size_t offset, u_int max_latency)
{
	struct bhnd_core_clkctl	*clkctl;

	clkctl = malloc(sizeof(*clkctl), M_BHND, M_ZERO | M_NOWAIT);
	if (clkctl == NULL)
		return (NULL);

	clkctl->cc_dev = dev;
	clkctl->cc_pmu_dev = pmu_dev;
	clkctl->cc_res = r;
	clkctl->cc_res_offset = offset;
	clkctl->cc_max_latency = max_latency;
	clkctl->cc_quirks = bhnd_device_quirks(dev, bhnd_clkctl_devices,
	    sizeof(bhnd_clkctl_devices[0]));

	BHND_CLKCTL_LOCK_INIT(clkctl);

	return (clkctl);
}

/**
 * Free a clkctl instance previously allocated via bhnd_alloc_core_clkctl().
 * 
 * @param clkctl	The clkctl instance to be freed.
 */
void
bhnd_free_core_clkctl(struct bhnd_core_clkctl *clkctl)
{
	BHND_CLKCTL_LOCK_DESTROY(clkctl);

	free(clkctl, M_BHND);
}

/**
 * Wait for the per-core clock status to be equal to @p value after
 * applying @p mask, timing out after the maximum transition latency is reached.
 * 
 * @param clkctl	Per-core clkctl state to be queryied.
 * @param value		Value to wait for.
 * @param mask		Mask to apply prior to value comparison.
 * 
 * @retval 0		success
 * @retval ETIMEDOUT	if the PMU's maximum transition delay is reached before
 *			the clock status matches @p value and @p mask.
 */
int
bhnd_core_clkctl_wait(struct bhnd_core_clkctl *clkctl, uint32_t value,
    uint32_t mask)
{
	uint32_t	clkst;

	BHND_CLKCTL_LOCK_ASSERT(clkctl, MA_OWNED);

	/* Bitswapped HTAVAIL/ALPAVAIL work-around */
	if (clkctl->cc_quirks & BHND_CLKCTL_QUIRK_CCS0) {
		uint32_t fmask, fval;

		fmask = mask & ~(BHND_CCS_HTAVAIL | BHND_CCS_ALPAVAIL);
		fval = value & ~(BHND_CCS_HTAVAIL | BHND_CCS_ALPAVAIL);

		if (mask & BHND_CCS_HTAVAIL)
			fmask |= BHND_CCS0_HTAVAIL;
		if (value & BHND_CCS_HTAVAIL)
			fval |= BHND_CCS0_HTAVAIL;

		if (mask & BHND_CCS_ALPAVAIL) 
			fmask |= BHND_CCS0_ALPAVAIL;
		if (value & BHND_CCS_ALPAVAIL)
			fval |= BHND_CCS0_ALPAVAIL;

		mask = fmask;
		value = fval;
	}

	for (u_int i = 0; i < clkctl->cc_max_latency; i += 10) {
		clkst = bhnd_bus_read_4(clkctl->cc_res, clkctl->cc_res_offset);
		if ((clkst & mask) == (value & mask))
			return (0);

		DELAY(10);
	}

	device_printf(clkctl->cc_dev, "clkst wait timeout (value=%#x, "
	    "mask=%#x)\n", value, mask);

	return (ETIMEDOUT);
}

/**
 * Read an NVRAM variable's NUL-terminated string value.
 *
 * @param 	dev	A bhnd bus child device.
 * @param	name	The NVRAM variable name.
 * @param[out]	buf	A buffer large enough to hold @p len bytes. On
 *			success, the NUL-terminated string value will be
 *			written to this buffer. This argment may be NULL if
 *			the value is not desired.
 * @param	len	The maximum capacity of @p buf.
 * @param[out]	rlen	On success, will be set to the actual size of
 *			the requested value (including NUL termination). This
 *			argment may be NULL if the size is not desired.
 *
 * @retval 0		success
 * @retval ENOENT	The requested variable was not found.
 * @retval ENODEV	No valid NVRAM source could be found.
 * @retval ENOMEM	If @p buf is non-NULL and a buffer of @p len is too
 *			small to hold the requested value.
 * @retval EFTYPE	If the variable data cannot be coerced to a valid
 *			string representation.
 * @retval ERANGE	If value coercion would overflow @p type.
 * @retval non-zero	If reading @p name otherwise fails, a regular unix
 *			error code will be returned.
 */
int
bhnd_nvram_getvar_str(device_t dev, const char *name, char *buf, size_t len,
    size_t *rlen)
{
	size_t	larg;
	int	error;

	larg = len;
	error = bhnd_nvram_getvar(dev, name, buf, &larg,
	    BHND_NVRAM_TYPE_STRING);
	if (rlen != NULL)
		*rlen = larg;

	return (error);
}

/**
 * Read an NVRAM variable's unsigned integer value.
 *
 * @param 		dev	A bhnd bus child device.
 * @param		name	The NVRAM variable name.
 * @param[out]		value	On success, the requested value will be written
 *				to this pointer.
 * @param		width	The output integer type width (1, 2, or
 *				4 bytes).
 * 
 * @retval 0		success
 * @retval ENOENT	The requested variable was not found.
 * @retval ENODEV	No valid NVRAM source could be found.
 * @retval EFTYPE	If the variable data cannot be coerced to a
 *			a valid unsigned integer representation.
 * @retval ERANGE	If value coercion would overflow (or underflow) an
 *			unsigned representation of the given @p width.
 * @retval non-zero	If reading @p name otherwise fails, a regular unix
 *			error code will be returned.
 */
int
bhnd_nvram_getvar_uint(device_t dev, const char *name, void *value, int width)
{
	bhnd_nvram_type	type;
	size_t		len;

	switch (width) {
	case 1:
		type = BHND_NVRAM_TYPE_UINT8;
		break;
	case 2:
		type = BHND_NVRAM_TYPE_UINT16;
		break;
	case 4:
		type = BHND_NVRAM_TYPE_UINT32;
		break;
	default:
		device_printf(dev, "unsupported NVRAM integer width: %d\n",
		    width);
		return (EINVAL);
	}

	len = width;
	return (bhnd_nvram_getvar(dev, name, value, &len, type));
}

/**
 * Read an NVRAM variable's unsigned 8-bit integer value.
 *
 * @param 		dev	A bhnd bus child device.
 * @param		name	The NVRAM variable name.
 * @param[out]		value	On success, the requested value will be written
 *				to this pointer.
 * 
 * @retval 0		success
 * @retval ENOENT	The requested variable was not found.
 * @retval ENODEV	No valid NVRAM source could be found.
 * @retval EFTYPE	If the variable data cannot be coerced to a
 *			a valid unsigned integer representation.
 * @retval ERANGE	If value coercion would overflow (or underflow) uint8_t.
 * @retval non-zero	If reading @p name otherwise fails, a regular unix
 *			error code will be returned.
 */
int
bhnd_nvram_getvar_uint8(device_t dev, const char *name, uint8_t *value)
{
	return (bhnd_nvram_getvar_uint(dev, name, value, sizeof(*value)));
}

/**
 * Read an NVRAM variable's unsigned 16-bit integer value.
 *
 * @param 		dev	A bhnd bus child device.
 * @param		name	The NVRAM variable name.
 * @param[out]		value	On success, the requested value will be written
 *				to this pointer.
 * 
 * @retval 0		success
 * @retval ENOENT	The requested variable was not found.
 * @retval ENODEV	No valid NVRAM source could be found.
 * @retval EFTYPE	If the variable data cannot be coerced to a
 *			a valid unsigned integer representation.
 * @retval ERANGE	If value coercion would overflow (or underflow)
 *			uint16_t.
 * @retval non-zero	If reading @p name otherwise fails, a regular unix
 *			error code will be returned.
 */
int
bhnd_nvram_getvar_uint16(device_t dev, const char *name, uint16_t *value)
{
	return (bhnd_nvram_getvar_uint(dev, name, value, sizeof(*value)));
}

/**
 * Read an NVRAM variable's unsigned 32-bit integer value.
 *
 * @param 		dev	A bhnd bus child device.
 * @param		name	The NVRAM variable name.
 * @param[out]		value	On success, the requested value will be written
 *				to this pointer.
 * 
 * @retval 0		success
 * @retval ENOENT	The requested variable was not found.
 * @retval ENODEV	No valid NVRAM source could be found.
 * @retval EFTYPE	If the variable data cannot be coerced to a
 *			a valid unsigned integer representation.
 * @retval ERANGE	If value coercion would overflow (or underflow)
 *			uint32_t.
 * @retval non-zero	If reading @p name otherwise fails, a regular unix
 *			error code will be returned.
 */
int
bhnd_nvram_getvar_uint32(device_t dev, const char *name, uint32_t *value)
{
	return (bhnd_nvram_getvar_uint(dev, name, value, sizeof(*value)));
}

/**
 * Read an NVRAM variable's signed integer value.
 *
 * @param 		dev	A bhnd bus child device.
 * @param		name	The NVRAM variable name.
 * @param[out]		value	On success, the requested value will be written
 *				to this pointer.
 * @param		width	The output integer type width (1, 2, or
 *				4 bytes).
 * 
 * @retval 0		success
 * @retval ENOENT	The requested variable was not found.
 * @retval ENODEV	No valid NVRAM source could be found.
 * @retval EFTYPE	If the variable data cannot be coerced to a
 *			a valid integer representation.
 * @retval ERANGE	If value coercion would overflow (or underflow) an
 *			signed representation of the given @p width.
 * @retval non-zero	If reading @p name otherwise fails, a regular unix
 *			error code will be returned.
 */
int
bhnd_nvram_getvar_int(device_t dev, const char *name, void *value, int width)
{
	bhnd_nvram_type	type;
	size_t		len;

	switch (width) {
	case 1:
		type = BHND_NVRAM_TYPE_INT8;
		break;
	case 2:
		type = BHND_NVRAM_TYPE_INT16;
		break;
	case 4:
		type = BHND_NVRAM_TYPE_INT32;
		break;
	default:
		device_printf(dev, "unsupported NVRAM integer width: %d\n",
		    width);
		return (EINVAL);
	}

	len = width;
	return (bhnd_nvram_getvar(dev, name, value, &len, type));
}

/**
 * Read an NVRAM variable's signed 8-bit integer value.
 *
 * @param 		dev	A bhnd bus child device.
 * @param		name	The NVRAM variable name.
 * @param[out]		value	On success, the requested value will be written
 *				to this pointer.
 * 
 * @retval 0		success
 * @retval ENOENT	The requested variable was not found.
 * @retval ENODEV	No valid NVRAM source could be found.
 * @retval EFTYPE	If the variable data cannot be coerced to a
 *			a valid integer representation.
 * @retval ERANGE	If value coercion would overflow (or underflow) int8_t.
 * @retval non-zero	If reading @p name otherwise fails, a regular unix
 *			error code will be returned.
 */
int
bhnd_nvram_getvar_int8(device_t dev, const char *name, int8_t *value)
{
	return (bhnd_nvram_getvar_int(dev, name, value, sizeof(*value)));
}

/**
 * Read an NVRAM variable's signed 16-bit integer value.
 *
 * @param 		dev	A bhnd bus child device.
 * @param		name	The NVRAM variable name.
 * @param[out]		value	On success, the requested value will be written
 *				to this pointer.
 * 
 * @retval 0		success
 * @retval ENOENT	The requested variable was not found.
 * @retval ENODEV	No valid NVRAM source could be found.
 * @retval EFTYPE	If the variable data cannot be coerced to a
 *			a valid integer representation.
 * @retval ERANGE	If value coercion would overflow (or underflow)
 *			int16_t.
 * @retval non-zero	If reading @p name otherwise fails, a regular unix
 *			error code will be returned.
 */
int
bhnd_nvram_getvar_int16(device_t dev, const char *name, int16_t *value)
{
	return (bhnd_nvram_getvar_int(dev, name, value, sizeof(*value)));
}

/**
 * Read an NVRAM variable's signed 32-bit integer value.
 *
 * @param 		dev	A bhnd bus child device.
 * @param		name	The NVRAM variable name.
 * @param[out]		value	On success, the requested value will be written
 *				to this pointer.
 * 
 * @retval 0		success
 * @retval ENOENT	The requested variable was not found.
 * @retval ENODEV	No valid NVRAM source could be found.
 * @retval EFTYPE	If the variable data cannot be coerced to a
 *			a valid integer representation.
 * @retval ERANGE	If value coercion would overflow (or underflow)
 *			int32_t.
 * @retval non-zero	If reading @p name otherwise fails, a regular unix
 *			error code will be returned.
 */
int
bhnd_nvram_getvar_int32(device_t dev, const char *name, int32_t *value)
{
	return (bhnd_nvram_getvar_int(dev, name, value, sizeof(*value)));
}


/**
 * Read an NVRAM variable's array value.
 *
 * @param 		dev	A bhnd bus child device.
 * @param		name	The NVRAM variable name.
 * @param[out]		buf	A buffer large enough to hold @p size bytes.
 *				On success, the requested value will be written
 *				to this buffer.
 * @param[in,out]	size	The required number of bytes to write to
 *				@p buf.
 * @param		type	The desired array element data representation.
 * 
 * @retval 0		success
 * @retval ENOENT	The requested variable was not found.
 * @retval ENODEV	No valid NVRAM source could be found.
 * @retval ENXIO	If less than @p size bytes are available.
 * @retval ENOMEM	If a buffer of @p size is too small to hold the
 *			requested value.
 * @retval EFTYPE	If the variable data cannot be coerced to a
 *			a valid instance of @p type.
 * @retval ERANGE	If value coercion would overflow (or underflow) a
 *			representation of @p type.
 * @retval non-zero	If reading @p name otherwise fails, a regular unix
 *			error code will be returned.
 */
int
bhnd_nvram_getvar_array(device_t dev, const char *name, void *buf, size_t size,
    bhnd_nvram_type type)
{
	size_t	nbytes;
	int	error;

	/* Attempt read */
	nbytes = size;
	if ((error = bhnd_nvram_getvar(dev, name, buf, &nbytes, type)))
		return (error);

	/* Verify that the expected number of bytes were fetched */
	if (nbytes < size)
		return (ENXIO);

	return (0);
}

/**
 * Initialize a service provider registry.
 * 
 * @param bsr		The service registry to initialize.
 * 
 * @retval 0            success
 * @retval non-zero     if an error occurs initializing the service registry,
 *                      a regular unix error code will be returned.

 */
int
bhnd_service_registry_init(struct bhnd_service_registry *bsr)
{
	STAILQ_INIT(&bsr->entries);
	mtx_init(&bsr->lock, "bhnd_service_registry lock", NULL, MTX_DEF);

	return (0);
}

/**
 * Release all resources held by @p bsr.
 * 
 * @param bsr		A service registry instance previously successfully
 *			initialized via bhnd_service_registry_init().
 *
 * @retval 0		success
 * @retval EBUSY	if active references to service providers registered
 *			with @p bsr exist.
 */
int
bhnd_service_registry_fini(struct bhnd_service_registry *bsr)
{
	struct bhnd_service_entry *entry, *enext;

	/* Remove everthing we can */
	mtx_lock(&bsr->lock);
	STAILQ_FOREACH_SAFE(entry, &bsr->entries, link, enext) {
		if (entry->refs > 0)
			continue;

		STAILQ_REMOVE(&bsr->entries, entry, bhnd_service_entry, link);
		free(entry, M_BHND);
	}

	if (!STAILQ_EMPTY(&bsr->entries)) {
		mtx_unlock(&bsr->lock);
		return (EBUSY);
	}
	mtx_unlock(&bsr->lock);

	mtx_destroy(&bsr->lock);
	return (0);
}

/**
 * Register a @p provider for the given @p service.
 *
 * @param bsr		Service registry to be modified.
 * @param provider	Service provider to register.
 * @param service	Service for which @p provider will be registered.
 * @param flags		Service provider flags (see BHND_SPF_*).
 *
 * @retval 0		success
 * @retval EEXIST	if an entry for @p service already exists.
 * @retval EINVAL	if @p service is BHND_SERVICE_ANY.
 * @retval non-zero	if registering @p provider otherwise fails, a regular
 *			unix error code will be returned.
 */
int
bhnd_service_registry_add(struct bhnd_service_registry *bsr, device_t provider,
    bhnd_service_t service, uint32_t flags)
{
	struct bhnd_service_entry *entry;

	if (service == BHND_SERVICE_ANY)
		return (EINVAL);

	mtx_lock(&bsr->lock);

	/* Is a service provider already registered? */
	STAILQ_FOREACH(entry, &bsr->entries, link) {
		if (entry->service == service) {
			mtx_unlock(&bsr->lock);
			return (EEXIST);
		}
	}

	/* Initialize and insert our new entry */
	entry = malloc(sizeof(*entry), M_BHND, M_NOWAIT);
	if (entry == NULL) {
		mtx_unlock(&bsr->lock);
		return (ENOMEM);
	}

	entry->provider = provider;
	entry->service = service;
	entry->flags = flags;
	refcount_init(&entry->refs, 0);

	STAILQ_INSERT_HEAD(&bsr->entries, entry, link);

	mtx_unlock(&bsr->lock);
	return (0);
}

/**
 * Free an unreferenced registry entry.
 * 
 * @param entry	The entry to be deallocated.
 */
static void
bhnd_service_registry_free_entry(struct bhnd_service_entry *entry)
{
	KASSERT(entry->refs == 0, ("provider has active references"));
	free(entry, M_BHND);
}

/**
 * Attempt to remove the @p service provider registration for @p provider.
 *
 * @param bsr		The service registry to be modified.
 * @param provider	The service provider to be deregistered.
 * @param service	The service for which @p provider will be deregistered,
 *			or BHND_SERVICE_ANY to remove all service
 *			registrations for @p provider.
 *
 * @retval 0		success
 * @retval EBUSY	if active references to @p provider exist; see
 *			bhnd_service_registry_retain() and
 *			bhnd_service_registry_release().
 */
int
bhnd_service_registry_remove(struct bhnd_service_registry *bsr,
    device_t provider, bhnd_service_t service)
{
	struct bhnd_service_entry *entry, *enext;

	mtx_lock(&bsr->lock);

#define	BHND_PROV_MATCH(_e)	\
	((_e)->provider == provider &&	\
	 (service == BHND_SERVICE_ANY || (_e)->service == service))

	/* Validate matching provider entries before making any
	 * modifications */
	STAILQ_FOREACH(entry, &bsr->entries, link) {
		/* Skip non-matching entries */
		if (!BHND_PROV_MATCH(entry))
			continue;

		/* Entry is in use? */
		if (entry->refs > 0) {
			mtx_unlock(&bsr->lock);
			return (EBUSY);
		}
	}

	/* We can now safely remove matching entries */
	STAILQ_FOREACH_SAFE(entry, &bsr->entries, link, enext) {
		/* Skip non-matching entries */
		if (!BHND_PROV_MATCH(entry))
			continue;

		/* Remove from list */
		STAILQ_REMOVE(&bsr->entries, entry, bhnd_service_entry, link);

		/* Free provider entry */
		bhnd_service_registry_free_entry(entry);
	}
#undef	BHND_PROV_MATCH

	mtx_unlock(&bsr->lock);
	return (0);
}

/**
 * Retain and return a reference to a registered @p service provider, if any.
 *
 * @param bsr		The service registry to be queried.
 * @param service	The service for which a provider should be returned.
 *
 * On success, the caller assumes ownership the returned provider, and
 * is responsible for releasing this reference via
 * bhnd_service_registry_release().
 *
 * @retval device_t	success
 * @retval NULL		if no provider is registered for @p service.
 */
device_t
bhnd_service_registry_retain(struct bhnd_service_registry *bsr,
    bhnd_service_t service)
{
	struct bhnd_service_entry *entry;

	mtx_lock(&bsr->lock);
	STAILQ_FOREACH(entry, &bsr->entries, link) {
		if (entry->service != service)
			continue;

		/* With a live refcount, entry is gauranteed to remain alive
		 * after we release our lock */
		refcount_acquire(&entry->refs);

		mtx_unlock(&bsr->lock);
		return (entry->provider);
	}
	mtx_unlock(&bsr->lock);

	/* Not found */
	return (NULL);
}

/**
 * Release a reference to a service provider previously returned by
 * bhnd_service_registry_retain().
 * 
 * If this is the last reference to an inherited service provider registration
 * (see BHND_SPF_INHERITED), the registration will also be removed, and
 * true will be returned.
 *
 * @param bsr		The service registry from which @p provider
 *			was returned.
 * @param provider	The provider to be released.
 * @param service	The service for which @p provider was previously
 *			retained.
 * @retval true		The inherited service provider registration was removed;
 *			the caller should release its own reference to the
 *			provider.
 * @retval false	The service provider was not inherited, or active
 *			references to the provider remain.
 * 
 * @see BHND_SPF_INHERITED
 */
bool
bhnd_service_registry_release(struct bhnd_service_registry *bsr,
    device_t provider, bhnd_service_t service)
{
	struct bhnd_service_entry *entry;

	/* Exclusive lock, as we need to prevent any new references to the
	 * entry from being taken if it's to be removed */
	mtx_lock(&bsr->lock);
	STAILQ_FOREACH(entry, &bsr->entries, link) {
		bool removed;

		if (entry->provider != provider)
			continue;

		if (entry->service != service)
			continue;

		if (refcount_release(&entry->refs) &&
		    (entry->flags & BHND_SPF_INHERITED))
		{
			/* If an inherited entry is no longer actively
			 * referenced, remove the local registration and inform
			 * the caller. */
			STAILQ_REMOVE(&bsr->entries, entry, bhnd_service_entry,
			    link);
			bhnd_service_registry_free_entry(entry);
			removed = true;
		} else {
			removed = false;
		}

		mtx_unlock(&bsr->lock);
		return (removed);
	}

	/* Caller owns a reference, but no such provider is registered? */
	panic("invalid service provider reference");
}

/**
 * Using the bhnd(4) bus-level core information and a custom core name,
 * populate @p dev's device description.
 * 
 * @param dev A bhnd-bus attached device.
 * @param dev_name The core's name (e.g. "SDIO Device Core").
 */
void
bhnd_set_custom_core_desc(device_t dev, const char *dev_name)
{
	const char *vendor_name;
	char *desc;

	vendor_name = bhnd_get_vendor_name(dev);
	asprintf(&desc, M_BHND, "%s %s, rev %hhu", vendor_name, dev_name,
	    bhnd_get_hwrev(dev));

	if (desc != NULL) {
		device_set_desc_copy(dev, desc);
		free(desc, M_BHND);
	} else {
		device_set_desc(dev, dev_name);
	}
}

/**
 * Using the bhnd(4) bus-level core information, populate @p dev's device
 * description.
 * 
 * @param dev A bhnd-bus attached device.
 */
void
bhnd_set_default_core_desc(device_t dev)
{
	bhnd_set_custom_core_desc(dev, bhnd_get_device_name(dev));
}


/**
 * Using the bhnd @p chip_id, populate the bhnd(4) bus @p dev's device
 * description.
 * 
 * @param dev A bhnd-bus attached device.
 * @param chip_id The chip identification.
 */
void
bhnd_set_default_bus_desc(device_t dev, const struct bhnd_chipid *chip_id)
{
	const char	*bus_name;
	char		*desc;
	char		 chip_name[BHND_CHIPID_MAX_NAMELEN];

	/* Determine chip type's bus name */
	switch (chip_id->chip_type) {
	case BHND_CHIPTYPE_SIBA:
		bus_name = "SIBA bus";
		break;
	case BHND_CHIPTYPE_BCMA:
	case BHND_CHIPTYPE_BCMA_ALT:
		bus_name = "BCMA bus";
		break;
	case BHND_CHIPTYPE_UBUS:
		bus_name = "UBUS bus";
		break;
	default:
		bus_name = "Unknown Type";
		break;
	}

	/* Format chip name */
	bhnd_format_chip_id(chip_name, sizeof(chip_name),
	     chip_id->chip_id);

	/* Format and set device description */
	asprintf(&desc, M_BHND, "%s %s", chip_name, bus_name);
	if (desc != NULL) {
		device_set_desc_copy(dev, desc);
		free(desc, M_BHND);
	} else {
		device_set_desc(dev, bus_name);
	}
	
}

/**
 * Helper function for implementing BHND_BUS_REGISTER_PROVIDER().
 * 
 * This implementation delegates the request to the BHND_BUS_REGISTER_PROVIDER()
 * method on the parent of @p dev. If no parent exists, the implementation
 * will return an error. 
 */
int
bhnd_bus_generic_register_provider(device_t dev, device_t child,
    device_t provider, bhnd_service_t service)
{
	device_t parent = device_get_parent(dev);

	if (parent != NULL) {
		return (BHND_BUS_REGISTER_PROVIDER(parent, child,
		    provider, service));
	}

	return (ENXIO);
}

/**
 * Helper function for implementing BHND_BUS_DEREGISTER_PROVIDER().
 * 
 * This implementation delegates the request to the
 * BHND_BUS_DEREGISTER_PROVIDER() method on the parent of @p dev. If no parent
 * exists, the implementation will panic.
 */
int
bhnd_bus_generic_deregister_provider(device_t dev, device_t child,
    device_t provider, bhnd_service_t service)
{
	device_t parent = device_get_parent(dev);

	if (parent != NULL) {
		return (BHND_BUS_DEREGISTER_PROVIDER(parent, child,
		    provider, service));
	}

	panic("missing BHND_BUS_DEREGISTER_PROVIDER()");
}

/**
 * Helper function for implementing BHND_BUS_RETAIN_PROVIDER().
 * 
 * This implementation delegates the request to the
 * BHND_BUS_DEREGISTER_PROVIDER() method on the parent of @p dev. If no parent
 * exists, the implementation will return NULL.
 */
device_t
bhnd_bus_generic_retain_provider(device_t dev, device_t child,
    bhnd_service_t service)
{
	device_t parent = device_get_parent(dev);

	if (parent != NULL) {
		return (BHND_BUS_RETAIN_PROVIDER(parent, child,
		    service));
	}

	return (NULL);
}

/**
 * Helper function for implementing BHND_BUS_RELEASE_PROVIDER().
 * 
 * This implementation delegates the request to the
 * BHND_BUS_DEREGISTER_PROVIDER() method on the parent of @p dev. If no parent
 * exists, the implementation will panic.
 */
void
bhnd_bus_generic_release_provider(device_t dev, device_t child,
    device_t provider, bhnd_service_t service)
{
	device_t parent = device_get_parent(dev);

	if (parent != NULL) {
		return (BHND_BUS_RELEASE_PROVIDER(parent, child,
		    provider, service));
	}

	panic("missing BHND_BUS_RELEASE_PROVIDER()");
}

/**
 * Helper function for implementing BHND_BUS_REGISTER_PROVIDER().
 * 
 * This implementation uses the bhnd_service_registry_add() function to
 * do most of the work. It calls BHND_BUS_GET_SERVICE_REGISTRY() to find
 * a suitable service registry to edit.
 */
int
bhnd_bus_generic_sr_register_provider(device_t dev, device_t child,
    device_t provider, bhnd_service_t service)
{
	struct bhnd_service_registry *bsr;

	bsr = BHND_BUS_GET_SERVICE_REGISTRY(dev, child);

	KASSERT(bsr != NULL, ("NULL service registry"));

	return (bhnd_service_registry_add(bsr, provider, service, 0));
}

/**
 * Helper function for implementing BHND_BUS_DEREGISTER_PROVIDER().
 * 
 * This implementation uses the bhnd_service_registry_remove() function to
 * do most of the work. It calls BHND_BUS_GET_SERVICE_REGISTRY() to find
 * a suitable service registry to edit.
 */
int
bhnd_bus_generic_sr_deregister_provider(device_t dev, device_t child,
    device_t provider, bhnd_service_t service)
{
	struct bhnd_service_registry *bsr;

	bsr = BHND_BUS_GET_SERVICE_REGISTRY(dev, child);

	KASSERT(bsr != NULL, ("NULL service registry"));

	return (bhnd_service_registry_remove(bsr, provider, service));
}

/**
 * Helper function for implementing BHND_BUS_RETAIN_PROVIDER().
 * 
 * This implementation uses the bhnd_service_registry_retain() function to
 * do most of the work. It calls BHND_BUS_GET_SERVICE_REGISTRY() to find
 * a suitable service registry.
 * 
 * If a local provider for the service is not available, and a parent device is
 * available, this implementation will attempt to fetch and locally register
 * a service provider reference from the parent of @p dev.
 */
device_t
bhnd_bus_generic_sr_retain_provider(device_t dev, device_t child,
    bhnd_service_t service)
{
	struct bhnd_service_registry	*bsr;
	device_t			 parent, provider;
	int				 error;

	bsr = BHND_BUS_GET_SERVICE_REGISTRY(dev, child);
	KASSERT(bsr != NULL, ("NULL service registry"));

	/*
	 * Attempt to fetch a service provider reference from either the local
	 * service registry, or if not found, from our parent.
	 * 
	 * If we fetch a provider from our parent, we register the provider
	 * with the local service registry to prevent conflicting local
	 * registrations from being added.
	 */
	while (1) {
		/* Check the local service registry first */
		provider = bhnd_service_registry_retain(bsr, service);
		if (provider != NULL)
			return (provider);

		/* Otherwise, try to delegate to our parent (if any) */
		if ((parent = device_get_parent(dev)) == NULL)
			return (NULL);

		provider = BHND_BUS_RETAIN_PROVIDER(parent, dev, service);
		if (provider == NULL)
			return (NULL);

		/* Register the inherited service registration with the local
		 * registry */
		error = bhnd_service_registry_add(bsr, provider, service,
		    BHND_SPF_INHERITED);
		if (error) {
			BHND_BUS_RELEASE_PROVIDER(parent, dev, provider,
			    service);
			if (error == EEXIST) {
				/* A valid service provider was registered
				 * concurrently; retry fetching from the local
				 * registry */
				continue;
			}

			device_printf(dev, "failed to register service "
			    "provider: %d\n", error);
			return (NULL);
		}
	}
}

/**
 * Helper function for implementing BHND_BUS_RELEASE_PROVIDER().
 * 
 * This implementation uses the bhnd_service_registry_release() function to
 * do most of the work. It calls BHND_BUS_GET_SERVICE_REGISTRY() to find
 * a suitable service registry.
 */
void
bhnd_bus_generic_sr_release_provider(device_t dev, device_t child,
    device_t provider, bhnd_service_t service)
{
	struct bhnd_service_registry	*bsr;

	bsr = BHND_BUS_GET_SERVICE_REGISTRY(dev, child);
	KASSERT(bsr != NULL, ("NULL service registry"));

	/* Release the provider reference; if the refcount hits zero on an
	 * inherited reference, true will be returned, and we need to drop
	 * our own bus reference to the provider */
	if (!bhnd_service_registry_release(bsr, provider, service))
		return;

	/* Drop our reference to the borrowed provider */
	BHND_BUS_RELEASE_PROVIDER(device_get_parent(dev), dev, provider,
	    service);
}

/**
 * Helper function for implementing BHND_BUS_IS_HW_DISABLED().
 * 
 * If a parent device is available, this implementation delegates the
 * request to the BHND_BUS_IS_HW_DISABLED() method on the parent of @p dev.
 * 
 * If no parent device is available (i.e. on a the bus root), the hardware
 * is assumed to be usable and false is returned.
 */
bool
bhnd_bus_generic_is_hw_disabled(device_t dev, device_t child)
{
	if (device_get_parent(dev) != NULL)
		return (BHND_BUS_IS_HW_DISABLED(device_get_parent(dev), child));

	return (false);
}

/**
 * Helper function for implementing BHND_BUS_GET_CHIPID().
 * 
 * This implementation delegates the request to the BHND_BUS_GET_CHIPID()
 * method on the parent of @p dev. If no parent exists, the implementation
 * will panic.
 */
const struct bhnd_chipid *
bhnd_bus_generic_get_chipid(device_t dev, device_t child)
{
	if (device_get_parent(dev) != NULL)
		return (BHND_BUS_GET_CHIPID(device_get_parent(dev), child));

	panic("missing BHND_BUS_GET_CHIPID()");
}

/**
 * Helper function for implementing BHND_BUS_GET_DMA_TRANSLATION().
 * 
 * If a parent device is available, this implementation delegates the
 * request to the BHND_BUS_GET_DMA_TRANSLATION() method on the parent of @p dev.
 *
 * If no parent device is available, this implementation will panic.
 */
int
bhnd_bus_generic_get_dma_translation(device_t dev, device_t child, u_int width,
    uint32_t flags, bus_dma_tag_t *dmat,
    struct bhnd_dma_translation *translation)
{
	if (device_get_parent(dev) != NULL) {
		return (BHND_BUS_GET_DMA_TRANSLATION(device_get_parent(dev),
		    child, width, flags, dmat, translation));
	}

	panic("missing BHND_BUS_GET_DMA_TRANSLATION()");
}

/* nvram board_info population macros for bhnd_bus_generic_read_board_info() */
#define	BHND_GV(_dest, _name)	\
	bhnd_nvram_getvar_uint(child, BHND_NVAR_ ## _name, &_dest,	\
	    sizeof(_dest))

#define	REQ_BHND_GV(_dest, _name)		do {			\
	if ((error = BHND_GV(_dest, _name))) {				\
		device_printf(dev,					\
		    "error reading " __STRING(_name) ": %d\n", error);	\
		return (error);						\
	}								\
} while(0)

#define	OPT_BHND_GV(_dest, _name, _default)	do {			\
	if ((error = BHND_GV(_dest, _name))) {				\
		if (error != ENOENT) {					\
			device_printf(dev,				\
			    "error reading "				\
			       __STRING(_name) ": %d\n", error);	\
			return (error);					\
		}							\
		_dest = _default;					\
	}								\
} while(0)

/**
 * Helper function for implementing BHND_BUS_READ_BOARDINFO().
 * 
 * This implementation populates @p info with information from NVRAM,
 * defaulting board_vendor and board_type fields to 0 if the
 * requested variables cannot be found.
 * 
 * This behavior is correct for most SoCs, but must be overridden on
 * bridged (PCI, PCMCIA, etc) devices to produce a complete bhnd_board_info
 * result.
 */
int
bhnd_bus_generic_read_board_info(device_t dev, device_t child,
    struct bhnd_board_info *info)
{
	int	error;

	OPT_BHND_GV(info->board_vendor,	BOARDVENDOR,	0);
	OPT_BHND_GV(info->board_type,	BOARDTYPE,	0);	/* srom >= 2 */
	OPT_BHND_GV(info->board_devid,	DEVID,		0);	/* srom >= 8 */
	REQ_BHND_GV(info->board_rev,	BOARDREV);
	OPT_BHND_GV(info->board_srom_rev,SROMREV,	0);	/* missing in
								   some SoC
								   NVRAM */
	REQ_BHND_GV(info->board_flags,	BOARDFLAGS);
	OPT_BHND_GV(info->board_flags2,	BOARDFLAGS2,	0);	/* srom >= 4 */
	OPT_BHND_GV(info->board_flags3,	BOARDFLAGS3,	0);	/* srom >= 11 */

	return (0);
}

#undef	BHND_GV
#undef	BHND_GV_REQ
#undef	BHND_GV_OPT

/**
 * Helper function for implementing BHND_BUS_GET_NVRAM_VAR().
 * 
 * This implementation searches @p dev for a usable NVRAM child device.
 * 
 * If no usable child device is found on @p dev, the request is delegated to
 * the BHND_BUS_GET_NVRAM_VAR() method on the parent of @p dev.
 */
int
bhnd_bus_generic_get_nvram_var(device_t dev, device_t child, const char *name,
    void *buf, size_t *size, bhnd_nvram_type type)
{
	device_t	nvram;
	device_t	parent;

        /* Make sure we're holding Giant for newbus */
	GIANT_REQUIRED;

	/* Look for a directly-attached NVRAM child */
	if ((nvram = device_find_child(dev, "bhnd_nvram", -1)) != NULL)
		return BHND_NVRAM_GETVAR(nvram, name, buf, size, type);

	/* Try to delegate to parent */
	if ((parent = device_get_parent(dev)) == NULL)
		return (ENODEV);

	return (BHND_BUS_GET_NVRAM_VAR(device_get_parent(dev), child,
	    name, buf, size, type));
}

/**
 * Helper function for implementing BHND_BUS_ALLOC_RESOURCE().
 * 
 * This implementation of BHND_BUS_ALLOC_RESOURCE() delegates allocation
 * of the underlying resource to BUS_ALLOC_RESOURCE(), and activation
 * to @p dev's BHND_BUS_ACTIVATE_RESOURCE().
 */
struct bhnd_resource *
bhnd_bus_generic_alloc_resource(device_t dev, device_t child, int type,
	int *rid, rman_res_t start, rman_res_t end, rman_res_t count,
	u_int flags)
{
	struct bhnd_resource	*br;
	struct resource		*res;
	int			 error;

	br = NULL;
	res = NULL;

	/* Allocate the real bus resource (without activating it) */
	res = BUS_ALLOC_RESOURCE(dev, child, type, rid, start, end, count,
	    (flags & ~RF_ACTIVE));
	if (res == NULL)
		return (NULL);

	/* Allocate our bhnd resource wrapper. */
	br = malloc(sizeof(struct bhnd_resource), M_BHND, M_NOWAIT);
	if (br == NULL)
		goto failed;
	
	br->direct = false;
	br->res = res;

	/* Attempt activation */
	if (flags & RF_ACTIVE) {
		error = BHND_BUS_ACTIVATE_RESOURCE(dev, child, type, *rid, br);
		if (error)
			goto failed;
	}

	return (br);
	
failed:
	if (res != NULL)
		BUS_RELEASE_RESOURCE(dev, child, type, *rid, res);

	free(br, M_BHND);
	return (NULL);
}

/**
 * Helper function for implementing BHND_BUS_RELEASE_RESOURCE().
 * 
 * This implementation of BHND_BUS_RELEASE_RESOURCE() delegates release of
 * the backing resource to BUS_RELEASE_RESOURCE().
 */
int
bhnd_bus_generic_release_resource(device_t dev, device_t child, int type,
    int rid, struct bhnd_resource *r)
{
	int error;

	if ((error = BUS_RELEASE_RESOURCE(dev, child, type, rid, r->res)))
		return (error);

	free(r, M_BHND);
	return (0);
}


/**
 * Helper function for implementing BHND_BUS_ACTIVATE_RESOURCE().
 * 
 * This implementation of BHND_BUS_ACTIVATE_RESOURCE() first calls the
 * BHND_BUS_ACTIVATE_RESOURCE() method of the parent of @p dev.
 * 
 * If this fails, and if @p dev is the direct parent of @p child, standard
 * resource activation is attempted via bus_activate_resource(). This enables
 * direct use of the bhnd(4) resource APIs on devices that may not be attached
 * to a parent bhnd bus or bridge.
 */
int
bhnd_bus_generic_activate_resource(device_t dev, device_t child, int type,
    int rid, struct bhnd_resource *r)
{
	int	error;
	bool	passthrough;

	passthrough = (device_get_parent(child) != dev);

	/* Try to delegate to the parent */
	if (device_get_parent(dev) != NULL) {
		error = BHND_BUS_ACTIVATE_RESOURCE(device_get_parent(dev),
		    child, type, rid, r);
	} else {
		error = ENODEV;
	}

	/* If bhnd(4) activation has failed and we're the child's direct
	 * parent, try falling back on standard resource activation.
	 */
	if (error && !passthrough) {
		error = bus_activate_resource(child, type, rid, r->res);
		if (!error)
			r->direct = true;
	}

	return (error);
}

/**
 * Helper function for implementing BHND_BUS_DEACTIVATE_RESOURCE().
 * 
 * This implementation of BHND_BUS_ACTIVATE_RESOURCE() simply calls the
 * BHND_BUS_ACTIVATE_RESOURCE() method of the parent of @p dev.
 */
int
bhnd_bus_generic_deactivate_resource(device_t dev, device_t child,
    int type, int rid, struct bhnd_resource *r)
{
	if (device_get_parent(dev) != NULL)
		return (BHND_BUS_DEACTIVATE_RESOURCE(device_get_parent(dev),
		    child, type, rid, r));

	return (EINVAL);
}

/**
 * Helper function for implementing BHND_BUS_GET_INTR_DOMAIN().
 * 
 * This implementation simply returns the address of nearest bhnd(4) bus,
 * which may be @p dev; this behavior may be incompatible with FDT/OFW targets.
 */
uintptr_t
bhnd_bus_generic_get_intr_domain(device_t dev, device_t child, bool self)
{
	return ((uintptr_t)dev);
}
