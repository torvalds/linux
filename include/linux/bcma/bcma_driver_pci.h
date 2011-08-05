#ifndef LINUX_BCMA_DRIVER_PCI_H_
#define LINUX_BCMA_DRIVER_PCI_H_

#include <linux/types.h>

struct pci_dev;

/** PCI core registers. **/
#define BCMA_CORE_PCI_CTL			0x0000	/* PCI Control */
#define  BCMA_CORE_PCI_CTL_RST_OE		0x00000001 /* PCI_RESET Output Enable */
#define  BCMA_CORE_PCI_CTL_RST			0x00000002 /* PCI_RESET driven out to pin */
#define  BCMA_CORE_PCI_CTL_CLK_OE		0x00000004 /* Clock gate Output Enable */
#define  BCMA_CORE_PCI_CTL_CLK			0x00000008 /* Gate for clock driven out to pin */
#define BCMA_CORE_PCI_ARBCTL			0x0010	/* PCI Arbiter Control */
#define  BCMA_CORE_PCI_ARBCTL_INTERN		0x00000001 /* Use internal arbiter */
#define  BCMA_CORE_PCI_ARBCTL_EXTERN		0x00000002 /* Use external arbiter */
#define  BCMA_CORE_PCI_ARBCTL_PARKID		0x00000006 /* Mask, selects which agent is parked on an idle bus */
#define   BCMA_CORE_PCI_ARBCTL_PARKID_LAST	0x00000000 /* Last requestor */
#define   BCMA_CORE_PCI_ARBCTL_PARKID_4710	0x00000002 /* 4710 */
#define   BCMA_CORE_PCI_ARBCTL_PARKID_EXT0	0x00000004 /* External requestor 0 */
#define   BCMA_CORE_PCI_ARBCTL_PARKID_EXT1	0x00000006 /* External requestor 1 */
#define BCMA_CORE_PCI_ISTAT			0x0020	/* Interrupt status */
#define  BCMA_CORE_PCI_ISTAT_INTA		0x00000001 /* PCI INTA# */
#define  BCMA_CORE_PCI_ISTAT_INTB		0x00000002 /* PCI INTB# */
#define  BCMA_CORE_PCI_ISTAT_SERR		0x00000004 /* PCI SERR# (write to clear) */
#define  BCMA_CORE_PCI_ISTAT_PERR		0x00000008 /* PCI PERR# (write to clear) */
#define  BCMA_CORE_PCI_ISTAT_PME		0x00000010 /* PCI PME# */
#define BCMA_CORE_PCI_IMASK			0x0024	/* Interrupt mask */
#define  BCMA_CORE_PCI_IMASK_INTA		0x00000001 /* PCI INTA# */
#define  BCMA_CORE_PCI_IMASK_INTB		0x00000002 /* PCI INTB# */
#define  BCMA_CORE_PCI_IMASK_SERR		0x00000004 /* PCI SERR# */
#define  BCMA_CORE_PCI_IMASK_PERR		0x00000008 /* PCI PERR# */
#define  BCMA_CORE_PCI_IMASK_PME		0x00000010 /* PCI PME# */
#define BCMA_CORE_PCI_MBOX			0x0028	/* Backplane to PCI Mailbox */
#define  BCMA_CORE_PCI_MBOX_F0_0		0x00000100 /* PCI function 0, INT 0 */
#define  BCMA_CORE_PCI_MBOX_F0_1		0x00000200 /* PCI function 0, INT 1 */
#define  BCMA_CORE_PCI_MBOX_F1_0		0x00000400 /* PCI function 1, INT 0 */
#define  BCMA_CORE_PCI_MBOX_F1_1		0x00000800 /* PCI function 1, INT 1 */
#define  BCMA_CORE_PCI_MBOX_F2_0		0x00001000 /* PCI function 2, INT 0 */
#define  BCMA_CORE_PCI_MBOX_F2_1		0x00002000 /* PCI function 2, INT 1 */
#define  BCMA_CORE_PCI_MBOX_F3_0		0x00004000 /* PCI function 3, INT 0 */
#define  BCMA_CORE_PCI_MBOX_F3_1		0x00008000 /* PCI function 3, INT 1 */
#define BCMA_CORE_PCI_BCAST_ADDR		0x0050	/* Backplane Broadcast Address */
#define  BCMA_CORE_PCI_BCAST_ADDR_MASK		0x000000FF
#define BCMA_CORE_PCI_BCAST_DATA		0x0054	/* Backplane Broadcast Data */
#define BCMA_CORE_PCI_GPIO_IN			0x0060	/* rev >= 2 only */
#define BCMA_CORE_PCI_GPIO_OUT			0x0064	/* rev >= 2 only */
#define BCMA_CORE_PCI_GPIO_ENABLE		0x0068	/* rev >= 2 only */
#define BCMA_CORE_PCI_GPIO_CTL			0x006C	/* rev >= 2 only */
#define BCMA_CORE_PCI_SBTOPCI0			0x0100	/* Backplane to PCI translation 0 (sbtopci0) */
#define  BCMA_CORE_PCI_SBTOPCI0_MASK		0xFC000000
#define BCMA_CORE_PCI_SBTOPCI1			0x0104	/* Backplane to PCI translation 1 (sbtopci1) */
#define  BCMA_CORE_PCI_SBTOPCI1_MASK		0xFC000000
#define BCMA_CORE_PCI_SBTOPCI2			0x0108	/* Backplane to PCI translation 2 (sbtopci2) */
#define  BCMA_CORE_PCI_SBTOPCI2_MASK		0xC0000000
#define BCMA_CORE_PCI_PCICFG0			0x0400	/* PCI config space 0 (rev >= 8) */
#define BCMA_CORE_PCI_PCICFG1			0x0500	/* PCI config space 1 (rev >= 8) */
#define BCMA_CORE_PCI_PCICFG2			0x0600	/* PCI config space 2 (rev >= 8) */
#define BCMA_CORE_PCI_PCICFG3			0x0700	/* PCI config space 3 (rev >= 8) */
#define BCMA_CORE_PCI_SPROM(wordoffset)		(0x0800 + ((wordoffset) * 2)) /* SPROM shadow area (72 bytes) */

