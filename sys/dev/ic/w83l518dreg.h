/*	$OpenBSD: w83l518dreg.h,v 1.1 2009/10/03 19:51:53 kettenis Exp $	*/
/*	$NetBSD: w83l518dreg.h,v 1.1 2009/09/30 20:44:50 jmcneill Exp $ */

/*
 * Copyright (c) 2009 Jared D. McNeill <jmcneill@invisible.ca>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _SYS_DEV_IC_W83L518DREG_H
#define _SYS_DEV_IC_W83L518DREG_H

/*
 * Global Registers
 */

#define	WB_REG_RESET		0x02
#define	 WB_RESET_SWRST		 0x01	/* software reset */

#define	WB_REG_DEVNO		0x07
#define	 WB_DEVNO_SC		 0x00	/* Smart Card interface */
#define	 WB_DEVNO_MS		 0x01	/* Memory Stick interface */
#define	 WB_DEVNO_GPIO		 0x02	/* GPIO */
#define	 WB_DEVNO_SD		 0x03	/* SD memory card interface */

#define	WB_REG_DEVID_HI		0x20
#define	WB_REG_DEVID_LO		0x21
#define	 WB_DEVID_W83L518D	 0x7110
#define	 WB_DEVID_W83L519D	 0x7120
#define	 WB_DEVID_REVISION(id)	 ((id) & 0xf)

#define	WB_REG_POWER		0x22
#define	 WB_POWER_SC		 0x80	/* Smart Card interface */
#define	 WB_POWER_MS		 0x40	/* Memory Stick interface */
#define	 WB_POWER_SD		 0x20	/* SD memory card interface */

#define	WB_REG_PME		0x23
#define	 WB_PME_PME_EN		 0x80	/* Global PM event enable */
#define	 WB_PME_MSPME_EN	 0x40	/* MS PM event enable */
#define	 WB_PME_SDPME_EN	 0x20	/* SD PM event enable */
#define	 WB_PME_SCPME_EN	 0x10	/* SC PM event enable */

#define	WB_REG_PMESTS		0x24	/* PM event status */
#define	 WB_PMESTS_MSPME_STS	 0x40	/* MS PM event status */
#define	 WB_PMESTS_SDPME_STS	 0x20	/* SD PM event status */
#define	 WB_PMESTS_SCPME_STS	 0x10	/* SC PM event status */

#define	WB_REG_CFG		0x26
#define	 WB_CFG_HEFRAS		 0x40	/* Extended func reg addr select */
#define	 WB_CFG_LOCKREG		 0x20	/* Config register access control */

#define	WB_REG_MFSEL		0x29	/* Multi-function sel (518 only) */

/*
 * Logical Device Interface
 */

#define WB_REG_DEV_EN		0x30
#define	 WB_DEV_EN_ACTIVE	 0x01	/* Logical device active bit */

#define	WB_REG_DEV_BASE_HI	0x60
#define	WB_REG_DEV_BASE_LO	0x61

#define	WB_REG_DEV_IRQ		0x70
#define	 WB_DEV_IRQ_MASK	 0x0f

#define	WB_REG_DEV_DRQ		0x74
#define	 WB_DEV_DRQ_MASK	 0x0f

#define	WB_REG_DEV_MISC		0xf0
#define	 WB_DEV_MISC_SCIRQ_SHR	 0x80	/* SC: IRQ sharing control */
#define	 WB_DEV_MISC_SCPSNT_POL	 0x01	/* SC: SC present polarity */
#define	 WB_DEV_MISC_MSIRQ_POLL	 0x10	/* MS: IRQ polarity control (level) */
#define	 WB_DEV_MISC_MSIRQ_POLP	 0x08	/* MS: IRQ polarity control (pulse) */
#define	 WB_DEV_MISC_MSIRQ_SHR	 0x04	/* MS: IRQ sharing control */
#define	 WB_DEV_MISC_MS4OUT_POL	 0x02	/* MS: MS4 output polarity control */
#define	 WB_DEV_MISC_MS4OUT_EN	 0x01	/* MS: MS4 output enable */
#define	 WB_DEV_MISC_SDDATA3_HI	 0x20	/* SD: DATA3 pin will output high */
#define	 WB_DEV_MISC_SDDATA3_OUT 0x10	/* SD: DATA3 pin to output pin */
#define	 WB_DEV_MISC_SDGP11_HI	 0x04	/* SD: GP11 card-detect pin pole */
#define	 WB_DEV_MISC_SDGP11_DET	 0x02	/* SD: GP11 card-detect enable */
#define	 WB_DEV_MISC_SDDATA3_DET 0x01	/* SD: DATA3 card-detect enable */

