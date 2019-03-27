/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997, Stefan Esser <se@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 */

#ifndef _PCIVAR_H_
#define	_PCIVAR_H_

#include <sys/queue.h>
#include <sys/eventhandler.h>

/* some PCI bus constants */
#define	PCI_MAXMAPS_0	6	/* max. no. of memory/port maps */
#define	PCI_MAXMAPS_1	2	/* max. no. of maps for PCI to PCI bridge */
#define	PCI_MAXMAPS_2	1	/* max. no. of maps for CardBus bridge */

typedef uint64_t pci_addr_t;

/* Config registers for PCI-PCI and PCI-Cardbus bridges. */
struct pcicfg_bridge {
    uint8_t	br_seclat;
    uint8_t	br_subbus;
    uint8_t	br_secbus;
    uint8_t	br_pribus;
    uint16_t	br_control;
};

/* Interesting values for PCI power management */
struct pcicfg_pp {
    uint16_t	pp_cap;		/* PCI power management capabilities */
    uint8_t	pp_status;	/* conf. space addr. of PM control/status reg */
    uint8_t	pp_bse;		/* conf. space addr. of PM BSE reg */
    uint8_t	pp_data;	/* conf. space addr. of PM data reg */
};

struct pci_map {
    pci_addr_t	pm_value;	/* Raw BAR value */
    pci_addr_t	pm_size;
    uint16_t	pm_reg;
    STAILQ_ENTRY(pci_map) pm_link;
};

struct vpd_readonly {
    char	keyword[2];
    char	*value;
    int		len;
};

struct vpd_write {
    char	keyword[2];
    char	*value;
    int 	start;
    int 	len;
};

struct pcicfg_vpd {
    uint8_t	vpd_reg;	/* base register, + 2 for addr, + 4 data */
    char	vpd_cached;
    char	*vpd_ident;	/* string identifier */
    int 	vpd_rocnt;
    struct vpd_readonly *vpd_ros;
    int 	vpd_wcnt;
    struct vpd_write *vpd_w;
};

/* Interesting values for PCI MSI */
struct pcicfg_msi {
    uint16_t	msi_ctrl;	/* Message Control */
    uint8_t	msi_location;	/* Offset of MSI capability registers. */
    uint8_t	msi_msgnum;	/* Number of messages */
    int		msi_alloc;	/* Number of allocated messages. */
    uint64_t	msi_addr;	/* Contents of address register. */
    uint16_t	msi_data;	/* Contents of data register. */
    u_int	msi_handlers;
};

/* Interesting values for PCI MSI-X */
struct msix_vector {
    uint64_t	mv_address;	/* Contents of address register. */
    uint32_t	mv_data;	/* Contents of data register. */
    int		mv_irq;
};

struct msix_table_entry {
    u_int	mte_vector;	/* 1-based index into msix_vectors array. */
    u_int	mte_handlers;
};

struct pcicfg_msix {
    uint16_t	msix_ctrl;	/* Message Control */
    uint16_t	msix_msgnum;	/* Number of messages */
    uint8_t	msix_location;	/* Offset of MSI-X capability registers. */
    uint8_t	msix_table_bar;	/* BAR containing vector table. */
    uint8_t	msix_pba_bar;	/* BAR containing PBA. */
    uint32_t	msix_table_offset;
    uint32_t	msix_pba_offset;
    int		msix_alloc;	/* Number of allocated vectors. */
    int		msix_table_len;	/* Length of virtual table. */
    struct msix_table_entry *msix_table; /* Virtual table. */
    struct msix_vector *msix_vectors;	/* Array of allocated vectors. */
    struct resource *msix_table_res;	/* Resource containing vector table. */
    struct resource *msix_pba_res;	/* Resource containing PBA. */
};

/* Interesting values for HyperTransport */
struct pcicfg_ht {
    uint8_t	ht_slave;	/* Non-zero if device is an HT slave. */
    uint8_t	ht_msimap;	/* Offset of MSI mapping cap registers. */
    uint16_t	ht_msictrl;	/* MSI mapping control */
    uint64_t	ht_msiaddr;	/* MSI mapping base address */
};

