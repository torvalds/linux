/* $OpenBSD: rkiis.c,v 1.4 2022/10/28 15:09:45 kn Exp $ */
/* $NetBSD: rk_i2s.c,v 1.3 2020/02/29 05:51:10 isaki Exp $ */
/*-
 * Copyright (c) 2019 Jared McNeill <jmcneill@invisible.ca>
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/fcntl.h>

#include <machine/intr.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/fdt.h>

#include <sys/audioio.h>
#include <dev/audio_if.h>
#include <dev/midi_if.h>

#define	RK_I2S_FIFO_DEPTH	32
#define	RK_I2S_SAMPLE_RATE	48000

#define	I2S_TXCR		0x00
#define	 TXCR_RCNT_MASK			(0x3f << 17)
#define	 TXCR_RCNT_SHIFT		17
#define	 TXCR_TCSR_MASK			(0x3 << 15)
#define	 TXCR_TCSR_SHIFT		15
#define	 TXCR_HWT			(1 << 14)
#define	 TXCR_SJM			(1 << 12)
#define	 TXCR_FBM			(1 << 11)
#define	 TXCR_IBM_MASK			(0x3 << 9)
#define	 TXCR_IBM_SHIFT			9
#define	 TXCR_PBM_MASK			(0x3 << 7)
#define	 TXCR_PBM_SHIFT			7
#define	 TXCR_TFS			(1 << 5)
#define	 TXCR_VDW_MASK			(0x1f << 0)
#define	 TXCR_VDW_SHIFT			0
#define	I2S_RXCR		0x04
#define	 RXCR_RCSR_MASK			(0x3 << 15)
#define	 RXCR_RCSR_SHIFT		15
#define	 RXCR_HWT			(1 << 14)
#define	 RXCR_SJM			(1 << 12)
#define	 RXCR_FBM			(1 << 11)
#define	 RXCR_IBM_MASK			(0x3 << 9)
#define	 RXCR_IBM_SHIFT			9
#define	 RXCR_PBM_MASK			(0x3 << 7)
#define	 RXCR_PBM_SHIFT			7
#define	 RXCR_TFS			(1 << 5)
#define	 RXCR_VDW_MASK			(0x1f << 0)
#define	 RXCR_VDW_SHIFT			0
#define	I2S_CKR			0x08
#define	 CKR_TRCM_MASK			(0x3 << 28)
#define	 CKR_TRCM_SHIFT			28
#define	 CKR_MSS			(1 << 27)
#define	 CKR_CKP			(1 << 26)
#define	 CKR_RLP			(1 << 25)
#define	 CKR_TLP			(1 << 24)
#define	 CKR_MDIV_MASK			(0xff << 16)
#define	 CKR_MDIV_SHIFT			16
#define	 CKR_RSD_MASK			(0xff << 8)
#define	 CKR_RSD_SHIFT			8
#define	 CKR_TSD_MASK			(0xff << 0)
#define	 CKR_TSD_SHIFT			0
#define	I2S_TXFIFOLR		0x0c
#define	 TXFIFOLR_TFL_MASK(n)		(0x3f << ((n) * 6))
#define	 TXFIFOLR_TFL_SHIFT(n)		((n) * 6)
#define	I2S_DMACR		0x10
#define	 DMACR_RDE			(1 << 24)
#define	 DMACR_RDL_MASK			(0x1f << 16)
#define	 DMACR_RDL_SHIFT		16
#define	 DMACR_TDE			(1 << 8)
#define	 DMACR_TDL_MASK			(0x1f << 0)
#define	 DMACR_TDL_SHIFT		0
#define	I2S_INTCR		0x14
#define	 INTCR_RFT_MASK			(0x1f << 20)
#define	 INTCR_RFT_SHIFT		20
#define	 INTCR_RXOIC			(1 << 18)
#define	 INTCR_RXOIE			(1 << 17)
#define	 INTCR_RXFIE			(1 << 16)
#define	 INTCR_TFT_MASK			(0x1f << 4)
#define	 INTCR_TFT_SHIFT		4
#define	 INTCR_TXUIC			(1 << 2)
#define	 INTCR_TXUIE			(1 << 1)
#define	 INTCR_TXEIE			(1 << 0)
#define	I2S_INTSR		0x18
#define	 INTSR_RXOI			(1 << 17)
#define	 INTSR_RXFI			(1 << 16)
#define	 INTSR_TXUI			(1 << 1)
#define	 INTSR_TXEI			(1 << 0)
#define	I2S_XFER		0x1c
#define	 XFER_RXS			(1 << 1)
#define	 XFER_TXS			(1 << 0)
#define	I2S_CLR			0x20
#define	 CLR_RXC			(1 << 1)
#define	 CLR_TXC			(1 << 0)
#define	I2S_TXDR		0x24
#define	I2S_RXDR		0x28
#define	I2S_RXFIFOLR		0x2c
#define	 RXFIFOLR_RFL_MASK(n)		(0x3f << ((n) * 6))
#define	 RXFIFOLR_RFL_SHIFT(n)		((n) * 6)

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

int rkiis_match(struct device *, void *, void *);
void rkiis_attach(struct device *, struct device *, void *);

int rkiis_intr(void *);
int rkiis_set_format(void *, uint32_t, uint32_t, uint32_t);
int rkiis_set_sysclk(void *, uint32_t);

int rkiis_open(void *, int);
int rkiis_set_params(void *, int, int,
    struct audio_params *, struct audio_params *);
void *rkiis_allocm(void *, int, size_t, int, int);
void rkiis_freem(void *, void *, int);
int rkiis_trigger_output(void *, void *, void *, int,
    void (*)(void *), void *, struct audio_params *);
int rkiis_trigger_input(void *, void *, void *, int,
    void (*)(void *), void *, struct audio_params *);
int rkiis_halt_output(void *);
int rkiis_halt_input(void *);

struct rkiis_config {
	bus_size_t		oe_reg;
	uint32_t		oe_mask;
	uint32_t		oe_shift;
	uint32_t		oe_val;
};

struct rkiis_config rk3399_i2s_config = {
	.oe_reg = 0xe220,
	.oe_mask = 0x7,
	.oe_shift = 11,
	.oe_val = 0x7,
};

struct rkiis_chan {
	uint32_t		*ch_start;
	uint32_t		*ch_end;
	uint32_t		*ch_cur;

	int			ch_blksize;
	int			ch_resid;

	void			(*ch_intr)(void *);
	void			*ch_intrarg;
};

struct rkiis_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	void			*sc_ih;

	int			sc_node;
	struct rkiis_config	*sc_conf;

	struct rkiis_chan	sc_pchan;
	struct rkiis_chan	sc_rchan;

	uint32_t		sc_active;

	struct dai_device	sc_dai;
};

const struct audio_hw_if rkiis_hw_if = {
	.open = rkiis_open,
	.set_params = rkiis_set_params,
	.allocm = rkiis_allocm,
	.freem = rkiis_freem,
	.trigger_output = rkiis_trigger_output,
	.trigger_input = rkiis_trigger_input,
	.halt_output = rkiis_halt_output,
	.halt_input = rkiis_halt_input,
};

const struct cfattach rkiis_ca = {
	sizeof (struct rkiis_softc), rkiis_match, rkiis_attach
};

struct cfdriver rkiis_cd = {
	NULL, "rkiis", DV_DULL
};

int
rkiis_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "rockchip,rk3399-i2s");
}

void
rkiis_attach(struct device *parent, struct device *self, void *aux)
{
	struct rkiis_softc *sc = (struct rkiis_softc *)self;
	struct fdt_attach_args *faa = aux;
	struct regmap *rm;
	uint32_t grf, val;

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	sc->sc_iot = faa->fa_iot;
	sc->sc_node = faa->fa_node;
	sc->sc_conf = &rk3399_i2s_config;

	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	pinctrl_byname(sc->sc_node, "default");
	clock_enable_all(sc->sc_node);

	grf = OF_getpropint(sc->sc_node, "rockchip,grf", 0);
	rm = regmap_byphandle(grf);
	if (rm && sc->sc_conf->oe_mask) {
		val = sc->sc_conf->oe_val << sc->sc_conf->oe_shift;
		val |= (sc->sc_conf->oe_mask << sc->sc_conf->oe_shift) << 16;
		regmap_write_4(rm, sc->sc_conf->oe_reg, val);
	}

	sc->sc_ih = fdt_intr_establish(faa->fa_node, IPL_AUDIO | IPL_MPSAFE,
	    rkiis_intr, sc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't establish interrupt\n");
		goto unmap;
	}

	printf("\n");

	sc->sc_dai.dd_node = faa->fa_node;
	sc->sc_dai.dd_cookie = sc;
	sc->sc_dai.dd_hw_if = &rkiis_hw_if;
	sc->sc_dai.dd_set_format = rkiis_set_format;
	sc->sc_dai.dd_set_sysclk = rkiis_set_sysclk;
	dai_register(&sc->sc_dai);
	return;

unmap:
	bus_space_unmap(sc->sc_iot, sc->sc_ioh, faa->fa_reg[0].size);
}

int
rkiis_intr(void *cookie)
{
	struct rkiis_softc *sc = cookie;
	struct rkiis_chan *pch = &sc->sc_pchan;
#if notyet
	struct rkiis_chan *rch = &sc->sc_rchan;
#endif
	uint32_t sr, val;
	int fifolr;

	mtx_enter(&audio_lock);

	sr = HREAD4(sc, I2S_INTSR);

	if ((sr & INTSR_RXFI) != 0) {
#if notyet
		val = HREAD4(sc, I2S_RXFIFOLR);
		fifolr = val & RXFIFOLR_RFL_MASK(0);
		fifolr >>= RXFIFOLR_RFL_SHIFT(0);
		while (fifolr > 0) {
			*rch->ch_data = HREAD4(sc, I2S_RXDR);
			rch->ch_data++;
			rch->ch_resid -= 4;
			if (rch->ch_resid == 0)
				rch->ch_intr(rch->ch_intrarg);
			--fifolr;
		}
#endif
	}

	if ((sr & INTSR_TXEI) != 0) {
		val = HREAD4(sc, I2S_TXFIFOLR);
		fifolr = val & TXFIFOLR_TFL_MASK(0);
		fifolr >>= TXFIFOLR_TFL_SHIFT(0);
		fifolr = min(fifolr, RK_I2S_FIFO_DEPTH);
		while (fifolr < RK_I2S_FIFO_DEPTH - 1) {
			HWRITE4(sc, I2S_TXDR, *pch->ch_cur);
			pch->ch_cur++;
			if (pch->ch_cur == pch->ch_end)
				pch->ch_cur = pch->ch_start;
			pch->ch_resid -= 4;
			if (pch->ch_resid == 0) {
				pch->ch_intr(pch->ch_intrarg);
				pch->ch_resid = pch->ch_blksize;
			}
			++fifolr;
		}
	}

	mtx_leave(&audio_lock);

	return 1;
}

int
rkiis_set_format(void *cookie, uint32_t fmt, uint32_t pol,
    uint32_t clk)
{
	struct rkiis_softc *sc = cookie;
	uint32_t txcr, rxcr, ckr;

	txcr = HREAD4(sc, I2S_TXCR);
	rxcr = HREAD4(sc, I2S_RXCR);
	ckr = HREAD4(sc, I2S_CKR);

	txcr &= ~(TXCR_IBM_MASK|TXCR_PBM_MASK|TXCR_TFS);
	rxcr &= ~(RXCR_IBM_MASK|RXCR_PBM_MASK|RXCR_TFS);
	switch (fmt) {
	case DAI_FORMAT_I2S:
		txcr |= 0 << TXCR_IBM_SHIFT;
		rxcr |= 0 << RXCR_IBM_SHIFT;
		break;
	case DAI_FORMAT_LJ:
		txcr |= 1 << TXCR_IBM_SHIFT;
		rxcr |= 1 << RXCR_IBM_SHIFT;
		break;
	case DAI_FORMAT_RJ:
		txcr |= 2 << TXCR_IBM_SHIFT;
		rxcr |= 2 << RXCR_IBM_SHIFT;
		break;
	case DAI_FORMAT_DSPA:
		txcr |= 0 << TXCR_PBM_SHIFT;
		txcr |= TXCR_TFS;
		rxcr |= 0 << RXCR_PBM_SHIFT;
		txcr |= RXCR_TFS;
		break;
	case DAI_FORMAT_DSPB:
		txcr |= 1 << TXCR_PBM_SHIFT;
		txcr |= TXCR_TFS;
		rxcr |= 1 << RXCR_PBM_SHIFT;
		txcr |= RXCR_TFS;
		break;
	default:
		return EINVAL;
	}

	HWRITE4(sc, I2S_TXCR, txcr);
	HWRITE4(sc, I2S_RXCR, rxcr);

	switch (pol) {
	case DAI_POLARITY_IB|DAI_POLARITY_NF:
		ckr |= CKR_CKP;
		break;
	case DAI_POLARITY_NB|DAI_POLARITY_NF:
		ckr &= ~CKR_CKP;
		break;
	default:
		return EINVAL;
	}

	switch (clk) {
	case DAI_CLOCK_CBM|DAI_CLOCK_CFM:
		ckr |= CKR_MSS;		/* sclk input */
		break;
	case DAI_CLOCK_CBS|DAI_CLOCK_CFS:
		ckr &= ~CKR_MSS;	/* sclk output */
		break;
	default:
		return EINVAL;
	}

	HWRITE4(sc, I2S_CKR, ckr);

	return 0;
}

