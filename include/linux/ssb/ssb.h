#ifndef LINUX_SSB_H_
#define LINUX_SSB_H_

#include <linux/device.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/pci.h>
#include <linux/mod_devicetable.h>
#include <linux/dma-mapping.h>

#include <linux/ssb/ssb_regs.h>


struct pcmcia_device;
struct ssb_bus;
struct ssb_driver;

struct ssb_sprom {
	u8 revision;
	u8 il0mac[6];		/* MAC address for 802.11b/g */
	u8 et0mac[6];		/* MAC address for Ethernet */
	u8 et1mac[6];		/* MAC address for 802.11a */
	u8 et0phyaddr;		/* MII address for enet0 */
	u8 et1phyaddr;		/* MII address for enet1 */
	u8 et0mdcport;		/* MDIO for enet0 */
	u8 et1mdcport;		/* MDIO for enet1 */
	u8 board_rev;		/* Board revision number from SPROM. */
	u8 country_code;	/* Country Code */
	u8 ant_available_a;	/* A-PHY antenna available bits (up to 4) */
	u8 ant_available_bg;	/* B/G-PHY antenna available bits (up to 4) */
	u16 pa0b0;
	u16 pa0b1;
	u16 pa0b2;
	u16 pa1b0;
	u16 pa1b1;
	u16 pa1b2;
	u8 gpio0;		/* GPIO pin 0 */
	u8 gpio1;		/* GPIO pin 1 */
	u8 gpio2;		/* GPIO pin 2 */
	u8 gpio3;		/* GPIO pin 3 */
	u16 maxpwr_a;		/* A-PHY Amplifier Max Power (in dBm Q5.2) */
	u16 maxpwr_bg;		/* B/G-PHY Amplifier Max Power (in dBm Q5.2) */
	u8 itssi_a;		/* Idle TSSI Target for A-PHY */
	u8 itssi_bg;		/* Idle TSSI Target for B/G-PHY */
	u16 boardflags_lo;	/* Boardflags (low 16 bits) */
	u16 boardflags_hi;	/* Boardflags (high 16 bits) */

	/* Antenna gain values for up to 4 antennas
	 * on each band. Values in dBm/4 (Q5.2). Negative gain means the
	 * loss in the connectors is bigger than the gain. */
	struct {
		struct {
			s8 a0, a1, a2, a3;
		} ghz24;	/* 2.4GHz band */
		struct {
			s8 a0, a1, a2, a3;
		} ghz5;		/* 5GHz band */
	} antenna_gain;

	/* TODO - add any parameters needed from rev 2, 3, or 4 SPROMs */
};

/* Information about the PCB the circuitry is soldered on. */
struct ssb_boardinfo {
	u16 vendor;
	u16 type;
	u16 rev;
};


struct ssb_device;
/* Lowlevel read/write operations on the device MMIO.
 * Internal, don't use that outside of ssb. */
struct ssb_bus_ops {
	u8 (*read8)(struct ssb_device *dev, u16 offset);
	u16 (*read16)(struct ssb_device *dev, u16 offset);
	u32 (*read32)(struct ssb_device *dev, u16 offset);
	void (*write8)(struct ssb_device *dev, u16 offset, u8 value);
	void (*write16)(struct ssb_device *dev, u16 offset, u16 value);
	void (*write32)(struct ssb_device *dev, u16 offset, u32 value);
#ifdef CONFIG_SSB_BLOCKIO
	void (*block_read)(struct ssb_device *dev, void *buffer,
			   size_t count, u16 offset, u8 reg_width);
	void (*block_write)(struct ssb_device *dev, const void *buffer,
			    size_t count, u16 offset, u8 reg_width);
#endif
};