/* Interesting values for PCI-express */
struct pcicfg_pcie {
    uint8_t	pcie_location;	/* Offset of PCI-e capability registers. */
    uint8_t	pcie_type;	/* Device type. */
    uint16_t	pcie_flags;	/* Device capabilities register. */
    uint16_t	pcie_device_ctl; /* Device control register. */
    uint16_t	pcie_link_ctl;	/* Link control register. */
    uint16_t	pcie_slot_ctl;	/* Slot control register. */
    uint16_t	pcie_root_ctl;	/* Root control register. */
    uint16_t	pcie_device_ctl2; /* Second device control register. */
    uint16_t	pcie_link_ctl2;	/* Second link control register. */
    uint16_t	pcie_slot_ctl2;	/* Second slot control register. */
};

struct pcicfg_pcix {
    uint16_t	pcix_command;
    uint8_t	pcix_location;	/* Offset of PCI-X capability registers. */
};

struct pcicfg_vf {
       int index;
};

struct pci_ea_entry {
    int		eae_bei;
    uint32_t	eae_flags;
    uint64_t	eae_base;
    uint64_t	eae_max_offset;
    uint32_t	eae_cfg_offset;
    STAILQ_ENTRY(pci_ea_entry) eae_link;
};

struct pcicfg_ea {
    int ea_location;	/* Structure offset in Configuration Header */
    STAILQ_HEAD(, pci_ea_entry) ea_entries;	/* EA entries */
};

#define	PCICFG_VF	0x0001 /* Device is an SR-IOV Virtual Function */

/* config header information common to all header types */
typedef struct pcicfg {
    device_t	dev;		/* device which owns this */

    STAILQ_HEAD(, pci_map) maps; /* BARs */

    uint16_t	subvendor;	/* card vendor ID */
    uint16_t	subdevice;	/* card device ID, assigned by card vendor */
    uint16_t	vendor;		/* chip vendor ID */
    uint16_t	device;		/* chip device ID, assigned by chip vendor */

    uint16_t	cmdreg;		/* disable/enable chip and PCI options */
    uint16_t	statreg;	/* supported PCI features and error state */

    uint8_t	baseclass;	/* chip PCI class */
    uint8_t	subclass;	/* chip PCI subclass */
    uint8_t	progif;		/* chip PCI programming interface */
    uint8_t	revid;		/* chip revision ID */

    uint8_t	hdrtype;	/* chip config header type */
    uint8_t	cachelnsz;	/* cache line size in 4byte units */
    uint8_t	intpin;		/* PCI interrupt pin */
    uint8_t	intline;	/* interrupt line (IRQ for PC arch) */

    uint8_t	mingnt;		/* min. useful bus grant time in 250ns units */
    uint8_t	maxlat;		/* max. tolerated bus grant latency in 250ns */
    uint8_t	lattimer;	/* latency timer in units of 30ns bus cycles */

    uint8_t	mfdev;		/* multi-function device (from hdrtype reg) */
    uint8_t	nummaps;	/* actual number of PCI maps used */

    uint32_t	domain;		/* PCI domain */
    uint8_t	bus;		/* config space bus address */
    uint8_t	slot;		/* config space slot address */
    uint8_t	func;		/* config space function number */

    uint32_t	flags;		/* flags defined above */

    struct pcicfg_bridge bridge; /* Bridges */
    struct pcicfg_pp pp;	/* Power management */
    struct pcicfg_vpd vpd;	/* Vital product data */
    struct pcicfg_msi msi;	/* PCI MSI */
    struct pcicfg_msix msix;	/* PCI MSI-X */
    struct pcicfg_ht ht;	/* HyperTransport */
    struct pcicfg_pcie pcie;	/* PCI Express */
    struct pcicfg_pcix pcix;	/* PCI-X */
    struct pcicfg_iov *iov;	/* SR-IOV */
    struct pcicfg_vf vf;	/* SR-IOV Virtual Function */
    struct pcicfg_ea ea;	/* Enhanced Allocation */
} pcicfgregs;

/* additional type 1 device config header information (PCI to PCI bridge) */

typedef struct {
    pci_addr_t	pmembase;	/* base address of prefetchable memory */
    pci_addr_t	pmemlimit;	/* topmost address of prefetchable memory */
    uint32_t	membase;	/* base address of memory window */
    uint32_t	memlimit;	/* topmost address of memory window */
    uint32_t	iobase;		/* base address of port window */
    uint32_t	iolimit;	/* topmost address of port window */
    uint16_t	secstat;	/* secondary bus status register */
    uint16_t	bridgectl;	/* bridge control register */
    uint8_t	seclat;		/* CardBus latency timer */
} pcih1cfgregs;

