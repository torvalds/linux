#ifndef LINUX_BCMA_H_
#define LINUX_BCMA_H_

#include <linux/pci.h>
#include <linux/mod_devicetable.h>

#include <linux/bcma/bcma_driver_chipcommon.h>
#include <linux/bcma/bcma_driver_pci.h>
#include <linux/bcma/bcma_driver_mips.h>
#include <linux/bcma/bcma_driver_gmac_cmn.h>
#include <linux/ssb/ssb.h> /* SPROM sharing */

#include <linux/bcma/bcma_regs.h>

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

struct bcma_boardinfo {
	u16 vendor;
	u16 type;
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
#define BCMA_CORE_4706_CHIPCOMMON	0x500
#define BCMA_CORE_4706_SOC_RAM		0x50E
#define BCMA_CORE_4706_MAC_GBIT		0x52D
#define BCMA_CORE_AMEMC			0x52E	/* DDR1/2 memory controller core */
#define BCMA_CORE_ALTA			0x534	/* I2S core */
#define BCMA_CORE_4706_MAC_GBIT_COMMON	0x5DC
#define BCMA_CORE_DDR23_PHY		0x5DD
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
#define BCMA_CORE_PHY_AC		0x83B
#define BCMA_CORE_PCIE2			0x83C	/* PCI Express Gen2 */
#define BCMA_CORE_USB30_DEV		0x83D
#define BCMA_CORE_ARM_CR4		0x83E
#define BCMA_CORE_DEFAULT		0xFFF

#define BCMA_MAX_NR_CORES		16

/* Chip IDs of PCIe devices */
#define BCMA_CHIP_ID_BCM4313	0x4313
#define BCMA_CHIP_ID_BCM43142	43142
#define BCMA_CHIP_ID_BCM43224	43224
#define  BCMA_PKG_ID_BCM43224_FAB_CSM	0x8
#define  BCMA_PKG_ID_BCM43224_FAB_SMIC	0xa
#define BCMA_CHIP_ID_BCM43225	43225
#define BCMA_CHIP_ID_BCM43227	43227
#define BCMA_CHIP_ID_BCM43228	43228
#define BCMA_CHIP_ID_BCM43421	43421
#define BCMA_CHIP_ID_BCM43428	43428
#define BCMA_CHIP_ID_BCM43431	43431
#define BCMA_CHIP_ID_BCM43460	43460
#define BCMA_CHIP_ID_BCM4331	0x4331
#define BCMA_CHIP_ID_BCM6362	0x6362
#define BCMA_CHIP_ID_BCM4360	0x4360
#define BCMA_CHIP_ID_BCM4352	0x4352

/* Chip IDs of SoCs */
#define BCMA_CHIP_ID_BCM4706	0x5300
#define  BCMA_PKG_ID_BCM4706L	1
#define BCMA_CHIP_ID_BCM4716	0x4716
#define  BCMA_PKG_ID_BCM4716	8
#define  BCMA_PKG_ID_BCM4717	9
#define  BCMA_PKG_ID_BCM4718	10
#define BCMA_CHIP_ID_BCM47162	47162
#define BCMA_CHIP_ID_BCM4748	0x4748
#define BCMA_CHIP_ID_BCM4749	0x4749
#define BCMA_CHIP_ID_BCM5356	0x5356
#define BCMA_CHIP_ID_BCM5357	0x5357
#define  BCMA_PKG_ID_BCM5358	9
#define  BCMA_PKG_ID_BCM47186	10
#define  BCMA_PKG_ID_BCM5357	11
#define BCMA_CHIP_ID_BCM53572	53572
#define  BCMA_PKG_ID_BCM47188	9

/* Board types (on PCI usually equals to the subsystem dev id) */
/* BCM4313 */
#define BCMA_BOARD_TYPE_BCM94313BU	0X050F
#define BCMA_BOARD_TYPE_BCM94313HM	0X0510
#define BCMA_BOARD_TYPE_BCM94313EPA	0X0511
#define BCMA_BOARD_TYPE_BCM94313HMG	0X051C
/* BCM4716 */
#define BCMA_BOARD_TYPE_BCM94716NR2	0X04CD
/* BCM43224 */
#define BCMA_BOARD_TYPE_BCM943224X21	0X056E
#define BCMA_BOARD_TYPE_BCM943224X21_FCC	0X00D1
#define BCMA_BOARD_TYPE_BCM943224X21B	0X00E9
#define BCMA_BOARD_TYPE_BCM943224M93	0X008B
#define BCMA_BOARD_TYPE_BCM943224M93A	0X0090
#define BCMA_BOARD_TYPE_BCM943224X16	0X0093
#define BCMA_BOARD_TYPE_BCM94322X9	0X008D
#define BCMA_BOARD_TYPE_BCM94322M35E	0X008E
/* BCM43228 */
#define BCMA_BOARD_TYPE_BCM943228BU8	0X0540
#define BCMA_BOARD_TYPE_BCM943228BU9	0X0541
#define BCMA_BOARD_TYPE_BCM943228BU	0X0542
#define BCMA_BOARD_TYPE_BCM943227HM4L	0X0543
#define BCMA_BOARD_TYPE_BCM943227HMB	0X0544
#define BCMA_BOARD_TYPE_BCM943228HM4L	0X0545
#define BCMA_BOARD_TYPE_BCM943228SD	0X0573
/* BCM4331 */
#define BCMA_BOARD_TYPE_BCM94331X19	0X00D6
#define BCMA_BOARD_TYPE_BCM94331X28	0X00E4
#define BCMA_BOARD_TYPE_BCM94331X28B	0X010E
#define BCMA_BOARD_TYPE_BCM94331PCIEBT3AX	0X00E4
#define BCMA_BOARD_TYPE_BCM94331X12_2G	0X00EC
#define BCMA_BOARD_TYPE_BCM94331X12_5G	0X00ED
#define BCMA_BOARD_TYPE_BCM94331X29B	0X00EF
#define BCMA_BOARD_TYPE_BCM94331CSAX	0X00EF
#define BCMA_BOARD_TYPE_BCM94331X19C	0X00F5
#define BCMA_BOARD_TYPE_BCM94331X33	0X00F4
#define BCMA_BOARD_TYPE_BCM94331BU	0X0523
#define BCMA_BOARD_TYPE_BCM94331S9BU	0X0524
#define BCMA_BOARD_TYPE_BCM94331MC	0X0525
#define BCMA_BOARD_TYPE_BCM94331MCI	0X0526
#define BCMA_BOARD_TYPE_BCM94331PCIEBT4	0X0527
#define BCMA_BOARD_TYPE_BCM94331HM	0X0574
#define BCMA_BOARD_TYPE_BCM94331PCIEDUAL	0X059B
#define BCMA_BOARD_TYPE_BCM94331MCH5	0X05A9
#define BCMA_BOARD_TYPE_BCM94331CS	0X05C6
#define BCMA_BOARD_TYPE_BCM94331CD	0X05DA
/* BCM53572 */
#define BCMA_BOARD_TYPE_BCM953572BU	0X058D
#define BCMA_BOARD_TYPE_BCM953572NR2	0X058E
#define BCMA_BOARD_TYPE_BCM947188NR2	0X058F
#define BCMA_BOARD_TYPE_BCM953572SDRNR2	0X0590
/* BCM43142 */
#define BCMA_BOARD_TYPE_BCM943142HM	0X05E0

struct bcma_device {
	struct bcma_bus *bus;
	struct bcma_device_id id;

