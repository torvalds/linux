/* SPDX-License-Identifier: GPL-2.0 */
/*
 *	pci.h
 *
 *	PCI defines and function prototypes
 *	Copyright 1994, Drew Eckhardt
 *	Copyright 1997--1999 Martin Mares <mj@ucw.cz>
 *
 *	For more information, please consult the following manuals (look at
 *	http://www.pcisig.com/ for how to get them):
 *
 *	PCI BIOS Specification
 *	PCI Local Bus Specification
 *	PCI to PCI Bridge Specification
 *	PCI System Design Guide
 */
#ifndef LINUX_PCI_H
#define LINUX_PCI_H


#include <linux/mod_devicetable.h>

#include <linux/types.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/list.h>
#include <linux/compiler.h>
#include <linux/errno.h>
#include <linux/kobject.h>
#include <linux/atomic.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/resource_ext.h>
#include <uapi/linux/pci.h>

#include <linux/pci_ids.h>

/*
 * The PCI interface treats multi-function devices as independent
 * devices.  The slot/function address of each device is encoded
 * in a single byte as follows:
 *
 *	7:3 = slot
 *	2:0 = function
 *
 * PCI_DEVFN(), PCI_SLOT(), and PCI_FUNC() are defined in uapi/linux/pci.h.
 * In the interest of not exposing interfaces to user-space unnecessarily,
 * the following kernel-only defines are being added here.
 */
#define PCI_DEVID(bus, devfn)  ((((u16)(bus)) << 8) | (devfn))
/* return bus from PCI devid = ((u16)bus_number) << 8) | devfn */
#define PCI_BUS_NUM(x) (((x) >> 8) & 0xff)

/* pci_slot represents a physical slot */
struct pci_slot {
	struct pci_bus *bus;		/* The bus this slot is on */
	struct list_head list;		/* node in list of slots on this bus */
	struct hotplug_slot *hotplug;	/* Hotplug info (migrate over time) */
	unsigned char number;		/* PCI_SLOT(pci_dev->devfn) */
	struct kobject kobj;
};

static inline const char *pci_slot_name(const struct pci_slot *slot)
{
	return kobject_name(&slot->kobj);
}

/* File state for mmap()s on /proc/bus/pci/X/Y */
enum pci_mmap_state {
	pci_mmap_io,
	pci_mmap_mem
};

/*
 *  For PCI devices, the region numbers are assigned this way:
 */
enum {
	/* #0-5: standard PCI resources */
	PCI_STD_RESOURCES,
	PCI_STD_RESOURCE_END = 5,

	/* #6: expansion ROM resource */
	PCI_ROM_RESOURCE,

	/* device specific resources */
#ifdef CONFIG_PCI_IOV
	PCI_IOV_RESOURCES,
	PCI_IOV_RESOURCE_END = PCI_IOV_RESOURCES + PCI_SRIOV_NUM_BARS - 1,
#endif

	/* resources assigned to buses behind the bridge */
#define PCI_BRIDGE_RESOURCE_NUM 4

	PCI_BRIDGE_RESOURCES,
	PCI_BRIDGE_RESOURCE_END = PCI_BRIDGE_RESOURCES +
				  PCI_BRIDGE_RESOURCE_NUM - 1,

	/* total resources associated with a PCI device */
	PCI_NUM_RESOURCES,

	/* preserve this for compatibility */
	DEVICE_COUNT_RESOURCE = PCI_NUM_RESOURCES,
};

/**
 * enum pci_interrupt_pin - PCI INTx interrupt values
 * @PCI_INTERRUPT_UNKNOWN: Unknown or unassigned interrupt
 * @PCI_INTERRUPT_INTA: PCI INTA pin
 * @PCI_INTERRUPT_INTB: PCI INTB pin
 * @PCI_INTERRUPT_INTC: PCI INTC pin
 * @PCI_INTERRUPT_INTD: PCI INTD pin
 *
 * Corresponds to values for legacy PCI INTx interrupts, as can be found in the
 * PCI_INTERRUPT_PIN register.
 */
enum pci_interrupt_pin {
	PCI_INTERRUPT_UNKNOWN,
	PCI_INTERRUPT_INTA,
	PCI_INTERRUPT_INTB,
	PCI_INTERRUPT_INTC,
	PCI_INTERRUPT_INTD,
};

/* The number of legacy PCI INTx interrupts */
#define PCI_NUM_INTX	4

/*
 * pci_power_t values must match the bits in the Capabilities PME_Support
 * and Control/Status PowerState fields in the Power Management capability.
 */
typedef int __bitwise pci_power_t;

#define PCI_D0		((pci_power_t __force) 0)
#define PCI_D1		((pci_power_t __force) 1)
#define PCI_D2		((pci_power_t __force) 2)
#define PCI_D3hot	((pci_power_t __force) 3)
#define PCI_D3cold	((pci_power_t __force) 4)
#define PCI_UNKNOWN	((pci_power_t __force) 5)
#define PCI_POWER_ERROR	((pci_power_t __force) -1)

/* Remember to update this when the list above changes! */
extern const char *pci_power_names[];

static inline const char *pci_power_name(pci_power_t state)
{
	return pci_power_names[1 + (__force int) state];
}

#define PCI_PM_D2_DELAY		200
#define PCI_PM_D3_WAIT		10
#define PCI_PM_D3COLD_WAIT	100
#define PCI_PM_BUS_WAIT		50

/** The pci_channel state describes connectivity between the CPU and
 *  the pci device.  If some PCI bus between here and the pci device
 *  has crashed or locked up, this info is reflected here.
 */
typedef unsigned int __bitwise pci_channel_state_t;

enum pci_channel_state {
	/* I/O channel is in normal state */
	pci_channel_io_normal = (__force pci_channel_state_t) 1,

	/* I/O to channel is blocked */
	pci_channel_io_frozen = (__force pci_channel_state_t) 2,

	/* PCI card is dead */
	pci_channel_io_perm_failure = (__force pci_channel_state_t) 3,
};

typedef unsigned int __bitwise pcie_reset_state_t;

enum pcie_reset_state {
	/* Reset is NOT asserted (Use to deassert reset) */
	pcie_deassert_reset = (__force pcie_reset_state_t) 1,

	/* Use #PERST to reset PCIe device */
	pcie_warm_reset = (__force pcie_reset_state_t) 2,

	/* Use PCIe Hot Reset to reset device */
	pcie_hot_reset = (__force pcie_reset_state_t) 3
};

typedef unsigned short __bitwise pci_dev_flags_t;
enum pci_dev_flags {
	/* INTX_DISABLE in PCI_COMMAND register disables MSI
	 * generation too.
	 */
	PCI_DEV_FLAGS_MSI_INTX_DISABLE_BUG = (__force pci_dev_flags_t) (1 << 0),
	/* Device configuration is irrevocably lost if disabled into D3 */
	PCI_DEV_FLAGS_NO_D3 = (__force pci_dev_flags_t) (1 << 1),
	/* Provide indication device is assigned by a Virtual Machine Manager */
	PCI_DEV_FLAGS_ASSIGNED = (__force pci_dev_flags_t) (1 << 2),
	/* Flag for quirk use to store if quirk-specific ACS is enabled */
	PCI_DEV_FLAGS_ACS_ENABLED_QUIRK = (__force pci_dev_flags_t) (1 << 3),
	/* Use a PCIe-to-PCI bridge alias even if !pci_is_pcie */
	PCI_DEV_FLAG_PCIE_BRIDGE_ALIAS = (__force pci_dev_flags_t) (1 << 5),
	/* Do not use bus resets for device */
	PCI_DEV_FLAGS_NO_BUS_RESET = (__force pci_dev_flags_t) (1 << 6),
	/* Do not use PM reset even if device advertises NoSoftRst- */
	PCI_DEV_FLAGS_NO_PM_RESET = (__force pci_dev_flags_t) (1 << 7),
	/* Get VPD from function 0 VPD */
	PCI_DEV_FLAGS_VPD_REF_F0 = (__force pci_dev_flags_t) (1 << 8),
	/* a non-root bridge where translation occurs, stop alias search here */
	PCI_DEV_FLAGS_BRIDGE_XLATE_ROOT = (__force pci_dev_flags_t) (1 << 9),
	/* Do not use FLR even if device advertises PCI_AF_CAP */
	PCI_DEV_FLAGS_NO_FLR_RESET = (__force pci_dev_flags_t) (1 << 10),
	/* Don't use Relaxed Ordering for TLPs directed at this device */
	PCI_DEV_FLAGS_NO_RELAXED_ORDERING = (__force pci_dev_flags_t) (1 << 11),
};

enum pci_irq_reroute_variant {
	INTEL_IRQ_REROUTE_VARIANT = 1,
	MAX_IRQ_REROUTE_VARIANTS = 3
};

typedef unsigned short __bitwise pci_bus_flags_t;
enum pci_bus_flags {
	PCI_BUS_FLAGS_NO_MSI	= (__force pci_bus_flags_t) 1,
	PCI_BUS_FLAGS_NO_MMRBC	= (__force pci_bus_flags_t) 2,
	PCI_BUS_FLAGS_NO_AERSID	= (__force pci_bus_flags_t) 4,
};

/* These values come from the PCI Express Spec */
enum pcie_link_width {
	PCIE_LNK_WIDTH_RESRV	= 0x00,
	PCIE_LNK_X1		= 0x01,
	PCIE_LNK_X2		= 0x02,
	PCIE_LNK_X4		= 0x04,
	PCIE_LNK_X8		= 0x08,
	PCIE_LNK_X12		= 0x0C,
	PCIE_LNK_X16		= 0x10,
	PCIE_LNK_X32		= 0x20,
	PCIE_LNK_WIDTH_UNKNOWN  = 0xFF,
};

/* Based on the PCI Hotplug Spec, but some values are made up by us */
enum pci_bus_speed {
	PCI_SPEED_33MHz			= 0x00,
	PCI_SPEED_66MHz			= 0x01,
	PCI_SPEED_66MHz_PCIX		= 0x02,
	PCI_SPEED_100MHz_PCIX		= 0x03,
	PCI_SPEED_133MHz_PCIX		= 0x04,
	PCI_SPEED_66MHz_PCIX_ECC	= 0x05,
	PCI_SPEED_100MHz_PCIX_ECC	= 0x06,
	PCI_SPEED_133MHz_PCIX_ECC	= 0x07,
	PCI_SPEED_66MHz_PCIX_266	= 0x09,
	PCI_SPEED_100MHz_PCIX_266	= 0x0a,
	PCI_SPEED_133MHz_PCIX_266	= 0x0b,
	AGP_UNKNOWN			= 0x0c,
	AGP_1X				= 0x0d,
	AGP_2X				= 0x0e,
	AGP_4X				= 0x0f,
	AGP_8X				= 0x10,
	PCI_SPEED_66MHz_PCIX_533	= 0x11,
	PCI_SPEED_100MHz_PCIX_533	= 0x12,
	PCI_SPEED_133MHz_PCIX_533	= 0x13,
	PCIE_SPEED_2_5GT		= 0x14,
	PCIE_SPEED_5_0GT		= 0x15,
	PCIE_SPEED_8_0GT		= 0x16,
	PCI_SPEED_UNKNOWN		= 0xff,
};

struct pci_cap_saved_data {
	u16 cap_nr;
	bool cap_extended;
	unsigned int size;
	u32 data[0];
};

struct pci_cap_saved_state {
	struct hlist_node next;
	struct pci_cap_saved_data cap;
};

struct irq_affinity;
struct pcie_link_state;
struct pci_vpd;
struct pci_sriov;
struct pci_ats;

/*
 * The pci_dev structure is used to describe PCI devices.
 */
struct pci_dev {
	struct list_head bus_list;	/* node in per-bus list */
	struct pci_bus	*bus;		/* bus this device is on */
	struct pci_bus	*subordinate;	/* bus this device bridges to */

	void		*sysdata;	/* hook for sys-specific extension */
	struct proc_dir_entry *procent;	/* device entry in /proc/bus/pci */
	struct pci_slot	*slot;		/* Physical slot this device is in */