/* additional type 2 device config header information (CardBus bridge) */

typedef struct {
    uint32_t	membase0;	/* base address of memory window */
    uint32_t	memlimit0;	/* topmost address of memory window */
    uint32_t	membase1;	/* base address of memory window */
    uint32_t	memlimit1;	/* topmost address of memory window */
    uint32_t	iobase0;	/* base address of port window */
    uint32_t	iolimit0;	/* topmost address of port window */
    uint32_t	iobase1;	/* base address of port window */
    uint32_t	iolimit1;	/* topmost address of port window */
    uint32_t	pccardif;	/* PC Card 16bit IF legacy more base addr. */
    uint16_t	secstat;	/* secondary bus status register */
    uint16_t	bridgectl;	/* bridge control register */
    uint8_t	seclat;		/* CardBus latency timer */
} pcih2cfgregs;

extern uint32_t pci_numdevs;

/*
 * The bitfield has to be stable and match the fields below (so that
 * match_flag_vendor must be bit 0) so we have to do the endian dance. We can't
 * use enums or #define constants because then the macros for subsetting matches
 * wouldn't work. These tables are parsed by devmatch and others to connect
 * modules with devices on the PCI bus.
 */
struct pci_device_table {
#if BYTE_ORDER == LITTLE_ENDIAN
	uint16_t
		match_flag_vendor:1,
		match_flag_device:1,
		match_flag_subvendor:1,
		match_flag_subdevice:1,
		match_flag_class:1,
		match_flag_subclass:1,
		match_flag_revid:1,
		match_flag_unused:9;
#else
	uint16_t
		match_flag_unused:9,
		match_flag_revid:1,
		match_flag_subclass:1,
		match_flag_class:1,
		match_flag_subdevice:1,
		match_flag_subvendor:1,
		match_flag_device:1,
		match_flag_vendor:1;
#endif
	uint16_t	vendor;
	uint16_t	device;
	uint16_t	subvendor;
	uint16_t	subdevice;
	uint16_t	class_id;
	uint16_t	subclass;
	uint16_t	revid;
	uint16_t	unused;
	uintptr_t	driver_data;
	char		*descr;
};

#define	PCI_DEV(v, d)							\
	.match_flag_vendor = 1, .vendor = (v),				\
	.match_flag_device = 1, .device = (d)
#define	PCI_SUBDEV(sv, sd)						\
	.match_flag_subvendor = 1, .subvendor = (sv),			\
	.match_flag_subdevice = 1, .subdevice = (sd)
#define	PCI_CLASS(x)							\
	.match_flag_class = 1, .class_id = (x)
#define	PCI_SUBCLASS(x)							\
	.match_flag_subclass = 1, .subclass = (x)
#define	PCI_REVID(x)							\
	.match_flag_revid = 1, .revid = (x)
#define	PCI_DESCR(x)							\
	.descr = (x)
#define PCI_PNP_STR							\
	"M16:mask;U16:vendor;U16:device;U16:subvendor;U16:subdevice;"	\
	"U16:class;U16:subclass;U16:revid;"
#define PCI_PNP_INFO(table)						\
	MODULE_PNP_INFO(PCI_PNP_STR, pci, table, table,			\
	    sizeof(table) / sizeof(table[0]))

const struct pci_device_table *pci_match_device(device_t child,
    const struct pci_device_table *id, size_t nelt);
#define PCI_MATCH(child, table) \
	pci_match_device(child, (table), nitems(table));

/* Only if the prerequisites are present */
#if defined(_SYS_BUS_H_) && defined(_SYS_PCIIO_H_)
struct pci_devinfo {
        STAILQ_ENTRY(pci_devinfo) pci_links;
	struct resource_list resources;
	pcicfgregs		cfg;
	struct pci_conf		conf;
};
#endif

#ifdef _SYS_BUS_H_

#include "pci_if.h"

enum pci_device_ivars {
    PCI_IVAR_SUBVENDOR,
    PCI_IVAR_SUBDEVICE,
    PCI_IVAR_VENDOR,
    PCI_IVAR_DEVICE,
    PCI_IVAR_DEVID,
    PCI_IVAR_CLASS,
    PCI_IVAR_SUBCLASS,
    PCI_IVAR_PROGIF,
    PCI_IVAR_REVID,
    PCI_IVAR_INTPIN,
    PCI_IVAR_IRQ,
    PCI_IVAR_DOMAIN,
    PCI_IVAR_BUS,
    PCI_IVAR_SLOT,
    PCI_IVAR_FUNCTION,
    PCI_IVAR_ETHADDR,
    PCI_IVAR_CMDREG,
    PCI_IVAR_CACHELNSZ,
    PCI_IVAR_MINGNT,
    PCI_IVAR_MAXLAT,
    PCI_IVAR_LATTIMER
};

