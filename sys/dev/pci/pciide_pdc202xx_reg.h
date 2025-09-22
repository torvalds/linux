/*	$OpenBSD: pciide_pdc202xx_reg.h,v 1.17 2024/09/01 03:09:00 jsg Exp $	*/
/*	$NetBSD: pciide_pdc202xx_reg.h,v 1.5 2001/07/05 08:38:27 toshii Exp $ */

/*
 * Copyright (c) 1999 Manuel Bouyer.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef _DEV_PCI_PCIIDE_PDC202XX_REG_H_
#define _DEV_PCI_PCIIDE_PDC202XX_REG_H_

/*
 * Registers definitions for PROMISE PDC20246/PDC20262 PCI IDE controller.
 * Unfortunately the HW docs are not publicly available. I've been able
 * to get a partial one for the PDC20246, and a better one for the PDC20262
 * from Promise.
 */

#define PDC2xx_STATE		0x50
#define PDC2xx_STATE_IDERAID		0x0001
#define PDC2xx_STATE_NATIVE		0x0080
/* controller initial state values(PDC20246 only) */
#define PDC246_STATE_SHIPID		0x8000
#define PDC246_STATE_IOCHRDY		0x0400
#define PDC246_STATE_LBA(channel)	(0x0100 << (channel))
#define PDC246_STATE_ISAIRQ		0x0008
#define PDC246_STATE_EN(channel)	(0x0002 << (channel))
/* controller initial state values(PDC20262 only) */
#define PDC262_STATE_EN(chan)		(0x1000 << (chan))
#define PDC262_STATE_80P(chan)		(0x0400 << (chan))

/* per-drive timings */
#define PDC2xx_TIM(channel, drive) (0x60 + 4 * (drive) + 8 * (channel))
#define PDC2xx_TIM_SET_PA(r, x)	(((r) & 0xfffffff0) | ((x) & 0xf))
#define PDC2xx_TIM_SET_PB(r, x)	(((r) & 0xffffe0ff) | (((x) & 0x1f) << 8))
#define PDC2xx_TIM_SET_MB(r, x)	(((r) & 0xffff1fff) | (((x) & 0x7) << 13))
#define PDC2xx_TIM_SET_MC(r, x)	(((r) & 0xfff0ffff) | (((x) & 0xf) << 16))
#define PDC2xx_TIM_PRE		0x00000010
#define PDC2xx_TIM_IORDY	0x00000020
#define PDC2xx_TIM_ERRDY	0x00000040
#define PDC2xx_TIM_SYNC		0x00000080
#define PDC2xx_TIM_DMAW		0x00100000
#define PDC2xx_TIM_DMAR		0x00200000
#define PDC2xx_TIM_IORDYp	0x00400000
#define PDC2xx_TIM_DMARQp	0x00800000

/* The following are extensions of the DMA registers */

/* Ultra-DMA mode 3/4 control (PDC20262 only, 1 byte) */
#define PDC262_U66	0x11
#define PDC262_U66_EN(chan) (0x2 << ((chan) *2))
/* primary mode (1 byte) */
#define PDC2xx_PM	0x1a
/* secondary mode (1 byte) */
#define PDC2xx_SM	0x1b
/* System control register (4 bytes) */
#define PDC2xx_SCR	0x1c
#define PDC2xx_SCR_SET_GEN(r,x) (((r) & 0xffffff00) | ((x) & 0xff))
#define PDC2xx_SCR_EMPTY(channel) (0x00000100 << (4 * channel))
#define PDC2xx_SCR_FULL(channel) (0x00000200 << (4 * channel))
#define PDC2xx_SCR_INT(channel) (0x00000400 << (4 * channel))
#define PDC2xx_SCR_ERR(channel) (0x00000800 << (4 * channel))
#define PDC2xx_SCR_SET_I2C(r,x) (((r) & 0xfff0ffff) | (((x) & 0xf) << 16))
#define PDC2xx_SCR_SET_POLL(r,x) (((r) & 0xff0fffff) | (((x) & 0xf) << 20))
#define PDC2xx_SCR_DMA		0x01000000
#define PDC2xx_SCR_IORDY	0x02000000
#define PDC2xx_SCR_G2FD		0x04000000
#define PDC2xx_SCR_FLOAT	0x08000000
#define PDC2xx_SCR_RSET		0x10000000
#define PDC2xx_SCR_TST		0x20000000
/* Values for "General Purpose Register" (PDC20262 only) */
#define PDC262_SCR_GEN_LAT	0x20