	unsigned int	devfn;		/* encoded device & function index */
	unsigned short	vendor;
	unsigned short	device;
	unsigned short	subsystem_vendor;
	unsigned short	subsystem_device;
	unsigned int	class;		/* 3 bytes: (base,sub,prog-if) */
	u8		revision;	/* PCI revision, low byte of class word */
	u8		hdr_type;	/* PCI header type (`multi' flag masked out) */
#ifdef CONFIG_PCIEAER
	u16		aer_cap;	/* AER capability offset */
#endif
	u8		pcie_cap;	/* PCIe capability offset */
	u8		msi_cap;	/* MSI capability offset */
	u8		msix_cap;	/* MSI-X capability offset */
	u8		pcie_mpss:3;	/* PCIe Max Payload Size Supported */
	u8		rom_base_reg;	/* which config register controls the ROM */
	u8		pin;		/* which interrupt pin this device uses */
	u16		pcie_flags_reg;	/* cached PCIe Capabilities Register */
	unsigned long	*dma_alias_mask;/* mask of enabled devfn aliases */

	struct pci_driver *driver;	/* which driver has allocated this device */
	u64		dma_mask;	/* Mask of the bits of bus address this
					   device implements.  Normally this is
					   0xffffffff.  You only need to change
					   this if your device has broken DMA
					   or supports 64-bit transfers.  */

	struct device_dma_parameters dma_parms;

	pci_power_t     current_state;  /* Current operating state. In ACPI-speak,
					   this is D0-D3, D0 being fully functional,
					   and D3 being off. */
	u8		pm_cap;		/* PM capability offset */
	unsigned int	pme_support:5;	/* Bitmask of states from which PME#
					   can be generated */
	unsigned int	pme_poll:1;	/* Poll device's PME status bit */
	unsigned int	d1_support:1;	/* Low power state D1 is supported */
	unsigned int	d2_support:1;	/* Low power state D2 is supported */
	unsigned int	no_d1d2:1;	/* D1 and D2 are forbidden */
	unsigned int	no_d3cold:1;	/* D3cold is forbidden */
	unsigned int	bridge_d3:1;	/* Allow D3 for bridge */
	unsigned int	d3cold_allowed:1;	/* D3cold is allowed by user */
	unsigned int	mmio_always_on:1;	/* disallow turning off io/mem
						   decoding during bar sizing */
	unsigned int	wakeup_prepared:1;
	unsigned int	runtime_d3cold:1;	/* whether go through runtime
						   D3cold, not set for devices
						   powered on/off by the
						   corresponding bridge */
	unsigned int	ignore_hotplug:1;	/* Ignore hotplug events */
	unsigned int	hotplug_user_indicators:1; /* SlotCtl indicators
						      controlled exclusively by
						      user sysfs */
	unsigned int	d3_delay;	/* D3->D0 transition time in ms */
	unsigned int	d3cold_delay;	/* D3cold->D0 transition time in ms */

#ifdef CONFIG_PCIEASPM
	struct pcie_link_state	*link_state;	/* ASPM link state */
#endif

	pci_channel_state_t error_state;	/* current connectivity state */
	struct	device	dev;		/* Generic device interface */

	int		cfg_size;	/* Size of configuration space */

	/*
	 * Instead of touching interrupt line and base address registers
	 * directly, use the values stored here. They might be different!
	 */
	unsigned int	irq;
	struct resource resource[DEVICE_COUNT_RESOURCE]; /* I/O and memory regions + expansion ROMs */

	bool match_driver;		/* Skip attaching driver */
	/* These fields are used by common fixups */
	unsigned int	transparent:1;	/* Subtractive decode PCI bridge */
	unsigned int	multifunction:1;/* Part of multi-function device */
	/* keep track of device state */
	unsigned int	is_added:1;
	unsigned int	is_busmaster:1; /* device is busmaster */
	unsigned int	no_msi:1;	/* device may not use msi */
	unsigned int	no_64bit_msi:1; /* device may only use 32-bit MSIs */
	unsigned int	block_cfg_access:1;	/* config space access is blocked */
	unsigned int	broken_parity_status:1;	/* Device generates false positive parity */
	unsigned int	irq_reroute_variant:2;	/* device needs IRQ rerouting variant */
	unsigned int	msi_enabled:1;
	unsigned int	msix_enabled:1;
	unsigned int	ari_enabled:1;	/* ARI forwarding */
	unsigned int	ats_enabled:1;	/* Address Translation Service */
	unsigned int	pasid_enabled:1;	/* Process Address Space ID */
	unsigned int	pri_enabled:1;		/* Page Request Interface */
	unsigned int	is_managed:1;
	unsigned int    needs_freset:1; /* Dev requires fundamental reset */
	unsigned int	state_saved:1;
	unsigned int	is_physfn:1;
	unsigned int	is_virtfn:1;
	unsigned int	reset_fn:1;
	unsigned int    is_hotplug_bridge:1;
	unsigned int	is_thunderbolt:1; /* Thunderbolt controller */
	unsigned int    __aer_firmware_first_valid:1;
	unsigned int	__aer_firmware_first:1;
	unsigned int	broken_intx_masking:1; /* INTx masking can't be used */
	unsigned int	io_window_1k:1;	/* Intel P2P bridge 1K I/O windows */
	unsigned int	irq_managed:1;
	unsigned int	has_secondary_link:1;
	unsigned int	non_compliant_bars:1;	/* broken BARs; ignore them */
	unsigned int	is_probed:1;		/* device probing in progress */
	pci_dev_flags_t dev_flags;
	atomic_t	enable_cnt;	/* pci_enable_device has been called */

	u32		saved_config_space[16]; /* config space saved at suspend time */
	struct hlist_head saved_cap_space;
	struct bin_attribute *rom_attr; /* attribute descriptor for sysfs ROM entry */
	int rom_attr_enabled;		/* has display of the rom attribute been enabled? */
	struct bin_attribute *res_attr[DEVICE_COUNT_RESOURCE]; /* sysfs file for resources */
	struct bin_attribute *res_attr_wc[DEVICE_COUNT_RESOURCE]; /* sysfs file for WC mapping of resources */

#ifdef CONFIG_PCIE_PTM
	unsigned int	ptm_root:1;
	unsigned int	ptm_enabled:1;
	u8		ptm_granularity;
#endif
#ifdef CONFIG_PCI_MSI
	const struct attribute_group **msi_irq_groups;
#endif
	struct pci_vpd *vpd;
#ifdef CONFIG_PCI_ATS
	union {
		struct pci_sriov *sriov;	/* SR-IOV capability related */
		struct pci_dev *physfn;	/* the PF this VF is associated with */
	};
	u16		ats_cap;	/* ATS Capability offset */
	u8		ats_stu;	/* ATS Smallest Translation Unit */
	atomic_t	ats_ref_cnt;	/* number of VFs with ATS enabled */
#endif
#ifdef CONFIG_PCI_PRI
	u32		pri_reqs_alloc; /* Number of PRI requests allocated */
#endif
#ifdef CONFIG_PCI_PASID
	u16		pasid_features;
#endif
	phys_addr_t rom; /* Physical address of ROM if it's not from the BAR */
	size_t romlen; /* Length of ROM if it's not from the BAR */
	char *driver_override; /* Driver name to force a match */

	unsigned long priv_flags; /* Private flags for the pci driver */
};

static inline struct pci_dev *pci_physfn(struct pci_dev *dev)
{
#ifdef CONFIG_PCI_IOV
	if (dev->is_virtfn)
		dev = dev->physfn;
#endif
	return dev;
}

struct pci_dev *pci_alloc_dev(struct pci_bus *bus);

#define	to_pci_dev(n) container_of(n, struct pci_dev, dev)
#define for_each_pci_dev(d) while ((d = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, d)) != NULL)

static inline int pci_channel_offline(struct pci_dev *pdev)
{
	return (pdev->error_state != pci_channel_io_normal);
}

struct pci_host_bridge {
	struct device dev;
	struct pci_bus *bus;		/* root bus */
	struct pci_ops *ops;
	void *sysdata;
	int busnr;
	struct list_head windows;	/* resource_entry */
	u8 (*swizzle_irq)(struct pci_dev *, u8 *); /* platform IRQ swizzler */
	int (*map_irq)(const struct pci_dev *, u8, u8);
	void (*release_fn)(struct pci_host_bridge *);
	void *release_data;
	struct msi_controller *msi;
	unsigned int ignore_reset_delay:1;	/* for entire hierarchy */
	unsigned int no_ext_tags:1;		/* no Extended Tags */
	/* Resource alignment requirements */
	resource_size_t (*align_resource)(struct pci_dev *dev,
			const struct resource *res,
			resource_size_t start,
			resource_size_t size,
			resource_size_t align);
	unsigned long private[0] ____cacheline_aligned;
};

#define	to_pci_host_bridge(n) container_of(n, struct pci_host_bridge, dev)

static inline void *pci_host_bridge_priv(struct pci_host_bridge *bridge)
{
	return (void *)bridge->private;
}

static inline struct pci_host_bridge *pci_host_bridge_from_priv(void *priv)
{
	return container_of(priv, struct pci_host_bridge, private);
}

struct pci_host_bridge *pci_alloc_host_bridge(size_t priv);
struct pci_host_bridge *devm_pci_alloc_host_bridge(struct device *dev,
						   size_t priv);
void pci_free_host_bridge(struct pci_host_bridge *bridge);
struct pci_host_bridge *pci_find_host_bridge(struct pci_bus *bus);

void pci_set_host_bridge_release(struct pci_host_bridge *bridge,
		     void (*release_fn)(struct pci_host_bridge *),
		     void *release_data);

int pcibios_root_bridge_prepare(struct pci_host_bridge *bridge);

/*
 * The first PCI_BRIDGE_RESOURCE_NUM PCI bus resources (those that correspond
 * to P2P or CardBus bridge windows) go in a table.  Additional ones (for
 * buses below host bridges or subtractive decode bridges) go in the list.
 * Use pci_bus_for_each_resource() to iterate through all the resources.
 */

/*
 * PCI_SUBTRACTIVE_DECODE means the bridge forwards the window implicitly
 * and there's no way to program the bridge with the details of the window.
 * This does not apply to ACPI _CRS windows, even with the _DEC subtractive-
 * decode bit set, because they are explicit and can be programmed with _SRS.
 */
#define PCI_SUBTRACTIVE_DECODE	0x1

struct pci_bus_resource {
	struct list_head list;
	struct resource *res;
	unsigned int flags;
};

#define PCI_REGION_FLAG_MASK	0x0fU	/* These bits of resource flags tell us the PCI region flags */

struct pci_bus {
	struct list_head node;		/* node in list of buses */
	struct pci_bus	*parent;	/* parent bus this bridge is on */
	struct list_head children;	/* list of child buses */
	struct list_head devices;	/* list of devices on this bus */
	struct pci_dev	*self;		/* bridge device as seen by parent */
	struct list_head slots;		/* list of slots on this bus;
					   protected by pci_slot_mutex */
	struct resource *resource[PCI_BRIDGE_RESOURCE_NUM];
	struct list_head resources;	/* address space routed to this bus */
	struct resource busn_res;	/* bus numbers routed to this bus */

	struct pci_ops	*ops;		/* configuration access functions */
	struct msi_controller *msi;	/* MSI controller */
	void		*sysdata;	/* hook for sys-specific extension */
	struct proc_dir_entry *procdir;	/* directory entry in /proc/bus/pci */

	unsigned char	number;		/* bus number */
	unsigned char	primary;	/* number of primary bridge */
	unsigned char	max_bus_speed;	/* enum pci_bus_speed */
	unsigned char	cur_bus_speed;	/* enum pci_bus_speed */
#ifdef CONFIG_PCI_DOMAINS_GENERIC
	int		domain_nr;
#endif

	char		name[48];

	unsigned short  bridge_ctl;	/* manage NO_ISA/FBB/et al behaviors */
	pci_bus_flags_t bus_flags;	/* inherited by child buses */
	struct device		*bridge;
	struct device		dev;
	struct bin_attribute	*legacy_io; /* legacy I/O for this bus */
	struct bin_attribute	*legacy_mem; /* legacy mem */
	unsigned int		is_added:1;
};

#define to_pci_bus(n)	container_of(n, struct pci_bus, dev)

/*
 * Returns true if the PCI bus is root (behind host-PCI bridge),
 * false otherwise
 *
 * Some code assumes that "bus->self == NULL" means that bus is a root bus.
 * This is incorrect because "virtual" buses added for SR-IOV (via
 * virtfn_add_bus()) have "bus->self == NULL" but are not root buses.
 */
static inline bool pci_is_root_bus(struct pci_bus *pbus)
{
	return !(pbus->parent);
}

/**
 * pci_is_bridge - check if the PCI device is a bridge
 * @dev: PCI device
 *
 * Return true if the PCI device is bridge whether it has subordinate
 * or not.
 */
