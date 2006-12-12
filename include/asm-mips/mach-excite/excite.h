#ifndef __EXCITE_H__
#define __EXCITE_H__

#include <linux/init.h>
#include <asm/addrspace.h>
#include <asm/types.h>

#define EXCITE_CPU_EXT_CLOCK 100000000

#if !defined(__ASSEMBLY__)
void __init excite_kgdb_init(void);
void excite_procfs_init(void);
extern unsigned long memsize;
extern char modetty[];
extern u32 unit_id;
#endif

/* Base name for XICAP devices */
#define XICAP_NAME	"xicap_gpi"

/* OCD register offsets */
#define LKB0		0x0038
#define LKB5		0x0128
#define LKM5		0x012C
#define LKB7		0x0138
#define LKM7		0x013c
#define LKB8		0x0140
#define LKM8		0x0144
#define LKB9		0x0148
#define LKM9		0x014c
#define LKB10		0x0150
#define LKM10		0x0154
#define LKB11		0x0158
#define LKM11		0x015c
#define LKB12		0x0160
#define LKM12		0x0164
#define LKB13		0x0168
#define LKM13		0x016c
#define LDP0		0x0200
#define LDP1		0x0210
#define LDP2		0x0220
#define LDP3		0x0230
#define INTPIN0		0x0A40
#define INTPIN1		0x0A44
#define INTPIN2		0x0A48
#define INTPIN3		0x0A4C
#define INTPIN4		0x0A50
#define INTPIN5		0x0A54
#define INTPIN6		0x0A58
#define INTPIN7		0x0A5C




/* TITAN register offsets */
#define CPRR		0x0004
#define CPDSR		0x0008
#define CPTC0R		0x000c
#define CPTC1R		0x0010
#define CPCFG0		0x0020
#define CPCFG1		0x0024
#define CPDST0A		0x0028
#define CPDST0B		0x002c
#define CPDST1A		0x0030
#define CPDST1B		0x0034
#define CPXDSTA		0x0038
#define CPXDSTB		0x003c
#define CPXCISRA	0x0048
#define CPXCISRB	0x004c
#define CPGIG0ER	0x0050
#define CPGIG1ER	0x0054
#define CPGRWL		0x0068
#define CPURSLMT	0x00f8
#define UACFG		0x0200
#define UAINTS		0x0204
#define SDRXFCIE	0x4828
#define SDTXFCIE	0x4928
#define INTP0Status0	0x1B00
#define INTP0Mask0	0x1B04
#define INTP0Set0	0x1B08
#define INTP0Clear0	0x1B0C
#define GXCFG		0x5000
#define GXDMADRPFX	0x5018
#define GXDMA_DESCADR	0x501c
#define GXCH0TDESSTRT	0x5054

/* IRQ definitions */
#define NMICONFIG		0xac0
#define TITAN_MSGINT	0xc4
#define TITAN_IRQ	((TITAN_MSGINT / 0x20) + 2)
#define FPGA0_MSGINT	0x5a
#define FPGA0_IRQ	((FPGA0_MSGINT / 0x20) + 2)
#define FPGA1_MSGINT	0x7b
#define FPGA1_IRQ	((FPGA1_MSGINT / 0x20) + 2)
#define PHY_MSGINT	0x9c
#define PHY_IRQ		((PHY_MSGINT   / 0x20) + 2)

#if defined(CONFIG_BASLER_EXCITE_PROTOTYPE)
/* Pre-release units used interrupt pin #9 */
#define USB_IRQ		11
#else
/* Re-designed units use interrupt pin #1 */
#define USB_MSGINT	0x39
#define USB_IRQ		((USB_MSGINT / 0x20) + 2)
#endif
#define TIMER_IRQ	12


/* Device address ranges */
#define EXCITE_OFFS_OCD		0x1fffc000
#define	EXCITE_SIZE_OCD		(16 * 1024)
#define EXCITE_PHYS_OCD		CPHYSADDR(EXCITE_OFFS_OCD)
#define EXCITE_ADDR_OCD		CKSEG1ADDR(EXCITE_OFFS_OCD)

#define EXCITE_OFFS_SCRAM 	0x1fffa000
#define	EXCITE_SIZE_SCRAM	(8 << 10)
#define EXCITE_PHYS_SCRAM 	CPHYSADDR(EXCITE_OFFS_SCRAM)
#define EXCITE_ADDR_SCRAM 	CKSEG1ADDR(EXCITE_OFFS_SCRAM)

#define EXCITE_OFFS_PCI_IO	0x1fff8000
#define	EXCITE_SIZE_PCI_IO	(8 << 10)
#define EXCITE_PHYS_PCI_IO	CPHYSADDR(EXCITE_OFFS_PCI_IO)
#define EXCITE_ADDR_PCI_IO 	CKSEG1ADDR(EXCITE_OFFS_PCI_IO)

#define EXCITE_OFFS_TITAN	0x1fff0000
#define EXCITE_SIZE_TITAN	(32 << 10)
#define EXCITE_PHYS_TITAN	CPHYSADDR(EXCITE_OFFS_TITAN)
#define EXCITE_ADDR_TITAN	CKSEG1ADDR(EXCITE_OFFS_TITAN)

#define EXCITE_OFFS_PCI_MEM	0x1ffe0000
#define EXCITE_SIZE_PCI_MEM	(64 << 10)
#define EXCITE_PHYS_PCI_MEM	CPHYSADDR(EXCITE_OFFS_PCI_MEM)
#define EXCITE_ADDR_PCI_MEM	CKSEG1ADDR(EXCITE_OFFS_PCI_MEM)

#define EXCITE_OFFS_FPGA	0x1ffdc000
#define EXCITE_SIZE_FPGA	(16 << 10)
#define EXCITE_PHYS_FPGA	CPHYSADDR(EXCITE_OFFS_FPGA)
#define EXCITE_ADDR_FPGA	CKSEG1ADDR(EXCITE_OFFS_FPGA)

#define EXCITE_OFFS_NAND	0x1ffd8000
#define EXCITE_SIZE_NAND	(16 << 10)
#define EXCITE_PHYS_NAND	CPHYSADDR(EXCITE_OFFS_NAND)
#define EXCITE_ADDR_NAND	CKSEG1ADDR(EXCITE_OFFS_NAND)

#define EXCITE_OFFS_BOOTROM	0x1f000000
#define EXCITE_SIZE_BOOTROM	(8 << 20)
#define EXCITE_PHYS_BOOTROM	CPHYSADDR(EXCITE_OFFS_BOOTROM)
#define EXCITE_ADDR_BOOTROM	CKSEG1ADDR(EXCITE_OFFS_BOOTROM)

/* FPGA address offsets */
#define EXCITE_FPGA_DPR		0x0104	/* dual-ported ram */
#define EXCITE_FPGA_SYSCTL	0x0200	/* system control register block */

#endif /* __EXCITE_H__ */