/* ATAPI port ((PDC20262 only) (4 bytes) */
#define PDC262_ATAPI(chan) (0x20 + (4 * (chan)))
#define PDC262_ATAPI_WC_MASK	0x00000fff
#define PDC262_ATAPI_DMA_READ	0x00001000
#define PDC262_ATAPI_DMA_WRITE	0x00002000
#define PDC262_ATAPI_UDMA	0x00004000
#define PDC262_ATAPI_LBA48_READ  0x05000000
#define PDC262_ATAPI_LBA48_WRITE 0x06000000

/*
 * The timings provided here comes from the PDC20262 docs. I hope they are
 * right for the PDC20246 too ...
 */

static int8_t pdc2xx_pa[] = {0x9, 0x5, 0x3, 0x2, 0x1};
static int8_t pdc2xx_pb[] = {0x13, 0xc, 0x8, 0x6, 0x4};
static int8_t pdc2xx_dma_mb[] = {0x3, 0x3, 0x3};
static int8_t pdc2xx_dma_mc[] = {0x5, 0x4, 0x3};
static int8_t pdc2xx_udma_mb[] = {0x3, 0x2, 0x1, 0x2, 0x1, 0x1};
static int8_t pdc2xx_udma_mc[] = {0x3, 0x2, 0x1, 0x2, 0x1, 0x1};

/*
 * Registers definitions for Promise PDC20268 and above chips
 */
#define PDC268_INDEX(chan)	(0x01 + IDEDMA_SCH_OFFSET * (chan))
#define PDC268_DATA(chan)	(0x03 + IDEDMA_SCH_OFFSET * (chan))
#define PDC268_CABLE		0x04
#define PDC268_INTR		0x20

/*
 * PDC203xx register definitions.
 */
#define PDC203xx_NCHANNELS	4
#define PDC203xx_BAR_IDEREGS	0x1c

/*
 * PDC205xx register definitions.
 */
#define PDC40718_NCHANNELS	4
#define PDC20575_NCHANNELS	3

#define	PDC205_REGADDR(base,ch)	((base)+((ch)<<8))
#define	PDC205_SSTATUS(ch)	PDC205_REGADDR(0x400,ch)
#define	PDC205_SERROR(ch)	PDC205_REGADDR(0x404,ch)
#define	PDC205_SCONTROL(ch)	PDC205_REGADDR(0x408,ch)
#define	PDC205_MULTIPLIER(ch)	PDC205_REGADDR(0x4e8,ch)

#define	SCONTROL_WRITE(ps,channel,scontrol)	\
	bus_space_write_4((ps)->ba5_st, (ps)->ba5_sh,	\
	PDC205_SCONTROL(channel), scontrol)

#define	SSTATUS_READ(sc,channel)	\
	bus_space_read_4((ps)->ba5_st, (ps)->ba5_sh,	\
	PDC205_SSTATUS(channel))


/* Private data */
struct pciide_pdcsata {
	bus_space_tag_t			ba5_st;
	bus_space_handle_t		ba5_sh;

	struct {
		bus_space_tag_t		cmd_iot;
		bus_space_handle_t	cmd_iohs[WDC_NREG+WDC_NSHADOWREG];

		bus_space_tag_t		ctl_iot;
		bus_space_handle_t	ctl_ioh;

		bus_space_handle_t	dma_iohs[IDEDMA_NREGS];
	} regs[PDC203xx_NCHANNELS];
};

u_int8_t pdc203xx_read_reg(struct channel_softc *, enum wdc_regs);
void     pdc203xx_write_reg(struct channel_softc *, enum wdc_regs, u_int8_t);

struct channel_softc_vtbl wdc_pdc203xx_vtbl = {
	pdc203xx_read_reg,
	pdc203xx_write_reg,
	wdc_default_lba48_write_reg,
	wdc_default_read_raw_multi_2,
	wdc_default_write_raw_multi_2,
	wdc_default_read_raw_multi_4,
	wdc_default_write_raw_multi_4
};

#endif	/* !_DEV_PCI_PCIIDE_PDC202XX_REG_H_ */