static inline bool pci_is_bridge(struct pci_dev *dev)
{
	return dev->hdr_type == PCI_HEADER_TYPE_BRIDGE ||
		dev->hdr_type == PCI_HEADER_TYPE_CARDBUS;
}

static inline struct pci_dev *pci_upstream_bridge(struct pci_dev *dev)
{
	dev = pci_physfn(dev);
	if (pci_is_root_bus(dev->bus))
		return NULL;

	return dev->bus->self;
}

struct device *pci_get_host_bridge_device(struct pci_dev *dev);
void pci_put_host_bridge_device(struct device *dev);

#ifdef CONFIG_PCI_MSI
static inline bool pci_dev_msi_enabled(struct pci_dev *pci_dev)
{
	return pci_dev->msi_enabled || pci_dev->msix_enabled;
}
#else
static inline bool pci_dev_msi_enabled(struct pci_dev *pci_dev) { return false; }
#endif

/*
 * Error values that may be returned by PCI functions.
 */
#define PCIBIOS_SUCCESSFUL		0x00
#define PCIBIOS_FUNC_NOT_SUPPORTED	0x81
#define PCIBIOS_BAD_VENDOR_ID		0x83
#define PCIBIOS_DEVICE_NOT_FOUND	0x86
#define PCIBIOS_BAD_REGISTER_NUMBER	0x87
#define PCIBIOS_SET_FAILED		0x88
#define PCIBIOS_BUFFER_TOO_SMALL	0x89

/*
 * Translate above to generic errno for passing back through non-PCI code.
 */
static inline int pcibios_err_to_errno(int err)
{
	if (err <= PCIBIOS_SUCCESSFUL)
		return err; /* Assume already errno */

	switch (err) {
	case PCIBIOS_FUNC_NOT_SUPPORTED:
		return -ENOENT;
	case PCIBIOS_BAD_VENDOR_ID:
		return -ENOTTY;
	case PCIBIOS_DEVICE_NOT_FOUND:
		return -ENODEV;
	case PCIBIOS_BAD_REGISTER_NUMBER:
		return -EFAULT;
	case PCIBIOS_SET_FAILED:
		return -EIO;
	case PCIBIOS_BUFFER_TOO_SMALL:
		return -ENOSPC;
	}

	return -ERANGE;
}

/* Low-level architecture-dependent routines */

struct pci_ops {
	int (*add_bus)(struct pci_bus *bus);
	void (*remove_bus)(struct pci_bus *bus);
	void __iomem *(*map_bus)(struct pci_bus *bus, unsigned int devfn, int where);
	int (*read)(struct pci_bus *bus, unsigned int devfn, int where, int size, u32 *val);
	int (*write)(struct pci_bus *bus, unsigned int devfn, int where, int size, u32 val);
};

/*
 * ACPI needs to be able to access PCI config space before we've done a
 * PCI bus scan and created pci_bus structures.
 */
int raw_pci_read(unsigned int domain, unsigned int bus, unsigned int devfn,
		 int reg, int len, u32 *val);
int raw_pci_write(unsigned int domain, unsigned int bus, unsigned int devfn,
		  int reg, int len, u32 val);

#ifdef CONFIG_PCI_BUS_ADDR_T_64BIT
typedef u64 pci_bus_addr_t;
#else
typedef u32 pci_bus_addr_t;
#endif

struct pci_bus_region {
	pci_bus_addr_t start;
	pci_bus_addr_t end;
};

struct pci_dynids {
	spinlock_t lock;            /* protects list, index */
	struct list_head list;      /* for IDs added at runtime */
};


/*
 * PCI Error Recovery System (PCI-ERS).  If a PCI device driver provides
 * a set of callbacks in struct pci_error_handlers, that device driver
 * will be notified of PCI bus errors, and will be driven to recovery
 * when an error occurs.
 */

typedef unsigned int __bitwise pci_ers_result_t;

enum pci_ers_result {
	/* no result/none/not supported in device driver */
	PCI_ERS_RESULT_NONE = (__force pci_ers_result_t) 1,

	/* Device driver can recover without slot reset */
	PCI_ERS_RESULT_CAN_RECOVER = (__force pci_ers_result_t) 2,

	/* Device driver wants slot to be reset. */
	PCI_ERS_RESULT_NEED_RESET = (__force pci_ers_result_t) 3,

	/* Device has completely failed, is unrecoverable */
	PCI_ERS_RESULT_DISCONNECT = (__force pci_ers_result_t) 4,

	/* Device driver is fully recovered and operational */
	PCI_ERS_RESULT_RECOVERED = (__force pci_ers_result_t) 5,

	/* No AER capabilities registered for the driver */
	PCI_ERS_RESULT_NO_AER_DRIVER = (__force pci_ers_result_t) 6,
};

/* PCI bus error event callbacks */
struct pci_error_handlers {
	/* PCI bus error detected on this device */
	pci_ers_result_t (*error_detected)(struct pci_dev *dev,
					   enum pci_channel_state error);

	/* MMIO has been re-enabled, but not DMA */
	pci_ers_result_t (*mmio_enabled)(struct pci_dev *dev);

	/* PCI slot has been reset */
	pci_ers_result_t (*slot_reset)(struct pci_dev *dev);

	/* PCI function reset prepare or completed */
	void (*reset_prepare)(struct pci_dev *dev);
	void (*reset_done)(struct pci_dev *dev);

	/* Device driver may resume normal operations */
	void (*resume)(struct pci_dev *dev);
};


struct module;
struct pci_driver {
	struct list_head node;
	const char *name;
	const struct pci_device_id *id_table;	/* must be non-NULL for probe to be called */
	int  (*probe)  (struct pci_dev *dev, const struct pci_device_id *id);	/* New device inserted */
	void (*remove) (struct pci_dev *dev);	/* Device removed (NULL if not a hot-plug capable driver) */
	int  (*suspend) (struct pci_dev *dev, pm_message_t state);	/* Device suspended */
	int  (*suspend_late) (struct pci_dev *dev, pm_message_t state);
	int  (*resume_early) (struct pci_dev *dev);
	int  (*resume) (struct pci_dev *dev);	                /* Device woken up */
	void (*shutdown) (struct pci_dev *dev);
	int (*sriov_configure) (struct pci_dev *dev, int num_vfs); /* PF pdev */
	const struct pci_error_handlers *err_handler;
	const struct attribute_group **groups;
	struct device_driver	driver;
	struct pci_dynids dynids;
};

#define	to_pci_driver(drv) container_of(drv, struct pci_driver, driver)

/**
 * PCI_DEVICE - macro used to describe a specific pci device
 * @vend: the 16 bit PCI Vendor ID
 * @dev: the 16 bit PCI Device ID
 *
 * This macro is used to create a struct pci_device_id that matches a
 * specific device.  The subvendor and subdevice fields will be set to
 * PCI_ANY_ID.
 */
#define PCI_DEVICE(vend,dev) \
	.vendor = (vend), .device = (dev), \
	.subvendor = PCI_ANY_ID, .subdevice = PCI_ANY_ID

/**
 * PCI_DEVICE_SUB - macro used to describe a specific pci device with subsystem
 * @vend: the 16 bit PCI Vendor ID
 * @dev: the 16 bit PCI Device ID
 * @subvend: the 16 bit PCI Subvendor ID
 * @subdev: the 16 bit PCI Subdevice ID
 *
 * This macro is used to create a struct pci_device_id that matches a
 * specific device with subsystem information.
 */
#define PCI_DEVICE_SUB(vend, dev, subvend, subdev) \
	.vendor = (vend), .device = (dev), \
	.subvendor = (subvend), .subdevice = (subdev)

/**
 * PCI_DEVICE_CLASS - macro used to describe a specific pci device class
 * @dev_class: the class, subclass, prog-if triple for this device
 * @dev_class_mask: the class mask for this device
 *
 * This macro is used to create a struct pci_device_id that matches a
 * specific PCI class.  The vendor, device, subvendor, and subdevice
 * fields will be set to PCI_ANY_ID.
 */
#define PCI_DEVICE_CLASS(dev_class,dev_class_mask) \
	.class = (dev_class), .class_mask = (dev_class_mask), \
	.vendor = PCI_ANY_ID, .device = PCI_ANY_ID, \
	.subvendor = PCI_ANY_ID, .subdevice = PCI_ANY_ID

/**
 * PCI_VDEVICE - macro used to describe a specific pci device in short form
 * @vend: the vendor name
 * @dev: the 16 bit PCI Device ID
 *
 * This macro is used to create a struct pci_device_id that matches a
 * specific PCI device.  The subvendor, and subdevice fields will be set
 * to PCI_ANY_ID. The macro allows the next field to follow as the device
 * private data.
 */

#define PCI_VDEVICE(vend, dev) \
	.vendor = PCI_VENDOR_ID_##vend, .device = (dev), \
	.subvendor = PCI_ANY_ID, .subdevice = PCI_ANY_ID, 0, 0

enum {
	PCI_REASSIGN_ALL_RSRC	= 0x00000001,	/* ignore firmware setup */
	PCI_REASSIGN_ALL_BUS	= 0x00000002,	/* reassign all bus numbers */
	PCI_PROBE_ONLY		= 0x00000004,	/* use existing setup */
	PCI_CAN_SKIP_ISA_ALIGN	= 0x00000008,	/* don't do ISA alignment */
	PCI_ENABLE_PROC_DOMAINS	= 0x00000010,	/* enable domains in /proc */
	PCI_COMPAT_DOMAIN_0	= 0x00000020,	/* ... except domain 0 */
	PCI_SCAN_ALL_PCIE_DEVS	= 0x00000040,	/* scan all, not just dev 0 */
};

/* these external functions are only available when PCI support is enabled */
#ifdef CONFIG_PCI

extern unsigned int pci_flags;

static inline void pci_set_flags(int flags) { pci_flags = flags; }
static inline void pci_add_flags(int flags) { pci_flags |= flags; }
static inline void pci_clear_flags(int flags) { pci_flags &= ~flags; }
static inline int pci_has_flag(int flag) { return pci_flags & flag; }

void pcie_bus_configure_settings(struct pci_bus *bus);

enum pcie_bus_config_types {
	PCIE_BUS_TUNE_OFF,	/* don't touch MPS at all */
	PCIE_BUS_DEFAULT,	/* ensure MPS matches upstream bridge */
	PCIE_BUS_SAFE,		/* use largest MPS boot-time devices support */
	PCIE_BUS_PERFORMANCE,	/* use MPS and MRRS for best performance */
	PCIE_BUS_PEER2PEER,	/* set MPS = 128 for all devices */
};

extern enum pcie_bus_config_types pcie_bus_config;

extern struct bus_type pci_bus_type;

/* Do NOT directly access these two variables, unless you are arch-specific PCI
 * code, or PCI core code. */
extern struct list_head pci_root_buses;	/* list of all known PCI buses */
/* Some device drivers need know if PCI is initiated */
int no_pci_devices(void);

void pcibios_resource_survey_bus(struct pci_bus *bus);
void pcibios_bus_add_device(struct pci_dev *pdev);
void pcibios_add_bus(struct pci_bus *bus);
void pcibios_remove_bus(struct pci_bus *bus);
void pcibios_fixup_bus(struct pci_bus *);
int __must_check pcibios_enable_device(struct pci_dev *, int mask);
/* Architecture-specific versions may override this (weak) */
char *pcibios_setup(char *str);

/* Used only when drivers/pci/setup.c is used */
resource_size_t pcibios_align_resource(void *, const struct resource *,
				resource_size_t,
				resource_size_t);

/* Weak but can be overriden by arch */
void pci_fixup_cardbus(struct pci_bus *);

/* Generic PCI functions used internally */

void pcibios_resource_to_bus(struct pci_bus *bus, struct pci_bus_region *region,
			     struct resource *res);
void pcibios_bus_to_resource(struct pci_bus *bus, struct resource *res,
			     struct pci_bus_region *region);
void pcibios_scan_specific_bus(int busn);
struct pci_bus *pci_find_bus(int domain, int busnr);
void pci_bus_add_devices(const struct pci_bus *bus);
struct pci_bus *pci_scan_bus(int bus, struct pci_ops *ops, void *sysdata);
struct pci_bus *pci_create_root_bus(struct device *parent, int bus,
				    struct pci_ops *ops, void *sysdata,
				    struct list_head *resources);