/* SBtoPCIx */
#define BCMA_CORE_PCI_SBTOPCI_MEM		0x00000000
#define BCMA_CORE_PCI_SBTOPCI_IO		0x00000001
#define BCMA_CORE_PCI_SBTOPCI_CFG0		0x00000002
#define BCMA_CORE_PCI_SBTOPCI_CFG1		0x00000003
#define BCMA_CORE_PCI_SBTOPCI_PREF		0x00000004 /* Prefetch enable */
#define BCMA_CORE_PCI_SBTOPCI_BURST		0x00000008 /* Burst enable */
#define BCMA_CORE_PCI_SBTOPCI_MRM		0x00000020 /* Memory Read Multiple */
#define BCMA_CORE_PCI_SBTOPCI_RC		0x00000030 /* Read Command mask (rev >= 11) */
#define  BCMA_CORE_PCI_SBTOPCI_RC_READ		0x00000000 /* Memory read */
#define  BCMA_CORE_PCI_SBTOPCI_RC_READL		0x00000010 /* Memory read line */
#define  BCMA_CORE_PCI_SBTOPCI_RC_READM		0x00000020 /* Memory read multiple */

/* PCIcore specific boardflags */
#define BCMA_CORE_PCI_BFL_NOPCI			0x00000400 /* Board leaves PCI floating */

struct bcma_drv_pci {
	struct bcma_device *core;
	u8 setup_done:1;
};

/* Register access */
#define pcicore_read32(pc, offset)		bcma_read32((pc)->core, offset)
#define pcicore_write32(pc, offset, val)	bcma_write32((pc)->core, offset, val)

extern void bcma_core_pci_init(struct bcma_drv_pci *pc);
extern int bcma_core_pci_irq_ctl(struct bcma_drv_pci *pc,
				 struct bcma_device *core, bool enable);

#endif /* LINUX_BCMA_DRIVER_PCI_H_ */