int
rkiis_set_sysclk(void *cookie, uint32_t rate)
{
	struct rkiis_softc *sc = cookie;
	int error;

	error = clock_set_frequency(sc->sc_node, "i2s_clk", rate);
	if (error != 0) {
		printf("%s: can't set sysclk to %u Hz\n",
		    sc->sc_dev.dv_xname, rate);
		return error;
	}

	return 0;
}

int
rkiis_open(void *cookie, int flags)
{
	if ((flags & (FWRITE | FREAD)) == (FWRITE | FREAD))
		return ENXIO;

	return 0;
}

int
rkiis_set_params(void *cookie, int setmode, int usemode,
    struct audio_params *play, struct audio_params *rec)
{
	struct rkiis_softc *sc = cookie;
	uint32_t mclk_rate, bclk_rate;
	uint32_t bclk_div, lrck_div;
	uint32_t ckr, txcr, rxcr;
	int i;

	ckr = HREAD4(sc, I2S_CKR);
	if ((ckr & CKR_MSS) == 0) {
		mclk_rate = clock_get_frequency(sc->sc_node, "i2s_clk");
		bclk_rate = 2 * 32 * RK_I2S_SAMPLE_RATE;
		bclk_div = mclk_rate / bclk_rate;
		lrck_div = bclk_rate / RK_I2S_SAMPLE_RATE;

		ckr &= ~CKR_MDIV_MASK;
		ckr |= (bclk_div - 1) << CKR_MDIV_SHIFT;
		ckr &= ~CKR_TSD_MASK;
		ckr |= (lrck_div - 1) << CKR_TSD_SHIFT;
		ckr &= ~CKR_RSD_MASK;
		ckr |= (lrck_div - 1) << CKR_RSD_SHIFT;
	}