int pci_bus_insert_busn_res(struct pci_bus *b, int bus, int busmax);
int pci_bus_update_busn_res_end(struct pci_bus *b, int busmax);
void pci_bus_release_busn_res(struct pci_bus *b);
struct pci_bus *pci_scan_root_bus(struct device *parent, int bus,
					     struct pci_ops *ops, void *sysdata,
					     struct list_head *resources);
int pci_scan_root_bus_bridge(struct pci_host_bridge *bridge);
struct pci_bus *pci_add_new_bus(struct pci_bus *parent, struct pci_dev *dev,
				int busnr);
void pcie_update_link_speed(struct pci_bus *bus, u16 link_status);
struct pci_slot *pci_create_slot(struct pci_bus *parent, int slot_nr,
				 const char *name,
				 struct hotplug_slot *hotplug);
void pci_destroy_slot(struct pci_slot *slot);
#ifdef CONFIG_SYSFS
void pci_dev_assign_slot(struct pci_dev *dev);
#else
static inline void pci_dev_assign_slot(struct pci_dev *dev) { }
#endif
int pci_scan_slot(struct pci_bus *bus, int devfn);
struct pci_dev *pci_scan_single_device(struct pci_bus *bus, int devfn);
void pci_device_add(struct pci_dev *dev, struct pci_bus *bus);
unsigned int pci_scan_child_bus(struct pci_bus *bus);
void pci_bus_add_device(struct pci_dev *dev);
void pci_read_bridge_bases(struct pci_bus *child);
struct resource *pci_find_parent_resource(const struct pci_dev *dev,
					  struct resource *res);
struct pci_dev *pci_find_pcie_root_port(struct pci_dev *dev);
u8 pci_swizzle_interrupt_pin(const struct pci_dev *dev, u8 pin);
int pci_get_interrupt_pin(struct pci_dev *dev, struct pci_dev **bridge);
u8 pci_common_swizzle(struct pci_dev *dev, u8 *pinp);
struct pci_dev *pci_dev_get(struct pci_dev *dev);
void pci_dev_put(struct pci_dev *dev);
void pci_remove_bus(struct pci_bus *b);
void pci_stop_and_remove_bus_device(struct pci_dev *dev);
void pci_stop_and_remove_bus_device_locked(struct pci_dev *dev);
void pci_stop_root_bus(struct pci_bus *bus);
void pci_remove_root_bus(struct pci_bus *bus);
void pci_setup_cardbus(struct pci_bus *bus);
void pcibios_setup_bridge(struct pci_bus *bus, unsigned long type);
void pci_sort_breadthfirst(void);
#define dev_is_pci(d) ((d)->bus == &pci_bus_type)
#define dev_is_pf(d) ((dev_is_pci(d) ? to_pci_dev(d)->is_physfn : false))

/* Generic PCI functions exported to card drivers */

enum pci_lost_interrupt_reason {
	PCI_LOST_IRQ_NO_INFORMATION = 0,
	PCI_LOST_IRQ_DISABLE_MSI,
	PCI_LOST_IRQ_DISABLE_MSIX,
	PCI_LOST_IRQ_DISABLE_ACPI,
};
enum pci_lost_interrupt_reason pci_lost_interrupt(struct pci_dev *dev);
int pci_find_capability(struct pci_dev *dev, int cap);
int pci_find_next_capability(struct pci_dev *dev, u8 pos, int cap);
int pci_find_ext_capability(struct pci_dev *dev, int cap);
int pci_find_next_ext_capability(struct pci_dev *dev, int pos, int cap);
int pci_find_ht_capability(struct pci_dev *dev, int ht_cap);
int pci_find_next_ht_capability(struct pci_dev *dev, int pos, int ht_cap);
struct pci_bus *pci_find_next_bus(const struct pci_bus *from);

struct pci_dev *pci_get_device(unsigned int vendor, unsigned int device,
				struct pci_dev *from);
struct pci_dev *pci_get_subsys(unsigned int vendor, unsigned int device,
				unsigned int ss_vendor, unsigned int ss_device,
				struct pci_dev *from);
struct pci_dev *pci_get_slot(struct pci_bus *bus, unsigned int devfn);
struct pci_dev *pci_get_domain_bus_and_slot(int domain, unsigned int bus,
					    unsigned int devfn);
static inline struct pci_dev *pci_get_bus_and_slot(unsigned int bus,
						   unsigned int devfn)
{
	return pci_get_domain_bus_and_slot(0, bus, devfn);
}
struct pci_dev *pci_get_class(unsigned int class, struct pci_dev *from);
int pci_dev_present(const struct pci_device_id *ids);

int pci_bus_read_config_byte(struct pci_bus *bus, unsigned int devfn,
			     int where, u8 *val);
int pci_bus_read_config_word(struct pci_bus *bus, unsigned int devfn,
			     int where, u16 *val);
int pci_bus_read_config_dword(struct pci_bus *bus, unsigned int devfn,
			      int where, u32 *val);
int pci_bus_write_config_byte(struct pci_bus *bus, unsigned int devfn,
			      int where, u8 val);
int pci_bus_write_config_word(struct pci_bus *bus, unsigned int devfn,
			      int where, u16 val);
int pci_bus_write_config_dword(struct pci_bus *bus, unsigned int devfn,
			       int where, u32 val);

int pci_generic_config_read(struct pci_bus *bus, unsigned int devfn,
			    int where, int size, u32 *val);
int pci_generic_config_write(struct pci_bus *bus, unsigned int devfn,
			    int where, int size, u32 val);
int pci_generic_config_read32(struct pci_bus *bus, unsigned int devfn,
			      int where, int size, u32 *val);
int pci_generic_config_write32(struct pci_bus *bus, unsigned int devfn,
			       int where, int size, u32 val);

struct pci_ops *pci_bus_set_ops(struct pci_bus *bus, struct pci_ops *ops);

int pci_read_config_byte(const struct pci_dev *dev, int where, u8 *val);
int pci_read_config_word(const struct pci_dev *dev, int where, u16 *val);
int pci_read_config_dword(const struct pci_dev *dev, int where, u32 *val);
int pci_write_config_byte(const struct pci_dev *dev, int where, u8 val);
int pci_write_config_word(const struct pci_dev *dev, int where, u16 val);
int pci_write_config_dword(const struct pci_dev *dev, int where, u32 val);

int pcie_capability_read_word(struct pci_dev *dev, int pos, u16 *val);
int pcie_capability_read_dword(struct pci_dev *dev, int pos, u32 *val);
int pcie_capability_write_word(struct pci_dev *dev, int pos, u16 val);
int pcie_capability_write_dword(struct pci_dev *dev, int pos, u32 val);
int pcie_capability_clear_and_set_word(struct pci_dev *dev, int pos,
				       u16 clear, u16 set);
int pcie_capability_clear_and_set_dword(struct pci_dev *dev, int pos,
					u32 clear, u32 set);

static inline int pcie_capability_set_word(struct pci_dev *dev, int pos,
					   u16 set)
{
	return pcie_capability_clear_and_set_word(dev, pos, 0, set);
}

static inline int pcie_capability_set_dword(struct pci_dev *dev, int pos,
					    u32 set)
{
	return pcie_capability_clear_and_set_dword(dev, pos, 0, set);
}

static inline int pcie_capability_clear_word(struct pci_dev *dev, int pos,
					     u16 clear)
{
	return pcie_capability_clear_and_set_word(dev, pos, clear, 0);
}

static inline int pcie_capability_clear_dword(struct pci_dev *dev, int pos,
					      u32 clear)
{
	return pcie_capability_clear_and_set_dword(dev, pos, clear, 0);
}

/* user-space driven config access */
int pci_user_read_config_byte(struct pci_dev *dev, int where, u8 *val);
int pci_user_read_config_word(struct pci_dev *dev, int where, u16 *val);
int pci_user_read_config_dword(struct pci_dev *dev, int where, u32 *val);
int pci_user_write_config_byte(struct pci_dev *dev, int where, u8 val);
int pci_user_write_config_word(struct pci_dev *dev, int where, u16 val);
int pci_user_write_config_dword(struct pci_dev *dev, int where, u32 val);

int __must_check pci_enable_device(struct pci_dev *dev);
int __must_check pci_enable_device_io(struct pci_dev *dev);
int __must_check pci_enable_device_mem(struct pci_dev *dev);
int __must_check pci_reenable_device(struct pci_dev *);
int __must_check pcim_enable_device(struct pci_dev *pdev);
void pcim_pin_device(struct pci_dev *pdev);

static inline bool pci_intx_mask_supported(struct pci_dev *pdev)
{
	/*
	 * INTx masking is supported if PCI_COMMAND_INTX_DISABLE is
	 * writable and no quirk has marked the feature broken.
	 */
	return !pdev->broken_intx_masking;
}

static inline int pci_is_enabled(struct pci_dev *pdev)
{
	return (atomic_read(&pdev->enable_cnt) > 0);
}

static inline int pci_is_managed(struct pci_dev *pdev)
{
	return pdev->is_managed;
}

void pci_disable_device(struct pci_dev *dev);

extern unsigned int pcibios_max_latency;
void pci_set_master(struct pci_dev *dev);
void pci_clear_master(struct pci_dev *dev);

int pci_set_pcie_reset_state(struct pci_dev *dev, enum pcie_reset_state state);
int pci_set_cacheline_size(struct pci_dev *dev);
#define HAVE_PCI_SET_MWI
int __must_check pci_set_mwi(struct pci_dev *dev);
int pci_try_set_mwi(struct pci_dev *dev);
void pci_clear_mwi(struct pci_dev *dev);
void pci_intx(struct pci_dev *dev, int enable);
bool pci_check_and_mask_intx(struct pci_dev *dev);
bool pci_check_and_unmask_intx(struct pci_dev *dev);
int pci_wait_for_pending(struct pci_dev *dev, int pos, u16 mask);
int pci_wait_for_pending_transaction(struct pci_dev *dev);
int pcix_get_max_mmrbc(struct pci_dev *dev);
int pcix_get_mmrbc(struct pci_dev *dev);
int pcix_set_mmrbc(struct pci_dev *dev, int mmrbc);
int pcie_get_readrq(struct pci_dev *dev);
int pcie_set_readrq(struct pci_dev *dev, int rq);
int pcie_get_mps(struct pci_dev *dev);
int pcie_set_mps(struct pci_dev *dev, int mps);
int pcie_get_minimum_link(struct pci_dev *dev, enum pci_bus_speed *speed,
			  enum pcie_link_width *width);
void pcie_flr(struct pci_dev *dev);
int __pci_reset_function(struct pci_dev *dev);
int __pci_reset_function_locked(struct pci_dev *dev);
int pci_reset_function(struct pci_dev *dev);
int pci_reset_function_locked(struct pci_dev *dev);
int pci_try_reset_function(struct pci_dev *dev);
int pci_probe_reset_slot(struct pci_slot *slot);
int pci_reset_slot(struct pci_slot *slot);
int pci_try_reset_slot(struct pci_slot *slot);
int pci_probe_reset_bus(struct pci_bus *bus);
int pci_reset_bus(struct pci_bus *bus);
int pci_try_reset_bus(struct pci_bus *bus);
void pci_reset_secondary_bus(struct pci_dev *dev);
void pcibios_reset_secondary_bus(struct pci_dev *dev);
void pci_reset_bridge_secondary_bus(struct pci_dev *dev);
void pci_update_resource(struct pci_dev *dev, int resno);
int __must_check pci_assign_resource(struct pci_dev *dev, int i);
int __must_check pci_reassign_resource(struct pci_dev *dev, int i, resource_size_t add_size, resource_size_t align);
int pci_select_bars(struct pci_dev *dev, unsigned long flags);
bool pci_device_is_present(struct pci_dev *pdev);
void pci_ignore_hotplug(struct pci_dev *dev);

int __printf(6, 7) pci_request_irq(struct pci_dev *dev, unsigned int nr,
		irq_handler_t handler, irq_handler_t thread_fn, void *dev_id,
		const char *fmt, ...);
void pci_free_irq(struct pci_dev *dev, unsigned int nr, void *dev_id);

/* ROM control related routines */
int pci_enable_rom(struct pci_dev *pdev);
void pci_disable_rom(struct pci_dev *pdev);
void __iomem __must_check *pci_map_rom(struct pci_dev *pdev, size_t *size);
void pci_unmap_rom(struct pci_dev *pdev, void __iomem *rom);
size_t pci_get_rom_size(struct pci_dev *pdev, void __iomem *rom, size_t size);
void __iomem __must_check *pci_platform_rom(struct pci_dev *pdev, size_t *size);