/*
 * Simplified accessors for pci devices
 */
#define	PCI_ACCESSOR(var, ivar, type)					\
	__BUS_ACCESSOR(pci, var, PCI, ivar, type)

PCI_ACCESSOR(subvendor,		SUBVENDOR,	uint16_t)
PCI_ACCESSOR(subdevice,		SUBDEVICE,	uint16_t)
PCI_ACCESSOR(vendor,		VENDOR,		uint16_t)
PCI_ACCESSOR(device,		DEVICE,		uint16_t)
PCI_ACCESSOR(devid,		DEVID,		uint32_t)
PCI_ACCESSOR(class,		CLASS,		uint8_t)
PCI_ACCESSOR(subclass,		SUBCLASS,	uint8_t)
PCI_ACCESSOR(progif,		PROGIF,		uint8_t)
PCI_ACCESSOR(revid,		REVID,		uint8_t)
PCI_ACCESSOR(intpin,		INTPIN,		uint8_t)
PCI_ACCESSOR(irq,		IRQ,		uint8_t)
PCI_ACCESSOR(domain,		DOMAIN,		uint32_t)
PCI_ACCESSOR(bus,		BUS,		uint8_t)
PCI_ACCESSOR(slot,		SLOT,		uint8_t)
PCI_ACCESSOR(function,		FUNCTION,	uint8_t)
PCI_ACCESSOR(ether,		ETHADDR,	uint8_t *)
PCI_ACCESSOR(cmdreg,		CMDREG,		uint8_t)
PCI_ACCESSOR(cachelnsz,		CACHELNSZ,	uint8_t)
PCI_ACCESSOR(mingnt,		MINGNT,		uint8_t)
PCI_ACCESSOR(maxlat,		MAXLAT,		uint8_t)
PCI_ACCESSOR(lattimer,		LATTIMER,	uint8_t)

#undef PCI_ACCESSOR

/*
 * Operations on configuration space.
 */
static __inline uint32_t
pci_read_config(device_t dev, int reg, int width)
{
    return PCI_READ_CONFIG(device_get_parent(dev), dev, reg, width);
}

static __inline void
pci_write_config(device_t dev, int reg, uint32_t val, int width)
{
    PCI_WRITE_CONFIG(device_get_parent(dev), dev, reg, val, width);
}

/*
 * Ivars for pci bridges.
 */

/*typedef enum pci_device_ivars pcib_device_ivars;*/
enum pcib_device_ivars {
	PCIB_IVAR_DOMAIN,
	PCIB_IVAR_BUS
};

#define	PCIB_ACCESSOR(var, ivar, type)					 \
    __BUS_ACCESSOR(pcib, var, PCIB, ivar, type)

PCIB_ACCESSOR(domain,		DOMAIN,		uint32_t)
PCIB_ACCESSOR(bus,		BUS,		uint32_t)

#undef PCIB_ACCESSOR

/*
 * PCI interrupt validation.  Invalid interrupt values such as 0 or 128
 * on i386 or other platforms should be mapped out in the MD pcireadconf
 * code and not here, since the only MI invalid IRQ is 255.
 */
#define	PCI_INVALID_IRQ		255
#define	PCI_INTERRUPT_VALID(x)	((x) != PCI_INVALID_IRQ)

/*
 * Convenience functions.
 *
 * These should be used in preference to manually manipulating
 * configuration space.
 */
static __inline int
pci_enable_busmaster(device_t dev)
{
    return(PCI_ENABLE_BUSMASTER(device_get_parent(dev), dev));
}

static __inline int
pci_disable_busmaster(device_t dev)
{
    return(PCI_DISABLE_BUSMASTER(device_get_parent(dev), dev));
}

static __inline int
pci_enable_io(device_t dev, int space)
{
    return(PCI_ENABLE_IO(device_get_parent(dev), dev, space));
}

static __inline int
pci_disable_io(device_t dev, int space)
{
    return(PCI_DISABLE_IO(device_get_parent(dev), dev, space));
}

