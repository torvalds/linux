/*	$OpenBSD: pci.h,v 1.19 2025/02/07 03:03:31 jsg Exp $	*/
/*
 * Copyright (c) 2015 Mark Kettenis
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _LINUX_PCI_H_
#define _LINUX_PCI_H_

#include <sys/types.h>
/* sparc64 cpu.h needs time.h and siginfo.h (indirect via param.h) */
#include <sys/param.h>
#include <machine/cpu.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>
#include <uvm/uvm_extern.h>

#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/kobject.h>
#include <linux/dma-mapping.h>
#include <linux/mod_devicetable.h>
#include <linux/device.h>

struct pci_dev;

struct pci_bus {
	pci_chipset_tag_t pc;
	unsigned char	number;
	int		domain_nr;
	pcitag_t	*bridgetag;
	struct pci_dev	*self;
};

struct pci_acpi {
	struct aml_node	*node;
};

struct pci_dev {
	struct pci_bus	_bus;
	struct pci_bus	*bus;

	unsigned int	devfn;
	uint16_t	vendor;
	uint16_t	device;
	uint16_t	subsystem_vendor;
	uint16_t	subsystem_device;
	uint8_t		revision;
	uint32_t	class;		/* class:subclass:interface */

	pci_chipset_tag_t pc;
	pcitag_t	tag;
	struct pci_softc *pci;

	int		irq;
	int		msi_enabled;
	uint8_t		no_64bit_msi;
	uint8_t		ltr_path;

	struct pci_acpi dev;
	struct device *_dev;
};
#define PCI_ANY_ID (uint16_t) (~0U)

#define PCI_DEVICE(v, p)		\
	.vendor = (v),			\
	.device = (p),			\
	.subvendor = PCI_ANY_ID,	\
	.subdevice = PCI_ANY_ID

#ifndef PCI_MEM_START
#define PCI_MEM_START	0
#endif

#ifndef PCI_MEM_END
#define PCI_MEM_END	0xffffffff
#endif

#ifndef PCI_MEM64_END
#define PCI_MEM64_END	0xffffffffffffffff
#endif

#define PCI_VENDOR_ID_APPLE	PCI_VENDOR_APPLE
#define PCI_VENDOR_ID_ASUSTEK	PCI_VENDOR_ASUSTEK
#define PCI_VENDOR_ID_ATI	PCI_VENDOR_ATI
#define PCI_VENDOR_ID_DELL	PCI_VENDOR_DELL
#define PCI_VENDOR_ID_HP	PCI_VENDOR_HP
#define PCI_VENDOR_ID_IBM	PCI_VENDOR_IBM
#define PCI_VENDOR_ID_INTEL	PCI_VENDOR_INTEL
#define PCI_VENDOR_ID_SONY	PCI_VENDOR_SONY
#define PCI_VENDOR_ID_VIA	PCI_VENDOR_VIATECH

#define PCI_DEVICE_ID_ATI_RADEON_QY	PCI_PRODUCT_ATI_RADEON_QY

#define PCI_SUBVENDOR_ID_REDHAT_QUMRANET	0x1af4
#define PCI_SUBDEVICE_ID_QEMU			0x1100

#define PCI_DEVFN(slot, func)	((slot) << 3 | (func))
#define PCI_SLOT(devfn)		((devfn) >> 3)
#define PCI_FUNC(devfn)		((devfn) & 0x7)
#define PCI_BUS_NUM(devfn)	(((devfn) >> 8) & 0xff)

#define pci_dev_put(x)

#define PCI_EXP_DEVSTA		0x0a
#define PCI_EXP_DEVSTA_TRPND	(1 << 5)
#define PCI_EXP_LNKCAP		0x0c
#define PCI_EXP_LNKCAP_CLKPM	(1 << 18)
#define PCI_EXP_LNKCTL		0x10
#define PCI_EXP_LNKCTL_HAWD	(1 << 9)
#define PCI_EXP_LNKSTA		0x12
#define PCI_EXP_DEVCTL2		0x28
#define PCI_EXP_DEVCTL2_LTR_EN	(1 << 10)
#define PCI_EXP_LNKCTL2		0x30
#define PCI_EXP_LNKCTL2_ENTER_COMP	(1 << 4)
#define PCI_EXP_LNKCTL2_TX_MARGIN	0x0380
#define PCI_EXP_LNKCTL2_TLS		PCI_PCIE_LCSR2_TLS
#define PCI_EXP_LNKCTL2_TLS_2_5GT	PCI_PCIE_LCSR2_TLS_2_5
#define PCI_EXP_LNKCTL2_TLS_5_0GT	PCI_PCIE_LCSR2_TLS_5
#define PCI_EXP_LNKCTL2_TLS_8_0GT	PCI_PCIE_LCSR2_TLS_8