/* Power management related routines */
int pci_save_state(struct pci_dev *dev);
void pci_restore_state(struct pci_dev *dev);
struct pci_saved_state *pci_store_saved_state(struct pci_dev *dev);
int pci_load_saved_state(struct pci_dev *dev,
			 struct pci_saved_state *state);
int pci_load_and_free_saved_state(struct pci_dev *dev,
				  struct pci_saved_state **state);
struct pci_cap_saved_state *pci_find_saved_cap(struct pci_dev *dev, char cap);
struct pci_cap_saved_state *pci_find_saved_ext_cap(struct pci_dev *dev,
						   u16 cap);
int pci_add_cap_save_buffer(struct pci_dev *dev, char cap, unsigned int size);
int pci_add_ext_cap_save_buffer(struct pci_dev *dev,
				u16 cap, unsigned int size);
int __pci_complete_power_transition(struct pci_dev *dev, pci_power_t state);
int pci_set_power_state(struct pci_dev *dev, pci_power_t state);
pci_power_t pci_choose_state(struct pci_dev *dev, pm_message_t state);
bool pci_pme_capable(struct pci_dev *dev, pci_power_t state);
void pci_pme_active(struct pci_dev *dev, bool enable);
int pci_enable_wake(struct pci_dev *dev, pci_power_t state, bool enable);
int pci_wake_from_d3(struct pci_dev *dev, bool enable);
int pci_prepare_to_sleep(struct pci_dev *dev);
int pci_back_from_sleep(struct pci_dev *dev);
bool pci_dev_run_wake(struct pci_dev *dev);
bool pci_check_pme_status(struct pci_dev *dev);
void pci_pme_wakeup_bus(struct pci_bus *bus);
void pci_d3cold_enable(struct pci_dev *dev);
void pci_d3cold_disable(struct pci_dev *dev);
bool pcie_relaxed_ordering_enabled(struct pci_dev *dev);

/* PCI Virtual Channel */
int pci_save_vc_state(struct pci_dev *dev);
void pci_restore_vc_state(struct pci_dev *dev);
void pci_allocate_vc_save_buffers(struct pci_dev *dev);

/* For use by arch with custom probe code */
void set_pcie_port_type(struct pci_dev *pdev);
void set_pcie_hotplug_bridge(struct pci_dev *pdev);

/* Functions for PCI Hotplug drivers to use */
int pci_bus_find_capability(struct pci_bus *bus, unsigned int devfn, int cap);
unsigned int pci_rescan_bus_bridge_resize(struct pci_dev *bridge);
unsigned int pci_rescan_bus(struct pci_bus *bus);
void pci_lock_rescan_remove(void);
void pci_unlock_rescan_remove(void);

/* Vital product data routines */
ssize_t pci_read_vpd(struct pci_dev *dev, loff_t pos, size_t count, void *buf);
ssize_t pci_write_vpd(struct pci_dev *dev, loff_t pos, size_t count, const void *buf);
int pci_set_vpd_size(struct pci_dev *dev, size_t len);

/* Helper functions for low-level code (drivers/pci/setup-[bus,res].c) */
resource_size_t pcibios_retrieve_fw_addr(struct pci_dev *dev, int idx);
void pci_bus_assign_resources(const struct pci_bus *bus);
void pci_bus_claim_resources(struct pci_bus *bus);
void pci_bus_size_bridges(struct pci_bus *bus);
int pci_claim_resource(struct pci_dev *, int);
int pci_claim_bridge_resource(struct pci_dev *bridge, int i);
void pci_assign_unassigned_resources(void);
void pci_assign_unassigned_bridge_resources(struct pci_dev *bridge);
void pci_assign_unassigned_bus_resources(struct pci_bus *bus);
void pci_assign_unassigned_root_bus_resources(struct pci_bus *bus);
void pdev_enable_device(struct pci_dev *);
int pci_enable_resources(struct pci_dev *, int mask);
void pci_assign_irq(struct pci_dev *dev);
struct resource *pci_find_resource(struct pci_dev *dev, struct resource *res);
#define HAVE_PCI_REQ_REGIONS	2
int __must_check pci_request_regions(struct pci_dev *, const char *);
int __must_check pci_request_regions_exclusive(struct pci_dev *, const char *);
void pci_release_regions(struct pci_dev *);
int __must_check pci_request_region(struct pci_dev *, int, const char *);
int __must_check pci_request_region_exclusive(struct pci_dev *, int, const char *);
void pci_release_region(struct pci_dev *, int);
int pci_request_selected_regions(struct pci_dev *, int, const char *);
int pci_request_selected_regions_exclusive(struct pci_dev *, int, const char *);
void pci_release_selected_regions(struct pci_dev *, int);

/* drivers/pci/bus.c */
struct pci_bus *pci_bus_get(struct pci_bus *bus);
void pci_bus_put(struct pci_bus *bus);
void pci_add_resource(struct list_head *resources, struct resource *res);
void pci_add_resource_offset(struct list_head *resources, struct resource *res,
			     resource_size_t offset);
void pci_free_resource_list(struct list_head *resources);
void pci_bus_add_resource(struct pci_bus *bus, struct resource *res,
			  unsigned int flags);
struct resource *pci_bus_resource_n(const struct pci_bus *bus, int n);
void pci_bus_remove_resources(struct pci_bus *bus);
int devm_request_pci_bus_resources(struct device *dev,
				   struct list_head *resources);

#define pci_bus_for_each_resource(bus, res, i)				\
	for (i = 0;							\
	    (res = pci_bus_resource_n(bus, i)) || i < PCI_BRIDGE_RESOURCE_NUM; \
	     i++)

int __must_check pci_bus_alloc_resource(struct pci_bus *bus,
			struct resource *res, resource_size_t size,
			resource_size_t align, resource_size_t min,
			unsigned long type_mask,
			resource_size_t (*alignf)(void *,
						  const struct resource *,
						  resource_size_t,
						  resource_size_t),
			void *alignf_data);


int pci_register_io_range(phys_addr_t addr, resource_size_t size);
unsigned long pci_address_to_pio(phys_addr_t addr);
phys_addr_t pci_pio_to_address(unsigned long pio);
int pci_remap_iospace(const struct resource *res, phys_addr_t phys_addr);
void pci_unmap_iospace(struct resource *res);
void __iomem *devm_pci_remap_cfgspace(struct device *dev,
				      resource_size_t offset,
				      resource_size_t size);
void __iomem *devm_pci_remap_cfg_resource(struct device *dev,
					  struct resource *res);

static inline pci_bus_addr_t pci_bus_address(struct pci_dev *pdev, int bar)
{
	struct pci_bus_region region;

	pcibios_resource_to_bus(pdev->bus, &region, &pdev->resource[bar]);
	return region.start;
}

/* Proper probing supporting hot-pluggable devices */
int __must_check __pci_register_driver(struct pci_driver *, struct module *,
				       const char *mod_name);

/*
 * pci_register_driver must be a macro so that KBUILD_MODNAME can be expanded
 */
#define pci_register_driver(driver)		\
	__pci_register_driver(driver, THIS_MODULE, KBUILD_MODNAME)

void pci_unregister_driver(struct pci_driver *dev);

/**
 * module_pci_driver() - Helper macro for registering a PCI driver
 * @__pci_driver: pci_driver struct
 *
 * Helper macro for PCI drivers which do not do anything special in module
 * init/exit. This eliminates a lot of boilerplate. Each module may only
 * use this macro once, and calling it replaces module_init() and module_exit()
 */
#define module_pci_driver(__pci_driver) \
	module_driver(__pci_driver, pci_register_driver, \
		       pci_unregister_driver)

/**
 * builtin_pci_driver() - Helper macro for registering a PCI driver
 * @__pci_driver: pci_driver struct
 *
 * Helper macro for PCI drivers which do not do anything special in their
 * init code. This eliminates a lot of boilerplate. Each driver may only
 * use this macro once, and calling it replaces device_initcall(...)
 */
#define builtin_pci_driver(__pci_driver) \
	builtin_driver(__pci_driver, pci_register_driver)

struct pci_driver *pci_dev_driver(const struct pci_dev *dev);
int pci_add_dynid(struct pci_driver *drv,
		  unsigned int vendor, unsigned int device,
		  unsigned int subvendor, unsigned int subdevice,
		  unsigned int class, unsigned int class_mask,
		  unsigned long driver_data);
const struct pci_device_id *pci_match_id(const struct pci_device_id *ids,
					 struct pci_dev *dev);
int pci_scan_bridge(struct pci_bus *bus, struct pci_dev *dev, int max,
		    int pass);

void pci_walk_bus(struct pci_bus *top, int (*cb)(struct pci_dev *, void *),
		  void *userdata);
int pci_cfg_space_size(struct pci_dev *dev);
unsigned char pci_bus_max_busnr(struct pci_bus *bus);
void pci_setup_bridge(struct pci_bus *bus);
resource_size_t pcibios_window_alignment(struct pci_bus *bus,
					 unsigned long type);
resource_size_t pcibios_iov_resource_alignment(struct pci_dev *dev, int resno);

#define PCI_VGA_STATE_CHANGE_BRIDGE (1 << 0)
#define PCI_VGA_STATE_CHANGE_DECODES (1 << 1)

int pci_set_vga_state(struct pci_dev *pdev, bool decode,
		      unsigned int command_bits, u32 flags);

#define PCI_IRQ_LEGACY		(1 << 0) /* allow legacy interrupts */
#define PCI_IRQ_MSI		(1 << 1) /* allow MSI interrupts */
#define PCI_IRQ_MSIX		(1 << 2) /* allow MSI-X interrupts */
#define PCI_IRQ_AFFINITY	(1 << 3) /* auto-assign affinity */
#define PCI_IRQ_ALL_TYPES \
	(PCI_IRQ_LEGACY | PCI_IRQ_MSI | PCI_IRQ_MSIX)

/* kmem_cache style wrapper around pci_alloc_consistent() */

#include <linux/pci-dma.h>
#include <linux/dmapool.h>

#define	pci_pool dma_pool
#define pci_pool_create(name, pdev, size, align, allocation) \
		dma_pool_create(name, &pdev->dev, size, align, allocation)
#define	pci_pool_destroy(pool) dma_pool_destroy(pool)
#define	pci_pool_alloc(pool, flags, handle) dma_pool_alloc(pool, flags, handle)
#define	pci_pool_zalloc(pool, flags, handle) \
		dma_pool_zalloc(pool, flags, handle)
#define	pci_pool_free(pool, vaddr, addr) dma_pool_free(pool, vaddr, addr)

struct msix_entry {
	u32	vector;	/* kernel uses to write allocated vector */
	u16	entry;	/* driver uses to specify entry, OS writes */
};

#ifdef CONFIG_PCI_MSI
int pci_msi_vec_count(struct pci_dev *dev);
void pci_disable_msi(struct pci_dev *dev);
int pci_msix_vec_count(struct pci_dev *dev);
void pci_disable_msix(struct pci_dev *dev);
void pci_restore_msi_state(struct pci_dev *dev);
int pci_msi_enabled(void);
int pci_enable_msi(struct pci_dev *dev);
int pci_enable_msix_range(struct pci_dev *dev, struct msix_entry *entries,
			  int minvec, int maxvec);
static inline int pci_enable_msix_exact(struct pci_dev *dev,
					struct msix_entry *entries, int nvec)
{
	int rc = pci_enable_msix_range(dev, entries, nvec, nvec);
	if (rc < 0)
		return rc;
	return 0;
}
int pci_alloc_irq_vectors_affinity(struct pci_dev *dev, unsigned int min_vecs,
				   unsigned int max_vecs, unsigned int flags,
				   const struct irq_affinity *affd);

void pci_free_irq_vectors(struct pci_dev *dev);
int pci_irq_vector(struct pci_dev *dev, unsigned int nr);
const struct cpumask *pci_irq_get_affinity(struct pci_dev *pdev, int vec);
int pci_irq_get_node(struct pci_dev *pdev, int vec);

#else
static inline int pci_msi_vec_count(struct pci_dev *dev) { return -ENOSYS; }
static inline void pci_disable_msi(struct pci_dev *dev) { }
static inline int pci_msix_vec_count(struct pci_dev *dev) { return -ENOSYS; }
static inline void pci_disable_msix(struct pci_dev *dev) { }
static inline void pci_restore_msi_state(struct pci_dev *dev) { }
static inline int pci_msi_enabled(void) { return 0; }
static inline int pci_enable_msi(struct pci_dev *dev)
{ return -ENOSYS; }
static inline int pci_enable_msix_range(struct pci_dev *dev,
		      struct msix_entry *entries, int minvec, int maxvec)
{ return -ENOSYS; }
static inline int pci_enable_msix_exact(struct pci_dev *dev,
		      struct msix_entry *entries, int nvec)
{ return -ENOSYS; }