#define	WB_REG_DEV_IRQCFG	0xf1
#define	 WB_DEV_IRQCFG_HI_L	 0x08
#define	 WB_DEV_IRQCFG_HI_P	 0x04
#define	 WB_DEV_IRQCFG_MODE	 0x02
#define	 WB_DEV_IRQCFG_DEBOUNCE	 0x01

/*
 * SD Card interface registers
 */

#define WB_SD_COMMAND		0x00
#define	WB_SD_FIFO		0x01
#define	WB_SD_INTCTL		0x02
#define	WB_SD_INTSTS		0x03
#define	 WB_INT_PENDING		 0x80
#define	 WB_INT_CARD		 0x40
#define	 WB_INT_FIFO		 0x20
#define	 WB_INT_CRC		 0x10
#define	 WB_INT_TIMEOUT		 0x08
#define	 WB_INT_PROGEND		 0x04
#define	 WB_INT_BUSYEND		 0x02
#define	 WB_INT_TC		 0x01
#define	 WB_INT_DEFAULT	\
	  (WB_INT_CARD|WB_INT_FIFO|WB_INT_CRC|WB_INT_TIMEOUT)
#define	WB_SD_FIFOSTS		0x04
#define	 WB_FIFO_EMPTY		 0x80
#define	 WB_FIFO_FULL		 0x40
#define	 WB_FIFO_EMPTY_THRES	 0x20
#define	 WB_FIFO_FULL_THRES	 0x10
#define	 WB_FIFO_DEPTH_MASK	 0x0f
#define	WB_SD_INDEX		0x05
#define	 WB_INDEX_CLK		 0x01
#define	  WB_CLK_375K		  0x00
#define	  WB_CLK_12M		  0x01
#define	  WB_CLK_16M		  0x02
#define	  WB_CLK_24M		  0x03
#define	 WB_INDEX_PBSMSB	 0x02
#define	 WB_INDEX_TAAC		 0x03
#define	 WB_INDEX_NSAC		 0x04
#define	 WB_INDEX_PBSLSB	 0x05
#define	 WB_INDEX_SETUP		 0x06
#define	  WB_SETUP_DATA3_HI	  0x08
#define	  WB_SETUP_FIFO_RST	  0x04
#define	  WB_SETUP_SOFT_RST	  0x02
#define	 WB_INDEX_DMA		 0x07
#define	 WB_INDEX_FIFOEN	 0x08
#define	  WB_FIFOEN_EMPTY	  0x20
#define	  WB_FIFOEN_FULL	  0x10
#define	 WB_INDEX_STATUS	 0x10
#define	  WB_STATUS_BLOCK_READ	  0x80
#define	  WB_STATUS_BLOCK_WRITE	  0x40
#define	  WB_STATUS_BUSY	  0x20
#define	  WB_STATUS_CARD_TRAFFIC  0x04
#define	  WB_STATUS_SEND_COMMAND  0x02
#define	  WB_STATUS_RECV_RES	  0x01
#define	 WB_INDEX_RESPLEN	 0x1e
#define	 WB_INDEX_RESP(n)	 (0x1f + (n))
#define	 WB_INDEX_CRCSTS	 0x30
#define	 WB_INDEX_ISR		 0x3f
#define	WB_SD_DATA		0x06
#define	WB_SD_CSR		0x07
#define	 WB_CSR_MS_LED		 0x20
#define	 WB_CSR_POWER_N		 0x10
#define	 WB_CSR_WRITE_PROTECT	 0x04
#define	 WB_CSR_CARD_PRESENT	 0x01

#endif
