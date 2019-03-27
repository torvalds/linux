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

#ifndef _BHND_BHND_H_
#define _BHND_BHND_H_

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#include <machine/bus.h>

#include "bhnd_ids.h"
#include "bhnd_types.h"
#include "bhnd_erom_types.h"
#include "bhnd_debug.h"
#include "bhnd_bus_if.h"
#include "bhnd_match.h"

#include "nvram/bhnd_nvram.h"

extern devclass_t bhnd_devclass;
extern devclass_t bhnd_hostb_devclass;
extern devclass_t bhnd_nvram_devclass;

#define	BHND_CHIPID_MAX_NAMELEN	32	/**< maximum buffer required for a
					     bhnd_format_chip_id() */

/**
 * bhnd child instance variables
 */
enum bhnd_device_vars {
	BHND_IVAR_VENDOR,	/**< Designer's JEP-106 manufacturer ID. */
	BHND_IVAR_DEVICE,	/**< Part number */
	BHND_IVAR_HWREV,	/**< Core revision */
	BHND_IVAR_DEVICE_CLASS,	/**< Core class (@sa bhnd_devclass_t) */
	BHND_IVAR_VENDOR_NAME,	/**< Core vendor name */
	BHND_IVAR_DEVICE_NAME,	/**< Core name */
	BHND_IVAR_CORE_INDEX,	/**< Bus-assigned core number */
	BHND_IVAR_CORE_UNIT,	/**< Bus-assigned core unit number,
				     assigned sequentially (starting at 0) for
				     each vendor/device pair. */
	BHND_IVAR_PMU_INFO,	/**< Internal bus-managed PMU state */
};

/**
 * bhnd device probe priority bands.
 */
enum {
	BHND_PROBE_ROOT         = 0,    /**< Nexus or host bridge */
	BHND_PROBE_BUS		= 1000,	/**< Buses and bridges */
	BHND_PROBE_CPU		= 2000,	/**< CPU devices */
	BHND_PROBE_INTERRUPT	= 3000,	/**< Interrupt controllers. */
	BHND_PROBE_TIMER	= 4000,	/**< Timers and clocks. */
	BHND_PROBE_RESOURCE	= 5000,	/**< Resource discovery (including NVRAM/SPROM) */
	BHND_PROBE_DEFAULT	= 6000,	/**< Default device priority */
};

/**
 * Constants defining fine grained ordering within a BHND_PROBE_* priority band.
 * 
 * Example:
 * @code
 * BHND_PROBE_BUS + BHND_PROBE_ORDER_FIRST
 * @endcode
 */
enum {
	BHND_PROBE_ORDER_FIRST		= 0,
	BHND_PROBE_ORDER_EARLY		= 25,
	BHND_PROBE_ORDER_MIDDLE		= 50,
	BHND_PROBE_ORDER_LATE		= 75,
	BHND_PROBE_ORDER_LAST		= 100

};


/**
 * Per-core IOCTL flags common to all bhnd(4) cores.
 */
enum {
	BHND_IOCTL_BIST		= 0x8000,	/**< Initiate a built-in self-test (BIST). Must be cleared
						     after BIST results are read via BHND_IOST_BIST_* */
	BHND_IOCTL_PME		= 0x4000,	/**< Enable posting of power management events by the core. */
	BHND_IOCTL_CFLAGS	= 0x3FFC,	/**< Reserved for core-specific ioctl flags. */
	BHND_IOCTL_CLK_FORCE	= 0x0002,	/**< Force disable of clock gating, resulting in all clocks
						     being distributed within the core. Should be set when
						     asserting/deasserting reset to ensure the reset signal
						     fully propagates to the entire core. */
	BHND_IOCTL_CLK_EN	= 0x0001,	/**< If cleared, the core clock will be disabled. Should be
						     set during normal operation, and cleared when the core is
						     held in reset. */
};

/**
 * Per-core IOST flags common to all bhnd(4) cores.
 */
enum {
	BHND_IOST_BIST_DONE	= 0x8000,	/**< Set upon BIST completion (see BHND_IOCTL_BIST), and cleared
						     if 0 is written to BHND_IOCTL_BIST. */ 
	BHND_IOST_BIST_FAIL	= 0x4000,	/**< Set upon detection of a BIST error; the value is unspecified
						     if BIST has not completed and BHND_IOST_BIST_DONE is not set. */
	BHND_IOST_CLK		= 0x2000,	/**< Set if the core has requested that gated clocks be enabled, or
						     cleared otherwise. The value is undefined if a core does not
						     support clock gating. */
	BHND_IOST_DMA64		= 0x1000,	/**< Set if this core supports 64-bit DMA */
	BHND_IOST_CFLAGS	= 0x0FFC,	/**< Reserved for core-specific status flags. */
};

/*
 * Simplified accessors for bhnd device ivars
 */
#define	BHND_ACCESSOR(var, ivar, type) \
	__BUS_ACCESSOR(bhnd, var, BHND, ivar, type)

BHND_ACCESSOR(vendor,		VENDOR,		uint16_t);
BHND_ACCESSOR(device,		DEVICE,		uint16_t);
BHND_ACCESSOR(hwrev,		HWREV,		uint8_t);
BHND_ACCESSOR(class,		DEVICE_CLASS,	bhnd_devclass_t);
BHND_ACCESSOR(vendor_name,	VENDOR_NAME,	const char *);
BHND_ACCESSOR(device_name,	DEVICE_NAME,	const char *);
BHND_ACCESSOR(core_index,	CORE_INDEX,	u_int);
BHND_ACCESSOR(core_unit,	CORE_UNIT,	int);
BHND_ACCESSOR(pmu_info,		PMU_INFO,	void *);

#undef	BHND_ACCESSOR

/**
 * A bhnd(4) board descriptor.
 */
struct bhnd_board_info {
	uint16_t	board_vendor;	/**< Board vendor (PCI-SIG vendor ID).
					  *
					  * On PCI devices, this will default to
					  * the PCI subsystem vendor ID, but may
					  * be overridden by the 'boardtype'
					  * NVRAM variable.
					  *
					  * On SoCs, this will default to
					  * PCI_VENDOR_BROADCOM, but may be
					  * overridden by the 'boardvendor'
					  * NVRAM variable.
					  */
	uint16_t	board_type;	/**< Board type (See BHND_BOARD_*)
					  *
					  *  This value is usually a
					  *  Broadcom-assigned reference board
					  *  identifier (see BHND_BOARD_*), but
					  *  may be set to an arbitrary value
					  *  assigned by the board vendor.
					  *
					  *  On PCI devices, this will default
					  *  to the PCI subsystem ID, but may be
					  *  overridden by the 'boardtype'
					  *  NVRAM variable.
					  *
					  *  On SoCs, this will always be
					  *  populated with the value of the
					  * 'boardtype' NVRAM variable.
					  */
	uint16_t	board_devid;	/**< Board device ID.
					  *
					  *  On PCI devices, this will default
					  *  to the PCI device ID, but may
					  *  be overridden by the 'devid'
					  *  NVRAM variable.
					  */
	uint16_t	board_rev;	/**< Board revision. */
	uint8_t		board_srom_rev;	/**< Board SROM format revision */

	uint32_t	board_flags;	/**< Board flags (see BHND_BFL_*) */
	uint32_t	board_flags2;	/**< Board flags 2 (see BHND_BFL2_*) */
	uint32_t	board_flags3;	/**< Board flags 3 (see BHND_BFL3_*) */
};


/**
 * Chip Identification
 * 
 * This is read from the ChipCommon ID register; on earlier bhnd(4) devices
 * where ChipCommon is unavailable, known values must be supplied.
 */
struct bhnd_chipid {
	uint16_t	chip_id;	/**< chip id (BHND_CHIPID_*) */
	uint8_t		chip_rev;	/**< chip revision */
	uint8_t		chip_pkg;	/**< chip package (BHND_PKGID_*) */
	uint8_t		chip_type;	/**< chip type (BHND_CHIPTYPE_*) */
	uint32_t	chip_caps;	/**< chip capabilities (BHND_CAP_*) */

	bhnd_addr_t	enum_addr;	/**< chip_type-specific enumeration
					  *  address; either the siba(4) base
					  *  core register block, or the bcma(4)
					  *  EROM core address. */

	uint8_t		ncores;		/**< number of cores, if known. 0 if
					  *  not available. */
};

/**
 * Chip capabilities
 */
enum bhnd_cap {
	BHND_CAP_BP64	= (1<<0),	/**< Backplane supports 64-bit
					  *  addressing */
	BHND_CAP_PMU	= (1<<1),	/**< PMU is present */
};

/**
 * A bhnd(4) core descriptor.
 */