/* Core-ID values. */
#define SSB_DEV_CHIPCOMMON	0x800
#define SSB_DEV_ILINE20		0x801
#define SSB_DEV_SDRAM		0x803
#define SSB_DEV_PCI		0x804
#define SSB_DEV_MIPS		0x805
#define SSB_DEV_ETHERNET	0x806
#define SSB_DEV_V90		0x807
#define SSB_DEV_USB11_HOSTDEV	0x808
#define SSB_DEV_ADSL		0x809
#define SSB_DEV_ILINE100	0x80A
#define SSB_DEV_IPSEC		0x80B
#define SSB_DEV_PCMCIA		0x80D
#define SSB_DEV_INTERNAL_MEM	0x80E
#define SSB_DEV_MEMC_SDRAM	0x80F
#define SSB_DEV_EXTIF		0x811
#define SSB_DEV_80211		0x812
#define SSB_DEV_MIPS_3302	0x816
#define SSB_DEV_USB11_HOST	0x817
#define SSB_DEV_USB11_DEV	0x818
#define SSB_DEV_USB20_HOST	0x819
#define SSB_DEV_USB20_DEV	0x81A
#define SSB_DEV_SDIO_HOST	0x81B
#define SSB_DEV_ROBOSWITCH	0x81C
#define SSB_DEV_PARA_ATA	0x81D
#define SSB_DEV_SATA_XORDMA	0x81E
#define SSB_DEV_ETHERNET_GBIT	0x81F
#define SSB_DEV_PCIE		0x820
#define SSB_DEV_MIMO_PHY	0x821
#define SSB_DEV_SRAM_CTRLR	0x822
#define SSB_DEV_MINI_MACPHY	0x823
#define SSB_DEV_ARM_1176	0x824
#define SSB_DEV_ARM_7TDMI	0x825

/* Vendor-ID values */
#define SSB_VENDOR_BROADCOM	0x4243

/* Some kernel subsystems poke with dev->drvdata, so we must use the
 * following ugly workaround to get from struct device to struct ssb_device */
struct __ssb_dev_wrapper {
	struct device dev;
	struct ssb_device *sdev;
};

struct ssb_device {
	/* Having a copy of the ops pointer in each dev struct
	 * is an optimization. */
	const struct ssb_bus_ops *ops;

	struct device *dev;

	struct ssb_bus *bus;
	struct ssb_device_id id;

	u8 core_index;
	unsigned int irq;

	/* Internal-only stuff follows. */
	void *drvdata;		/* Per-device data */
	void *devtypedata;	/* Per-devicetype (eg 802.11) data */
};

/* Go from struct device to struct ssb_device. */
static inline
struct ssb_device * dev_to_ssb_dev(struct device *dev)
{
	struct __ssb_dev_wrapper *wrap;
	wrap = container_of(dev, struct __ssb_dev_wrapper, dev);
	return wrap->sdev;
}

/* Device specific user data */
static inline
void ssb_set_drvdata(struct ssb_device *dev, void *data)
{
	dev->drvdata = data;
}
static inline
void * ssb_get_drvdata(struct ssb_device *dev)
{
	return dev->drvdata;
}

/* Devicetype specific user data. This is per device-type (not per device) */
void ssb_set_devtypedata(struct ssb_device *dev, void *data);
static inline
void * ssb_get_devtypedata(struct ssb_device *dev)
{
	return dev->devtypedata;
}


struct ssb_driver {
	const char *name;
	const struct ssb_device_id *id_table;

	int (*probe)(struct ssb_device *dev, const struct ssb_device_id *id);
	void (*remove)(struct ssb_device *dev);
	int (*suspend)(struct ssb_device *dev, pm_message_t state);
	int (*resume)(struct ssb_device *dev);
	void (*shutdown)(struct ssb_device *dev);

	struct device_driver drv;
};
#define drv_to_ssb_drv(_drv) container_of(_drv, struct ssb_driver, drv)

extern int __ssb_driver_register(struct ssb_driver *drv, struct module *owner);
static inline int ssb_driver_register(struct ssb_driver *drv)
{
	return __ssb_driver_register(drv, THIS_MODULE);
}
extern void ssb_driver_unregister(struct ssb_driver *drv);