static __inline int
pci_get_vpd_ident(device_t dev, const char **identptr)
{
    return(PCI_GET_VPD_IDENT(device_get_parent(dev), dev, identptr));
}

static __inline int
pci_get_vpd_readonly(device_t dev, const char *kw, const char **vptr)
{
    return(PCI_GET_VPD_READONLY(device_get_parent(dev), dev, kw, vptr));
}

/*
 * Check if the address range falls within the VGA defined address range(s)
 */
static __inline int
pci_is_vga_ioport_range(rman_res_t start, rman_res_t end)
{

	return (((start >= 0x3b0 && end <= 0x3bb) ||
	    (start >= 0x3c0 && end <= 0x3df)) ? 1 : 0);
}

static __inline int
pci_is_vga_memory_range(rman_res_t start, rman_res_t end)
{

	return ((start >= 0xa0000 && end <= 0xbffff) ? 1 : 0);
}

/*
 * PCI power states are as defined by ACPI:
 *
 * D0	State in which device is on and running.  It is receiving full
 *	power from the system and delivering full functionality to the user.
 * D1	Class-specific low-power state in which device context may or may not
 *	be lost.  Buses in D1 cannot do anything to the bus that would force
 *	devices on that bus to lose context.
 * D2	Class-specific low-power state in which device context may or may
 *	not be lost.  Attains greater power savings than D1.  Buses in D2
 *	can cause devices on that bus to lose some context.  Devices in D2
 *	must be prepared for the bus to be in D2 or higher.
 * D3	State in which the device is off and not running.  Device context is
 *	lost.  Power can be removed from the device.
 */
#define	PCI_POWERSTATE_D0	0
#define	PCI_POWERSTATE_D1	1
#define	PCI_POWERSTATE_D2	2
#define	PCI_POWERSTATE_D3	3
#define	PCI_POWERSTATE_UNKNOWN	-1

static __inline int
pci_set_powerstate(device_t dev, int state)
{
    return PCI_SET_POWERSTATE(device_get_parent(dev), dev, state);
}

static __inline int
pci_get_powerstate(device_t dev)
{
    return PCI_GET_POWERSTATE(device_get_parent(dev), dev);
}

static __inline int
pci_find_cap(device_t dev, int capability, int *capreg)
{
    return (PCI_FIND_CAP(device_get_parent(dev), dev, capability, capreg));
}

static __inline int
pci_find_next_cap(device_t dev, int capability, int start, int *capreg)
{
    return (PCI_FIND_NEXT_CAP(device_get_parent(dev), dev, capability, start,
        capreg));
}

static __inline int
pci_find_extcap(device_t dev, int capability, int *capreg)
{
    return (PCI_FIND_EXTCAP(device_get_parent(dev), dev, capability, capreg));
}

static __inline int
pci_find_next_extcap(device_t dev, int capability, int start, int *capreg)
{
    return (PCI_FIND_NEXT_EXTCAP(device_get_parent(dev), dev, capability,
        start, capreg));
}

static __inline int
pci_find_htcap(device_t dev, int capability, int *capreg)
{
    return (PCI_FIND_HTCAP(device_get_parent(dev), dev, capability, capreg));
}

static __inline int
pci_find_next_htcap(device_t dev, int capability, int start, int *capreg)
{
    return (PCI_FIND_NEXT_HTCAP(device_get_parent(dev), dev, capability,
        start, capreg));
}

static __inline int
pci_alloc_msi(device_t dev, int *count)
{
    return (PCI_ALLOC_MSI(device_get_parent(dev), dev, count));
}

static __inline int
pci_alloc_msix(device_t dev, int *count)
{
    return (PCI_ALLOC_MSIX(device_get_parent(dev), dev, count));
}

static __inline void
pci_enable_msi(device_t dev, uint64_t address, uint16_t data)
{
    PCI_ENABLE_MSI(device_get_parent(dev), dev, address, data);
}

static __inline void
pci_enable_msix(device_t dev, u_int index, uint64_t address, uint32_t data)
{
    PCI_ENABLE_MSIX(device_get_parent(dev), dev, index, address, data);
}

static __inline void
pci_disable_msi(device_t dev)
{
    PCI_DISABLE_MSI(device_get_parent(dev), dev);
}

static __inline int
pci_remap_msix(device_t dev, int count, const u_int *vectors)
{
    return (PCI_REMAP_MSIX(device_get_parent(dev), dev, count, vectors));
}

