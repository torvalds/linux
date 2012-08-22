/*
 * Copyright (C) 2005 Stephen Street / StreetFire Sound Labs
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#ifndef __linux_pxa2xx_spi_h
#define __linux_pxa2xx_spi_h

#include <linux/pxa2xx_ssp.h>

#define PXA2XX_CS_ASSERT (0x01)
#define PXA2XX_CS_DEASSERT (0x02)

/* device.platform_data for SSP controller devices */
struct pxa2xx_spi_master {
	u32 clock_enable;
	u16 num_chipselect;
	u8 enable_dma;
};

/* spi_board_info.controller_data for SPI slave devices,
 * copied to spi_device.platform_data ... mostly for dma tuning
 */
struct pxa2xx_spi_chip {
	u8 tx_threshold;
	u8 rx_threshold;
	u8 dma_burst_size;
	u32 timeout;
	u8 enable_loopback;
	int gpio_cs;
	void (*cs_control)(u32 command);
};

#if defined(CONFIG_ARCH_PXA) || defined(CONFIG_ARCH_MMP)

#include <linux/clk.h>
#include <mach/dma.h>

extern void pxa2xx_set_spi_info(unsigned id, struct pxa2xx_spi_master *info);

#else
/*
 * This is the implemtation for CE4100 on x86. ARM defines them in mach/ or
 * plat/ include path.
 * The CE4100 does not provide DMA support. This bits are here to let the driver
 * compile and will never be used. Maybe we get DMA support at a later point in
 * time.
 */

#define DCSR(n)         (n)
#define DSADR(n)        (n)
#define DTADR(n)        (n)
#define DCMD(n)         (n)
#define DRCMR(n)        (n)

#define DCSR_RUN	(1 << 31)	/* Run Bit */
#define DCSR_NODESC	(1 << 30)	/* No-Descriptor Fetch */
#define DCSR_STOPIRQEN	(1 << 29)	/* Stop Interrupt Enable */
#define DCSR_REQPEND	(1 << 8)	/* Request Pending (read-only) */
#define DCSR_STOPSTATE	(1 << 3)	/* Stop State (read-only) */
#define DCSR_ENDINTR	(1 << 2)	/* End Interrupt */
#define DCSR_STARTINTR	(1 << 1)	/* Start Interrupt */
#define DCSR_BUSERR	(1 << 0)	/* Bus Error Interrupt */

#define DCSR_EORIRQEN	(1 << 28)	/* End of Receive Interrupt Enable */
#define DCSR_EORJMPEN	(1 << 27)	/* Jump to next descriptor on EOR */
#define DCSR_EORSTOPEN	(1 << 26)	/* STOP on an EOR */
#define DCSR_SETCMPST	(1 << 25)	/* Set Descriptor Compare Status */
#define DCSR_CLRCMPST	(1 << 24)	/* Clear Descriptor Compare Status */
#define DCSR_CMPST	(1 << 10)	/* The Descriptor Compare Status */
#define DCSR_EORINTR	(1 << 9)	/* The end of Receive */

#define DRCMR_MAPVLD	(1 << 7)	/* Map Valid */
#define DRCMR_CHLNUM	0x1f		/* mask for Channel Number */

#define DDADR_DESCADDR	0xfffffff0	/* Address of next descriptor */
#define DDADR_STOP	(1 << 0)	/* Stop */

#define DCMD_INCSRCADDR	(1 << 31)	/* Source Address Increment Setting. */
#define DCMD_INCTRGADDR	(1 << 30)	/* Target Address Increment Setting. */
#define DCMD_FLOWSRC	(1 << 29)	/* Flow Control by the source. */
#define DCMD_FLOWTRG	(1 << 28)	/* Flow Control by the target. */
#define DCMD_STARTIRQEN	(1 << 22)	/* Start Interrupt Enable */
#define DCMD_ENDIRQEN	(1 << 21)	/* End Interrupt Enable */
#define DCMD_ENDIAN	(1 << 18)	/* Device Endian-ness. */
#define DCMD_BURST8	(1 << 16)	/* 8 byte burst */
#define DCMD_BURST16	(2 << 16)	/* 16 byte burst */
#define DCMD_BURST32	(3 << 16)	/* 32 byte burst */
#define DCMD_WIDTH1	(1 << 14)	/* 1 byte width */
#define DCMD_WIDTH2	(2 << 14)	/* 2 byte width (HalfWord) */
#define DCMD_WIDTH4	(3 << 14)	/* 4 byte width (Word) */
#define DCMD_LENGTH	0x01fff		/* length mask (max = 8K - 1) */

/*
 * Descriptor structure for PXA's DMA engine
 * Note: this structure must always be aligned to a 16-byte boundary.
 */

typedef enum {
	DMA_PRIO_HIGH = 0,
	DMA_PRIO_MEDIUM = 1,
	DMA_PRIO_LOW = 2
} pxa_dma_prio;

/*
 * DMA registration
 */

static inline int pxa_request_dma(char *name,
		pxa_dma_prio prio,
		void (*irq_handler)(int, void *),
		void *data)
{
	return -ENODEV;
}

static inline void pxa_free_dma(int dma_ch)
{
}

/*
 * The CE4100 does not have the clk framework implemented and SPI clock can
 * not be switched on/off or the divider changed.
 */
static inline void clk_disable(struct clk *clk)
{
}

static inline int clk_enable(struct clk *clk)
{
	return 0;
}

static inline unsigned long clk_get_rate(struct clk *clk)
{
	return 3686400;
}

#endif
#endif