	ckr &= ~CKR_TRCM_MASK;
	HWRITE4(sc, I2S_CKR, ckr);

	for (i = 0; i < 2; i++) {
		struct audio_params *p;
		int mode;

		switch (i) {
		case 0:
			mode = AUMODE_PLAY;
			p = play;
			break;
		case 1:
			mode = AUMODE_RECORD;
			p = rec;
			break;
		default:
			return EINVAL;
		}

		if (!(setmode & mode))
			continue;

		if (p->channels & 1)
			return EINVAL;

		if (setmode & AUMODE_PLAY) {
			txcr = HREAD4(sc, I2S_TXCR);
			txcr &= ~TXCR_VDW_MASK;
			txcr |= (16 - 1) << TXCR_VDW_SHIFT;
			txcr &= ~TXCR_TCSR_MASK;
			txcr |= (p->channels / 2 - 1) << TXCR_TCSR_SHIFT;
			HWRITE4(sc, I2S_TXCR, txcr);
		} else {
			rxcr = HREAD4(sc, I2S_RXCR);
			rxcr &= ~RXCR_VDW_MASK;
			rxcr |= (16 - 1) << RXCR_VDW_SHIFT;
			rxcr &= ~RXCR_RCSR_MASK;
			rxcr |= (p->channels / 2 - 1) << RXCR_RCSR_SHIFT;
			HWRITE4(sc, I2S_RXCR, rxcr);
		}

		p->encoding = AUDIO_ENCODING_SLINEAR_LE;
		p->precision = 16;
		p->bps = AUDIO_BPS(p->precision);
		p->msb = 1;
		p->sample_rate = RK_I2S_SAMPLE_RATE;
	}