	struct device dev;
	struct device *dma_dev;

	unsigned int irq;
	bool dev_registered;

	u8 core_index;
	u8 core_unit;

	u32 addr;
	u32 addr1;
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
	int (*suspend)(struct bcma_device *dev);
	int (*resume)(struct bcma_device *dev);
	void (*shutdown)(struct bcma_device *dev);

	struct device_driver drv;
};
extern
int __bcma_driver_register(struct bcma_driver *drv, struct module *owner);
#define bcma_driver_register(drv) \
	__bcma_driver_register(drv, THIS_MODULE)

extern void bcma_driver_unregister(struct bcma_driver *drv);

/* Set a fallback SPROM.
 * See kdoc at the function definition for complete documentation. */
extern int bcma_arch_register_fallback_sprom(
		int (*sprom_callback)(struct bcma_bus *bus,
		struct ssb_sprom *out));

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

	struct bcma_boardinfo boardinfo;

	struct bcma_device *mapped_core;
	struct list_head cores;
	u8 nr_cores;
	u8 init_done:1;
	u8 num;

	struct bcma_drv_cc drv_cc;
	struct bcma_drv_pci drv_pci[2];
	struct bcma_drv_mips drv_mips;
	struct bcma_drv_gmac_cmn drv_gmac_cmn;

