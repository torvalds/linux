/*	$OpenBSD: pciide_sii3112_reg.h,v 1.6 2008/02/05 20:22:22 blambert Exp $	*/
/*	$NetBSD: pciide_sii3112_reg.h,v 1.1 2003/03/20 04:22:50 thorpej Exp $	*/

/*
 * Copyright (c) 2003 Wasabi Systems, Inc.
 * All rights reserved.
 *
 * Written by Jason R. Thorpe for Wasabi Systems, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed for the NetBSD Project by
 *	Wasabi Systems, Inc.
 * 4. The name of Wasabi Systems, Inc. may not be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY WASABI SYSTEMS, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL WASABI SYSTEMS, INC
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _DEV_PCI_PCIIDE_SII3112_REG_H_
#define	_DEV_PCI_PCIIDE_SII3112_REG_H_

/*
 * PCI configuration space registers.
 */

#define	SII3112_PCI_CFGCTL	0x40
#define	CFGCTL_CFGWREN		(1U << 0)	/* enable cfg writes */
#define	CFGCTL_BA5INDEN		(1U << 1)	/* BA5 indirect access enable */

#define	SII3112_PCI_SWDATA	0x44

#define	SII3112_PCI_BM_IDE0	0x70
	/* == BAR4+0x00 */

#define	SII3112_PCI_PRD_IDE0	0x74
	/* == BAR4+0x04 */

#define	SII3112_PCI_BM_IDE1	0x78
	/* == BAR4+0x08 */

#define	SII3112_PCI_PRD_IDE1	0x7c
	/* == BAR4+0x0c */

#define	SII3112_DTM_IDE0	0x80	/* Data Transfer Mode - IDE0 */
#define	SII3112_DTM_IDE1	0x84	/* Data Transfer Mode - IDE1 */
#define	DTM_IDEx_PIO		0x00000000	/* PCI DMA, IDE PIO (or 1) */
#define	DTM_IDEx_DMA		0x00000002	/* PCI DMA, IDE DMA (or 3) */


#define	SII3112_SCS_CMD		0x88	/* System Config Status */
#define	SCS_CMD_PBM_RESET	(1U << 0)	/* PBM module reset */
#define	SCS_CMD_ARB_RESET	(1U << 1)	/* ARB module reset */
#define	SCS_CMD_FF1_RESET	(1U << 4)	/* IDE1 FIFO reset */
#define	SCS_CMD_FF0_RESET	(1U << 5)	/* IDE0 FIFO reset */
#define	SCS_CMD_IDE1_RESET	(1U << 6)	/* IDE1 module reset */
#define	SCS_CMD_IDE0_RESET	(1U << 7)	/* IDE0 module reset */
#define	SCS_CMD_FF3_RESET	(1U << 8)	/* IDE3 FIFO reset (3114) */
#define	SCS_CMD_FF2_RESET	(1U << 9)	/* IDE2 FIFO reset (3114) */
#define	SCS_CMD_IDE3_RESET	(1U << 10)	/* IDE3 module reset (3114) */
#define	SCS_CMD_IDE2_RESET	(1U << 11)	/* IDE2 module reset (3114) */
#define	SCS_CMD_BA5_EN		(1U << 16)	/* BA5 is enabled (3112) */
#define	SCS_CMD_M66EN		(1U << 16)	/* 1=66MHz, 0=33MHz (3114) */
#define	SCS_CMD_IDE0_INT_BLOCK	(1U << 22)	/* IDE0 interrupt block */
#define	SCS_CMD_IDE1_INT_BLOCK	(1U << 23)	/* IDE1 interrupt block */
#define	SCS_CMD_IDE2_INT_BLOCK	(1U << 24)	/* IDE2 interrupt block */
#define	SCS_CMD_IDE3_INT_BLOCK	(1U << 25)	/* IDE3 interrupt block */

#define	SII3112_SSDR		0x8c	/* System SW Data Register */

#define	SII3112_FMA_CSR		0x90	/* Flash Memory Addr - CSR */

#define	SII3112_FM_DATA		0x94	/* Flash Memory Data */

#define	SII3112_EEA_CSR		0x98	/* EEPROM Memory Addr - CSR */

#define	SII3112_EE_DATA		0x9c	/* EEPROM Data */