#define PCI_COMMAND		PCI_COMMAND_STATUS_REG
#define PCI_COMMAND_MEMORY	PCI_COMMAND_MEM_ENABLE

#define PCI_PRIMARY_BUS		PCI_PRIBUS_1

static inline int
pci_read_config_dword(struct pci_dev *pdev, int reg, u32 *val)
{
	*val = pci_conf_read(pdev->pc, pdev->tag, reg);
	return 0;
} 

static inline int
pci_read_config_word(struct pci_dev *pdev, int reg, u16 *val)
{
	uint32_t v;

	v = pci_conf_read(pdev->pc, pdev->tag, (reg & ~0x2));
	*val = (v >> ((reg & 0x2) * 8));
	return 0;
} 

static inline int
pci_read_config_byte(struct pci_dev *pdev, int reg, u8 *val)
{
	uint32_t v;

	v = pci_conf_read(pdev->pc, pdev->tag, (reg & ~0x3));
	*val = (v >> ((reg & 0x3) * 8));
	return 0;
} 

static inline int
pci_write_config_dword(struct pci_dev *pdev, int reg, u32 val)
{
	pci_conf_write(pdev->pc, pdev->tag, reg, val);
	return 0;
} 

static inline int
pci_write_config_word(struct pci_dev *pdev, int reg, u16 val)
{
	uint32_t v;

	v = pci_conf_read(pdev->pc, pdev->tag, (reg & ~0x2));
	v &= ~(0xffff << ((reg & 0x2) * 8));
	v |= (val << ((reg & 0x2) * 8));
	pci_conf_write(pdev->pc, pdev->tag, (reg & ~0x2), v);
	return 0;
} 

static inline int
pci_write_config_byte(struct pci_dev *pdev, int reg, u8 val)
{
	uint32_t v;

	v = pci_conf_read(pdev->pc, pdev->tag, (reg & ~0x3));
	v &= ~(0xff << ((reg & 0x3) * 8));
	v |= (val << ((reg & 0x3) * 8));
	pci_conf_write(pdev->pc, pdev->tag, (reg & ~0x3), v);
	return 0;
}

static inline int
pci_bus_read_config_word(struct pci_bus *bus, unsigned int devfn,
    int reg, u16 *val)
{
	pcitag_t tag = pci_make_tag(bus->pc, bus->number,
	    PCI_SLOT(devfn), PCI_FUNC(devfn));
	uint32_t v;

	v = pci_conf_read(bus->pc, tag, (reg & ~0x2));
	*val = (v >> ((reg & 0x2) * 8));
	return 0;
}

static inline int
pci_bus_read_config_byte(struct pci_bus *bus, unsigned int devfn,
    int reg, u8 *val)
{
	pcitag_t tag = pci_make_tag(bus->pc, bus->number,
	    PCI_SLOT(devfn), PCI_FUNC(devfn));
	uint32_t v;

	v = pci_conf_read(bus->pc, tag, (reg & ~0x3));
	*val = (v >> ((reg & 0x3) * 8));
	return 0;
}

static inline int
pci_bus_write_config_byte(struct pci_bus *bus, unsigned int devfn,
    int reg, u8 val)
{
	pcitag_t tag = pci_make_tag(bus->pc, bus->number,
	    PCI_SLOT(devfn), PCI_FUNC(devfn));
	uint32_t v;

	v = pci_conf_read(bus->pc, tag, (reg & ~0x3));
	v &= ~(0xff << ((reg & 0x3) * 8));
	v |= (val << ((reg & 0x3) * 8));
	pci_conf_write(bus->pc, tag, (reg & ~0x3), v);
	return 0;
}