enum ssb_bustype {
	SSB_BUSTYPE_SSB,	/* This SSB bus is the system bus */
	SSB_BUSTYPE_PCI,	/* SSB is connected to PCI bus */
	SSB_BUSTYPE_PCMCIA,	/* SSB is connected to PCMCIA bus */
};

/* board_vendor */
#define SSB_BOARDVENDOR_BCM	0x14E4	/* Broadcom */
#define SSB_BOARDVENDOR_DELL	0x1028	/* Dell */
#define SSB_BOARDVENDOR_HP	0x0E11	/* HP */
/* board_type */
#define SSB_BOARD_BCM94306MP	0x0418
#define SSB_BOARD_BCM4309G	0x0421
#define SSB_BOARD_BCM4306CB	0x0417
#define SSB_BOARD_BCM4309MP	0x040C
#define SSB_BOARD_MP4318	0x044A
#define SSB_BOARD_BU4306	0x0416
#define SSB_BOARD_BU4309	0x040A
/* chip_package */
#define SSB_CHIPPACK_BCM4712S	1	/* Small 200pin 4712 */
#define SSB_CHIPPACK_BCM4712M	2	/* Medium 225pin 4712 */
#define SSB_CHIPPACK_BCM4712L	0	/* Large 340pin 4712 */

#include <linux/ssb/ssb_driver_chipcommon.h>
#include <linux/ssb/ssb_driver_mips.h>
#include <linux/ssb/ssb_driver_extif.h>
#include <linux/ssb/ssb_driver_pci.h>

struct ssb_bus {
	/* The MMIO area. */
	void __iomem *mmio;

	const struct ssb_bus_ops *ops;

	/* The core in the basic address register window. (PCI bus only) */
	struct ssb_device *mapped_device;
	/* Currently mapped PCMCIA segment. (bustype == SSB_BUSTYPE_PCMCIA only) */
	u8 mapped_pcmcia_seg;
	/* Lock for core and segment switching.
	 * On PCMCIA-host busses this is used to protect the whole MMIO access. */
	spinlock_t bar_lock;

	/* The bus this backplane is running on. */
	enum ssb_bustype bustype;
	/* Pointer to the PCI bus (only valid if bustype == SSB_BUSTYPE_PCI). */
	struct pci_dev *host_pci;
	/* Pointer to the PCMCIA device (only if bustype == SSB_BUSTYPE_PCMCIA). */
	struct pcmcia_device *host_pcmcia;

#ifdef CONFIG_SSB_SPROM
	/* Mutex to protect the SPROM writing. */
	struct mutex sprom_mutex;
#endif

	/* ID information about the Chip. */
	u16 chip_id;
	u16 chip_rev;
	u16 sprom_size;		/* number of words in sprom */
	u8 chip_package;

	/* List of devices (cores) on the backplane. */
	struct ssb_device devices[SSB_MAX_NR_CORES];
	u8 nr_devices;

	/* Software ID number for this bus. */
	unsigned int busnumber;

	/* The ChipCommon device (if available). */
	struct ssb_chipcommon chipco;
	/* The PCI-core device (if available). */
	struct ssb_pcicore pcicore;
	/* The MIPS-core device (if available). */
	struct ssb_mipscore mipscore;
	/* The EXTif-core device (if available). */
	struct ssb_extif extif;

	/* The following structure elements are not available in early
	 * SSB initialization. Though, they are available for regular
	 * registered drivers at any stage. So be careful when
	 * using them in the ssb core code. */

	/* ID information about the PCB. */
	struct ssb_boardinfo boardinfo;
	/* Contents of the SPROM. */
	struct ssb_sprom sprom;
	/* If the board has a cardbus slot, this is set to true. */
	bool has_cardbus_slot;

#ifdef CONFIG_SSB_EMBEDDED
	/* Lock for GPIO register access. */
	spinlock_t gpio_lock;
#endif /* EMBEDDED */