#define	SII3112_TCS_IDE0	0xa0	/* IDEx config, status */
#define	SII3112_TCS_IDE1	0xb0
#define	TCS_IDEx_BCA		(1U << 1)	/* buffered command active */
#define	TCS_IDEx_CH_RESET	(1U << 2)	/* channel reset */
#define	TCS_IDEx_VDMA_INT	(1U << 10)	/* virtual DMA interrupt */
#define	TCS_IDEx_INT		(1U << 11)	/* interrupt status */
#define	TCS_IDEx_WTT		(1U << 12)	/* watchdog timer timeout */
#define	TCS_IDEx_WTEN		(1U << 13)	/* watchdog timer enable */
#define	TCS_IDEx_WTINTEN	(1U << 14)	/* watchdog timer int. enable */

#define	SII3112_BA5_IND_ADDR	0xc0	/* BA5 indirect address */

#define	SII3112_BA5_IND_DATA	0xc4	/* BA5 indirect data */

/*
 * Register map for BA5 register space, indexed by channel.
 */
static const struct {
	bus_addr_t	ba5_IDEDMA_CMD;
	bus_addr_t	ba5_IDEDMA_CTL;
	bus_addr_t	ba5_IDEDMA_TBL;
	bus_addr_t	ba5_IDEDMA_CMD2;
	bus_addr_t	ba5_IDEDMA_CTL2;
	bus_addr_t	ba5_IDE_TF0;
	bus_addr_t	ba5_IDE_TF1;
	bus_addr_t	ba5_IDE_TF2;
	bus_addr_t	ba5_IDE_TF3;
	bus_addr_t	ba5_IDE_TF4;
	bus_addr_t	ba5_IDE_TF5;
	bus_addr_t	ba5_IDE_TF6;
	bus_addr_t	ba5_IDE_TF7;
	bus_addr_t	ba5_IDE_TF8;
	bus_addr_t	ba5_IDE_RAD;
	bus_addr_t	ba5_IDE_TF9;
	bus_addr_t	ba5_IDE_TF10;
	bus_addr_t	ba5_IDE_TF11;
	bus_addr_t	ba5_IDE_TF12;
	bus_addr_t	ba5_IDE_TF13;
	bus_addr_t	ba5_IDE_TF14;
	bus_addr_t	ba5_IDE_TF15;
	bus_addr_t	ba5_IDE_TF16;
	bus_addr_t	ba5_IDE_TF17;
	bus_addr_t	ba5_IDE_TF18;
	bus_addr_t	ba5_IDE_TF19;
	bus_addr_t	ba5_IDE_RABC;
	bus_addr_t	ba5_IDE_CMD_STS;
	bus_addr_t	ba5_IDE_CFG_STS;
	bus_addr_t	ba5_IDE_DTM;
	bus_addr_t	ba5_SControl;
	bus_addr_t	ba5_SStatus;
	bus_addr_t	ba5_SError;
	bus_addr_t	ba5_SActive;		/* 3114 */
	bus_addr_t	ba5_SMisc;
	bus_addr_t	ba5_PHY_CONFIG;
	bus_addr_t	ba5_SIEN;
	bus_addr_t	ba5_SFISCfg;
} satalink_ba5_regmap[] = {
	{	/* Channel 0 */
		.ba5_IDEDMA_CMD		=	0x000,
		.ba5_IDEDMA_CTL		=	0x002,
		.ba5_IDEDMA_TBL		=	0x004,
		.ba5_IDEDMA_CMD2	=	0x010,
		.ba5_IDEDMA_CTL2	=	0x012,
		.ba5_IDE_TF0		=	0x080,	/* wd_data */
		.ba5_IDE_TF1		=	0x081,	/* wd_error */
		.ba5_IDE_TF2		=	0x082,	/* wd_seccnt */
		.ba5_IDE_TF3		=	0x083,	/* wd_sector */
		.ba5_IDE_TF4		=	0x084,	/* wd_cyl_lo */
		.ba5_IDE_TF5		=	0x085,	/* wd_cyl_hi */
		.ba5_IDE_TF6		=	0x086,	/* wd_sdh */
		.ba5_IDE_TF7		=	0x087,	/* wd_command */
		.ba5_IDE_TF8		=	0x08a,	/* wd_altsts */
		.ba5_IDE_RAD		=	0x08c,
		.ba5_IDE_TF9		=	0x091,	/* Features 2 */
		.ba5_IDE_TF10		=	0x092,	/* Sector Count 2 */
		.ba5_IDE_TF11		=	0x093,	/* Start Sector 2 */
		.ba5_IDE_TF12		=	0x094,	/* Cylinder Low 2 */
		.ba5_IDE_TF13		=	0x095,	/* Cylinder High 2 */
		.ba5_IDE_TF14		=	0x096,	/* Device/Head 2 */
		.ba5_IDE_TF15		=	0x097,	/* Cmd Sts 2 */
		.ba5_IDE_TF16		=	0x098,	/* Sector Count 2 ext */
		.ba5_IDE_TF17		=	0x099,	/* Start Sector 2 ext */
		.ba5_IDE_TF18		=	0x09a,	/* Cyl Low 2 ext */
		.ba5_IDE_TF19		=	0x09b,	/* Cyl High 2 ext */
		.ba5_IDE_RABC		=	0x09c,
		.ba5_IDE_CMD_STS	=	0x0a0,
		.ba5_IDE_CFG_STS	=	0x0a1,
		.ba5_IDE_DTM		=	0x0b4,
		.ba5_SControl		=	0x100,
		.ba5_SStatus		=	0x104,
		.ba5_SError		=	0x108,
		.ba5_SActive		=	0x10c,
		.ba5_SMisc		=	0x140,
		.ba5_PHY_CONFIG		=	0x144,
		.ba5_SIEN		=	0x148,
		.ba5_SFISCfg		=	0x14c,
	},
	{	/* Channel 1 */
		.ba5_IDEDMA_CMD		=	0x008,
		.ba5_IDEDMA_CTL		=	0x00a,
		.ba5_IDEDMA_TBL		=	0x00c,
		.ba5_IDEDMA_CMD2	=	0x018,
		.ba5_IDEDMA_CTL2	=	0x01a,
		.ba5_IDE_TF0		=	0x0c0,	/* wd_data */
		.ba5_IDE_TF1		=	0x0c1,	/* wd_error */
		.ba5_IDE_TF2		=	0x0c2,	/* wd_seccnt */
		.ba5_IDE_TF3		=	0x0c3,	/* wd_sector */
		.ba5_IDE_TF4		=	0x0c4,	/* wd_cyl_lo */
		.ba5_IDE_TF5		=	0x0c5,	/* wd_cyl_hi */
		.ba5_IDE_TF6		=	0x0c6,	/* wd_sdh */
		.ba5_IDE_TF7		=	0x0c7,	/* wd_command */
		.ba5_IDE_TF8		=	0x0ca,	/* wd_altsts */
		.ba5_IDE_RAD		=	0x0cc,
		.ba5_IDE_TF9		=	0x0d1,	/* Features 2 */
		.ba5_IDE_TF10		=	0x0d2,	/* Sector Count 2 */
		.ba5_IDE_TF11		=	0x0d3,	/* Start Sector 2 */
		.ba5_IDE_TF12		=	0x0d4,	/* Cylinder Low 2 */
		.ba5_IDE_TF13		=	0x0d5,	/* Cylinder High 2 */
		.ba5_IDE_TF14		=	0x0d6,	/* Device/Head 2 */
		.ba5_IDE_TF15		=	0x0d7,	/* Cmd Sts 2 */
		.ba5_IDE_TF16		=	0x0d8,	/* Sector Count 2 ext */
		.ba5_IDE_TF17		=	0x0d9,	/* Start Sector 2 ext */
		.ba5_IDE_TF18		=	0x0da,	/* Cyl Low 2 ext */
		.ba5_IDE_TF19		=	0x0db,	/* Cyl High 2 ext */
		.ba5_IDE_RABC		=	0x0dc,
		.ba5_IDE_CMD_STS	=	0x0e0,
		.ba5_IDE_CFG_STS	=	0x0e1,
		.ba5_IDE_DTM		=	0x0f4,
		.ba5_SControl		=	0x180,
		.ba5_SStatus		=	0x184,
		.ba5_SError		=	0x188,
		.ba5_SActive		=	0x18c,
		.ba5_SMisc		=	0x1c0,
		.ba5_PHY_CONFIG		=	0x1c4,
		.ba5_SIEN		=	0x1c8,
		.ba5_SFISCfg		=	0x1cc,
	},
	{	/* Channel 2 (3114) */
		.ba5_IDEDMA_CMD		=	0x200,
		.ba5_IDEDMA_CTL		=	0x202,
		.ba5_IDEDMA_TBL		=	0x204,
		.ba5_IDEDMA_CMD2	=	0x210,
		.ba5_IDEDMA_CTL2	=	0x212,
		.ba5_IDE_TF0		=	0x280,	/* wd_data */
		.ba5_IDE_TF1		=	0x281,	/* wd_error */
		.ba5_IDE_TF2		=	0x282,	/* wd_seccnt */
		.ba5_IDE_TF3		=	0x283,	/* wd_sector */
		.ba5_IDE_TF4		=	0x284,	/* wd_cyl_lo */
		.ba5_IDE_TF5		=	0x285,	/* wd_cyl_hi */
		.ba5_IDE_TF6		=	0x286,	/* wd_sdh */
		.ba5_IDE_TF7		=	0x287,	/* wd_command */
		.ba5_IDE_TF8		=	0x28a,	/* wd_altsts */
		.ba5_IDE_RAD		=	0x28c,
		.ba5_IDE_TF9		=	0x291,	/* Features 2 */
		.ba5_IDE_TF10		=	0x292,	/* Sector Count 2 */
		.ba5_IDE_TF11		=	0x293,	/* Start Sector 2 */
		.ba5_IDE_TF12		=	0x294,	/* Cylinder Low 2 */
		.ba5_IDE_TF13		=	0x295,	/* Cylinder High 2 */
		.ba5_IDE_TF14		=	0x296,	/* Device/Head 2 */
		.ba5_IDE_TF15		=	0x297,	/* Cmd Sts 2 */
		.ba5_IDE_TF16		=	0x298,	/* Sector Count 2 ext */
		.ba5_IDE_TF17		=	0x299,	/* Start Sector 2 ext */
		.ba5_IDE_TF18		=	0x29a,	/* Cyl Low 2 ext */
		.ba5_IDE_TF19		=	0x29b,	/* Cyl High 2 ext */
		.ba5_IDE_RABC		=	0x29c,
		.ba5_IDE_CMD_STS	=	0x2a0,
		.ba5_IDE_CFG_STS	=	0x2a1,
		.ba5_IDE_DTM		=	0x2b4,
		.ba5_SControl		=	0x300,
		.ba5_SStatus		=	0x304,
		.ba5_SError		=	0x308,
		.ba5_SActive		=	0x30c,
		.ba5_SMisc		=	0x340,
		.ba5_PHY_CONFIG		=	0x344,
		.ba5_SIEN		=	0x348,
		.ba5_SFISCfg		=	0x34c,
	},
	{	/* Channel 3 (3114) */
		.ba5_IDEDMA_CMD		=	0x208,
		.ba5_IDEDMA_CTL		=	0x20a,
		.ba5_IDEDMA_TBL		=	0x20c,
		.ba5_IDEDMA_CMD2	=	0x218,
		.ba5_IDEDMA_CTL2	=	0x21a,
		.ba5_IDE_TF0		=	0x2c0,	/* wd_data */
		.ba5_IDE_TF1		=	0x2c1,	/* wd_error */
		.ba5_IDE_TF2		=	0x2c2,	/* wd_seccnt */
		.ba5_IDE_TF3		=	0x2c3,	/* wd_sector */
		.ba5_IDE_TF4		=	0x2c4,	/* wd_cyl_lo */
		.ba5_IDE_TF5		=	0x2c5,	/* wd_cyl_hi */
		.ba5_IDE_TF6		=	0x2c6,	/* wd_sdh */
		.ba5_IDE_TF7		=	0x2c7,	/* wd_command */
		.ba5_IDE_TF8		=	0x2ca,	/* wd_altsts */
		.ba5_IDE_RAD		=	0x2cc,
		.ba5_IDE_TF9		=	0x2d1,	/* Features 2 */
		.ba5_IDE_TF10		=	0x2d2,	/* Sector Count 2 */
		.ba5_IDE_TF11		=	0x2d3,	/* Start Sector 2 */
		.ba5_IDE_TF12		=	0x2d4,	/* Cylinder Low 2 */
		.ba5_IDE_TF13		=	0x2d5,	/* Cylinder High 2 */
		.ba5_IDE_TF14		=	0x2d6,	/* Device/Head 2 */
		.ba5_IDE_TF15		=	0x2d7,	/* Cmd Sts 2 */
		.ba5_IDE_TF16		=	0x2d8,	/* Sector Count 2 ext */
		.ba5_IDE_TF17		=	0x2d9,	/* Start Sector 2 ext */
		.ba5_IDE_TF18		=	0x2da,	/* Cyl Low 2 ext */
		.ba5_IDE_TF19		=	0x2db,	/* Cyl High 2 ext */
		.ba5_IDE_RABC		=	0x2dc,
		.ba5_IDE_CMD_STS	=	0x2e0,
		.ba5_IDE_CFG_STS	=	0x2e1,
		.ba5_IDE_DTM		=	0x2f4,
		.ba5_SControl		=	0x380,
		.ba5_SStatus		=	0x384,
		.ba5_SError		=	0x388,
		.ba5_SActive		=	0x38c,
		.ba5_SMisc		=	0x3c0,
		.ba5_PHY_CONFIG		=	0x3c4,
		.ba5_SIEN		=	0x3c8,
		.ba5_SFISCfg		=	0x3cc,
	},
};

