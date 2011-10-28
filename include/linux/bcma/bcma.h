#ifndef LINUX_BCMA_H_
#define LINUX_BCMA_H_

#include <linux/pci.h>
#include <linux/mod_devicetable.h>

#include <linux/bcma/bcma_driver_chipcommon.h>
#include <linux/bcma/bcma_driver_pci.h>
#include <linux/bcma/bcma_driver_mips.h>
#include <linux/ssb/ssb.h> /* SPROM sharing */

#include "bcma_regs.h"

struct bcma_device;
struct bcma_bus;

enum bcma_hosttype {
	BCMA_HOSTTYPE_PCI,
	BCMA_HOSTTYPE_SDIO,
	BCMA_HOSTTYPE_SOC,
};

struct bcma_chipinfo {
	u16 id;
	u8 rev;
	u8 pkg;
};

enum bcma_clkmode {
	BCMA_CLKMODE_FAST,
	BCMA_CLKMODE_DYNAMIC,
};

struct bcma_host_ops {
	u8 (*read8)(struct bcma_device *core, u16 offset);
	u16 (*read16)(struct bcma_device *core, u16 offset);
	u32 (*read32)(struct bcma_device *core, u16 offset);
	void (*write8)(struct bcma_device *core, u16 offset, u8 value);
	void (*write16)(struct bcma_device *core, u16 offset, u16 value);
	void (*write32)(struct bcma_device *core, u16 offset, u32 value);
#ifdef CONFIG_BCMA_BLOCKIO
	void (*block_read)(struct bcma_device *core, void *buffer,
			   size_t count, u16 offset, u8 reg_width);
	void (*block_write)(struct bcma_device *core, const void *buffer,
			    size_t count, u16 offset, u8 reg_width);
#endif
	/* Agent ops */
	u32 (*aread32)(struct bcma_device *core, u16 offset);
	void (*awrite32)(struct bcma_device *core, u16 offset, u32 value);
};

/* Core manufacturers */
#define BCMA_MANUF_ARM			0x43B
#define BCMA_MANUF_MIPS			0x4A7
#define BCMA_MANUF_BCM			0x4BF

/* Core class values. */
#define BCMA_CL_SIM			0x0
#define BCMA_CL_EROM			0x1
#define BCMA_CL_CORESIGHT		0x9
#define BCMA_CL_VERIF			0xB
#define BCMA_CL_OPTIMO			0xD
#define BCMA_CL_GEN			0xE
#define BCMA_CL_PRIMECELL		0xF

/* Core-ID values. */
#define BCMA_CORE_OOB_ROUTER		0x367	/* Out of band */
#define BCMA_CORE_INVALID		0x700
#define BCMA_CORE_CHIPCOMMON		0x800
#define BCMA_CORE_ILINE20		0x801
#define BCMA_CORE_SRAM			0x802
#define BCMA_CORE_SDRAM			0x803
#define BCMA_CORE_PCI			0x804
#define BCMA_CORE_MIPS			0x805
#define BCMA_CORE_ETHERNET		0x806
#define BCMA_CORE_V90			0x807
#define BCMA_CORE_USB11_HOSTDEV		0x808
#define BCMA_CORE_ADSL			0x809
#define BCMA_CORE_ILINE100		0x80A
#define BCMA_CORE_IPSEC			0x80B
#define BCMA_CORE_UTOPIA		0x80C
#define BCMA_CORE_PCMCIA		0x80D
#define BCMA_CORE_INTERNAL_MEM		0x80E
#define BCMA_CORE_MEMC_SDRAM		0x80F
#define BCMA_CORE_OFDM			0x810
#define BCMA_CORE_EXTIF			0x811
#define BCMA_CORE_80211			0x812
#define BCMA_CORE_PHY_A			0x813
#define BCMA_CORE_PHY_B			0x814
#define BCMA_CORE_PHY_G			0x815
#define BCMA_CORE_MIPS_3302		0x816
#define BCMA_CORE_USB11_HOST		0x817
#define BCMA_CORE_USB11_DEV		0x818
#define BCMA_CORE_USB20_HOST		0x819
#define BCMA_CORE_USB20_DEV		0x81A
#define BCMA_CORE_SDIO_HOST		0x81B
#define BCMA_CORE_ROBOSWITCH		0x81C
#define BCMA_CORE_PARA_ATA		0x81D
#define BCMA_CORE_SATA_XORDMA		0x81E
#define BCMA_CORE_ETHERNET_GBIT		0x81F
#define BCMA_CORE_PCIE			0x820
#define BCMA_CORE_PHY_N			0x821
#define BCMA_CORE_SRAM_CTL		0x822
#define BCMA_CORE_MINI_MACPHY		0x823
#define BCMA_CORE_ARM_1176		0x824
#define BCMA_CORE_ARM_7TDMI		0x825
#define BCMA_CORE_PHY_LP		0x826
#define BCMA_CORE_PMU			0x827
#define BCMA_CORE_PHY_SSN		0x828
#define BCMA_CORE_SDIO_DEV		0x829
#define BCMA_CORE_ARM_CM3		0x82A
#define BCMA_CORE_PHY_HT		0x82B
#define BCMA_CORE_MIPS_74K		0x82C
#define BCMA_CORE_MAC_GBIT		0x82D
#define BCMA_CORE_DDR12_MEM_CTL		0x82E
#define BCMA_CORE_PCIE_RC		0x82F	/* PCIe Root Complex */
#define BCMA_CORE_OCP_OCP_BRIDGE	0x830
#define BCMA_CORE_SHARED_COMMON		0x831
#define BCMA_CORE_OCP_AHB_BRIDGE	0x832
#define BCMA_CORE_SPI_HOST		0x833
#define BCMA_CORE_I2S			0x834
#define BCMA_CORE_SDR_DDR1_MEM_CTL	0x835	/* SDR/DDR1 memory controller core */
#define BCMA_CORE_SHIM			0x837	/* SHIM component in ubus/6362 */
#define BCMA_CORE_DEFAULT		0xFFF