	/* Internal-only stuff follows. Do not touch. */
	struct list_head list;
#ifdef CONFIG_SSB_DEBUG
	/* Is the bus already powered up? */
	bool powered_up;
	int power_warn_count;
#endif /* DEBUG */
};

/* The initialization-invariants. */
struct ssb_init_invariants {
	/* Versioning information about the PCB. */
	struct ssb_boardinfo boardinfo;
	/* The SPROM information. That's either stored in an
	 * EEPROM or NVRAM on the board. */
	struct ssb_sprom sprom;
	/* If the board has a cardbus slot, this is set to true. */
	bool has_cardbus_slot;
};
/* Type of function to fetch the invariants. */
typedef int (*ssb_invariants_func_t)(struct ssb_bus *bus,
				     struct ssb_init_invariants *iv);

/* Register a SSB system bus. get_invariants() is called after the
 * basic system devices are initialized.
 * The invariants are usually fetched from some NVRAM.
 * Put the invariants into the struct pointed to by iv. */
extern int ssb_bus_ssbbus_register(struct ssb_bus *bus,
				   unsigned long baseaddr,
				   ssb_invariants_func_t get_invariants);
#ifdef CONFIG_SSB_PCIHOST
extern int ssb_bus_pcibus_register(struct ssb_bus *bus,
				   struct pci_dev *host_pci);
#endif /* CONFIG_SSB_PCIHOST */
#ifdef CONFIG_SSB_PCMCIAHOST
extern int ssb_bus_pcmciabus_register(struct ssb_bus *bus,
				      struct pcmcia_device *pcmcia_dev,
				      unsigned long baseaddr);
#endif /* CONFIG_SSB_PCMCIAHOST */

extern void ssb_bus_unregister(struct ssb_bus *bus);

/* Suspend a SSB bus.
 * Call this from the parent bus suspend routine. */
extern int ssb_bus_suspend(struct ssb_bus *bus);
/* Resume a SSB bus.
 * Call this from the parent bus resume routine. */
extern int ssb_bus_resume(struct ssb_bus *bus);

extern u32 ssb_clockspeed(struct ssb_bus *bus);

/* Is the device enabled in hardware? */
int ssb_device_is_enabled(struct ssb_device *dev);
/* Enable a device and pass device-specific SSB_TMSLOW flags.
 * If no device-specific flags are available, use 0. */
void ssb_device_enable(struct ssb_device *dev, u32 core_specific_flags);
/* Disable a device in hardware and pass SSB_TMSLOW flags (if any). */
void ssb_device_disable(struct ssb_device *dev, u32 core_specific_flags);


/* Device MMIO register read/write functions. */
static inline u8 ssb_read8(struct ssb_device *dev, u16 offset)
{
	return dev->ops->read8(dev, offset);
}
static inline u16 ssb_read16(struct ssb_device *dev, u16 offset)
{
	return dev->ops->read16(dev, offset);
}
static inline u32 ssb_read32(struct ssb_device *dev, u16 offset)
{
	return dev->ops->read32(dev, offset);
}
static inline void ssb_write8(struct ssb_device *dev, u16 offset, u8 value)
{
	dev->ops->write8(dev, offset, value);
}
static inline void ssb_write16(struct ssb_device *dev, u16 offset, u16 value)
{
	dev->ops->write16(dev, offset, value);
}
static inline void ssb_write32(struct ssb_device *dev, u16 offset, u32 value)
{
	dev->ops->write32(dev, offset, value);
}
#ifdef CONFIG_SSB_BLOCKIO
static inline void ssb_block_read(struct ssb_device *dev, void *buffer,
				  size_t count, u16 offset, u8 reg_width)
{
	dev->ops->block_read(dev, buffer, count, offset, reg_width);
}

static inline void ssb_block_write(struct ssb_device *dev, const void *buffer,
				   size_t count, u16 offset, u8 reg_width)
{
	dev->ops->block_write(dev, buffer, count, offset, reg_width);
}
#endif /* CONFIG_SSB_BLOCKIO */