struct bhnd_core_info {
	uint16_t	vendor;		/**< JEP-106 vendor (BHND_MFGID_*) */
	uint16_t	device;		/**< device */
	uint16_t	hwrev;		/**< hardware revision */
	u_int		core_idx;	/**< bus-assigned core index */
	int		unit;		/**< bus-assigned core unit */
};

/**
 * bhnd(4) DMA address widths.
 */
typedef enum {
	BHND_DMA_ADDR_30BIT	= 30,	/**< 30-bit DMA */
	BHND_DMA_ADDR_32BIT	= 32,	/**< 32-bit DMA */
	BHND_DMA_ADDR_64BIT	= 64,	/**< 64-bit DMA */
} bhnd_dma_addrwidth;

/**
 * Convert an address width (in bits) to its corresponding mask.
 */
#define	BHND_DMA_ADDR_BITMASK(_width)	\
	((_width >= 64) ? ~0ULL :	\
	 (_width == 0) ? 0x0 :		\
	 ((1ULL << (_width)) - 1))	\

/**
 * bhnd(4) DMA address translation descriptor.
 */
struct bhnd_dma_translation {
	/**
	 * Host-to-device physical address translation.
	 * 
	 * This may be added to the host physical address to produce a device
	 * DMA address.
	 */
	bhnd_addr_t	base_addr;

	/**
	 * Device-addressable address mask.
	 * 
	 * This defines the device's DMA address range, excluding any bits
	 * reserved for mapping the address to the base_addr.
	 */
	bhnd_addr_t	addr_mask;

	/**
	 * Device-addressable extended address mask.
	 *
	 * If a per-core bhnd(4) DMA engine supports the 'addrext' control
	 * field, it can be used to provide address bits excluded by addr_mask.
	 *
	 * Support for DMA extended address changes – including coordination
	 * with the core providing DMA translation – is handled transparently by
	 * the DMA engine. For example, on PCI(e) Wi-Fi chipsets, the Wi-Fi
	 * core DMA engine will (in effect) update the PCI core's DMA
	 * sbtopcitranslation base address to map the full address prior to
	 * performing a DMA transaction.
	 */
	bhnd_addr_t	addrext_mask;

	/**
	 * Translation flags (see bhnd_dma_translation_flags).
	 */
	uint32_t	flags;
};

#define	BHND_DMA_TRANSLATION_TABLE_END	{ 0, 0, 0, 0 }

#define	BHND_DMA_IS_TRANSLATION_TABLE_END(_dt)			\
	((_dt)->base_addr == 0 && (_dt)->addr_mask == 0 &&	\
	 (_dt)->addrext_mask == 0 && (_dt)->flags == 0)

/**
 * bhnd(4) DMA address translation flags.
 */
enum bhnd_dma_translation_flags {
	/**
	 * The translation remaps the device's physical address space.
	 * 
	 * This is used in conjunction with BHND_DMA_TRANSLATION_BYTESWAPPED to
	 * define a DMA translation that provides byteswapped access to
	 * physical memory on big-endian MIPS SoCs.
	 */
	BHND_DMA_TRANSLATION_PHYSMAP		= (1<<0),

	/**
	 * Provides a byte-swapped mapping; write requests will be byte-swapped
	 * before being written to memory, and read requests will be
	 * byte-swapped before being returned.
	 *
	 * This is primarily used to perform efficient byte swapping of DMA
	 * data on embedded MIPS SoCs executing in big-endian mode.
	 */
	BHND_DMA_TRANSLATION_BYTESWAPPED	= (1<<1),	
};

/**
* A bhnd(4) bus resource.
* 
* This provides an abstract interface to per-core resources that may require
* bus-level remapping of address windows prior to access.
*/
struct bhnd_resource {
	struct resource	*res;		/**< the system resource. */
	bool		 direct;	/**< false if the resource requires
					 *   bus window remapping before it
					 *   is MMIO accessible. */
};

/** Wrap the active resource @p _r in a bhnd_resource structure */
#define	BHND_DIRECT_RESOURCE(_r)	((struct bhnd_resource) {	\
	.res = (_r),							\
	.direct = true,							\
})

/**
 * Device quirk table descriptor.
 */
struct bhnd_device_quirk {
	struct bhnd_device_match desc;		/**< device match descriptor */
	uint32_t		 quirks;	/**< quirk flags */
};

#define	BHND_CORE_QUIRK(_rev, _flags)		\
	{{ BHND_MATCH_CORE_REV(_rev) }, (_flags) }