#define BCMA_MAX_NR_CORES		16

struct bcma_device {
	struct bcma_bus *bus;
	struct bcma_device_id id;

	struct device dev;
	struct device *dma_dev;

	unsigned int irq;
	bool dev_registered;

	u8 core_index;

	u32 addr;
	u32 wrap;

	void __iomem *io_addr;
	void __iomem *io_wrap;

	void *drvdata;
	struct list_head list;
};

static inline void *bcma_get_drvdata(struct bcma_device *core)
{
	return core->drvdata;
}
static inline void bcma_set_drvdata(struct bcma_device *core, void *drvdata)
{
	core->drvdata = drvdata;
}

struct bcma_driver {
	const char *name;
	const struct bcma_device_id *id_table;

	int (*probe)(struct bcma_device *dev);
	void (*remove)(struct bcma_device *dev);
	int (*suspend)(struct bcma_device *dev, pm_message_t state);
	int (*resume)(struct bcma_device *dev);
	void (*shutdown)(struct bcma_device *dev);

	struct device_driver drv;
};
extern
int __bcma_driver_register(struct bcma_driver *drv, struct module *owner);
static inline int bcma_driver_register(struct bcma_driver *drv)
{
	return __bcma_driver_register(drv, THIS_MODULE);
}
extern void bcma_driver_unregister(struct bcma_driver *drv);

struct bcma_bus {
	/* The MMIO area. */
	void __iomem *mmio;

	const struct bcma_host_ops *ops;

	enum bcma_hosttype hosttype;
	union {
		/* Pointer to the PCI bus (only for BCMA_HOSTTYPE_PCI) */
		struct pci_dev *host_pci;
		/* Pointer to the SDIO device (only for BCMA_HOSTTYPE_SDIO) */
		struct sdio_func *host_sdio;
	};

	struct bcma_chipinfo chipinfo;

	struct bcma_device *mapped_core;
	struct list_head cores;
	u8 nr_cores;
	u8 init_done:1;

	struct bcma_drv_cc drv_cc;
	struct bcma_drv_pci drv_pci;
	struct bcma_drv_mips drv_mips;

	/* We decided to share SPROM struct with SSB as long as we do not need
	 * any hacks for BCMA. This simplifies drivers code. */
	struct ssb_sprom sprom;
};

extern inline u32 bcma_read8(struct bcma_device *core, u16 offset)
{
	return core->bus->ops->read8(core, offset);
}
extern inline u32 bcma_read16(struct bcma_device *core, u16 offset)
{
	return core->bus->ops->read16(core, offset);
}
extern inline u32 bcma_read32(struct bcma_device *core, u16 offset)
{
	return core->bus->ops->read32(core, offset);
}
extern inline
void bcma_write8(struct bcma_device *core, u16 offset, u32 value)
{
	core->bus->ops->write8(core, offset, value);
}
extern inline
void bcma_write16(struct bcma_device *core, u16 offset, u32 value)
{
	core->bus->ops->write16(core, offset, value);
}
extern inline
void bcma_write32(struct bcma_device *core, u16 offset, u32 value)
{
	core->bus->ops->write32(core, offset, value);
}
#ifdef CONFIG_BCMA_BLOCKIO
extern inline void bcma_block_read(struct bcma_device *core, void *buffer,
				   size_t count, u16 offset, u8 reg_width)
{
	core->bus->ops->block_read(core, buffer, count, offset, reg_width);
}
extern inline void bcma_block_write(struct bcma_device *core, const void *buffer,
				    size_t count, u16 offset, u8 reg_width)
{
	core->bus->ops->block_write(core, buffer, count, offset, reg_width);
}
#endif
extern inline u32 bcma_aread32(struct bcma_device *core, u16 offset)
{
	return core->bus->ops->aread32(core, offset);
}
extern inline
void bcma_awrite32(struct bcma_device *core, u16 offset, u32 value)
{
	core->bus->ops->awrite32(core, offset, value);
}

#define bcma_mask32(cc, offset, mask) \
	bcma_write32(cc, offset, bcma_read32(cc, offset) & (mask))
#define bcma_set32(cc, offset, set) \
	bcma_write32(cc, offset, bcma_read32(cc, offset) | (set))
#define bcma_maskset32(cc, offset, mask, set) \
	bcma_write32(cc, offset, (bcma_read32(cc, offset) & (mask)) | (set))

extern bool bcma_core_is_enabled(struct bcma_device *core);
extern void bcma_core_disable(struct bcma_device *core, u32 flags);
extern int bcma_core_enable(struct bcma_device *core, u32 flags);
extern void bcma_core_set_clockmode(struct bcma_device *core,
				    enum bcma_clkmode clkmode);
extern void bcma_core_pll_ctl(struct bcma_device *core, u32 req, u32 status,
			      bool on);
#define BCMA_DMA_TRANSLATION_MASK	0xC0000000
#define  BCMA_DMA_TRANSLATION_NONE	0x00000000
#define  BCMA_DMA_TRANSLATION_DMA32_CMT	0x40000000 /* Client Mode Translation for 32-bit DMA */
#define  BCMA_DMA_TRANSLATION_DMA64_CMT	0x80000000 /* Client Mode Translation for 64-bit DMA */
extern u32 bcma_core_dma_translation(struct bcma_device *core);

#endif /* LINUX_BCMA_H_ */
