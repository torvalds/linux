/*-
 * Copyright (c) 2016 Stanislav Galabov.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef __MTK_PCIE_H__
#define __MTK_PCIE_H__

#define MTK_PCI_NIRQS			3
#define MTK_PCI_BASESLOT		0

struct mtk_pci_softc {
	device_t		sc_dev;

	struct resource *	pci_res[MTK_PCI_NIRQS + 1];
	void *			pci_intrhand[MTK_PCI_NIRQS];

	int			sc_busno;
	int			sc_cur_secbus;

	struct rman		sc_mem_rman;
	struct rman		sc_io_rman;
	struct rman		sc_irq_rman;

	uint32_t		sc_num_irq;
	uint32_t		sc_irq_start;
	uint32_t		sc_irq_end;

	bus_addr_t		sc_mem_base;
	bus_addr_t		sc_mem_size;

	bus_addr_t		sc_io_base;
	bus_addr_t		sc_io_size;

	struct intr_event	*sc_eventstab[MTK_PCI_NIRQS];

	uint32_t		pcie_link_status;
	uint32_t		num_slots;
	uint32_t		socid;
	uint32_t		addr_mask;
};

#define MTK_PCI_PCICFG			0x0000
#define    MTK_PCI_RESET			(1<<1)
#define MTK_PCI_PCIINT			0x0008
#define MTK_PCI_PCIENA			0x000C
#define MTK_PCI_CFGADDR			0x0020
#define MTK_PCI_CFGDATA			0x0024
#define MTK_PCI_MEMBASE			0x0028
#define MTK_PCI_IOBASE			0x002C
#define MTK_PCI_ARBCTL			0x0080
#define MTK_PCI_PHY0_CFG		0x0090

#define MTK_PCI_PCIE0_BAR0SETUP		0x2010
#define MTK_PCI_PCIE0_BAR1SETUP		0x2014
#define MTK_PCI_PCIE0_IMBASEBAR0	0x2018
#define MTK_PCI_PCIE0_ID		0x2030
#define MTK_PCI_PCIE0_CLASS		0x2034
#define MTK_PCI_PCIE0_SUBID		0x2038
#define MTK_PCI_PCIE0_STATUS		0x2050
#define MTK_PCI_PCIE0_DLECR		0x2060
#define MTK_PCI_PCIE0_ECRC		0x2064

#define MTK_PCIE_BAR0SETUP(_s)		(MTK_PCI_PCIE0_BAR0SETUP + (_s)*0x1000)
#define MTK_PCIE_BAR1SETUP(_s)		(MTK_PCI_PCIE0_BAR1SETUP + (_s)*0x1000)
#define MTK_PCIE_IMBASEBAR0(_s)		(MTK_PCI_PCIE0_IMBASEBAR0 + (_s)*0x1000)
#define MTK_PCIE_ID(_s)			(MTK_PCI_PCIE0_ID + (_s)*0x1000)
#define MTK_PCIE_CLASS(_s)		(MTK_PCI_PCIE0_CLASS + (_s)*0x1000)
#define MTK_PCIE_SUBID(_s)		(MTK_PCI_PCIE0_SUBID + (_s)*0x1000)
#define MTK_PCIE_STATUS(_s)		(MTK_PCI_PCIE0_STATUS + (_s)*0x1000)

#define MTK_PCIE0_IRQ			20
#define MTK_PCIE1_IRQ			21
#define MTK_PCIE2_IRQ			22

#define MTK_PCI_INTR_PIN		2

/* Chip specific defines */
#define MT7620_MAX_RETRIES	10
#define MT7620_PCIE_PHY_CFG	0x90
#define    PHY_BUSY			(1<<31)
#define    PHY_MODE_WRITE		(1<<23)
#define    PHY_ADDR_OFFSET		8
#define MT7620_PPLL_CFG0	0x98
#define    PPLL_SW_SET			(1<<31)
#define MT7620_PPLL_CFG1	0x9c
#define    PPLL_PD			(1<<26)
#define    PPLL_LOCKED			(1<<23)
#define MT7620_PPLL_DRV		0xa0
#define   PDRV_SW_SET			(1<<31)
#define   LC_CKDRVPD			(1<<19)
#define   LC_CKDRVOHZ			(1<<18)
#define   LC_CKDRVHZ			(1<<17)
#define MT7620_PERST_GPIO_MODE	(3<<16)
#define   MT7620_PERST			(0<<16)
#define   MT7620_GPIO			(2<<16)
#define MT7620_PKG_BGA		(1<<16)

#define MT7628_PERST_GPIO_MODE	(1<<16)
#define   MT7628_PERST			(0<<16)

#define MT7621_PERST_GPIO_MODE	(3<<10)
#define   MT7621_PERST_GPIO		(1<<10)
#define MT7621_UARTL3_GPIO_MODE	(3<<3)
#define   MT7621_UARTL3_GPIO		(1<<3)
#define MT7621_PCIE0_RST	(1<<19)
#define MT7621_PCIE1_RST	(1<<8)
#define MT7621_PCIE2_RST	(1<<7)
#define MT7621_PCIE_RST		(MT7621_PCIE0_RST | MT7621_PCIE1_RST | \
				 MT7621_PCIE2_RST)

#define RT3883_PCI_RST		(1<<24)
#define RT3883_PCI_CLK		(1<<19)
#define RT3883_PCI_HOST_MODE	(1<<7)
#define RT3883_PCIE_RC_MODE	(1<<8)
/* End of chip specific defines */

#define MT_WRITE32(sc, off, val) \
	bus_write_4((sc)->pci_res[0], (off), (val))
#define MT_WRITE16(sc, off, val) \
	bus_write_2((sc)->pci_res[0], (off), (val))
#define MT_WRITE8(sc, off, val) \
	bus_write_1((sc)->pci_res[0], (off), (val))
#define MT_READ32(sc, off) \
	bus_read_4((sc)->pci_res[0], (off))
#define MT_READ16(sc, off) \
	bus_read_2((sc)->pci_res[0], (off))
#define MT_READ8(sc, off) \
	bus_read_1((sc)->pci_res[0], (off))

#define MT_CLR_SET32(sc, off, clr, set)	\
	MT_WRITE32((sc), (off), ((MT_READ32((sc), (off)) & ~(clr)) | (off)))

#endif /* __MTK_PCIE_H__ */