static inline int
pci_pcie_cap(struct pci_dev *pdev)
{
	int pos;
	if (!pci_get_capability(pdev->pc, pdev->tag, PCI_CAP_PCIEXPRESS,
	    &pos, NULL))
		return -EINVAL;
	return pos;
}

bool pcie_aspm_enabled(struct pci_dev *);

static inline bool
pci_is_pcie(struct pci_dev *pdev)
{
	return (pci_pcie_cap(pdev) > 0);
}

static inline bool
pci_is_root_bus(struct pci_bus *pbus)
{
	return (pbus->bridgetag == NULL);
}

static inline struct pci_dev *
pci_upstream_bridge(struct pci_dev *pdev)
{
	if (pci_is_root_bus(pdev->bus))
		return NULL;
	return pdev->bus->self;
}

/* XXX check for ACPI _PR3 */
static inline bool
pci_pr3_present(struct pci_dev *pdev)
{
	return false;
}

static inline int
pcie_capability_read_dword(struct pci_dev *pdev, int off, u32 *val)
{
	int pos;
	if (!pci_get_capability(pdev->pc, pdev->tag, PCI_CAP_PCIEXPRESS,
	    &pos, NULL)) {
		*val = 0;
		return -EINVAL;
	}
	*val = pci_conf_read(pdev->pc, pdev->tag, pos + off);
	return 0;
}

static inline int
pcie_capability_read_word(struct pci_dev *pdev, int off, u16 *val)
{
	int pos;
	if (!pci_get_capability(pdev->pc, pdev->tag, PCI_CAP_PCIEXPRESS,
	    &pos, NULL)) {
		*val = 0;
		return -EINVAL;
	}
	pci_read_config_word(pdev, pos + off, val);
	return 0;
}

static inline int
pcie_capability_write_word(struct pci_dev *pdev, int off, u16 val)
{
	int pos;
	if (!pci_get_capability(pdev->pc, pdev->tag, PCI_CAP_PCIEXPRESS,
	    &pos, NULL))
		return -EINVAL;
	pci_write_config_word(pdev, pos + off, val);
	return 0;
}

static inline int
pcie_capability_set_word(struct pci_dev *pdev, int off, u16 val)
{
	u16 r;
	pcie_capability_read_word(pdev, off, &r);
	r |= val;
	pcie_capability_write_word(pdev, off, r);
	return 0;
}

static inline int
pcie_capability_clear_word(struct pci_dev *pdev, int off, u16 c)
{
	u16 r;
	pcie_capability_read_word(pdev, off, &r);
	r &= ~c;
	pcie_capability_write_word(pdev, off, r);
	return 0;
}

static inline int
pcie_capability_clear_and_set_word(struct pci_dev *pdev, int off, u16 c, u16 s)
{
	u16 r;
	pcie_capability_read_word(pdev, off, &r);
	r &= ~c;
	r |= s;
	pcie_capability_write_word(pdev, off, r);
	return 0;
}

static inline int
pcie_get_readrq(struct pci_dev *pdev)
{
	uint16_t val;

	pcie_capability_read_word(pdev, PCI_PCIE_DCSR, &val);

	return 128 << ((val & PCI_PCIE_DCSR_MPS) >> 12);
}

static inline int
pcie_set_readrq(struct pci_dev *pdev, int rrq)
{
	uint16_t val;
	
	pcie_capability_read_word(pdev, PCI_PCIE_DCSR, &val);
	val &= ~PCI_PCIE_DCSR_MPS;
	val |= (ffs(rrq) - 8) << 12;
	return pcie_capability_write_word(pdev, PCI_PCIE_DCSR, val);
}

static inline void
pci_set_master(struct pci_dev *pdev)
{
}

static inline void
pci_clear_master(struct pci_dev *pdev)
{
}

static inline void
pci_save_state(struct pci_dev *pdev)
{
}

static inline void
pci_restore_state(struct pci_dev *pdev)
{
}

static inline int
pci_enable_msi(struct pci_dev *pdev)
{
	return 0;
}

static inline void
pci_disable_msi(struct pci_dev *pdev)
{
}

typedef enum {
	PCI_D0,
	PCI_D1,
	PCI_D2,
	PCI_D3hot,
	PCI_D3cold
} pci_power_t;

