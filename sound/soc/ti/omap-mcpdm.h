/*
 * omap-mcpdm.h
 *
 * Copyright (C) 2009 - 2011 Texas Instruments
 *
 * Contact: Misael Lopez Cruz <misael.lopez@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifndef __OMAP_MCPDM_H__
#define __OMAP_MCPDM_H__

#define MCPDM_REG_REVISION		0x00
#define MCPDM_REG_SYSCONFIG		0x10
#define MCPDM_REG_IRQSTATUS_RAW		0x24
#define MCPDM_REG_IRQSTATUS		0x28
#define MCPDM_REG_IRQENABLE_SET		0x2C
#define MCPDM_REG_IRQENABLE_CLR		0x30
#define MCPDM_REG_IRQWAKE_EN		0x34
#define MCPDM_REG_DMAENABLE_SET		0x38
#define MCPDM_REG_DMAENABLE_CLR		0x3C
#define MCPDM_REG_DMAWAKEEN		0x40
#define MCPDM_REG_CTRL			0x44
#define MCPDM_REG_DN_DATA		0x48
#define MCPDM_REG_UP_DATA		0x4C
#define MCPDM_REG_FIFO_CTRL_DN		0x50
#define MCPDM_REG_FIFO_CTRL_UP		0x54
#define MCPDM_REG_DN_OFFSET		0x58

/*
 * MCPDM_IRQ bit fields
 * IRQSTATUS_RAW, IRQSTATUS, IRQENABLE_SET, IRQENABLE_CLR
 */

#define MCPDM_DN_IRQ			(1 << 0)
#define MCPDM_DN_IRQ_EMPTY		(1 << 1)
#define MCPDM_DN_IRQ_ALMST_EMPTY	(1 << 2)
#define MCPDM_DN_IRQ_FULL		(1 << 3)

#define MCPDM_UP_IRQ			(1 << 8)
#define MCPDM_UP_IRQ_EMPTY		(1 << 9)
#define MCPDM_UP_IRQ_ALMST_FULL		(1 << 10)
#define MCPDM_UP_IRQ_FULL		(1 << 11)

#define MCPDM_DOWNLINK_IRQ_MASK		0x00F
#define MCPDM_UPLINK_IRQ_MASK		0xF00

/*
 * MCPDM_DMAENABLE bit fields
 */

#define MCPDM_DMA_DN_ENABLE		(1 << 0)
#define MCPDM_DMA_UP_ENABLE		(1 << 1)

/*
 * MCPDM_CTRL bit fields
 */

#define MCPDM_PDM_UPLINK_EN(x)		(1 << (x - 1)) /* ch1 is at bit 0 */
#define MCPDM_PDM_DOWNLINK_EN(x)	(1 << (x + 2)) /* ch1 is at bit 3 */
#define MCPDM_PDMOUTFORMAT		(1 << 8)
#define MCPDM_CMD_INT			(1 << 9)
#define MCPDM_STATUS_INT		(1 << 10)
#define MCPDM_SW_UP_RST			(1 << 11)
#define MCPDM_SW_DN_RST			(1 << 12)
#define MCPDM_WD_EN			(1 << 14)
#define MCPDM_PDM_UP_MASK		0x7
#define MCPDM_PDM_DN_MASK		(0x1f << 3)


#define MCPDM_PDMOUTFORMAT_LJUST	(0 << 8)
#define MCPDM_PDMOUTFORMAT_RJUST	(1 << 8)

/*
 * MCPDM_FIFO_CTRL bit fields
 */

#define MCPDM_UP_THRES_MAX		0xF
#define MCPDM_DN_THRES_MAX		0xF

/*
 * MCPDM_DN_OFFSET bit fields
 */

#define MCPDM_DN_OFST_RX1_EN		(1 << 0)
#define MCPDM_DNOFST_RX1(x)		((x & 0x1f) << 1)
#define MCPDM_DN_OFST_RX2_EN		(1 << 8)
#define MCPDM_DNOFST_RX2(x)		((x & 0x1f) << 9)

void omap_mcpdm_configure_dn_offsets(struct snd_soc_pcm_runtime *rtd,
				    u8 rx1, u8 rx2);

#endif	/* End of __OMAP_MCPDM_H__ */
