/*
 * linux/include/asm-arm/arch-omap/gpio.h
 *
 * Defines for Multi-Channel Buffered Serial Port
 *
 * Copyright (C) 2002 RidgeRun, Inc.
 * Author: Steve Johnson
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */
#ifndef __ASM_ARCH_OMAP_MCBSP_H
#define __ASM_ARCH_OMAP_MCBSP_H

#include <asm/arch/hardware.h>

#define OMAP730_MCBSP1_BASE	0xfffb1000
#define OMAP730_MCBSP2_BASE	0xfffb1800

#define OMAP1510_MCBSP1_BASE	0xe1011800
#define OMAP1510_MCBSP2_BASE	0xfffb1000
#define OMAP1510_MCBSP3_BASE	0xe1017000

#define OMAP1610_MCBSP1_BASE	0xe1011800
#define OMAP1610_MCBSP2_BASE	0xfffb1000
#define OMAP1610_MCBSP3_BASE	0xe1017000

#define OMAP_MCBSP_REG_DRR2	0x00
#define OMAP_MCBSP_REG_DRR1	0x02
#define OMAP_MCBSP_REG_DXR2	0x04
#define OMAP_MCBSP_REG_DXR1	0x06
#define OMAP_MCBSP_REG_SPCR2	0x08
#define OMAP_MCBSP_REG_SPCR1	0x0a
#define OMAP_MCBSP_REG_RCR2	0x0c
#define OMAP_MCBSP_REG_RCR1	0x0e
#define OMAP_MCBSP_REG_XCR2	0x10
#define OMAP_MCBSP_REG_XCR1	0x12
#define OMAP_MCBSP_REG_SRGR2	0x14
#define OMAP_MCBSP_REG_SRGR1	0x16
#define OMAP_MCBSP_REG_MCR2	0x18
#define OMAP_MCBSP_REG_MCR1	0x1a
#define OMAP_MCBSP_REG_RCERA	0x1c
#define OMAP_MCBSP_REG_RCERB	0x1e
#define OMAP_MCBSP_REG_XCERA	0x20
#define OMAP_MCBSP_REG_XCERB	0x22
#define OMAP_MCBSP_REG_PCR0	0x24
#define OMAP_MCBSP_REG_RCERC	0x26
#define OMAP_MCBSP_REG_RCERD	0x28
#define OMAP_MCBSP_REG_XCERC	0x2A
#define OMAP_MCBSP_REG_XCERD	0x2C
#define OMAP_MCBSP_REG_RCERE	0x2E
#define OMAP_MCBSP_REG_RCERF	0x30
#define OMAP_MCBSP_REG_XCERE	0x32
#define OMAP_MCBSP_REG_XCERF	0x34
#define OMAP_MCBSP_REG_RCERG	0x36
#define OMAP_MCBSP_REG_RCERH	0x38
#define OMAP_MCBSP_REG_XCERG	0x3A
#define OMAP_MCBSP_REG_XCERH	0x3C

#define OMAP_MAX_MCBSP_COUNT 3

#define OMAP_MCBSP_READ(base, reg)		__raw_readw((base) + OMAP_MCBSP_REG_##reg)
#define OMAP_MCBSP_WRITE(base, reg, val)	__raw_writew((val), (base) + OMAP_MCBSP_REG_##reg)

/************************** McBSP SPCR1 bit definitions ***********************/
#define RRST			0x0001
#define RRDY			0x0002
#define RFULL			0x0004
#define RSYNC_ERR		0x0008
#define RINTM(value)		((value)<<4)	/* bits 4:5 */
#define ABIS			0x0040
#define DXENA			0x0080
#define CLKSTP(value)		((value)<<11)	/* bits 11:12 */
#define RJUST(value)		((value)<<13)	/* bits 13:14 */
#define DLB			0x8000

/************************** McBSP SPCR2 bit definitions ***********************/
#define XRST		0x0001
#define XRDY		0x0002
#define XEMPTY		0x0004
#define XSYNC_ERR	0x0008
#define XINTM(value)	((value)<<4)		/* bits 4:5 */
#define GRST		0x0040
#define FRST		0x0080
#define SOFT		0x0100
#define FREE		0x0200

/************************** McBSP PCR bit definitions *************************/
#define CLKRP		0x0001
#define CLKXP		0x0002
#define FSRP		0x0004
#define FSXP		0x0008
#define DR_STAT		0x0010
#define DX_STAT		0x0020
#define CLKS_STAT	0x0040
#define SCLKME		0x0080
#define CLKRM		0x0100
#define CLKXM		0x0200
#define FSRM		0x0400
#define FSXM		0x0800
#define RIOEN		0x1000
#define XIOEN		0x2000
#define IDLE_EN		0x4000

/************************** McBSP RCR1 bit definitions ************************/
#define RWDLEN1(value)		((value)<<5)	/* Bits 5:7 */
#define RFRLEN1(value)		((value)<<8)	/* Bits 8:14 */

/************************** McBSP XCR1 bit definitions ************************/
#define XWDLEN1(value)		((value)<<5)	/* Bits 5:7 */
#define XFRLEN1(value)		((value)<<8)	/* Bits 8:14 */

