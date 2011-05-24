#ifndef LINUX_BCMA_REGS_H_
#define LINUX_BCMA_REGS_H_

/* Agent registers (common for every core) */
#define BCMA_IOCTL			0x0408
#define  BCMA_IOCTL_CLK			0x0001
#define  BCMA_IOCTL_FGC			0x0002
#define  BCMA_IOCTL_CORE_BITS		0x3FFC
#define  BCMA_IOCTL_PME_EN		0x4000
#define  BCMA_IOCTL_BIST_EN		0x8000
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

#endif /* LINUX_BCMA_REGS_H_ */