/* The SSB DMA API. Use this API for any DMA operation on the device.
 * This API basically is a wrapper that calls the correct DMA API for
 * the host device type the SSB device is attached to. */

/* Translation (routing) bits that need to be ORed to DMA
 * addresses before they are given to a device. */
extern u32 ssb_dma_translation(struct ssb_device *dev);
#define SSB_DMA_TRANSLATION_MASK	0xC0000000
#define SSB_DMA_TRANSLATION_SHIFT	30

extern int ssb_dma_set_mask(struct ssb_device *dev, u64 mask);

extern void * ssb_dma_alloc_consistent(struct ssb_device *dev, size_t size,
				       dma_addr_t *dma_handle, gfp_t gfp_flags);
extern void ssb_dma_free_consistent(struct ssb_device *dev, size_t size,
				    void *vaddr, dma_addr_t dma_handle,
				    gfp_t gfp_flags);

static inline void __cold __ssb_dma_not_implemented(struct ssb_device *dev)
{
#ifdef CONFIG_SSB_DEBUG
	printk(KERN_ERR "SSB: BUG! Calling DMA API for "
	       "unsupported bustype %d\n", dev->bus->bustype);
#endif /* DEBUG */
}

static inline int ssb_dma_mapping_error(struct ssb_device *dev, dma_addr_t addr)
{
	switch (dev->bus->bustype) {
	case SSB_BUSTYPE_PCI:
#ifdef CONFIG_SSB_PCIHOST
		return pci_dma_mapping_error(dev->bus->host_pci, addr);
#endif
		break;
	case SSB_BUSTYPE_SSB:
		return dma_mapping_error(dev->dev, addr);
	default:
		break;
	}
	__ssb_dma_not_implemented(dev);
	return -ENOSYS;
}

static inline dma_addr_t ssb_dma_map_single(struct ssb_device *dev, void *p,
					    size_t size, enum dma_data_direction dir)
{
	switch (dev->bus->bustype) {
	case SSB_BUSTYPE_PCI:
#ifdef CONFIG_SSB_PCIHOST
		return pci_map_single(dev->bus->host_pci, p, size, dir);
#endif
		break;
	case SSB_BUSTYPE_SSB:
		return dma_map_single(dev->dev, p, size, dir);
	default:
		break;
	}
	__ssb_dma_not_implemented(dev);
	return 0;
}

static inline void ssb_dma_unmap_single(struct ssb_device *dev, dma_addr_t dma_addr,
					size_t size, enum dma_data_direction dir)
{
	switch (dev->bus->bustype) {
	case SSB_BUSTYPE_PCI:
#ifdef CONFIG_SSB_PCIHOST
		pci_unmap_single(dev->bus->host_pci, dma_addr, size, dir);
		return;
#endif
		break;
	case SSB_BUSTYPE_SSB:
		dma_unmap_single(dev->dev, dma_addr, size, dir);
		return;
	default:
		break;
	}
	__ssb_dma_not_implemented(dev);
}

static inline void ssb_dma_sync_single_for_cpu(struct ssb_device *dev,
					       dma_addr_t dma_addr,
					       size_t size,
					       enum dma_data_direction dir)
{
	switch (dev->bus->bustype) {
	case SSB_BUSTYPE_PCI:
#ifdef CONFIG_SSB_PCIHOST
		pci_dma_sync_single_for_cpu(dev->bus->host_pci, dma_addr,
					    size, dir);
		return;
#endif
		break;
	case SSB_BUSTYPE_SSB:
		dma_sync_single_for_cpu(dev->dev, dma_addr, size, dir);
		return;
	default:
		break;
	}
	__ssb_dma_not_implemented(dev);
}