	return 0;
}

void *
rkiis_allocm(void *cookie, int direction, size_t size, int type,
    int flags)
{
	return malloc(size, M_DEVBUF, M_WAITOK | M_ZERO);
}

void
rkiis_freem(void *cookie, void *addr, int size)
{
	free(addr, M_DEVBUF, size);
}

int
rkiis_trigger_output(void *cookie, void *start, void *end, int blksize,
    void (*intr)(void *), void *intrarg, struct audio_params *params)
{
	struct rkiis_softc *sc = cookie;
	struct rkiis_chan *ch = &sc->sc_pchan;
	uint32_t val;

	if (sc->sc_active == 0) {
		val = HREAD4(sc, I2S_XFER);
		val |= (XFER_TXS | XFER_RXS);
		HWRITE4(sc, I2S_XFER, val);
	}

	sc->sc_active |= XFER_TXS;

	val = HREAD4(sc, I2S_INTCR);
	val |= INTCR_TXEIE;
	val &= ~INTCR_TFT_MASK;
	val |= (RK_I2S_FIFO_DEPTH / 2) << INTCR_TFT_SHIFT;
	HWRITE4(sc, I2S_INTCR, val);

	ch->ch_intr = intr;
	ch->ch_intrarg = intrarg;
	ch->ch_start = ch->ch_cur = start;
	ch->ch_end = end;
	ch->ch_blksize = blksize;
	ch->ch_resid = blksize;

	return 0;
}