enum pci_bus_speed {
	PCIE_SPEED_2_5GT,
	PCIE_SPEED_5_0GT,
	PCIE_SPEED_8_0GT,
	PCIE_SPEED_16_0GT,
	PCIE_SPEED_32_0GT,
	PCIE_SPEED_64_0GT,
	PCI_SPEED_UNKNOWN
};

enum pcie_link_width {
	PCIE_LNK_X1	= 1,
	PCIE_LNK_X2	= 2,
	PCIE_LNK_X4	= 4,
	PCIE_LNK_X8	= 8,
	PCIE_LNK_X12	= 12,
	PCIE_LNK_X16	= 16,
	PCIE_LNK_X32	= 32,
	PCIE_LNK_WIDTH_UNKNOWN	= 0xff
};

typedef unsigned int pci_ers_result_t;
typedef unsigned int pci_channel_state_t;

#define PCI_ERS_RESULT_DISCONNECT	0
#define PCI_ERS_RESULT_RECOVERED	1

enum pci_bus_speed pcie_get_speed_cap(struct pci_dev *);
enum pcie_link_width pcie_get_width_cap(struct pci_dev *);
int pci_resize_resource(struct pci_dev *, int, int);

static inline void
pcie_bandwidth_available(struct pci_dev *pdev, struct pci_dev **ldev,
    enum pci_bus_speed *speed, enum pcie_link_width *width)
{
	struct pci_dev *bdev = pdev->bus->self;
	if (bdev == NULL)
		return;

	if (speed)
		*speed = pcie_get_speed_cap(bdev);
	if (width)
		*width = pcie_get_width_cap(bdev);
}

static inline int
pci_enable_device(struct pci_dev *pdev)
{
	return 0;
}

static inline void
pci_disable_device(struct pci_dev *pdev)
{
}

static inline int
pci_wait_for_pending_transaction(struct pci_dev *pdev)
{
	return 0;
}

static inline bool
pci_is_thunderbolt_attached(struct pci_dev *pdev)
{
	return false;
}

static inline void
pci_set_drvdata(struct pci_dev *pdev, void *data)
{
	dev_set_drvdata(pdev->_dev, data);
}

static inline void *
pci_get_drvdata(struct pci_dev *pdev)
{
	return dev_get_drvdata(pdev->_dev);
}

static inline int
pci_domain_nr(struct pci_bus *pbus)
{
	return pbus->domain_nr;
}

static inline int
pci_irq_vector(struct pci_dev *pdev, unsigned int num)
{
	return pdev->irq;
}

static inline void
pci_free_irq_vectors(struct pci_dev *pdev)
{
}

static inline int
pci_set_power_state(struct pci_dev *dev, int state)
{
	return 0;
}

struct pci_driver;

static inline int
pci_register_driver(struct pci_driver *pci_drv)
{
	return 0;
}

static inline void
pci_unregister_driver(void *d)
{
}

static inline u16
pci_dev_id(struct pci_dev *dev)
{
	return dev->devfn | (dev->bus->number << 8);
}

static inline const struct pci_device_id *
pci_match_id(const struct pci_device_id *ids, struct pci_dev *pdev)
{
	int i = 0;

	for (i = 0; ids[i].vendor != 0; i++) {
		if ((ids[i].vendor == pdev->vendor) &&
		    (ids[i].device == pdev->device ||
		     ids[i].device == PCI_ANY_ID) &&
		    (ids[i].subvendor == PCI_ANY_ID) &&
		    (ids[i].subdevice == PCI_ANY_ID))
			return &ids[i];
	}
	return NULL;
}

#define PCI_CLASS_DISPLAY_VGA \
    ((PCI_CLASS_DISPLAY << 8) | PCI_SUBCLASS_DISPLAY_VGA)
#define PCI_CLASS_DISPLAY_OTHER \
    ((PCI_CLASS_DISPLAY << 8) | PCI_SUBCLASS_DISPLAY_MISC)
#define PCI_CLASS_ACCELERATOR_PROCESSING \
    (PCI_CLASS_ACCELERATOR << 8)

static inline int
pci_device_is_present(struct pci_dev *pdev)
{
	return 1;
}

static inline int
dev_is_pci(struct device *dev)
{
	return 1;
}
#endif /* _LINUX_PCI_H_ */