static inline void ssb_dma_sync_single_for_device(struct ssb_device *dev,
						  dma_addr_t dma_addr,
						  size_t size,
						  enum dma_data_direction dir)
{
	switch (dev->bus->bustype) {
	case SSB_BUSTYPE_PCI:
#ifdef CONFIG_SSB_PCIHOST
		pci_dma_sync_single_for_device(dev->bus->host_pci, dma_addr,
					       size, dir);
		return;
#endif
		break;
	case SSB_BUSTYPE_SSB:
		dma_sync_single_for_device(dev->dev, dma_addr, size, dir);
		return;
	default:
		break;
	}
	__ssb_dma_not_implemented(dev);
}

static inline void ssb_dma_sync_single_range_for_cpu(struct ssb_device *dev,
						     dma_addr_t dma_addr,
						     unsigned long offset,
						     size_t size,
						     enum dma_data_direction dir)
{
	switch (dev->bus->bustype) {
	case SSB_BUSTYPE_PCI:
#ifdef CONFIG_SSB_PCIHOST
		/* Just sync everything. That's all the PCI API can do. */
		pci_dma_sync_single_for_cpu(dev->bus->host_pci, dma_addr,
					    offset + size, dir);
		return;
#endif
		break;
	case SSB_BUSTYPE_SSB:
		dma_sync_single_range_for_cpu(dev->dev, dma_addr, offset,
					      size, dir);
		return;
	default:
		break;
	}
	__ssb_dma_not_implemented(dev);
}

static inline void ssb_dma_sync_single_range_for_device(struct ssb_device *dev,
							dma_addr_t dma_addr,
							unsigned long offset,
							size_t size,
							enum dma_data_direction dir)
{
	switch (dev->bus->bustype) {
	case SSB_BUSTYPE_PCI:
#ifdef CONFIG_SSB_PCIHOST
		/* Just sync everything. That's all the PCI API can do. */
		pci_dma_sync_single_for_device(dev->bus->host_pci, dma_addr,
					       offset + size, dir);
		return;
#endif
		break;
	case SSB_BUSTYPE_SSB:
		dma_sync_single_range_for_device(dev->dev, dma_addr, offset,
						 size, dir);
		return;
	default:
		break;
	}
	__ssb_dma_not_implemented(dev);
}


#ifdef CONFIG_SSB_PCIHOST
/* PCI-host wrapper driver */
extern int ssb_pcihost_register(struct pci_driver *driver);
static inline void ssb_pcihost_unregister(struct pci_driver *driver)
{
	pci_unregister_driver(driver);
}

static inline
void ssb_pcihost_set_power_state(struct ssb_device *sdev, pci_power_t state)
{
	if (sdev->bus->bustype == SSB_BUSTYPE_PCI)
		pci_set_power_state(sdev->bus->host_pci, state);
}
#else
static inline void ssb_pcihost_unregister(struct pci_driver *driver)
{
}

static inline
void ssb_pcihost_set_power_state(struct ssb_device *sdev, pci_power_t state)
{
}
#endif /* CONFIG_SSB_PCIHOST */


/* If a driver is shutdown or suspended, call this to signal
 * that the bus may be completely powered down. SSB will decide,
 * if it's really time to power down the bus, based on if there
 * are other devices that want to run. */
extern int ssb_bus_may_powerdown(struct ssb_bus *bus);
/* Before initializing and enabling a device, call this to power-up the bus.
 * If you want to allow use of dynamic-power-control, pass the flag.
 * Otherwise static always-on powercontrol will be used. */
extern int ssb_bus_powerup(struct ssb_bus *bus, bool dynamic_pctl);


/* Various helper functions */
extern u32 ssb_admatch_base(u32 adm);
extern u32 ssb_admatch_size(u32 adm);

/* PCI device mapping and fixup routines.
 * Called from the architecture pcibios init code.
 * These are only available on SSB_EMBEDDED configurations. */
#ifdef CONFIG_SSB_EMBEDDED
int ssb_pcibios_plat_dev_init(struct pci_dev *dev);
int ssb_pcibios_map_irq(const struct pci_dev *dev, u8 slot, u8 pin);
#endif /* CONFIG_SSB_EMBEDDED */

#endif /* LINUX_SSB_H_ */