/*************************** McBSP RCR2 bit definitions ***********************/
#define RDATDLY(value)		(value)		/* Bits 0:1 */
#define RFIG			0x0004
#define RCOMPAND(value)		((value)<<3)	/* Bits 3:4 */
#define RWDLEN2(value)		((value)<<5)	/* Bits 5:7 */
#define RFRLEN2(value)		((value)<<8)	/* Bits 8:14 */
#define RPHASE			0x8000

/*************************** McBSP XCR2 bit definitions ***********************/
#define XDATDLY(value)		(value)		/* Bits 0:1 */
#define XFIG			0x0004
#define XCOMPAND(value)		((value)<<3)	/* Bits 3:4 */
#define XWDLEN2(value)		((value)<<5)	/* Bits 5:7 */
#define XFRLEN2(value)		((value)<<8)	/* Bits 8:14 */
#define XPHASE			0x8000

/************************* McBSP SRGR1 bit definitions ************************/
#define CLKGDV(value)		(value)		/* Bits 0:7 */
#define FWID(value)		((value)<<8)	/* Bits 8:15 */

/************************* McBSP SRGR2 bit definitions ************************/
#define FPER(value)		(value)		/* Bits 0:11 */
#define FSGM			0x1000
#define CLKSM			0x2000
#define CLKSP			0x4000
#define GSYNC			0x8000

/************************* McBSP MCR1 bit definitions *************************/
#define RMCM			0x0001
#define RCBLK(value)		((value)<<2)	/* Bits 2:4 */
#define RPABLK(value)		((value)<<5)	/* Bits 5:6 */
#define RPBBLK(value)		((value)<<7)	/* Bits 7:8 */

/************************* McBSP MCR2 bit definitions *************************/
#define XMCM(value)		(value)		/* Bits 0:1 */
#define XCBLK(value)		((value)<<2)	/* Bits 2:4 */
#define XPABLK(value)		((value)<<5)	/* Bits 5:6 */
#define XPBBLK(value)		((value)<<7)	/* Bits 7:8 */


/* we don't do multichannel for now */
struct omap_mcbsp_reg_cfg {
	u16 spcr2;
	u16 spcr1;
	u16 rcr2;
	u16 rcr1;
	u16 xcr2;
	u16 xcr1;
	u16 srgr2;
	u16 srgr1;
	u16 mcr2;
	u16 mcr1;
	u16 pcr0;
	u16 rcerc;
	u16 rcerd;
	u16 xcerc;
	u16 xcerd;
	u16 rcere;
	u16 rcerf;
	u16 xcere;
	u16 xcerf;
	u16 rcerg;
	u16 rcerh;
	u16 xcerg;
	u16 xcerh;
};

typedef enum {
	OMAP_MCBSP1 = 0,
	OMAP_MCBSP2,
	OMAP_MCBSP3,
} omap_mcbsp_id;

typedef enum {
	OMAP_MCBSP_WORD_8 = 0,
	OMAP_MCBSP_WORD_12,
	OMAP_MCBSP_WORD_16,
	OMAP_MCBSP_WORD_20,
	OMAP_MCBSP_WORD_24,
	OMAP_MCBSP_WORD_32,
} omap_mcbsp_word_length;

typedef enum {
	OMAP_MCBSP_CLK_RISING = 0,
	OMAP_MCBSP_CLK_FALLING,
} omap_mcbsp_clk_polarity;

typedef enum {
	OMAP_MCBSP_FS_ACTIVE_HIGH = 0,
	OMAP_MCBSP_FS_ACTIVE_LOW,
} omap_mcbsp_fs_polarity;

typedef enum {
	OMAP_MCBSP_CLK_STP_MODE_NO_DELAY = 0,
	OMAP_MCBSP_CLK_STP_MODE_DELAY,
} omap_mcbsp_clk_stp_mode;


/******* SPI specific mode **********/
typedef enum {
	OMAP_MCBSP_SPI_MASTER = 0,
	OMAP_MCBSP_SPI_SLAVE,
} omap_mcbsp_spi_mode;

struct omap_mcbsp_spi_cfg {
	omap_mcbsp_spi_mode		spi_mode;
	omap_mcbsp_clk_polarity		rx_clock_polarity;
	omap_mcbsp_clk_polarity		tx_clock_polarity;
	omap_mcbsp_fs_polarity		fsx_polarity;
	u8				clk_div;
	omap_mcbsp_clk_stp_mode		clk_stp_mode;
	omap_mcbsp_word_length		word_length;
};

void omap_mcbsp_config(unsigned int id, const struct omap_mcbsp_reg_cfg * config);
int omap_mcbsp_request(unsigned int id);
void omap_mcbsp_free(unsigned int id);
void omap_mcbsp_start(unsigned int id);
void omap_mcbsp_stop(unsigned int id);
void omap_mcbsp_xmit_word(unsigned int id, u32 word);
u32 omap_mcbsp_recv_word(unsigned int id);

int omap_mcbsp_xmit_buffer(unsigned int id, dma_addr_t buffer, unsigned int length);
int omap_mcbsp_recv_buffer(unsigned int id, dma_addr_t buffer, unsigned int length);

/* SPI specific API */
void omap_mcbsp_set_spi_mode(unsigned int id, const struct omap_mcbsp_spi_cfg * spi_cfg);

/* Polled read/write functions */
int omap_mcbsp_pollread(unsigned int id, u16 * buf);
int omap_mcbsp_pollwrite(unsigned int id, u16 buf);

#endif
