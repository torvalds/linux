#ifndef LINUX_BCMA_REGS_H_
#define LINUX_BCMA_REGS_H_

/* Some single registers are shared between many cores */
/* BCMA_CLKCTLST: ChipCommon (rev >= 20), PCIe, 80211 */
#define BCMA_CLKCTLST			0x01E0 /* Clock control and status */
#define  BCMA_CLKCTLST_FORCEALP		0x00000001 /* Force ALP request */
#define  BCMA_CLKCTLST_FORCEHT		0x00000002 /* Force HT request */
#define  BCMA_CLKCTLST_FORCEILP		0x00000004 /* Force ILP request */
#define  BCMA_CLKCTLST_HAVEALPREQ	0x00000008 /* ALP available request */
#define  BCMA_CLKCTLST_HAVEHTREQ	0x00000010 /* HT available request */
#define  BCMA_CLKCTLST_HWCROFF		0x00000020 /* Force HW clock request off */
#define  BCMA_CLKCTLST_EXTRESREQ	0x00000700 /* Mask of external resource requests */
#define  BCMA_CLKCTLST_EXTRESREQ_SHIFT	8
#define  BCMA_CLKCTLST_HAVEALP		0x00010000 /* ALP available */
#define  BCMA_CLKCTLST_HAVEHT		0x00020000 /* HT available */
#define  BCMA_CLKCTLST_BP_ON_ALP	0x00040000 /* RO: running on ALP clock */
#define  BCMA_CLKCTLST_BP_ON_HT		0x00080000 /* RO: running on HT clock */
#define  BCMA_CLKCTLST_EXTRESST		0x07000000 /* Mask of external resource status */
#define  BCMA_CLKCTLST_EXTRESST_SHIFT	24
/* Is there any BCM4328 on BCMA bus? */
#define  BCMA_CLKCTLST_4328A0_HAVEHT	0x00010000 /* 4328a0 has reversed bits */
#define  BCMA_CLKCTLST_4328A0_HAVEALP	0x00020000 /* 4328a0 has reversed bits */

/* Agent registers (common for every core) */
#define BCMA_IOCTL			0x0408 /* IO control */
#define  BCMA_IOCTL_CLK			0x0001
#define  BCMA_IOCTL_FGC			0x0002
#define  BCMA_IOCTL_CORE_BITS		0x3FFC
#define  BCMA_IOCTL_PME_EN		0x4000
#define  BCMA_IOCTL_BIST_EN		0x8000
#define BCMA_IOST			0x0500 /* IO status */
#define  BCMA_IOST_CORE_BITS		0x0FFF
#define  BCMA_IOST_DMA64		0x1000
#define  BCMA_IOST_GATED_CLK		0x2000
#define  BCMA_IOST_BIST_ERROR		0x4000
#define  BCMA_IOST_BIST_DONE		0x8000
#define BCMA_RESET_CTL			0x0800
#define  BCMA_RESET_CTL_RESET		0x0001

/* BCMA PCI config space registers. */
#define BCMA_PCI_PMCSR			0x44
#define  BCMA_PCI_PE			0x100
#define BCMA_PCI_BAR0_WIN		0x80	/* Backplane address space 0 */
#define BCMA_PCI_BAR1_WIN		0x84	/* Backplane address space 1 */
#define BCMA_PCI_SPROMCTL		0x88	/* SPROM control */
#define  BCMA_PCI_SPROMCTL_WE		0x10	/* SPROM write enable */
#define BCMA_PCI_BAR1_CONTROL		0x8c	/* Address space 1 burst control */
#define BCMA_PCI_IRQS			0x90	/* PCI interrupts */
#define BCMA_PCI_IRQMASK		0x94	/* PCI IRQ control and mask (pcirev >= 6 only) */
#define BCMA_PCI_BACKPLANE_IRQS		0x98	/* Backplane Interrupts */
#define BCMA_PCI_BAR0_WIN2		0xAC
#define BCMA_PCI_GPIO_IN		0xB0	/* GPIO Input (pcirev >= 3 only) */
#define BCMA_PCI_GPIO_OUT		0xB4	/* GPIO Output (pcirev >= 3 only) */
#define BCMA_PCI_GPIO_OUT_ENABLE	0xB8	/* GPIO Output Enable/Disable (pcirev >= 3 only) */
#define  BCMA_PCI_GPIO_SCS		0x10	/* PCI config space bit 4 for 4306c0 slow clock source */
#define  BCMA_PCI_GPIO_HWRAD		0x20	/* PCI config space GPIO 13 for hw radio disable */
#define  BCMA_PCI_GPIO_XTAL		0x40	/* PCI config space GPIO 14 for Xtal powerup */
#define  BCMA_PCI_GPIO_PLL		0x80	/* PCI config space GPIO 15 for PLL powerdown */

/* SiliconBackplane Address Map.
 * All regions may not exist on all chips.
 */
#define BCMA_SOC_SDRAM_BASE		0x00000000U	/* Physical SDRAM */
#define BCMA_SOC_PCI_MEM		0x08000000U	/* Host Mode sb2pcitranslation0 (64 MB) */
#define BCMA_SOC_PCI_MEM_SZ		(64 * 1024 * 1024)
#define BCMA_SOC_PCI_CFG		0x0c000000U	/* Host Mode sb2pcitranslation1 (64 MB) */
#define BCMA_SOC_SDRAM_SWAPPED		0x10000000U	/* Byteswapped Physical SDRAM */
#define BCMA_SOC_SDRAM_R2		0x80000000U	/* Region 2 for sdram (512 MB) */


#define BCMA_SOC_PCI_DMA		0x40000000U	/* Client Mode sb2pcitranslation2 (1 GB) */
#define BCMA_SOC_PCI_DMA2		0x80000000U	/* Client Mode sb2pcitranslation2 (1 GB) */
#define BCMA_SOC_PCI_DMA_SZ		0x40000000U	/* Client Mode sb2pcitranslation2 size in bytes */
#define BCMA_SOC_PCIE_DMA_L32		0x00000000U	/* PCIE Client Mode sb2pcitranslation2
							 * (2 ZettaBytes), low 32 bits
							 */
#define BCMA_SOC_PCIE_DMA_H32		0x80000000U	/* PCIE Client Mode sb2pcitranslation2
							 * (2 ZettaBytes), high 32 bits
							 */

#define BCMA_SOC_PCI1_MEM		0x40000000U	/* Host Mode sb2pcitranslation0 (64 MB) */
#define BCMA_SOC_PCI1_CFG		0x44000000U	/* Host Mode sb2pcitranslation1 (64 MB) */
#define BCMA_SOC_PCIE1_DMA_H32		0xc0000000U	/* PCIE Client Mode sb2pcitranslation2
							 * (2 ZettaBytes), high 32 bits
							 */

#define BCMA_SFLASH			0x1c000000

#endif /* LINUX_BCMA_REGS_H_ */