#define	BHND_CHIP_QUIRK(_chip, _rev, _flags)	\
	{{ BHND_MATCH_CHIP_IR(BCM ## _chip, _rev) }, (_flags) }

#define	BHND_PKG_QUIRK(_chip, _pkg, _flags)	\
	{{ BHND_MATCH_CHIP_IP(BCM ## _chip, BCM ## _chip ## _pkg) }, (_flags) }

#define	BHND_BOARD_QUIRK(_board, _flags)	\
	{{ BHND_MATCH_BOARD_TYPE(_board) },	\
	    (_flags) }

#define	BHND_DEVICE_QUIRK_END		{ { BHND_MATCH_ANY }, 0 }
#define	BHND_DEVICE_QUIRK_IS_END(_q)	\
	(((_q)->desc.m.match_flags == 0) && (_q)->quirks == 0)

enum {
	BHND_DF_ANY	= 0,
	BHND_DF_HOSTB	= (1<<0),	/**< core is serving as the bus' host
					  *  bridge. implies BHND_DF_ADAPTER */
	BHND_DF_SOC	= (1<<1),	/**< core is attached to a native
					     bus (BHND_ATTACH_NATIVE) */
	BHND_DF_ADAPTER	= (1<<2),	/**< core is attached to a bridged
					  *  adapter (BHND_ATTACH_ADAPTER) */
};

/** Device probe table descriptor */
struct bhnd_device {
	const struct bhnd_device_match	 core;		/**< core match descriptor */ 
	const char			*desc;		/**< device description, or NULL. */
	const struct bhnd_device_quirk	*quirks_table;	/**< quirks table for this device, or NULL */
	uint32_t			 device_flags;	/**< required BHND_DF_* flags */
};

#define	_BHND_DEVICE(_vendor, _device, _desc, _quirks,		\
     _flags, ...)						\
	{ { BHND_MATCH_CORE(BHND_MFGID_ ## _vendor,		\
	    BHND_COREID_ ## _device) }, _desc, _quirks,		\
	    _flags }

#define	BHND_DEVICE(_vendor, _device, _desc, _quirks, ...)	\
	_BHND_DEVICE(_vendor, _device, _desc, _quirks,		\
	    ## __VA_ARGS__, 0)

#define	BHND_DEVICE_END		{ { BHND_MATCH_ANY }, NULL, NULL, 0 }
#define	BHND_DEVICE_IS_END(_d)	\
	(BHND_MATCH_IS_ANY(&(_d)->core) && (_d)->desc == NULL)

/**
 * bhnd device sort order.
 */
typedef enum {
	BHND_DEVICE_ORDER_ATTACH,	/**< sort by bhnd(4) device attach order;
					     child devices should be probed/attached
					     in this order */
	BHND_DEVICE_ORDER_DETACH,	/**< sort by bhnd(4) device detach order;
					     child devices should be detached, suspended,
					     and shutdown in this order */
} bhnd_device_order;

/**
 * A registry of bhnd service providers.
 */
struct bhnd_service_registry {
	STAILQ_HEAD(,bhnd_service_entry)	entries;	/**< registered services */
	struct mtx				lock;		/**< state lock */
};

/**
 * bhnd service provider flags.
 */
enum {
	BHND_SPF_INHERITED	= (1<<0),	/**< service provider reference was inherited from
						     a parent bus, and should be deregistered when the
						     last active reference is released */
};

const char			*bhnd_vendor_name(uint16_t vendor);
const char			*bhnd_port_type_name(bhnd_port_type port_type);
const char			*bhnd_nvram_src_name(bhnd_nvram_src nvram_src);

const char 			*bhnd_find_core_name(uint16_t vendor,
				     uint16_t device);
bhnd_devclass_t			 bhnd_find_core_class(uint16_t vendor,
				     uint16_t device);

const char			*bhnd_core_name(const struct bhnd_core_info *ci);
bhnd_devclass_t			 bhnd_core_class(const struct bhnd_core_info *ci);

int				 bhnd_format_chip_id(char *buffer, size_t size,
				     uint16_t chip_id);

device_t			 bhnd_bus_match_child(device_t bus,
				     const struct bhnd_core_match *desc);

device_t			 bhnd_bus_find_child(device_t bus,
				     bhnd_devclass_t class, int unit);

int				 bhnd_bus_get_children(device_t bus,
				     device_t **devlistp, int *devcountp,
				     bhnd_device_order order);

void				 bhnd_bus_free_children(device_t *devlist);

int				 bhnd_bus_probe_children(device_t bus);

int				 bhnd_sort_devices(device_t *devlist,
				     size_t devcount, bhnd_device_order order);

device_t			 bhnd_find_bridge_root(device_t dev,
				     devclass_t bus_class);

const struct bhnd_core_info	*bhnd_match_core(
				     const struct bhnd_core_info *cores,
				     u_int num_cores,
				     const struct bhnd_core_match *desc);

const struct bhnd_core_info	*bhnd_find_core(
				     const struct bhnd_core_info *cores,
				     u_int num_cores, bhnd_devclass_t class);

struct bhnd_core_match		 bhnd_core_get_match_desc(
				     const struct bhnd_core_info *core);

bool				 bhnd_cores_equal(
				     const struct bhnd_core_info *lhs,
				     const struct bhnd_core_info *rhs);

bool				 bhnd_core_matches(
				     const struct bhnd_core_info *core,
				     const struct bhnd_core_match *desc);

bool				 bhnd_chip_matches(
				     const struct bhnd_chipid *chipid,
				     const struct bhnd_chip_match *desc);

bool				 bhnd_board_matches(
				     const struct bhnd_board_info *info,
				     const struct bhnd_board_match *desc);

bool				 bhnd_hwrev_matches(uint16_t hwrev,
				     const struct bhnd_hwrev_match *desc);

bool				 bhnd_device_matches(device_t dev,
				     const struct bhnd_device_match *desc);

const struct bhnd_device	*bhnd_device_lookup(device_t dev,
				     const struct bhnd_device *table,
				     size_t entry_size);

uint32_t			 bhnd_device_quirks(device_t dev,
				     const struct bhnd_device *table,
				     size_t entry_size);

struct bhnd_core_info		 bhnd_get_core_info(device_t dev);

int				 bhnd_alloc_resources(device_t dev,
				     struct resource_spec *rs,
				     struct bhnd_resource **res);

void				 bhnd_release_resources(device_t dev,
				     const struct resource_spec *rs,
				     struct bhnd_resource **res);

void				 bhnd_set_custom_core_desc(device_t dev,
				     const char *name);
void				 bhnd_set_default_core_desc(device_t dev);

void				 bhnd_set_default_bus_desc(device_t dev,
				     const struct bhnd_chipid *chip_id);

int				 bhnd_nvram_getvar_str(device_t dev,
				     const char *name, char *buf, size_t len,
				     size_t *rlen);

int				 bhnd_nvram_getvar_uint(device_t dev,
				     const char *name, void *value, int width);
int				 bhnd_nvram_getvar_uint8(device_t dev,
				     const char *name, uint8_t *value);
int				 bhnd_nvram_getvar_uint16(device_t dev,
				     const char *name, uint16_t *value);
int				 bhnd_nvram_getvar_uint32(device_t dev,
				     const char *name, uint32_t *value);

int				 bhnd_nvram_getvar_int(device_t dev,
				     const char *name, void *value, int width);
int				 bhnd_nvram_getvar_int8(device_t dev,
				     const char *name, int8_t *value);
int				 bhnd_nvram_getvar_int16(device_t dev,
				     const char *name, int16_t *value);
int				 bhnd_nvram_getvar_int32(device_t dev,
				     const char *name, int32_t *value);

int				 bhnd_nvram_getvar_array(device_t dev,
				     const char *name, void *buf, size_t count,
				     bhnd_nvram_type type);

int				 bhnd_service_registry_init(
				     struct bhnd_service_registry *bsr);
int				 bhnd_service_registry_fini(
				     struct bhnd_service_registry *bsr);
int				 bhnd_service_registry_add(
				     struct bhnd_service_registry *bsr,
				     device_t provider,
				     bhnd_service_t service,
				     uint32_t flags);
int				 bhnd_service_registry_remove(
				     struct bhnd_service_registry *bsr,
				     device_t provider,
				     bhnd_service_t service);
device_t			 bhnd_service_registry_retain(
				     struct bhnd_service_registry *bsr,
				     bhnd_service_t service);
bool				 bhnd_service_registry_release(
				     struct bhnd_service_registry *bsr,
				     device_t provider,
				     bhnd_service_t service);

int				 bhnd_bus_generic_register_provider(
				     device_t dev, device_t child,
				     device_t provider, bhnd_service_t service);
int				 bhnd_bus_generic_deregister_provider(
				     device_t dev, device_t child,
				     device_t provider, bhnd_service_t service);
device_t			 bhnd_bus_generic_retain_provider(device_t dev,
				     device_t child, bhnd_service_t service);
void				 bhnd_bus_generic_release_provider(device_t dev,
				     device_t child, device_t provider,
				     bhnd_service_t service);

int				 bhnd_bus_generic_sr_register_provider(
				     device_t dev, device_t child,
				     device_t provider, bhnd_service_t service);
int				 bhnd_bus_generic_sr_deregister_provider(
				     device_t dev, device_t child,
				     device_t provider, bhnd_service_t service);
device_t			 bhnd_bus_generic_sr_retain_provider(device_t dev,
				     device_t child, bhnd_service_t service);
void				 bhnd_bus_generic_sr_release_provider(device_t dev,
				     device_t child, device_t provider,
				     bhnd_service_t service);

bool				 bhnd_bus_generic_is_hw_disabled(device_t dev,
				     device_t child);
bool				 bhnd_bus_generic_is_region_valid(device_t dev,
				     device_t child, bhnd_port_type type,
				     u_int port, u_int region);
int				 bhnd_bus_generic_get_nvram_var(device_t dev,
				     device_t child, const char *name,
				     void *buf, size_t *size,
				     bhnd_nvram_type type);
const struct bhnd_chipid	*bhnd_bus_generic_get_chipid(device_t dev,
				     device_t child);
int				 bhnd_bus_generic_get_dma_translation(
				     device_t dev, device_t child, u_int width,
				     uint32_t flags, bus_dma_tag_t *dmat,
				     struct bhnd_dma_translation *translation);
int				 bhnd_bus_generic_read_board_info(device_t dev,
				     device_t child,
				     struct bhnd_board_info *info);
struct bhnd_resource		*bhnd_bus_generic_alloc_resource (device_t dev,
				     device_t child, int type, int *rid,
				     rman_res_t start, rman_res_t end,
				     rman_res_t count, u_int flags);
int				 bhnd_bus_generic_release_resource (device_t dev,
				     device_t child, int type, int rid,
				     struct bhnd_resource *r);
int				 bhnd_bus_generic_activate_resource (device_t dev,
				     device_t child, int type, int rid,
				     struct bhnd_resource *r);
int				 bhnd_bus_generic_deactivate_resource (device_t dev,
				     device_t child, int type, int rid,
				     struct bhnd_resource *r);
uintptr_t			 bhnd_bus_generic_get_intr_domain(device_t dev,
				     device_t child, bool self);

/**
 * Return the bhnd(4) bus driver's device enumeration parser class
 *
 * @param driver A bhnd bus driver instance.
 */
static inline bhnd_erom_class_t *
bhnd_driver_get_erom_class(driver_t *driver)
{
	return (BHND_BUS_GET_EROM_CLASS(driver));
}

/**
 * Return the active host bridge core for the bhnd bus, if any, or NULL if
 * not found.
 *
 * @param dev A bhnd bus device.
 */
static inline device_t
bhnd_bus_find_hostb_device(device_t dev) {
	return (BHND_BUS_FIND_HOSTB_DEVICE(dev));
}

/**
 * Register a provider for a given @p service.
 *
 * @param dev		The device to register as a service provider
 *			with its parent bus.
 * @param service	The service for which @p dev will be registered.
 *
 * @retval 0		success
 * @retval EEXIST	if an entry for @p service already exists.
 * @retval non-zero	if registering @p dev otherwise fails, a regular
 *			unix error code will be returned.
 */
static inline int
bhnd_register_provider(device_t dev, bhnd_service_t service)
{
	return (BHND_BUS_REGISTER_PROVIDER(device_get_parent(dev), dev, dev,
	    service));
}

 /**
 * Attempt to remove a service provider registration for @p dev.
 *
 * @param dev		The device to be deregistered as a service provider.
 * @param service	The service for which @p dev will be deregistered, or
 *			BHND_SERVICE_INVALID to remove all service registrations
 *			for @p dev.
 *
 * @retval 0		success
 * @retval EBUSY	if active references to @p dev exist; @see
 *			bhnd_retain_provider() and bhnd_release_provider().
 */
static inline int
bhnd_deregister_provider(device_t dev, bhnd_service_t service)
{
	return (BHND_BUS_DEREGISTER_PROVIDER(device_get_parent(dev), dev, dev,
	    service));
}

/**
 * Retain and return a reference to the registered @p service provider, if any.
 *
 * @param dev		The requesting device.
 * @param service	The service for which a provider should be returned.
 *
 * On success, the caller assumes ownership the returned provider, and
 * is responsible for releasing this reference via
 * BHND_BUS_RELEASE_PROVIDER().
 *
 * @retval device_t	success
 * @retval NULL		if no provider is registered for @p service. 
 */
static inline device_t
bhnd_retain_provider(device_t dev, bhnd_service_t service)
{
	return (BHND_BUS_RETAIN_PROVIDER(device_get_parent(dev), dev,
	    service));
}

/**
 * Release a reference to a provider device previously returned by
 * bhnd_retain_provider().
 *
 * @param dev The requesting device.
 * @param provider The provider to be released.
 * @param service The service for which @p provider was previously retained.
 */
static inline void
bhnd_release_provider(device_t dev, device_t provider,
    bhnd_service_t service)
{
	return (BHND_BUS_RELEASE_PROVIDER(device_get_parent(dev), dev,
	    provider, service));
}

/**
 * Return true if the hardware components required by @p dev are known to be
 * unpopulated or otherwise unusable.
 *
 * In some cases, enumerated devices may have pins that are left floating, or
 * the hardware may otherwise be non-functional; this method allows a parent
 * device to explicitly specify if a successfully enumerated @p dev should
 * be disabled.
 *
 * @param dev A bhnd bus child device.
 */
static inline bool
bhnd_is_hw_disabled(device_t dev) {
	return (BHND_BUS_IS_HW_DISABLED(device_get_parent(dev), dev));
}

/**
 * Return the BHND chip identification info for the bhnd bus.
 *
 * @param dev A bhnd bus child device.
 */
static inline const struct bhnd_chipid *
bhnd_get_chipid(device_t dev) {
	return (BHND_BUS_GET_CHIPID(device_get_parent(dev), dev));
};


/**
 * Read the current value of a bhnd(4) device's per-core I/O control register.
 *
 * @param dev The bhnd bus child device to be queried.
 * @param[out] ioctl On success, the I/O control register value.
 *
 * @retval 0 success
 * @retval EINVAL If @p child is not a direct child of @p dev.
 * @retval ENODEV If agent/config space for @p child is unavailable.
 * @retval non-zero If reading the IOCTL register otherwise fails, a regular
 * unix error code will be returned.
 */
static inline int
bhnd_read_ioctl(device_t dev, uint16_t *ioctl)
{
	return (BHND_BUS_READ_IOCTL(device_get_parent(dev), dev, ioctl));
}

/**
 * Write @p value and @p mask to a bhnd(4) device's per-core I/O control
 * register.
 * 
 * @param dev The bhnd bus child device for which the IOCTL register will be
 * written.
 * @param value The value to be written (see BHND_IOCTL_*).
 * @param mask Only the bits defined by @p mask will be updated from @p value.
 *
 * @retval 0 success
 * @retval EINVAL If @p child is not a direct child of @p dev.
 * @retval ENODEV If agent/config space for @p child is unavailable.
 * @retval non-zero If writing the IOCTL register otherwise fails, a regular
 * unix error code will be returned.
 */
static inline int
bhnd_write_ioctl(device_t dev, uint16_t value, uint16_t mask)
{
	return (BHND_BUS_WRITE_IOCTL(device_get_parent(dev), dev, value, mask));
}

/**
 * Read the current value of a bhnd(4) device's per-core I/O status register.
 *
 * @param dev The bhnd bus child device to be queried.
 * @param[out] iost On success, the I/O status register value.
 *
 * @retval 0 success
 * @retval EINVAL If @p child is not a direct child of @p dev.
 * @retval ENODEV If agent/config space for @p child is unavailable.
 * @retval non-zero If reading the IOST register otherwise fails, a regular
 * unix error code will be returned.
 */
static inline int
bhnd_read_iost(device_t dev, uint16_t *iost)
{
	return (BHND_BUS_READ_IOST(device_get_parent(dev), dev, iost));
}

/**
 * Return true if the given bhnd device's hardware is currently held
 * in a RESET state or otherwise not clocked (BHND_IOCTL_CLK_EN).
 * 
 * @param dev The device to query.
 *
 * @retval true If @p dev is held in RESET or not clocked (BHND_IOCTL_CLK_EN),
 * or an error occured determining @p dev's hardware state.
 * @retval false If @p dev is clocked and is not held in RESET.
 */
static inline bool
bhnd_is_hw_suspended(device_t dev)
{
	return (BHND_BUS_IS_HW_SUSPENDED(device_get_parent(dev), dev));
}

/**
 * Place the bhnd(4) device's hardware into a low-power RESET state with
 * the @p reset_ioctl I/O control flags set, and then bring the hardware out of
 * RESET with the @p ioctl I/O control flags set.
 * 
 * Any clock or resource PMU requests previously made by @p child will be
 * invalidated.
 *
 * @param dev The device to be reset.
 * @param ioctl Device-specific I/O control flags to be set when bringing
 * the core out of its RESET state (see BHND_IOCTL_*).
 * @param reset_ioctl Device-specific I/O control flags to be set when placing
 * the core into its RESET state.
 *
 * @retval 0 success
 * @retval non-zero error
 */
static inline int
bhnd_reset_hw(device_t dev, uint16_t ioctl, uint16_t reset_ioctl)
{
	return (BHND_BUS_RESET_HW(device_get_parent(dev), dev, ioctl,
	    reset_ioctl));
}

/**
 * Suspend @p child's hardware in a low-power reset state.
 *
 * Any clock or resource PMU requests previously made by @p dev will be
 * invalidated.
 *
 * The hardware may be brought out of reset via bhnd_reset_hw().
 *
 * @param dev The device to be suspended.
 *
 * @retval 0 success
 * @retval non-zero error
 */
static inline int
bhnd_suspend_hw(device_t dev, uint16_t ioctl)
{
	return (BHND_BUS_SUSPEND_HW(device_get_parent(dev), dev, ioctl));
}

/**
 * Return the BHND attachment type of the parent bhnd bus.
 *
 * @param dev A bhnd bus child device.
 *
 * @retval BHND_ATTACH_ADAPTER if the bus is resident on a bridged adapter,
 * such as a WiFi chipset.
 * @retval BHND_ATTACH_NATIVE if the bus provides hardware services (clock,
 * CPU, etc) to a directly attached native host.
 */
static inline bhnd_attach_type
bhnd_get_attach_type (device_t dev) {
	return (BHND_BUS_GET_ATTACH_TYPE(device_get_parent(dev), dev));
}

/**
 * Find the best available DMA address translation capable of mapping a
 * physical host address to a BHND DMA device address of @p width with
 * @p flags.
 *
 * @param dev A bhnd bus child device.
 * @param width The address width within which the translation window must
 * reside (see BHND_DMA_ADDR_*).
 * @param flags Required translation flags (see BHND_DMA_TRANSLATION_*).
 * @param[out] dmat On success, will be populated with a DMA tag specifying the
 * @p translation DMA address restrictions. This argment may be NULL if the DMA
 * tag is not desired.
 * the set of valid host DMA addresses reachable via @p translation.
 * @param[out] translation On success, will be populated with a DMA address
 * translation descriptor for @p child. This argment may be NULL if the
 * descriptor is not desired.
 *
 * @retval 0 success
 * @retval ENODEV If DMA is not supported.
 * @retval ENOENT If no DMA translation matching @p width and @p flags is
 * available.
 * @retval non-zero If determining the DMA address translation for @p child
 * otherwise fails, a regular unix error code will be returned.
 */
static inline int
bhnd_get_dma_translation(device_t dev, u_int width, uint32_t flags,
    bus_dma_tag_t *dmat, struct bhnd_dma_translation *translation)
{
	return (BHND_BUS_GET_DMA_TRANSLATION(device_get_parent(dev), dev, width,
	    flags, dmat, translation));
}

/**
 * Attempt to read the BHND board identification from the bhnd bus.
 *
 * This relies on NVRAM access, and will fail if a valid NVRAM device cannot
 * be found, or is not yet attached.
 *
 * @param dev The bhnd device requesting board info.
 * @param[out] info On success, will be populated with the bhnd(4) device's
 * board information.
 *
 * @retval 0 success
 * @retval ENODEV	No valid NVRAM source could be found.
 * @retval non-zero	If reading @p name otherwise fails, a regular unix
 *			error code will be returned.
 */
static inline int
bhnd_read_board_info(device_t dev, struct bhnd_board_info *info)
{
	return (BHND_BUS_READ_BOARD_INFO(device_get_parent(dev), dev, info));
}

/**
 * Return the number of interrupt lines assigned to @p dev.
 * 
 * @param dev A bhnd bus child device.
 */
static inline u_int
bhnd_get_intr_count(device_t dev)
{
	return (BHND_BUS_GET_INTR_COUNT(device_get_parent(dev), dev));
}

/**
 * Get the backplane interrupt vector of the @p intr line attached to @p dev.
 * 
 * @param dev A bhnd bus child device.
 * @param intr The index of the interrupt line being queried.
 * @param[out] ivec On success, the assigned hardware interrupt vector will be
 * written to this pointer.
 *
 * On bcma(4) devices, this returns the OOB bus line assigned to the
 * interrupt.
 *
 * On siba(4) devices, this returns the target OCP slave flag number assigned
 * to the interrupt.
 *
 * @retval 0		success
 * @retval ENXIO	If @p intr exceeds the number of interrupt lines
 *			assigned to @p child.
 */
static inline int
bhnd_get_intr_ivec(device_t dev, u_int intr, u_int *ivec)
{
	return (BHND_BUS_GET_INTR_IVEC(device_get_parent(dev), dev, intr,
	    ivec));
}

/**
 * Map the given @p intr to an IRQ number; until unmapped, this IRQ may be used
 * to allocate a resource of type SYS_RES_IRQ.
 * 
 * On success, the caller assumes ownership of the interrupt mapping, and
 * is responsible for releasing the mapping via bhnd_unmap_intr().
 * 
 * @param dev The requesting device.
 * @param intr The interrupt being mapped.
 * @param[out] irq On success, the bus interrupt value mapped for @p intr.
 *
 * @retval 0		If an interrupt was assigned.
 * @retval non-zero	If mapping an interrupt otherwise fails, a regular
 *			unix error code will be returned.
 */
static inline int
bhnd_map_intr(device_t dev, u_int intr, rman_res_t *irq)
{
	return (BHND_BUS_MAP_INTR(device_get_parent(dev), dev, intr, irq));
}

/**
 * Unmap an bus interrupt previously mapped via bhnd_map_intr().
 * 
 * @param dev The requesting device.
 * @param irq The interrupt value being unmapped.
 */
static inline void
bhnd_unmap_intr(device_t dev, rman_res_t irq)
{
	return (BHND_BUS_UNMAP_INTR(device_get_parent(dev), dev, irq));
}

/**
 * Allocate and enable per-core PMU request handling for @p child.
 *
 * The region containing the core's PMU register block (if any) must be
 * allocated via bus_alloc_resource(9) (or bhnd_alloc_resource) before
 * calling bhnd_alloc_pmu(), and must not be released until after
 * calling bhnd_release_pmu().
 *
 * @param dev The requesting bhnd device.
 * 
 * @retval 0           success
 * @retval non-zero    If allocating PMU request state otherwise fails, a
 *                     regular unix error code will be returned.
 */
static inline int
bhnd_alloc_pmu(device_t dev)
{
	return (BHND_BUS_ALLOC_PMU(device_get_parent(dev), dev));
}

/**
 * Release any per-core PMU resources allocated for @p child. Any outstanding
 * PMU requests are are discarded.
 *
 * @param dev The requesting bhnd device.
 * 
 * @retval 0           success
 * @retval non-zero    If releasing PMU request state otherwise fails, a
 *                     regular unix error code will be returned, and
 *                     the core state will be left unmodified.
 */
static inline int
bhnd_release_pmu(device_t dev)
{
	return (BHND_BUS_RELEASE_PMU(device_get_parent(dev), dev));
}

/**
 * Return the transition latency required for @p clock in microseconds, if
 * known.
 *
 * The BHND_CLOCK_HT latency value is suitable for use as the D11 core's
 * 'fastpwrup_dly' value. 
 *
 * @note A driver must ask the bhnd bus to allocate PMU request state
 * via BHND_BUS_ALLOC_PMU() before querying PMU clocks.
 *
 * @param dev The requesting bhnd device.
 * @param clock	The clock to be queried for transition latency.
 * @param[out] latency On success, the transition latency of @p clock in
 * microseconds.
 * 
 * @retval 0		success
 * @retval ENODEV	If the transition latency for @p clock is not available.
 */
static inline int
bhnd_get_clock_latency(device_t dev, bhnd_clock clock, u_int *latency)
{
	return (BHND_BUS_GET_CLOCK_LATENCY(device_get_parent(dev), dev, clock,
	    latency));
}

/**
 * Return the frequency for @p clock in Hz, if known.
 *
 * @param dev The requesting bhnd device.
 * @param clock The clock to be queried.
 * @param[out] freq On success, the frequency of @p clock in Hz.
 *
 * @note A driver must ask the bhnd bus to allocate PMU request state
 * via BHND_BUS_ALLOC_PMU() before querying PMU clocks.
 * 
 * @retval 0		success
 * @retval ENODEV	If the frequency for @p clock is not available.
 */
static inline int
bhnd_get_clock_freq(device_t dev, bhnd_clock clock, u_int *freq)
{
	return (BHND_BUS_GET_CLOCK_FREQ(device_get_parent(dev), dev, clock,
	    freq));
}

/** 
 * Request that @p clock (or faster) be routed to @p dev.
 * 
 * @note A driver must ask the bhnd bus to allocate clock request state
 * via bhnd_alloc_pmu() before it can request clock resources.
 * 
 * @note Any outstanding PMU clock requests will be discarded upon calling
 * BHND_BUS_RESET_HW() or BHND_BUS_SUSPEND_HW().
 *
 * @param dev The bhnd(4) device to which @p clock should be routed.
 * @param clock The requested clock source. 
 *
 * @retval 0 success
 * @retval ENODEV If an unsupported clock was requested.
 * @retval ENXIO If the PMU has not been initialized or is otherwise unvailable,
 */
static inline int
bhnd_request_clock(device_t dev, bhnd_clock clock)
{
	return (BHND_BUS_REQUEST_CLOCK(device_get_parent(dev), dev, clock));
}

/**
 * Request that @p clocks be powered on behalf of @p dev.
 *
 * This will power any clock sources (e.g. XTAL, PLL, etc) required for
 * @p clocks and wait until they are ready, discarding any previous
 * requests by @p dev.
 * 
 * @note A driver must ask the bhnd bus to allocate clock request state
 * via bhnd_alloc_pmu() before it can request clock resources.
 * 
 * @note Any outstanding PMU clock requests will be discarded upon calling
 * BHND_BUS_RESET_HW() or BHND_BUS_SUSPEND_HW().
 * 
 * @param dev The requesting bhnd(4) device.
 * @param clocks The clock(s) to be enabled.
 *
 * @retval 0 success
 * @retval ENODEV If an unsupported clock was requested.
 * @retval ENXIO If the PMU has not been initialized or is otherwise unvailable.
 */
static inline int
bhnd_enable_clocks(device_t dev, uint32_t clocks)
{
	return (BHND_BUS_ENABLE_CLOCKS(device_get_parent(dev), dev, clocks));
}

/**
 * Power up an external PMU-managed resource assigned to @p dev.
 * 
 * @note A driver must ask the bhnd bus to allocate PMU request state
 * via bhnd_alloc_pmu() before it can request PMU resources.
 *
 * @note Any outstanding PMU resource requests will be released upon calling
 * bhnd_reset_hw() or bhnd_suspend_hw().
 *
 * @param dev The requesting bhnd(4) device.
 * @param rsrc The core-specific external resource identifier.
 *
 * @retval 0 success
 * @retval ENODEV If the PMU does not support @p rsrc.
 * @retval ENXIO If the PMU has not been initialized or is otherwise unvailable.
 */
static inline int
bhnd_request_ext_rsrc(device_t dev, u_int rsrc)
{
	return (BHND_BUS_REQUEST_EXT_RSRC(device_get_parent(dev), dev, rsrc));
}

/**
 * Power down an external PMU-managed resource assigned to @p dev.
 * 
 * A driver must ask the bhnd bus to allocate PMU request state
 * via bhnd_alloc_pmu() before it can request PMU resources.
 *
 * @param dev The requesting bhnd(4) device.
 * @param rsrc The core-specific external resource identifier.
 *
 * @retval 0 success
 * @retval ENODEV If the PMU does not support @p rsrc.
 * @retval ENXIO If the PMU has not been initialized or is otherwise unvailable.
 */
static inline int
bhnd_release_ext_rsrc(device_t dev, u_int rsrc)
{
	return (BHND_BUS_RELEASE_EXT_RSRC(device_get_parent(dev), dev, rsrc));
}

/**
 * Read @p width bytes at @p offset from the bus-specific agent/config
 * space of @p dev.
 *
 * @param dev The bhnd device for which @p offset should be read.
 * @param offset The offset to be read.
 * @param[out] value On success, the will be set to the @p width value read
 * at @p offset.
 * @param width The size of the access. Must be 1, 2 or 4 bytes.
 *
 * The exact behavior of this method is bus-specific. In the case of
 * bcma(4), this method provides access to the first agent port of @p child.
 *
 * @note Device drivers should only use this API for functionality
 * that is not available via another bhnd(4) function.
 * 
 * @retval 0 success
 * @retval EINVAL If @p child is not a direct child of @p dev.
 * @retval EINVAL If @p width is not one of 1, 2, or 4 bytes.
 * @retval ENODEV If accessing agent/config space for @p child is unsupported.
 * @retval EFAULT If reading @p width at @p offset exceeds the bounds of
 * the mapped agent/config space  for @p child.
 */
static inline uint32_t
bhnd_read_config(device_t dev, bus_size_t offset, void *value, u_int width)
{
	return (BHND_BUS_READ_CONFIG(device_get_parent(dev), dev, offset,
	    value, width));
}

/**
 * Write @p width bytes at @p offset to the bus-specific agent/config
 * space of @p dev.
 *
 * @param dev The bhnd device for which @p offset should be read.
 * @param offset The offset to be written.
 * @param value A pointer to the value to be written.
 * @param width The size of @p value. Must be 1, 2 or 4 bytes.
 *
 * The exact behavior of this method is bus-specific. In the case of
 * bcma(4), this method provides access to the first agent port of @p child.
 *
 * @note Device drivers should only use this API for functionality
 * that is not available via another bhnd(4) function.
 * 
 * @retval 0 success
 * @retval EINVAL If @p child is not a direct child of @p dev.
 * @retval EINVAL If @p width is not one of 1, 2, or 4 bytes.
 * @retval ENODEV If accessing agent/config space for @p child is unsupported.
 * @retval EFAULT If reading @p width at @p offset exceeds the bounds of
 * the mapped agent/config space  for @p child.
 */
static inline int
bhnd_write_config(device_t dev, bus_size_t offset, const void *value,
    u_int width)
{
	return (BHND_BUS_WRITE_CONFIG(device_get_parent(dev), dev, offset,
	    value, width));
}

/**
 * Read an NVRAM variable, coerced to the requested @p type.
 *
 * @param 		dev	A bhnd bus child device.
 * @param		name	The NVRAM variable name.
 * @param[out]		buf	A buffer large enough to hold @p len bytes. On
 *				success, the requested value will be written to
 *				this buffer. This argment may be NULL if
 *				the value is not desired.
 * @param[in,out]	len	The maximum capacity of @p buf. On success,
 *				will be set to the actual size of the requested
 *				value.
 * @param		type	The desired data representation to be written
 *				to @p buf.
 * 
 * @retval 0		success
 * @retval ENOENT	The requested variable was not found.
 * @retval ENODEV	No valid NVRAM source could be found.
 * @retval ENOMEM	If a buffer of @p size is too small to hold the
 *			requested value.
 * @retval EOPNOTSUPP	If the value cannot be coerced to @p type.
 * @retval ERANGE	If value coercion would overflow @p type.
 * @retval non-zero	If reading @p name otherwise fails, a regular unix
 *			error code will be returned.
 */
static inline int
bhnd_nvram_getvar(device_t dev, const char *name, void *buf, size_t *len,
     bhnd_nvram_type type)
{
	return (BHND_BUS_GET_NVRAM_VAR(device_get_parent(dev), dev, name, buf,
	    len, type));
}

/**
 * Allocate a resource from a device's parent bhnd(4) bus.
 * 
 * @param dev The device requesting resource ownership.
 * @param type The type of resource to allocate. This may be any type supported
 * by the standard bus APIs.
 * @param rid The bus-specific handle identifying the resource being allocated.
 * @param start The start address of the resource.
 * @param end The end address of the resource.
 * @param count The size of the resource.
 * @param flags The flags for the resource to be allocated. These may be any
 * values supported by the standard bus APIs.
 * 
 * To request the resource's default addresses, pass @p start and
 * @p end values of @c 0 and @c ~0, respectively, and
 * a @p count of @c 1.
 * 
 * @retval NULL The resource could not be allocated.
 * @retval resource The allocated resource.
 */
static inline struct bhnd_resource *
bhnd_alloc_resource(device_t dev, int type, int *rid, rman_res_t start,
    rman_res_t end, rman_res_t count, u_int flags)
{
	return BHND_BUS_ALLOC_RESOURCE(device_get_parent(dev), dev, type, rid,
	    start, end, count, flags);
}


/**
 * Allocate a resource from a device's parent bhnd(4) bus, using the
 * resource's default start, end, and count values.
 * 
 * @param dev The device requesting resource ownership.
 * @param type The type of resource to allocate. This may be any type supported
 * by the standard bus APIs.
 * @param rid The bus-specific handle identifying the resource being allocated.
 * @param flags The flags for the resource to be allocated. These may be any
 * values supported by the standard bus APIs.
 * 
 * @retval NULL The resource could not be allocated.
 * @retval resource The allocated resource.
 */
static inline struct bhnd_resource *
bhnd_alloc_resource_any(device_t dev, int type, int *rid, u_int flags)
{
	return bhnd_alloc_resource(dev, type, rid, 0, ~0, 1, flags);
}

/**
 * Activate a previously allocated bhnd resource.
 *
 * @param dev The device holding ownership of the allocated resource.
 * @param type The type of the resource. 
 * @param rid The bus-specific handle identifying the resource.
 * @param r A pointer to the resource returned by bhnd_alloc_resource or
 * BHND_BUS_ALLOC_RESOURCE.
 * 
 * @retval 0 success
 * @retval non-zero an error occurred while activating the resource.
 */
static inline int
bhnd_activate_resource(device_t dev, int type, int rid,
   struct bhnd_resource *r)
{
	return BHND_BUS_ACTIVATE_RESOURCE(device_get_parent(dev), dev, type,
	    rid, r);
}

/**
 * Deactivate a previously activated bhnd resource.
 *
 * @param dev The device holding ownership of the activated resource.
 * @param type The type of the resource. 
 * @param rid The bus-specific handle identifying the resource.
 * @param r A pointer to the resource returned by bhnd_alloc_resource or
 * BHND_BUS_ALLOC_RESOURCE.
 * 
 * @retval 0 success
 * @retval non-zero an error occurred while activating the resource.
 */
static inline int
bhnd_deactivate_resource(device_t dev, int type, int rid,
   struct bhnd_resource *r)
{
	return BHND_BUS_DEACTIVATE_RESOURCE(device_get_parent(dev), dev, type,
	    rid, r);
}

/**
 * Free a resource allocated by bhnd_alloc_resource().
 *
 * @param dev The device holding ownership of the resource.
 * @param type The type of the resource. 
 * @param rid The bus-specific handle identifying the resource.
 * @param r A pointer to the resource returned by bhnd_alloc_resource or
 * BHND_ALLOC_RESOURCE.
 * 
 * @retval 0 success
 * @retval non-zero an error occurred while activating the resource.
 */
static inline int
bhnd_release_resource(device_t dev, int type, int rid,
   struct bhnd_resource *r)
{
	return BHND_BUS_RELEASE_RESOURCE(device_get_parent(dev), dev, type,
	    rid, r);
}

/**
 * Return true if @p region_num is a valid region on @p port_num of
 * @p type attached to @p dev.
 *
 * @param dev A bhnd bus child device.
 * @param type The port type being queried.
 * @param port The port number being queried.
 * @param region The region number being queried.
 */
static inline bool
bhnd_is_region_valid(device_t dev, bhnd_port_type type, u_int port,
    u_int region)
{
	return (BHND_BUS_IS_REGION_VALID(device_get_parent(dev), dev, type,
	    port, region));
}

/**
 * Return the number of ports of type @p type attached to @p def.
 *
 * @param dev A bhnd bus child device.
 * @param type The port type being queried.
 */
static inline u_int
bhnd_get_port_count(device_t dev, bhnd_port_type type) {
	return (BHND_BUS_GET_PORT_COUNT(device_get_parent(dev), dev, type));
}

/**
 * Return the number of memory regions mapped to @p child @p port of
 * type @p type.
 *
 * @param dev A bhnd bus child device.
 * @param port The port number being queried.
 * @param type The port type being queried.
 */
static inline u_int
bhnd_get_region_count(device_t dev, bhnd_port_type type, u_int port) {
	return (BHND_BUS_GET_REGION_COUNT(device_get_parent(dev), dev, type,
	    port));
}

/**
 * Return the resource-ID for a memory region on the given device port.
 *
 * @param dev A bhnd bus child device.
 * @param type The port type.
 * @param port The port identifier.
 * @param region The identifier of the memory region on @p port.
 * 
 * @retval int The RID for the given @p port and @p region on @p device.
 * @retval -1 No such port/region found.
 */
static inline int
bhnd_get_port_rid(device_t dev, bhnd_port_type type, u_int port, u_int region)
{
	return BHND_BUS_GET_PORT_RID(device_get_parent(dev), dev, type, port,
	    region);
}

/**
 * Decode a port / region pair on @p dev defined by @p rid.
 *
 * @param dev A bhnd bus child device.
 * @param type The resource type.
 * @param rid The resource identifier.
 * @param[out] port_type The decoded port type.
 * @param[out] port The decoded port identifier.
 * @param[out] region The decoded region identifier.
 *
 * @retval 0 success
 * @retval non-zero No matching port/region found.
 */
static inline int
bhnd_decode_port_rid(device_t dev, int type, int rid, bhnd_port_type *port_type,
    u_int *port, u_int *region)
{
	return BHND_BUS_DECODE_PORT_RID(device_get_parent(dev), dev, type, rid,
	    port_type, port, region);
}

/**
 * Get the address and size of @p region on @p port.
 *
 * @param dev A bhnd bus child device.
 * @param port_type The port type.
 * @param port The port identifier.
 * @param region The identifier of the memory region on @p port.
 * @param[out] region_addr The region's base address.
 * @param[out] region_size The region's size.
 *
 * @retval 0 success
 * @retval non-zero No matching port/region found.
 */
static inline int
bhnd_get_region_addr(device_t dev, bhnd_port_type port_type, u_int port,
    u_int region, bhnd_addr_t *region_addr, bhnd_size_t *region_size)
{
	return BHND_BUS_GET_REGION_ADDR(device_get_parent(dev), dev, port_type,
	    port, region, region_addr, region_size);
}

/*
 * bhnd bus-level equivalents of the bus_(read|write|set|barrier|...)
 * macros (compatible with bhnd_resource).
 *
 * Generated with bhnd/tools/bus_macro.sh
 */
#define bhnd_bus_barrier(r, o, l, f) \
    (((r)->direct) ? \
	bus_barrier((r)->res, (o), (l), (f)) : \
	BHND_BUS_BARRIER( \
	    device_get_parent(rman_get_device((r)->res)),	\
	    rman_get_device((r)->res), (r), (o), (l), (f)))
#define bhnd_bus_read_1(r, o) \
    (((r)->direct) ? \
	bus_read_1((r)->res, (o)) : \
	BHND_BUS_READ_1( \
	    device_get_parent(rman_get_device((r)->res)),	\
	    rman_get_device((r)->res), (r), (o)))
#define bhnd_bus_read_multi_1(r, o, d, c) \
    (((r)->direct) ? \
	bus_read_multi_1((r)->res, (o), (d), (c)) : \
	BHND_BUS_READ_MULTI_1( \
	    device_get_parent(rman_get_device((r)->res)),	\
	    rman_get_device((r)->res), (r), (o), (d), (c)))
#define bhnd_bus_read_region_1(r, o, d, c) \
    (((r)->direct) ? \
	bus_read_region_1((r)->res, (o), (d), (c)) : \
	BHND_BUS_READ_REGION_1( \
	    device_get_parent(rman_get_device((r)->res)),	\
	    rman_get_device((r)->res), (r), (o), (d), (c)))
#define bhnd_bus_write_1(r, o, v) \
    (((r)->direct) ? \
	bus_write_1((r)->res, (o), (v)) : \
	BHND_BUS_WRITE_1( \
	    device_get_parent(rman_get_device((r)->res)),	\
	    rman_get_device((r)->res), (r), (o), (v)))
#define bhnd_bus_write_multi_1(r, o, d, c) \
    (((r)->direct) ? \
	bus_write_multi_1((r)->res, (o), (d), (c)) : \
	BHND_BUS_WRITE_MULTI_1( \
	    device_get_parent(rman_get_device((r)->res)),	\
	    rman_get_device((r)->res), (r), (o), (d), (c)))
#define bhnd_bus_write_region_1(r, o, d, c) \
    (((r)->direct) ? \
	bus_write_region_1((r)->res, (o), (d), (c)) : \
	BHND_BUS_WRITE_REGION_1( \
	    device_get_parent(rman_get_device((r)->res)),	\
	    rman_get_device((r)->res), (r), (o), (d), (c)))
#define bhnd_bus_read_stream_1(r, o) \
    (((r)->direct) ? \
	bus_read_stream_1((r)->res, (o)) : \
	BHND_BUS_READ_STREAM_1( \
	    device_get_parent(rman_get_device((r)->res)),	\
	    rman_get_device((r)->res), (r), (o)))
#define bhnd_bus_read_multi_stream_1(r, o, d, c) \
    (((r)->direct) ? \
	bus_read_multi_stream_1((r)->res, (o), (d), (c)) : \
	BHND_BUS_READ_MULTI_STREAM_1( \
	    device_get_parent(rman_get_device((r)->res)),	\
	    rman_get_device((r)->res), (r), (o), (d), (c)))
#define bhnd_bus_read_region_stream_1(r, o, d, c) \
    (((r)->direct) ? \
	bus_read_region_stream_1((r)->res, (o), (d), (c)) : \
	BHND_BUS_READ_REGION_STREAM_1( \
	    device_get_parent(rman_get_device((r)->res)),	\
	    rman_get_device((r)->res), (r), (o), (d), (c)))
#define bhnd_bus_write_stream_1(r, o, v) \
    (((r)->direct) ? \
	bus_write_stream_1((r)->res, (o), (v)) : \
	BHND_BUS_WRITE_STREAM_1( \
	    device_get_parent(rman_get_device((r)->res)),	\
	    rman_get_device((r)->res), (r), (o), (v)))
#define bhnd_bus_write_multi_stream_1(r, o, d, c) \
    (((r)->direct) ? \
	bus_write_multi_stream_1((r)->res, (o), (d), (c)) : \
	BHND_BUS_WRITE_MULTI_STREAM_1( \
	    device_get_parent(rman_get_device((r)->res)),	\
	    rman_get_device((r)->res), (r), (o), (d), (c)))
#define bhnd_bus_write_region_stream_1(r, o, d, c) \
    (((r)->direct) ? \
	bus_write_region_stream_1((r)->res, (o), (d), (c)) : \
	BHND_BUS_WRITE_REGION_STREAM_1( \
	    device_get_parent(rman_get_device((r)->res)),	\
	    rman_get_device((r)->res), (r), (o), (d), (c)))
#define bhnd_bus_set_multi_1(r, o, v, c) \
    (((r)->direct) ? \
	bus_set_multi_1((r)->res, (o), (v), (c)) : \
	BHND_BUS_SET_MULTI_1( \
	    device_get_parent(rman_get_device((r)->res)),	\
	    rman_get_device((r)->res), (r), (o), (v), (c)))
#define bhnd_bus_set_region_1(r, o, v, c) \
    (((r)->direct) ? \
	bus_set_region_1((r)->res, (o), (v), (c)) : \
	BHND_BUS_SET_REGION_1( \
	    device_get_parent(rman_get_device((r)->res)),	\
	    rman_get_device((r)->res), (r), (o), (v), (c)))
#define bhnd_bus_read_2(r, o) \
    (((r)->direct) ? \
	bus_read_2((r)->res, (o)) : \
	BHND_BUS_READ_2( \
	    device_get_parent(rman_get_device((r)->res)),	\
	    rman_get_device((r)->res), (r), (o)))
#define bhnd_bus_read_multi_2(r, o, d, c) \
    (((r)->direct) ? \
	bus_read_multi_2((r)->res, (o), (d), (c)) : \
	BHND_BUS_READ_MULTI_2( \
	    device_get_parent(rman_get_device((r)->res)),	\
	    rman_get_device((r)->res), (r), (o), (d), (c)))
#define bhnd_bus_read_region_2(r, o, d, c) \
    (((r)->direct) ? \
	bus_read_region_2((r)->res, (o), (d), (c)) : \
	BHND_BUS_READ_REGION_2( \
	    device_get_parent(rman_get_device((r)->res)),	\
	    rman_get_device((r)->res), (r), (o), (d), (c)))
#define bhnd_bus_write_2(r, o, v) \
    (((r)->direct) ? \
	bus_write_2((r)->res, (o), (v)) : \
	BHND_BUS_WRITE_2( \
	    device_get_parent(rman_get_device((r)->res)),	\
	    rman_get_device((r)->res), (r), (o), (v)))
#define bhnd_bus_write_multi_2(r, o, d, c) \
    (((r)->direct) ? \
	bus_write_multi_2((r)->res, (o), (d), (c)) : \
	BHND_BUS_WRITE_MULTI_2( \
	    device_get_parent(rman_get_device((r)->res)),	\
	    rman_get_device((r)->res), (r), (o), (d), (c)))
#define bhnd_bus_write_region_2(r, o, d, c) \
    (((r)->direct) ? \
	bus_write_region_2((r)->res, (o), (d), (c)) : \
	BHND_BUS_WRITE_REGION_2( \
	    device_get_parent(rman_get_device((r)->res)),	\
	    rman_get_device((r)->res), (r), (o), (d), (c)))
#define bhnd_bus_read_stream_2(r, o) \
    (((r)->direct) ? \
	bus_read_stream_2((r)->res, (o)) : \
	BHND_BUS_READ_STREAM_2( \
	    device_get_parent(rman_get_device((r)->res)),	\
	    rman_get_device((r)->res), (r), (o)))
#define bhnd_bus_read_multi_stream_2(r, o, d, c) \
    (((r)->direct) ? \
	bus_read_multi_stream_2((r)->res, (o), (d), (c)) : \
	BHND_BUS_READ_MULTI_STREAM_2( \
	    device_get_parent(rman_get_device((r)->res)),	\
	    rman_get_device((r)->res), (r), (o), (d), (c)))
#define bhnd_bus_read_region_stream_2(r, o, d, c) \
    (((r)->direct) ? \
	bus_read_region_stream_2((r)->res, (o), (d), (c)) : \
	BHND_BUS_READ_REGION_STREAM_2( \
	    device_get_parent(rman_get_device((r)->res)),	\
	    rman_get_device((r)->res), (r), (o), (d), (c)))
#define bhnd_bus_write_stream_2(r, o, v) \
    (((r)->direct) ? \
	bus_write_stream_2((r)->res, (o), (v)) : \
	BHND_BUS_WRITE_STREAM_2( \
	    device_get_parent(rman_get_device((r)->res)),	\
	    rman_get_device((r)->res), (r), (o), (v)))
#define bhnd_bus_write_multi_stream_2(r, o, d, c) \
    (((r)->direct) ? \
	bus_write_multi_stream_2((r)->res, (o), (d), (c)) : \
	BHND_BUS_WRITE_MULTI_STREAM_2( \
	    device_get_parent(rman_get_device((r)->res)),	\
	    rman_get_device((r)->res), (r), (o), (d), (c)))
#define bhnd_bus_write_region_stream_2(r, o, d, c) \
    (((r)->direct) ? \
	bus_write_region_stream_2((r)->res, (o), (d), (c)) : \
	BHND_BUS_WRITE_REGION_STREAM_2( \
	    device_get_parent(rman_get_device((r)->res)),	\
	    rman_get_device((r)->res), (r), (o), (d), (c)))
#define bhnd_bus_set_multi_2(r, o, v, c) \
    (((r)->direct) ? \
	bus_set_multi_2((r)->res, (o), (v), (c)) : \
	BHND_BUS_SET_MULTI_2( \
	    device_get_parent(rman_get_device((r)->res)),	\
	    rman_get_device((r)->res), (r), (o), (v), (c)))
#define bhnd_bus_set_region_2(r, o, v, c) \
    (((r)->direct) ? \
	bus_set_region_2((r)->res, (o), (v), (c)) : \
	BHND_BUS_SET_REGION_2( \
	    device_get_parent(rman_get_device((r)->res)),	\
	    rman_get_device((r)->res), (r), (o), (v), (c)))
#define bhnd_bus_read_4(r, o) \
    (((r)->direct) ? \
	bus_read_4((r)->res, (o)) : \
	BHND_BUS_READ_4( \
	    device_get_parent(rman_get_device((r)->res)),	\
	    rman_get_device((r)->res), (r), (o)))
#define bhnd_bus_read_multi_4(r, o, d, c) \
    (((r)->direct) ? \
	bus_read_multi_4((r)->res, (o), (d), (c)) : \
	BHND_BUS_READ_MULTI_4( \
	    device_get_parent(rman_get_device((r)->res)),	\
	    rman_get_device((r)->res), (r), (o), (d), (c)))
#define bhnd_bus_read_region_4(r, o, d, c) \
    (((r)->direct) ? \
	bus_read_region_4((r)->res, (o), (d), (c)) : \
	BHND_BUS_READ_REGION_4( \
	    device_get_parent(rman_get_device((r)->res)),	\
	    rman_get_device((r)->res), (r), (o), (d), (c)))
#define bhnd_bus_write_4(r, o, v) \
    (((r)->direct) ? \
	bus_write_4((r)->res, (o), (v)) : \
	BHND_BUS_WRITE_4( \
	    device_get_parent(rman_get_device((r)->res)),	\
	    rman_get_device((r)->res), (r), (o), (v)))
#define bhnd_bus_write_multi_4(r, o, d, c) \
    (((r)->direct) ? \
	bus_write_multi_4((r)->res, (o), (d), (c)) : \
	BHND_BUS_WRITE_MULTI_4( \
	    device_get_parent(rman_get_device((r)->res)),	\
	    rman_get_device((r)->res), (r), (o), (d), (c)))
#define bhnd_bus_write_region_4(r, o, d, c) \
    (((r)->direct) ? \
	bus_write_region_4((r)->res, (o), (d), (c)) : \
	BHND_BUS_WRITE_REGION_4( \
	    device_get_parent(rman_get_device((r)->res)),	\
	    rman_get_device((r)->res), (r), (o), (d), (c)))
#define bhnd_bus_read_stream_4(r, o) \
    (((r)->direct) ? \
	bus_read_stream_4((r)->res, (o)) : \
	BHND_BUS_READ_STREAM_4( \
	    device_get_parent(rman_get_device((r)->res)),	\
	    rman_get_device((r)->res), (r), (o)))
#define bhnd_bus_read_multi_stream_4(r, o, d, c) \
    (((r)->direct) ? \
	bus_read_multi_stream_4((r)->res, (o), (d), (c)) : \
	BHND_BUS_READ_MULTI_STREAM_4( \
	    device_get_parent(rman_get_device((r)->res)),	\
	    rman_get_device((r)->res), (r), (o), (d), (c)))
#define bhnd_bus_read_region_stream_4(r, o, d, c) \
    (((r)->direct) ? \
	bus_read_region_stream_4((r)->res, (o), (d), (c)) : \
	BHND_BUS_READ_REGION_STREAM_4( \
	    device_get_parent(rman_get_device((r)->res)),	\
	    rman_get_device((r)->res), (r), (o), (d), (c)))
#define bhnd_bus_write_stream_4(r, o, v) \
    (((r)->direct) ? \
	bus_write_stream_4((r)->res, (o), (v)) : \
	BHND_BUS_WRITE_STREAM_4( \
	    device_get_parent(rman_get_device((r)->res)),	\
	    rman_get_device((r)->res), (r), (o), (v)))
#define bhnd_bus_write_multi_stream_4(r, o, d, c) \
    (((r)->direct) ? \
	bus_write_multi_stream_4((r)->res, (o), (d), (c)) : \
	BHND_BUS_WRITE_MULTI_STREAM_4( \
	    device_get_parent(rman_get_device((r)->res)),	\
	    rman_get_device((r)->res), (r), (o), (d), (c)))
#define bhnd_bus_write_region_stream_4(r, o, d, c) \
    (((r)->direct) ? \
	bus_write_region_stream_4((r)->res, (o), (d), (c)) : \
	BHND_BUS_WRITE_REGION_STREAM_4( \
	    device_get_parent(rman_get_device((r)->res)),	\
	    rman_get_device((r)->res), (r), (o), (d), (c)))
#define bhnd_bus_set_multi_4(r, o, v, c) \
    (((r)->direct) ? \
	bus_set_multi_4((r)->res, (o), (v), (c)) : \
	BHND_BUS_SET_MULTI_4( \
	    device_get_parent(rman_get_device((r)->res)),	\
	    rman_get_device((r)->res), (r), (o), (v), (c)))
#define bhnd_bus_set_region_4(r, o, v, c) \
    (((r)->direct) ? \
	bus_set_region_4((r)->res, (o), (v), (c)) : \
	BHND_BUS_SET_REGION_4( \
	    device_get_parent(rman_get_device((r)->res)),	\
	    rman_get_device((r)->res), (r), (o), (v), (c)))

#endif /* _BHND_BHND_H_ */
