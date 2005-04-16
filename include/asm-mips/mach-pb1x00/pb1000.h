/*
 * Alchemy Semi PB1000 Referrence Board
 *
 * Copyright 2001 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *         	ppopov@mvista.com or source@mvista.com
 *
 * ########################################################################
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * ########################################################################
 *
 *
 */
#ifndef __ASM_PB1000_H
#define __ASM_PB1000_H

/* PCMCIA PB1000 specific defines */
#define PCMCIA_MAX_SOCK 1
#define PCMCIA_NUM_SOCKS (PCMCIA_MAX_SOCK+1)

#define PB1000_PCR     0xBE000000
  #define PCR_SLOT_0_VPP0  (1<<0)
  #define PCR_SLOT_0_VPP1  (1<<1)
  #define PCR_SLOT_0_VCC0  (1<<2)
  #define PCR_SLOT_0_VCC1  (1<<3)
  #define PCR_SLOT_0_RST   (1<<4)

  #define PCR_SLOT_1_VPP0  (1<<8)
  #define PCR_SLOT_1_VPP1  (1<<9)
  #define PCR_SLOT_1_VCC0  (1<<10)
  #define PCR_SLOT_1_VCC1  (1<<11)
  #define PCR_SLOT_1_RST   (1<<12)

#define PB1000_MDR     0xBE000004
  #define MDR_PI        (1<<5)  /* pcmcia int latch  */
  #define MDR_EPI      (1<<14)  /* enable pcmcia int */
  #define MDR_CPI      (1<<15)  /* clear pcmcia int  */

#define PB1000_ACR1    0xBE000008
  #define ACR1_SLOT_0_CD1    (1<<0)  /* card detect 1     */
  #define ACR1_SLOT_0_CD2    (1<<1)  /* card detect 2     */
  #define ACR1_SLOT_0_READY  (1<<2)  /* ready             */
  #define ACR1_SLOT_0_STATUS (1<<3)  /* status change     */
  #define ACR1_SLOT_0_VS1    (1<<4)  /* voltage sense 1   */
  #define ACR1_SLOT_0_VS2    (1<<5)  /* voltage sense 2   */
  #define ACR1_SLOT_0_INPACK (1<<6)  /* inpack pin status */
  #define ACR1_SLOT_1_CD1    (1<<8)  /* card detect 1     */
  #define ACR1_SLOT_1_CD2    (1<<9)  /* card detect 2     */
  #define ACR1_SLOT_1_READY  (1<<10) /* ready             */
  #define ACR1_SLOT_1_STATUS (1<<11) /* status change     */
  #define ACR1_SLOT_1_VS1    (1<<12) /* voltage sense 1   */
  #define ACR1_SLOT_1_VS2    (1<<13) /* voltage sense 2   */
  #define ACR1_SLOT_1_INPACK (1<<14) /* inpack pin status */

#define CPLD_AUX0      0xBE00000C
#define CPLD_AUX1      0xBE000010
#define CPLD_AUX2      0xBE000014

/* Voltage levels */

/* VPPEN1 - VPPEN0 */
#define VPP_GND ((0<<1) | (0<<0))
#define VPP_5V  ((1<<1) | (0<<0))
#define VPP_3V  ((0<<1) | (1<<0))
#define VPP_12V ((0<<1) | (1<<0))
#define VPP_HIZ ((1<<1) | (1<<0))

/* VCCEN1 - VCCEN0 */
#define VCC_3V  ((0<<1) | (1<<0))
#define VCC_5V  ((1<<1) | (0<<0))
#define VCC_HIZ ((0<<1) | (0<<0))

/* VPP/VCC */
#define SET_VCC_VPP(VCC, VPP, SLOT)\
	((((VCC)<<2) | ((VPP)<<0)) << ((SLOT)*8))


/* PCI PB1000 specific defines */
/* The reason these defines are here instead of au1000.h is because
 * the Au1000 does not have a PCI bus controller so the PCI implementation
 * on the some of the older Pb1000 boards was very board specific.
 */
#define PCI_CONFIG_BASE   0xBA020000 /* the only external slot */

#define SDRAM_DEVID       0xBA010000
#define SDRAM_CMD         0xBA010004
#define SDRAM_CLASS       0xBA010008
#define SDRAM_MISC        0xBA01000C
#define SDRAM_MBAR        0xBA010010

#define PCI_IO_DATA_PORT  0xBA800000

#define PCI_IO_ADDR       0xBE00001C
#define PCI_INT_ACK       0xBBC00000
#define PCI_IO_READ       0xBBC00020
#define PCI_IO_WRITE      0xBBC00030

#define PCI_BRIDGE_CONFIG 0xBE000018

#define PCI_IO_START      0x10000000
#define PCI_IO_END        0x1000ffff
#define PCI_MEM_START     0x18000000
#define PCI_MEM_END       0x18ffffff

#define PCI_FIRST_DEVFN   0
#define PCI_LAST_DEVFN    1

static inline u8 au_pci_io_readb(u32 addr)
{
	writel(addr, PCI_IO_ADDR);
	writel((readl(PCI_BRIDGE_CONFIG) & 0xffffcfff) | (1<<12), PCI_BRIDGE_CONFIG);
	return (readl(PCI_IO_DATA_PORT) & 0xff);
}

static inline u16 au_pci_io_readw(u32 addr)
{
	writel(addr, PCI_IO_ADDR);
	writel((readl(PCI_BRIDGE_CONFIG) & 0xffffcfff) | (1<<13), PCI_BRIDGE_CONFIG);
	return (readl(PCI_IO_DATA_PORT) & 0xffff);
}

static inline u32 au_pci_io_readl(u32 addr)
{
	writel(addr, PCI_IO_ADDR);
	writel((readl(PCI_BRIDGE_CONFIG) & 0xffffcfff), PCI_BRIDGE_CONFIG);
	return readl(PCI_IO_DATA_PORT);
}

static inline void au_pci_io_writeb(u8 val, u32 addr)
{
	writel(addr, PCI_IO_ADDR);
	writel((readl(PCI_BRIDGE_CONFIG) & 0xffffcfff) | (1<<12), PCI_BRIDGE_CONFIG);
	writel(val, PCI_IO_DATA_PORT);
}

static inline void au_pci_io_writew(u16 val, u32 addr)
{
	writel(addr, PCI_IO_ADDR);
	writel((readl(PCI_BRIDGE_CONFIG) & 0xffffcfff) | (1<<13), PCI_BRIDGE_CONFIG);
	writel(val, PCI_IO_DATA_PORT);
}

static inline void au_pci_io_writel(u32 val, u32 addr)
{
	writel(addr, PCI_IO_ADDR);
	writel(readl(PCI_BRIDGE_CONFIG) & 0xffffcfff, PCI_BRIDGE_CONFIG);
	writel(val, PCI_IO_DATA_PORT);
}

static inline void set_sdram_extbyte(void)
{
	writel(readl(PCI_BRIDGE_CONFIG) & 0xffffff00, PCI_BRIDGE_CONFIG);
}

static inline void set_slot_extbyte(void)
{
	writel((readl(PCI_BRIDGE_CONFIG) & 0xffffbf00) | 0x18, PCI_BRIDGE_CONFIG);
}
#endif /* __ASM_PB1000_H */