#define	ba5_SIS		0x214		/* summary interrupt status */

/* Interrupt steering bit in BA5[0x200]. */
#define	IDEDMA_CMD_INT_STEER	(1U << 1)

/* Private data */
struct pciide_satalink {
	bus_space_tag_t			ba5_st;
	bus_space_handle_t		ba5_sh;
	int				ba5_en;

	struct {
		bus_space_tag_t		cmd_iot;
		bus_space_handle_t	cmd_baseioh;
		bus_space_handle_t	cmd_iohs[WDC_NREG+WDC_NSHADOWREG];

		bus_space_tag_t		ctl_iot;
		bus_space_handle_t	ctl_ioh;

		bus_space_handle_t	dma_iohs[IDEDMA_NREGS];
	} regs[4];
};

static uint32_t
ba5_read_4_ind(struct pciide_softc *sc, pcireg_t reg)
{
	uint32_t rv;
	int s;

	s = splbio();
	pci_conf_write(sc->sc_pc, sc->sc_tag, SII3112_BA5_IND_ADDR, reg);
	rv = pci_conf_read(sc->sc_pc, sc->sc_tag, SII3112_BA5_IND_DATA);
	splx(s);

	return (rv);
}

static uint32_t
ba5_read_4(struct pciide_softc *sc, bus_size_t reg)
{
	struct pciide_satalink *sl = sc->sc_cookie;

	if (__predict_true(sl->ba5_en != 0))
		return (bus_space_read_4(sl->ba5_st, sl->ba5_sh, reg));

	return (ba5_read_4_ind(sc, reg));
}