	/* We decided to share SPROM struct with SSB as long as we do not need
	 * any hacks for BCMA. This simplifies drivers code. */
	struct ssb_sprom sprom;
};

static inline u32 bcma_read8(struct bcma_device *core, u16 offset)
{
	return core->bus->ops->read8(core, offset);
}
static inline u32 bcma_read16(struct bcma_device *core, u16 offset)
{
	return core->bus->ops->read16(core, offset);
}
static inline u32 bcma_read32(struct bcma_device *core, u16 offset)
{
	return core->bus->ops->read32(core, offset);
}
static inline
void bcma_write8(struct bcma_device *core, u16 offset, u32 value)
{
	core->bus->ops->write8(core, offset, value);
}
static inline
void bcma_write16(struct bcma_device *core, u16 offset, u32 value)
{
	core->bus->ops->write16(core, offset, value);
}
static inline
void bcma_write32(struct bcma_device *core, u16 offset, u32 value)
{
	core->bus->ops->write32(core, offset, value);
}
#ifdef CONFIG_BCMA_BLOCKIO
static inline void bcma_block_read(struct bcma_device *core, void *buffer,
				   size_t count, u16 offset, u8 reg_width)
{
	core->bus->ops->block_read(core, buffer, count, offset, reg_width);
}
static inline void bcma_block_write(struct bcma_device *core,
				    const void *buffer, size_t count,
				    u16 offset, u8 reg_width)
{
	core->bus->ops->block_write(core, buffer, count, offset, reg_width);
}
#endif
static inline u32 bcma_aread32(struct bcma_device *core, u16 offset)
{
	return core->bus->ops->aread32(core, offset);
}
static inline
void bcma_awrite32(struct bcma_device *core, u16 offset, u32 value)
{
	core->bus->ops->awrite32(core, offset, value);
}

static inline void bcma_mask32(struct bcma_device *cc, u16 offset, u32 mask)
{
	bcma_write32(cc, offset, bcma_read32(cc, offset) & mask);
}
static inline void bcma_set32(struct bcma_device *cc, u16 offset, u32 set)
{
	bcma_write32(cc, offset, bcma_read32(cc, offset) | set);
}
static inline void bcma_maskset32(struct bcma_device *cc,
				  u16 offset, u32 mask, u32 set)
{
	bcma_write32(cc, offset, (bcma_read32(cc, offset) & mask) | set);
}
static inline void bcma_mask16(struct bcma_device *cc, u16 offset, u16 mask)
{
	bcma_write16(cc, offset, bcma_read16(cc, offset) & mask);
}
static inline void bcma_set16(struct bcma_device *cc, u16 offset, u16 set)
{
	bcma_write16(cc, offset, bcma_read16(cc, offset) | set);
}
static inline void bcma_maskset16(struct bcma_device *cc,
				  u16 offset, u16 mask, u16 set)
{
	bcma_write16(cc, offset, (bcma_read16(cc, offset) & mask) | set);
}

extern struct bcma_device *bcma_find_core(struct bcma_bus *bus, u16 coreid);
extern bool bcma_core_is_enabled(struct bcma_device *core);
extern void bcma_core_disable(struct bcma_device *core, u32 flags);
extern int bcma_core_enable(struct bcma_device *core, u32 flags);
extern void bcma_core_set_clockmode(struct bcma_device *core,
				    enum bcma_clkmode clkmode);
extern void bcma_core_pll_ctl(struct bcma_device *core, u32 req, u32 status,
			      bool on);
extern u32 bcma_chipco_pll_read(struct bcma_drv_cc *cc, u32 offset);
#define BCMA_DMA_TRANSLATION_MASK	0xC0000000
#define  BCMA_DMA_TRANSLATION_NONE	0x00000000
#define  BCMA_DMA_TRANSLATION_DMA32_CMT	0x40000000 /* Client Mode Translation for 32-bit DMA */
#define  BCMA_DMA_TRANSLATION_DMA64_CMT	0x80000000 /* Client Mode Translation for 64-bit DMA */
extern u32 bcma_core_dma_translation(struct bcma_device *core);

#endif /* LINUX_BCMA_H_ */