static inline int
pci_alloc_irq_vectors_affinity(struct pci_dev *dev, unsigned int min_vecs,
			       unsigned int max_vecs, unsigned int flags,
			       const struct irq_affinity *aff_desc)
{
	if ((flags & PCI_IRQ_LEGACY) && min_vecs == 1 && dev->irq)
		return 1;
	return -ENOSPC;
}

static inline void pci_free_irq_vectors(struct pci_dev *dev)
{
}

static inline int pci_irq_vector(struct pci_dev *dev, unsigned int nr)
{
	if (WARN_ON_ONCE(nr > 0))
		return -EINVAL;
	return dev->irq;
}
static inline const struct cpumask *pci_irq_get_affinity(struct pci_dev *pdev,
		int vec)
{
	return cpu_possible_mask;
}

static inline int pci_irq_get_node(struct pci_dev *pdev, int vec)
{
	return first_online_node;
}
#endif

static inline int
pci_alloc_irq_vectors(struct pci_dev *dev, unsigned int min_vecs,
		      unsigned int max_vecs, unsigned int flags)
{
	return pci_alloc_irq_vectors_affinity(dev, min_vecs, max_vecs, flags,
					      NULL);
}

/**
 * pci_irqd_intx_xlate() - Translate PCI INTx value to an IRQ domain hwirq
 * @d: the INTx IRQ domain
 * @node: the DT node for the device whose interrupt we're translating
 * @intspec: the interrupt specifier data from the DT
 * @intsize: the number of entries in @intspec
 * @out_hwirq: pointer at which to write the hwirq number
 * @out_type: pointer at which to write the interrupt type
 *
 * Translate a PCI INTx interrupt number from device tree in the range 1-4, as
 * stored in the standard PCI_INTERRUPT_PIN register, to a value in the range
 * 0-3 suitable for use in a 4 entry IRQ domain. That is, subtract one from the
 * INTx value to obtain the hwirq number.
 *
 * Returns 0 on success, or -EINVAL if the interrupt specifier is out of range.
 */
static inline int pci_irqd_intx_xlate(struct irq_domain *d,
				      struct device_node *node,
				      const u32 *intspec,
				      unsigned int intsize,
				      unsigned long *out_hwirq,
				      unsigned int *out_type)
{
	const u32 intx = intspec[0];

	if (intx < PCI_INTERRUPT_INTA || intx > PCI_INTERRUPT_INTD)
		return -EINVAL;

	*out_hwirq = intx - PCI_INTERRUPT_INTA;
	return 0;
}

#ifdef CONFIG_PCIEPORTBUS
extern bool pcie_ports_disabled;
extern bool pcie_ports_auto;
#else
#define pcie_ports_disabled	true
#define pcie_ports_auto		false
#endif

#ifdef CONFIG_PCIEASPM
bool pcie_aspm_support_enabled(void);
#else
static inline bool pcie_aspm_support_enabled(void) { return false; }
#endif

#ifdef CONFIG_PCIEAER
void pci_no_aer(void);
bool pci_aer_available(void);
int pci_aer_init(struct pci_dev *dev);
#else
static inline void pci_no_aer(void) { }
static inline bool pci_aer_available(void) { return false; }
static inline int pci_aer_init(struct pci_dev *d) { return -ENODEV; }
#endif

#ifdef CONFIG_PCIE_ECRC
void pcie_set_ecrc_checking(struct pci_dev *dev);
void pcie_ecrc_get_policy(char *str);
#else
static inline void pcie_set_ecrc_checking(struct pci_dev *dev) { }
static inline void pcie_ecrc_get_policy(char *str) { }
#endif

#ifdef CONFIG_HT_IRQ
/* The functions a driver should call */
int  ht_create_irq(struct pci_dev *dev, int idx);
void ht_destroy_irq(unsigned int irq);
#endif /* CONFIG_HT_IRQ */

#ifdef CONFIG_PCI_ATS
/* Address Translation Service */
void pci_ats_init(struct pci_dev *dev);
int pci_enable_ats(struct pci_dev *dev, int ps);
void pci_disable_ats(struct pci_dev *dev);
int pci_ats_queue_depth(struct pci_dev *dev);
#else
static inline void pci_ats_init(struct pci_dev *d) { }
static inline int pci_enable_ats(struct pci_dev *d, int ps) { return -ENODEV; }
static inline void pci_disable_ats(struct pci_dev *d) { }
static inline int pci_ats_queue_depth(struct pci_dev *d) { return -ENODEV; }
#endif

#ifdef CONFIG_PCIE_PTM
int pci_enable_ptm(struct pci_dev *dev, u8 *granularity);
#else
static inline int pci_enable_ptm(struct pci_dev *dev, u8 *granularity)
{ return -EINVAL; }
#endif

void pci_cfg_access_lock(struct pci_dev *dev);
bool pci_cfg_access_trylock(struct pci_dev *dev);
void pci_cfg_access_unlock(struct pci_dev *dev);

/*
 * PCI domain support.  Sometimes called PCI segment (eg by ACPI),
 * a PCI domain is defined to be a set of PCI buses which share
 * configuration space.
 */
#ifdef CONFIG_PCI_DOMAINS
extern int pci_domains_supported;
int pci_get_new_domain_nr(void);
#else
enum { pci_domains_supported = 0 };
static inline int pci_domain_nr(struct pci_bus *bus) { return 0; }
static inline int pci_proc_domain(struct pci_bus *bus) { return 0; }
static inline int pci_get_new_domain_nr(void) { return -ENOSYS; }
#endif /* CONFIG_PCI_DOMAINS */

/*
 * Generic implementation for PCI domain support. If your
 * architecture does not need custom management of PCI
 * domains then this implementation will be used
 */
#ifdef CONFIG_PCI_DOMAINS_GENERIC
static inline int pci_domain_nr(struct pci_bus *bus)
{
	return bus->domain_nr;
}
#ifdef CONFIG_ACPI
int acpi_pci_bus_find_domain_nr(struct pci_bus *bus);
#else
static inline int acpi_pci_bus_find_domain_nr(struct pci_bus *bus)
{ return 0; }
#endif
int pci_bus_find_domain_nr(struct pci_bus *bus, struct device *parent);
#endif

/* some architectures require additional setup to direct VGA traffic */
typedef int (*arch_set_vga_state_t)(struct pci_dev *pdev, bool decode,
		      unsigned int command_bits, u32 flags);
void pci_register_set_vga_state(arch_set_vga_state_t func);

static inline int
pci_request_io_regions(struct pci_dev *pdev, const char *name)
{
	return pci_request_selected_regions(pdev,
			    pci_select_bars(pdev, IORESOURCE_IO), name);
}

static inline void
pci_release_io_regions(struct pci_dev *pdev)
{
	return pci_release_selected_regions(pdev,
			    pci_select_bars(pdev, IORESOURCE_IO));
}

static inline int
pci_request_mem_regions(struct pci_dev *pdev, const char *name)
{
	return pci_request_selected_regions(pdev,
			    pci_select_bars(pdev, IORESOURCE_MEM), name);
}

static inline void
pci_release_mem_regions(struct pci_dev *pdev)
{
	return pci_release_selected_regions(pdev,
			    pci_select_bars(pdev, IORESOURCE_MEM));
}

#else /* CONFIG_PCI is not enabled */

static inline void pci_set_flags(int flags) { }
static inline void pci_add_flags(int flags) { }
static inline void pci_clear_flags(int flags) { }
static inline int pci_has_flag(int flag) { return 0; }

/*
 *  If the system does not have PCI, clearly these return errors.  Define
 *  these as simple inline functions to avoid hair in drivers.
 */

#define _PCI_NOP(o, s, t) \
	static inline int pci_##o##_config_##s(struct pci_dev *dev, \
						int where, t val) \
		{ return PCIBIOS_FUNC_NOT_SUPPORTED; }

#define _PCI_NOP_ALL(o, x)	_PCI_NOP(o, byte, u8 x) \
				_PCI_NOP(o, word, u16 x) \
				_PCI_NOP(o, dword, u32 x)
_PCI_NOP_ALL(read, *)
_PCI_NOP_ALL(write,)

static inline struct pci_dev *pci_get_device(unsigned int vendor,
					     unsigned int device,
					     struct pci_dev *from)
{ return NULL; }

static inline struct pci_dev *pci_get_subsys(unsigned int vendor,
					     unsigned int device,
					     unsigned int ss_vendor,
					     unsigned int ss_device,
					     struct pci_dev *from)
{ return NULL; }

static inline struct pci_dev *pci_get_class(unsigned int class,
					    struct pci_dev *from)
{ return NULL; }

#define pci_dev_present(ids)	(0)
#define no_pci_devices()	(1)
#define pci_dev_put(dev)	do { } while (0)

static inline void pci_set_master(struct pci_dev *dev) { }
static inline int pci_enable_device(struct pci_dev *dev) { return -EIO; }
static inline void pci_disable_device(struct pci_dev *dev) { }
static inline int pci_assign_resource(struct pci_dev *dev, int i)
{ return -EBUSY; }
static inline int __pci_register_driver(struct pci_driver *drv,
					struct module *owner)
{ return 0; }
static inline int pci_register_driver(struct pci_driver *drv)
{ return 0; }
static inline void pci_unregister_driver(struct pci_driver *drv) { }
static inline int pci_find_capability(struct pci_dev *dev, int cap)
{ return 0; }
static inline int pci_find_next_capability(struct pci_dev *dev, u8 post,
					   int cap)
{ return 0; }
static inline int pci_find_ext_capability(struct pci_dev *dev, int cap)
{ return 0; }

/* Power management related routines */
static inline int pci_save_state(struct pci_dev *dev) { return 0; }
static inline void pci_restore_state(struct pci_dev *dev) { }
static inline int pci_set_power_state(struct pci_dev *dev, pci_power_t state)
{ return 0; }
static inline int pci_wake_from_d3(struct pci_dev *dev, bool enable)
{ return 0; }
static inline pci_power_t pci_choose_state(struct pci_dev *dev,
					   pm_message_t state)
{ return PCI_D0; }
static inline int pci_enable_wake(struct pci_dev *dev, pci_power_t state,
				  int enable)
{ return 0; }

static inline struct resource *pci_find_resource(struct pci_dev *dev,
						 struct resource *res)
{ return NULL; }
static inline int pci_request_regions(struct pci_dev *dev, const char *res_name)
{ return -EIO; }
static inline void pci_release_regions(struct pci_dev *dev) { }

static inline unsigned long pci_address_to_pio(phys_addr_t addr) { return -1; }

static inline void pci_block_cfg_access(struct pci_dev *dev) { }
static inline int pci_block_cfg_access_in_atomic(struct pci_dev *dev)
{ return 0; }
static inline void pci_unblock_cfg_access(struct pci_dev *dev) { }

static inline struct pci_bus *pci_find_next_bus(const struct pci_bus *from)
{ return NULL; }
static inline struct pci_dev *pci_get_slot(struct pci_bus *bus,
						unsigned int devfn)
{ return NULL; }
static inline struct pci_dev *pci_get_bus_and_slot(unsigned int bus,
						unsigned int devfn)
{ return NULL; }

static inline int pci_domain_nr(struct pci_bus *bus) { return 0; }
static inline struct pci_dev *pci_dev_get(struct pci_dev *dev) { return NULL; }
static inline int pci_get_new_domain_nr(void) { return -ENOSYS; }

#define dev_is_pci(d) (false)
#define dev_is_pf(d) (false)
static inline bool pci_acs_enabled(struct pci_dev *pdev, u16 acs_flags)
{ return false; }
#endif /* CONFIG_PCI */

/* Include architecture-dependent settings and functions */

#include <asm/pci.h>

/* These two functions provide almost identical functionality. Depennding
 * on the architecture, one will be implemented as a wrapper around the
 * other (in drivers/pci/mmap.c).
 *
 * pci_mmap_resource_range() maps a specific BAR, and vm->vm_pgoff
 * is expected to be an offset within that region.
 *
 * pci_mmap_page_range() is the legacy architecture-specific interface,
 * which accepts a "user visible" resource address converted by
 * pci_resource_to_user(), as used in the legacy mmap() interface in
 * /proc/bus/pci/.
 */