int
rkiis_trigger_input(void *cookie, void *start, void *end, int blksize,
    void (*intr)(void *), void *intrarg, struct audio_params *params)
{
	return EIO;
}

int
rkiis_halt_output(void *cookie)
{
	struct rkiis_softc *sc = cookie;
	struct rkiis_chan *ch = &sc->sc_pchan;
	uint32_t val;

	sc->sc_active &= ~XFER_TXS;
	if (sc->sc_active == 0) {
		val = HREAD4(sc, I2S_XFER);
		val &= ~(XFER_TXS|XFER_RXS);
		HWRITE4(sc, I2S_XFER, val);
	}

	val = HREAD4(sc, I2S_INTCR);
	val &= ~INTCR_TXEIE;
	HWRITE4(sc, I2S_INTCR, val);

	val = HREAD4(sc, I2S_CLR);
	val |= CLR_TXC;
	HWRITE4(sc, I2S_CLR, val);

	while ((HREAD4(sc, I2S_CLR) & CLR_TXC) != 0)
		delay(1);

	ch->ch_intr = NULL;
	ch->ch_intrarg = NULL;

	return 0;
}

int
rkiis_halt_input(void *cookie)
{
	struct rkiis_softc *sc = cookie;
	struct rkiis_chan *ch = &sc->sc_rchan;
	uint32_t val;

	sc->sc_active &= ~XFER_RXS;
	if (sc->sc_active == 0) {
		val = HREAD4(sc, I2S_XFER);
		val &= ~(XFER_TXS|XFER_RXS);
		HWRITE4(sc, I2S_XFER, val);
	}

	val = HREAD4(sc, I2S_INTCR);
	val &= ~INTCR_RXFIE;
	HWRITE4(sc, I2S_INTCR, val);

	ch->ch_intr = NULL;
	ch->ch_intrarg = NULL;

	return 0;
}