#define	BA5_READ_4(sc, chan, reg)					\
	ba5_read_4((sc), satalink_ba5_regmap[(chan)].reg)

static void
ba5_write_4_ind(struct pciide_softc *sc, pcireg_t reg, uint32_t val)
{
	int s;

	s = splbio();
	pci_conf_write(sc->sc_pc, sc->sc_tag, SII3112_BA5_IND_ADDR, reg);
	pci_conf_write(sc->sc_pc, sc->sc_tag, SII3112_BA5_IND_DATA, val);
	splx(s);
}

static void
ba5_write_4(struct pciide_softc *sc, bus_size_t reg, uint32_t val)
{
	struct pciide_satalink *sl = sc->sc_cookie;

	if (__predict_true(sl->ba5_en != 0))
		bus_space_write_4(sl->ba5_st, sl->ba5_sh, reg, val);
	else
		ba5_write_4_ind(sc, reg, val);
}

#define	BA5_WRITE_4(sc, chan, reg, val)					\
	ba5_write_4((sc), satalink_ba5_regmap[(chan)].reg, (val))

u_int8_t sii3114_read_reg(struct channel_softc *, enum wdc_regs);
void     sii3114_write_reg(struct channel_softc *, enum wdc_regs, u_int8_t);

struct channel_softc_vtbl wdc_sii3114_vtbl = {
	sii3114_read_reg,
	sii3114_write_reg,
	wdc_default_lba48_write_reg,
	wdc_default_read_raw_multi_2,
	wdc_default_write_raw_multi_2,
	wdc_default_read_raw_multi_4,
	wdc_default_write_raw_multi_4
};

#endif /* _DEV_PCI_PCIIDE_SII3112_REG_H_ */