int pci_mmap_resource_range(struct pci_dev *dev, int bar,
			    struct vm_area_struct *vma,
			    enum pci_mmap_state mmap_state, int write_combine);
int pci_mmap_page_range(struct pci_dev *pdev, int bar,
			struct vm_area_struct *vma,
			enum pci_mmap_state mmap_state, int write_combine);

#ifndef arch_can_pci_mmap_wc
#define arch_can_pci_mmap_wc()		0
#endif

#ifndef arch_can_pci_mmap_io
#define arch_can_pci_mmap_io()		0
#define pci_iobar_pfn(pdev, bar, vma) (-EINVAL)
#else
int pci_iobar_pfn(struct pci_dev *pdev, int bar, struct vm_area_struct *vma);
#endif

#ifndef pci_root_bus_fwnode
#define pci_root_bus_fwnode(bus)	NULL
#endif

/* these helpers provide future and backwards compatibility
 * for accessing popular PCI BAR info */
#define pci_resource_start(dev, bar)	((dev)->resource[(bar)].start)
#define pci_resource_end(dev, bar)	((dev)->resource[(bar)].end)
#define pci_resource_flags(dev, bar)	((dev)->resource[(bar)].flags)
#define pci_resource_len(dev,bar) \
	((pci_resource_start((dev), (bar)) == 0 &&	\
	  pci_resource_end((dev), (bar)) ==		\
	  pci_resource_start((dev), (bar))) ? 0 :	\
							\
	 (pci_resource_end((dev), (bar)) -		\
	  pci_resource_start((dev), (bar)) + 1))

/* Similar to the helpers above, these manipulate per-pci_dev
 * driver-specific data.  They are really just a wrapper around
 * the generic device structure functions of these calls.
 */
static inline void *pci_get_drvdata(struct pci_dev *pdev)
{
	return dev_get_drvdata(&pdev->dev);
}

static inline void pci_set_drvdata(struct pci_dev *pdev, void *data)
{
	dev_set_drvdata(&pdev->dev, data);
}

/* If you want to know what to call your pci_dev, ask this function.
 * Again, it's a wrapper around the generic device.
 */
static inline const char *pci_name(const struct pci_dev *pdev)
{
	return dev_name(&pdev->dev);
}


/* Some archs don't want to expose struct resource to userland as-is
 * in sysfs and /proc
 */
#ifdef HAVE_ARCH_PCI_RESOURCE_TO_USER
void pci_resource_to_user(const struct pci_dev *dev, int bar,
			  const struct resource *rsrc,
			  resource_size_t *start, resource_size_t *end);
#else
static inline void pci_resource_to_user(const struct pci_dev *dev, int bar,
		const struct resource *rsrc, resource_size_t *start,
		resource_size_t *end)
{
	*start = rsrc->start;
	*end = rsrc->end;
}
#endif /* HAVE_ARCH_PCI_RESOURCE_TO_USER */


/*
 *  The world is not perfect and supplies us with broken PCI devices.
 *  For at least a part of these bugs we need a work-around, so both
 *  generic (drivers/pci/quirks.c) and per-architecture code can define
 *  fixup hooks to be called for particular buggy devices.
 */

struct pci_fixup {
	u16 vendor;		/* You can use PCI_ANY_ID here of course */
	u16 device;		/* You can use PCI_ANY_ID here of course */
	u32 class;		/* You can use PCI_ANY_ID here too */
	unsigned int class_shift;	/* should be 0, 8, 16 */
	void (*hook)(struct pci_dev *dev);
};

enum pci_fixup_pass {
	pci_fixup_early,	/* Before probing BARs */
	pci_fixup_header,	/* After reading configuration header */
	pci_fixup_final,	/* Final phase of device fixups */
	pci_fixup_enable,	/* pci_enable_device() time */
	pci_fixup_resume,	/* pci_device_resume() */
	pci_fixup_suspend,	/* pci_device_suspend() */
	pci_fixup_resume_early, /* pci_device_resume_early() */
	pci_fixup_suspend_late,	/* pci_device_suspend_late() */
};

/* Anonymous variables would be nice... */
#define DECLARE_PCI_FIXUP_SECTION(section, name, vendor, device, class,	\
				  class_shift, hook)			\
	static const struct pci_fixup __PASTE(__pci_fixup_##name,__LINE__) __used	\
	__attribute__((__section__(#section), aligned((sizeof(void *)))))    \
		= { vendor, device, class, class_shift, hook };

#define DECLARE_PCI_FIXUP_CLASS_EARLY(vendor, device, class,		\
					 class_shift, hook)		\
	DECLARE_PCI_FIXUP_SECTION(.pci_fixup_early,			\
		hook, vendor, device, class, class_shift, hook)
#define DECLARE_PCI_FIXUP_CLASS_HEADER(vendor, device, class,		\
					 class_shift, hook)		\
	DECLARE_PCI_FIXUP_SECTION(.pci_fixup_header,			\
		hook, vendor, device, class, class_shift, hook)
#define DECLARE_PCI_FIXUP_CLASS_FINAL(vendor, device, class,		\
					 class_shift, hook)		\
	DECLARE_PCI_FIXUP_SECTION(.pci_fixup_final,			\
		hook, vendor, device, class, class_shift, hook)
#define DECLARE_PCI_FIXUP_CLASS_ENABLE(vendor, device, class,		\
					 class_shift, hook)		\
	DECLARE_PCI_FIXUP_SECTION(.pci_fixup_enable,			\
		hook, vendor, device, class, class_shift, hook)
#define DECLARE_PCI_FIXUP_CLASS_RESUME(vendor, device, class,		\
					 class_shift, hook)		\
	DECLARE_PCI_FIXUP_SECTION(.pci_fixup_resume,			\
		resume##hook, vendor, device, class,	\
		class_shift, hook)
#define DECLARE_PCI_FIXUP_CLASS_RESUME_EARLY(vendor, device, class,	\
					 class_shift, hook)		\
	DECLARE_PCI_FIXUP_SECTION(.pci_fixup_resume_early,		\
		resume_early##hook, vendor, device,	\
		class, class_shift, hook)
#define DECLARE_PCI_FIXUP_CLASS_SUSPEND(vendor, device, class,		\
					 class_shift, hook)		\
	DECLARE_PCI_FIXUP_SECTION(.pci_fixup_suspend,			\
		suspend##hook, vendor, device, class,	\
		class_shift, hook)
#define DECLARE_PCI_FIXUP_CLASS_SUSPEND_LATE(vendor, device, class,	\
					 class_shift, hook)		\
	DECLARE_PCI_FIXUP_SECTION(.pci_fixup_suspend_late,		\
		suspend_late##hook, vendor, device,	\
		class, class_shift, hook)

#define DECLARE_PCI_FIXUP_EARLY(vendor, device, hook)			\
	DECLARE_PCI_FIXUP_SECTION(.pci_fixup_early,			\
		hook, vendor, device, PCI_ANY_ID, 0, hook)
#define DECLARE_PCI_FIXUP_HEADER(vendor, device, hook)			\
	DECLARE_PCI_FIXUP_SECTION(.pci_fixup_header,			\
		hook, vendor, device, PCI_ANY_ID, 0, hook)
#define DECLARE_PCI_FIXUP_FINAL(vendor, device, hook)			\
	DECLARE_PCI_FIXUP_SECTION(.pci_fixup_final,			\
		hook, vendor, device, PCI_ANY_ID, 0, hook)
#define DECLARE_PCI_FIXUP_ENABLE(vendor, device, hook)			\
	DECLARE_PCI_FIXUP_SECTION(.pci_fixup_enable,			\
		hook, vendor, device, PCI_ANY_ID, 0, hook)
#define DECLARE_PCI_FIXUP_RESUME(vendor, device, hook)			\
	DECLARE_PCI_FIXUP_SECTION(.pci_fixup_resume,			\
		resume##hook, vendor, device,		\
		PCI_ANY_ID, 0, hook)
#define DECLARE_PCI_FIXUP_RESUME_EARLY(vendor, device, hook)		\
	DECLARE_PCI_FIXUP_SECTION(.pci_fixup_resume_early,		\
		resume_early##hook, vendor, device,	\
		PCI_ANY_ID, 0, hook)
#define DECLARE_PCI_FIXUP_SUSPEND(vendor, device, hook)			\
	DECLARE_PCI_FIXUP_SECTION(.pci_fixup_suspend,			\
		suspend##hook, vendor, device,		\
		PCI_ANY_ID, 0, hook)
#define DECLARE_PCI_FIXUP_SUSPEND_LATE(vendor, device, hook)		\
	DECLARE_PCI_FIXUP_SECTION(.pci_fixup_suspend_late,		\
		suspend_late##hook, vendor, device,	\
		PCI_ANY_ID, 0, hook)

#ifdef CONFIG_PCI_QUIRKS
void pci_fixup_device(enum pci_fixup_pass pass, struct pci_dev *dev);
int pci_dev_specific_acs_enabled(struct pci_dev *dev, u16 acs_flags);
int pci_dev_specific_enable_acs(struct pci_dev *dev);
#else
static inline void pci_fixup_device(enum pci_fixup_pass pass,
				    struct pci_dev *dev) { }
static inline int pci_dev_specific_acs_enabled(struct pci_dev *dev,
					       u16 acs_flags)
{
	return -ENOTTY;
}
static inline int pci_dev_specific_enable_acs(struct pci_dev *dev)
{
	return -ENOTTY;
}
#endif

void __iomem *pcim_iomap(struct pci_dev *pdev, int bar, unsigned long maxlen);
void pcim_iounmap(struct pci_dev *pdev, void __iomem *addr);
void __iomem * const *pcim_iomap_table(struct pci_dev *pdev);
int pcim_iomap_regions(struct pci_dev *pdev, int mask, const char *name);
int pcim_iomap_regions_request_all(struct pci_dev *pdev, int mask,
				   const char *name);
void pcim_iounmap_regions(struct pci_dev *pdev, int mask);

extern int pci_pci_problems;
#define PCIPCI_FAIL		1	/* No PCI PCI DMA */
#define PCIPCI_TRITON		2
#define PCIPCI_NATOMA		4
#define PCIPCI_VIAETBF		8
#define PCIPCI_VSFX		16
#define PCIPCI_ALIMAGIK		32	/* Need low latency setting */
#define PCIAGP_FAIL		64	/* No PCI to AGP DMA */

extern unsigned long pci_cardbus_io_size;
extern unsigned long pci_cardbus_mem_size;
extern u8 pci_dfl_cache_line_size;
extern u8 pci_cache_line_size;

extern unsigned long pci_hotplug_io_size;
extern unsigned long pci_hotplug_mem_size;
extern unsigned long pci_hotplug_bus_size;

/* Architecture-specific versions may override these (weak) */
void pcibios_disable_device(struct pci_dev *dev);
void pcibios_set_master(struct pci_dev *dev);
int pcibios_set_pcie_reset_state(struct pci_dev *dev,
				 enum pcie_reset_state state);
int pcibios_add_device(struct pci_dev *dev);
void pcibios_release_device(struct pci_dev *dev);
void pcibios_penalize_isa_irq(int irq, int active);
int pcibios_alloc_irq(struct pci_dev *dev);
void pcibios_free_irq(struct pci_dev *dev);

#ifdef CONFIG_HIBERNATE_CALLBACKS
extern struct dev_pm_ops pcibios_pm_ops;
#endif

#if defined(CONFIG_PCI_MMCONFIG) || defined(CONFIG_ACPI_MCFG)
void __init pci_mmcfg_early_init(void);
void __init pci_mmcfg_late_init(void);
#else
static inline void pci_mmcfg_early_init(void) { }
static inline void pci_mmcfg_late_init(void) { }
#endif

int pci_ext_cfg_avail(void);

void __iomem *pci_ioremap_bar(struct pci_dev *pdev, int bar);
void __iomem *pci_ioremap_wc_bar(struct pci_dev *pdev, int bar);

#ifdef CONFIG_PCI_IOV
int pci_iov_virtfn_bus(struct pci_dev *dev, int id);
int pci_iov_virtfn_devfn(struct pci_dev *dev, int id);