static __inline int
pci_release_msi(device_t dev)
{
    return (PCI_RELEASE_MSI(device_get_parent(dev), dev));
}

static __inline int
pci_msi_count(device_t dev)
{
    return (PCI_MSI_COUNT(device_get_parent(dev), dev));
}

static __inline int
pci_msix_count(device_t dev)
{
    return (PCI_MSIX_COUNT(device_get_parent(dev), dev));
}

static __inline int
pci_msix_pba_bar(device_t dev)
{
    return (PCI_MSIX_PBA_BAR(device_get_parent(dev), dev));
}

static __inline int
pci_msix_table_bar(device_t dev)
{
    return (PCI_MSIX_TABLE_BAR(device_get_parent(dev), dev));
}

static __inline int
pci_get_id(device_t dev, enum pci_id_type type, uintptr_t *id)
{
    return (PCI_GET_ID(device_get_parent(dev), dev, type, id));
}

/*
 * This is the deprecated interface, there is no way to tell the difference
 * between a failure and a valid value that happens to be the same as the
 * failure value.
 */
static __inline uint16_t
pci_get_rid(device_t dev)
{
    uintptr_t rid;

    if (pci_get_id(dev, PCI_ID_RID, &rid) != 0)
        return (0);

    return (rid);
}

static __inline void
pci_child_added(device_t dev)
{

    return (PCI_CHILD_ADDED(device_get_parent(dev), dev));
}

device_t pci_find_bsf(uint8_t, uint8_t, uint8_t);
device_t pci_find_dbsf(uint32_t, uint8_t, uint8_t, uint8_t);
device_t pci_find_device(uint16_t, uint16_t);
device_t pci_find_class(uint8_t class, uint8_t subclass);

/* Can be used by drivers to manage the MSI-X table. */
int	pci_pending_msix(device_t dev, u_int index);

int	pci_msi_device_blacklisted(device_t dev);
int	pci_msix_device_blacklisted(device_t dev);

void	pci_ht_map_msi(device_t dev, uint64_t addr);

device_t pci_find_pcie_root_port(device_t dev);
int	pci_get_max_payload(device_t dev);
int	pci_get_max_read_req(device_t dev);
void	pci_restore_state(device_t dev);
void	pci_save_state(device_t dev);
int	pci_set_max_read_req(device_t dev, int size);
uint32_t pcie_read_config(device_t dev, int reg, int width);
void	pcie_write_config(device_t dev, int reg, uint32_t value, int width);
uint32_t pcie_adjust_config(device_t dev, int reg, uint32_t mask,
	    uint32_t value, int width);
bool	pcie_flr(device_t dev, u_int max_delay, bool force);
int	pcie_get_max_completion_timeout(device_t dev);
bool	pcie_wait_for_pending_transactions(device_t dev, u_int max_delay);

void	pci_print_faulted_dev(void);

#ifdef BUS_SPACE_MAXADDR
#if (BUS_SPACE_MAXADDR > 0xFFFFFFFF)
#define	PCI_DMA_BOUNDARY	0x100000000
#else
#define	PCI_DMA_BOUNDARY	0
#endif
#endif

#endif	/* _SYS_BUS_H_ */

/*
 * cdev switch for control device, initialised in generic PCI code
 */
extern struct cdevsw pcicdev;

/*
 * List of all PCI devices, generation count for the list.
 */
STAILQ_HEAD(devlist, pci_devinfo);

extern struct devlist	pci_devq;
extern uint32_t	pci_generation;

struct pci_map *pci_find_bar(device_t dev, int reg);
int	pci_bar_enabled(device_t dev, struct pci_map *pm);
struct pcicfg_vpd *pci_fetch_vpd_list(device_t dev);

#define	VGA_PCI_BIOS_SHADOW_ADDR	0xC0000
#define	VGA_PCI_BIOS_SHADOW_SIZE	131072

int	vga_pci_is_boot_display(device_t dev);
void *	vga_pci_map_bios(device_t dev, size_t *size);
void	vga_pci_unmap_bios(device_t dev, void *bios);
int	vga_pci_repost(device_t dev);

/**
 * Global eventhandlers invoked when PCI devices are added or removed
 * from the system.
 */
typedef void (*pci_event_fn)(void *arg, device_t dev);
EVENTHANDLER_DECLARE(pci_add_device, pci_event_fn);
EVENTHANDLER_DECLARE(pci_delete_device, pci_event_fn);

#endif /* _PCIVAR_H_ */