int pci_enable_sriov(struct pci_dev *dev, int nr_virtfn);
void pci_disable_sriov(struct pci_dev *dev);
int pci_iov_add_virtfn(struct pci_dev *dev, int id, int reset);
void pci_iov_remove_virtfn(struct pci_dev *dev, int id, int reset);
int pci_num_vf(struct pci_dev *dev);
int pci_vfs_assigned(struct pci_dev *dev);
int pci_sriov_set_totalvfs(struct pci_dev *dev, u16 numvfs);
int pci_sriov_get_totalvfs(struct pci_dev *dev);
resource_size_t pci_iov_resource_size(struct pci_dev *dev, int resno);
#else
static inline int pci_iov_virtfn_bus(struct pci_dev *dev, int id)
{
	return -ENOSYS;
}
static inline int pci_iov_virtfn_devfn(struct pci_dev *dev, int id)
{
	return -ENOSYS;
}
static inline int pci_enable_sriov(struct pci_dev *dev, int nr_virtfn)
{ return -ENODEV; }
static inline int pci_iov_add_virtfn(struct pci_dev *dev, int id, int reset)
{
	return -ENOSYS;
}
static inline void pci_iov_remove_virtfn(struct pci_dev *dev,
					 int id, int reset) { }
static inline void pci_disable_sriov(struct pci_dev *dev) { }
static inline int pci_num_vf(struct pci_dev *dev) { return 0; }
static inline int pci_vfs_assigned(struct pci_dev *dev)
{ return 0; }
static inline int pci_sriov_set_totalvfs(struct pci_dev *dev, u16 numvfs)
{ return 0; }
static inline int pci_sriov_get_totalvfs(struct pci_dev *dev)
{ return 0; }
static inline resource_size_t pci_iov_resource_size(struct pci_dev *dev, int resno)
{ return 0; }
#endif

#if defined(CONFIG_HOTPLUG_PCI) || defined(CONFIG_HOTPLUG_PCI_MODULE)
void pci_hp_create_module_link(struct pci_slot *pci_slot);
void pci_hp_remove_module_link(struct pci_slot *pci_slot);
#endif

/**
 * pci_pcie_cap - get the saved PCIe capability offset
 * @dev: PCI device
 *
 * PCIe capability offset is calculated at PCI device initialization
 * time and saved in the data structure. This function returns saved
 * PCIe capability offset. Using this instead of pci_find_capability()
 * reduces unnecessary search in the PCI configuration space. If you
 * need to calculate PCIe capability offset from raw device for some
 * reasons, please use pci_find_capability() instead.
 */
static inline int pci_pcie_cap(struct pci_dev *dev)
{
	return dev->pcie_cap;
}

/**
 * pci_is_pcie - check if the PCI device is PCI Express capable
 * @dev: PCI device
 *
 * Returns: true if the PCI device is PCI Express capable, false otherwise.
 */
static inline bool pci_is_pcie(struct pci_dev *dev)
{
	return pci_pcie_cap(dev);
}

/**
 * pcie_caps_reg - get the PCIe Capabilities Register
 * @dev: PCI device
 */
static inline u16 pcie_caps_reg(const struct pci_dev *dev)
{
	return dev->pcie_flags_reg;
}

/**
 * pci_pcie_type - get the PCIe device/port type
 * @dev: PCI device
 */
static inline int pci_pcie_type(const struct pci_dev *dev)
{
	return (pcie_caps_reg(dev) & PCI_EXP_FLAGS_TYPE) >> 4;
}

static inline struct pci_dev *pcie_find_root_port(struct pci_dev *dev)
{
	while (1) {
		if (!pci_is_pcie(dev))
			break;
		if (pci_pcie_type(dev) == PCI_EXP_TYPE_ROOT_PORT)
			return dev;
		if (!dev->bus->self)
			break;
		dev = dev->bus->self;
	}
	return NULL;
}

void pci_request_acs(void);
bool pci_acs_enabled(struct pci_dev *pdev, u16 acs_flags);
bool pci_acs_path_enabled(struct pci_dev *start,
			  struct pci_dev *end, u16 acs_flags);

#define PCI_VPD_LRDT			0x80	/* Large Resource Data Type */
#define PCI_VPD_LRDT_ID(x)		((x) | PCI_VPD_LRDT)

/* Large Resource Data Type Tag Item Names */
#define PCI_VPD_LTIN_ID_STRING		0x02	/* Identifier String */
#define PCI_VPD_LTIN_RO_DATA		0x10	/* Read-Only Data */
#define PCI_VPD_LTIN_RW_DATA		0x11	/* Read-Write Data */

#define PCI_VPD_LRDT_ID_STRING		PCI_VPD_LRDT_ID(PCI_VPD_LTIN_ID_STRING)
#define PCI_VPD_LRDT_RO_DATA		PCI_VPD_LRDT_ID(PCI_VPD_LTIN_RO_DATA)
#define PCI_VPD_LRDT_RW_DATA		PCI_VPD_LRDT_ID(PCI_VPD_LTIN_RW_DATA)

/* Small Resource Data Type Tag Item Names */
#define PCI_VPD_STIN_END		0x0f	/* End */

#define PCI_VPD_SRDT_END		(PCI_VPD_STIN_END << 3)

#define PCI_VPD_SRDT_TIN_MASK		0x78
#define PCI_VPD_SRDT_LEN_MASK		0x07
#define PCI_VPD_LRDT_TIN_MASK		0x7f

#define PCI_VPD_LRDT_TAG_SIZE		3
#define PCI_VPD_SRDT_TAG_SIZE		1

#define PCI_VPD_INFO_FLD_HDR_SIZE	3

#define PCI_VPD_RO_KEYWORD_PARTNO	"PN"
#define PCI_VPD_RO_KEYWORD_MFR_ID	"MN"
#define PCI_VPD_RO_KEYWORD_VENDOR0	"V0"
#define PCI_VPD_RO_KEYWORD_CHKSUM	"RV"

/**
 * pci_vpd_lrdt_size - Extracts the Large Resource Data Type length
 * @lrdt: Pointer to the beginning of the Large Resource Data Type tag
 *
 * Returns the extracted Large Resource Data Type length.
 */
static inline u16 pci_vpd_lrdt_size(const u8 *lrdt)
{
	return (u16)lrdt[1] + ((u16)lrdt[2] << 8);
}

/**
 * pci_vpd_lrdt_tag - Extracts the Large Resource Data Type Tag Item
 * @lrdt: Pointer to the beginning of the Large Resource Data Type tag
 *
 * Returns the extracted Large Resource Data Type Tag item.
 */
static inline u16 pci_vpd_lrdt_tag(const u8 *lrdt)
{
    return (u16)(lrdt[0] & PCI_VPD_LRDT_TIN_MASK);
}

/**
 * pci_vpd_srdt_size - Extracts the Small Resource Data Type length
 * @srdt: Pointer to the beginning of the Small Resource Data Type tag
 *
 * Returns the extracted Small Resource Data Type length.
 */
static inline u8 pci_vpd_srdt_size(const u8 *srdt)
{
	return (*srdt) & PCI_VPD_SRDT_LEN_MASK;
}

/**
 * pci_vpd_srdt_tag - Extracts the Small Resource Data Type Tag Item
 * @srdt: Pointer to the beginning of the Small Resource Data Type tag
 *
 * Returns the extracted Small Resource Data Type Tag Item.
 */
static inline u8 pci_vpd_srdt_tag(const u8 *srdt)
{
	return ((*srdt) & PCI_VPD_SRDT_TIN_MASK) >> 3;
}

/**
 * pci_vpd_info_field_size - Extracts the information field length
 * @lrdt: Pointer to the beginning of an information field header
 *
 * Returns the extracted information field length.
 */
static inline u8 pci_vpd_info_field_size(const u8 *info_field)
{
	return info_field[2];
}

/**
 * pci_vpd_find_tag - Locates the Resource Data Type tag provided
 * @buf: Pointer to buffered vpd data
 * @off: The offset into the buffer at which to begin the search
 * @len: The length of the vpd buffer
 * @rdt: The Resource Data Type to search for
 *
 * Returns the index where the Resource Data Type was found or
 * -ENOENT otherwise.
 */
int pci_vpd_find_tag(const u8 *buf, unsigned int off, unsigned int len, u8 rdt);

/**
 * pci_vpd_find_info_keyword - Locates an information field keyword in the VPD
 * @buf: Pointer to buffered vpd data
 * @off: The offset into the buffer at which to begin the search
 * @len: The length of the buffer area, relative to off, in which to search
 * @kw: The keyword to search for
 *
 * Returns the index where the information field keyword was found or
 * -ENOENT otherwise.
 */
int pci_vpd_find_info_keyword(const u8 *buf, unsigned int off,
			      unsigned int len, const char *kw);

/* PCI <-> OF binding helpers */
#ifdef CONFIG_OF
struct device_node;
struct irq_domain;
void pci_set_of_node(struct pci_dev *dev);
void pci_release_of_node(struct pci_dev *dev);
void pci_set_bus_of_node(struct pci_bus *bus);
void pci_release_bus_of_node(struct pci_bus *bus);
struct irq_domain *pci_host_bridge_of_msi_domain(struct pci_bus *bus);

/* Arch may override this (weak) */
struct device_node *pcibios_get_phb_of_node(struct pci_bus *bus);

static inline struct device_node *
pci_device_to_OF_node(const struct pci_dev *pdev)
{
	return pdev ? pdev->dev.of_node : NULL;
}

static inline struct device_node *pci_bus_to_OF_node(struct pci_bus *bus)
{
	return bus ? bus->dev.of_node : NULL;
}

#else /* CONFIG_OF */
static inline void pci_set_of_node(struct pci_dev *dev) { }
static inline void pci_release_of_node(struct pci_dev *dev) { }
static inline void pci_set_bus_of_node(struct pci_bus *bus) { }
static inline void pci_release_bus_of_node(struct pci_bus *bus) { }
static inline struct device_node *
pci_device_to_OF_node(const struct pci_dev *pdev) { return NULL; }
static inline struct irq_domain *
pci_host_bridge_of_msi_domain(struct pci_bus *bus) { return NULL; }
#endif  /* CONFIG_OF */

#ifdef CONFIG_ACPI
struct irq_domain *pci_host_bridge_acpi_msi_domain(struct pci_bus *bus);

void
pci_msi_register_fwnode_provider(struct fwnode_handle *(*fn)(struct device *));
#else
static inline struct irq_domain *
pci_host_bridge_acpi_msi_domain(struct pci_bus *bus) { return NULL; }
#endif

#ifdef CONFIG_EEH
static inline struct eeh_dev *pci_dev_to_eeh_dev(struct pci_dev *pdev)
{
	return pdev->dev.archdata.edev;
}
#endif

void pci_add_dma_alias(struct pci_dev *dev, u8 devfn);
bool pci_devs_are_dma_aliases(struct pci_dev *dev1, struct pci_dev *dev2);
int pci_for_each_dma_alias(struct pci_dev *pdev,
			   int (*fn)(struct pci_dev *pdev,
				     u16 alias, void *data), void *data);

/* helper functions for operation of device flag */
static inline void pci_set_dev_assigned(struct pci_dev *pdev)
{
	pdev->dev_flags |= PCI_DEV_FLAGS_ASSIGNED;
}
static inline void pci_clear_dev_assigned(struct pci_dev *pdev)
{
	pdev->dev_flags &= ~PCI_DEV_FLAGS_ASSIGNED;
}
static inline bool pci_is_dev_assigned(struct pci_dev *pdev)
{
	return (pdev->dev_flags & PCI_DEV_FLAGS_ASSIGNED) == PCI_DEV_FLAGS_ASSIGNED;
}

/**
 * pci_ari_enabled - query ARI forwarding status
 * @bus: the PCI bus
 *
 * Returns true if ARI forwarding is enabled.
 */
static inline bool pci_ari_enabled(struct pci_bus *bus)
{
	return bus->self && bus->self->ari_enabled;
}

/**
 * pci_is_thunderbolt_attached - whether device is on a Thunderbolt daisy chain
 * @pdev: PCI device to check
 *
 * Walk upwards from @pdev and check for each encountered bridge if it's part
 * of a Thunderbolt controller.  Reaching the host bridge means @pdev is not
 * Thunderbolt-attached.  (But rather soldered to the mainboard usually.)
 */
static inline bool pci_is_thunderbolt_attached(struct pci_dev *pdev)
{
	struct pci_dev *parent = pdev;

	if (pdev->is_thunderbolt)
		return true;

	while ((parent = pci_upstream_bridge(parent)))
		if (parent->is_thunderbolt)
			return true;

	return false;
}

/* provide the legacy pci_dma_* API */
#include <linux/pci-dma-compat.h>

#endif /* LINUX_PCI_H */
