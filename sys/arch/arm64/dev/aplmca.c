/*	$OpenBSD: aplmca.c,v 1.7 2023/07/26 11:09:24 kettenis Exp $	*/
/*
 * Copyright (c) 2022 Mark Kettenis <kettenis@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/audioio.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/fcntl.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/ofw_power.h>
#include <dev/ofw/fdt.h>

#include <dev/audio_if.h>

#include <arm64/dev/apldma.h>

/*
 * This driver is based on preliminary device tree bindings and will
 * almost certainly need changes once the official bindings land in
 * mainline Linux.  Support for these preliminary bindings will be
 * dropped as soon as official bindings are available.
 */

#define MCA_CL_STRIDE		0x4000
#define MCA_SW_STRIDE		0x8000
#define MCA_SERDES_TXA		0x0300

#define MCA_STATUS(idx)			((idx) * MCA_CL_STRIDE + 0x0000)
#define  MCA_STATUS_MCLK_EN		(1 << 0)
#define MCA_MCLK_CONF(idx)		((idx) * MCA_CL_STRIDE + 0x0004)
#define  MCA_MCLK_CONF_DIV_MASK		(0xf << 8)
#define  MCA_MCLK_CONF_DIV_SHIFT	8

#define MCA_SYNCGEN_STATUS(idx)		((idx) * MCA_CL_STRIDE + 0x0100)
#define  MCA_SYNCGEN_STATUS_EN		(1 << 0)
#define MCA_SYNCGEN_MCLK_SEL(idx)	((idx) * MCA_CL_STRIDE + 0x0104)
#define MCA_SYNCGEN_HI_PERIOD(idx)	((idx) * MCA_CL_STRIDE + 0x0108)
#define MCA_SYNCGEN_LO_PERIOD(idx)	((idx) * MCA_CL_STRIDE + 0x010c)

#define MCA_SERDES_BASE(idx, off)	((idx) * MCA_CL_STRIDE + (off))
#define MCA_SERDES_STATUS(idx, off)	(MCA_SERDES_BASE(idx, off) + 0x0000)
#define  MCA_SERDES_STATUS_EN		(1 << 0)
#define  MCA_SERDES_STATUS_RST		(1 << 1)
#define MCA_SERDES_CONF(idx, off)	(MCA_SERDES_BASE(idx, off) + 0x0004)
#define MCA_SERDES_CONF_NSLOTS_MASK	(0xf << 0)
#define MCA_SERDES_CONF_NSLOTS_SHIFT	0
#define MCA_SERDES_CONF_WIDTH_MASK	(0x1f << 4)
#define MCA_SERDES_CONF_WIDTH_32BIT	(0x10 << 4)
#define MCA_SERDES_CONF_BCLK_POL	(1 << 10)
#define MCA_SERDES_CONF_MAGIC		(0x7 << 12)
#define MCA_SERDES_CONF_SYNC_SEL_MASK	(0x7 << 16)
#define MCA_SERDES_CONF_SYNC_SEL_SHIFT	16
#define MCA_SERDES_BITSTART(idx, off)	(MCA_SERDES_BASE(idx, off) + 0x0008)
#define MCA_SERDES_CHANMASK0(idx, off)	(MCA_SERDES_BASE(idx, off) + 0x000c)
#define MCA_SERDES_CHANMASK1(idx, off)	(MCA_SERDES_BASE(idx, off) + 0x0010)
#define MCA_SERDES_CHANMASK2(idx, off)	(MCA_SERDES_BASE(idx, off) + 0x0014)
#define MCA_SERDES_CHANMASK3(idx, off)	(MCA_SERDES_BASE(idx, off) + 0x0018)

#define MCA_PORT_ENABLE(idx)		((idx) * MCA_CL_STRIDE + 0x0600)
#define  MCA_PORT_ENABLE_CLOCKS		(0x3 << 1)
#define  MCA_PORT_ENABLE_TX_DATA	(1 << 3)
#define MCA_PORT_CLOCK_SEL(idx)		((idx) * MCA_CL_STRIDE + 0x0604)
#define  MCA_PORT_CLOCK_SEL_SHIFT	8
#define MCA_PORT_DATA_SEL(idx)		((idx) * MCA_CL_STRIDE + 0x0608)
#define  MCA_PORT_DATA_SEL_TXA(idx)	(1 << ((idx) * 2))
#define  MCA_PORT_DATA_SEL_TXB(idx)	(2 << ((idx) * 2))

#define MCA_DMA_ADAPTER_A(idx)		((idx) * MCA_SW_STRIDE + 0x0000)
#define MCA_DMA_ADAPTER_B(idx)		((idx) * MCA_SW_STRIDE + 0x4000)
#define  MCA_DMA_ADAPTER_TX_LSB_PAD_SHIFT	0
#define  MCA_DMA_ADAPTER_TX_NCHANS_SHIFT	5
#define  MCA_DMA_ADAPTER_NCHANS_SHIFT		20


#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

struct aplmca_dai {
	struct aplmca_softc	*ad_sc;
	struct dai_device	ad_dai;
	int			ad_cluster;

	struct apldma_channel	*ad_ac;
	void			*ad_pbuf;
};

struct aplmca_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	bus_space_handle_t	sc_sw_ioh;

	int			sc_node;
	uint32_t		sc_phandle;

	int			sc_nclusters;
	struct aplmca_dai	*sc_ad;
};

int	aplmca_set_format(void *, uint32_t, uint32_t, uint32_t);
int	aplmca_set_sysclk(void *, uint32_t);

int	aplmca_open(void *, int);
int	aplmca_set_params(void *, int, int,
	    struct audio_params *, struct audio_params *);
void	*aplmca_allocm(void *, int, size_t, int, int);
void	aplmca_freem(void *, void *, int);
int	aplmca_trigger_output(void *, void *, void *, int,
	    void (*)(void *), void *, struct audio_params *);
int	aplmca_trigger_input(void *, void *, void *, int,
	    void (*)(void *), void *, struct audio_params *);
int	aplmca_halt_output(void *);
int	aplmca_halt_input(void *);

const struct audio_hw_if aplmca_hw_if = {
	.open = aplmca_open,
	.set_params = aplmca_set_params,
	.allocm = aplmca_allocm,
	.freem = aplmca_freem,
	.trigger_output = aplmca_trigger_output,
	.trigger_input = aplmca_trigger_input,
	.halt_output = aplmca_halt_output,
	.halt_input = aplmca_halt_input,
};

int	aplmca_match(struct device *, void *, void *);
void	aplmca_attach(struct device *, struct device *, void *);
int	aplmca_activate(struct device *, int);

const struct cfattach aplmca_ca = {
	sizeof (struct aplmca_softc), aplmca_match, aplmca_attach, NULL,
	aplmca_activate
};

struct cfdriver aplmca_cd = {
	NULL, "aplmca", DV_DULL
};

int
aplmca_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "apple,mca");
}

void
aplmca_attach(struct device *parent, struct device *self, void *aux)
{
	struct aplmca_softc *sc = (struct aplmca_softc *)self;
	struct fdt_attach_args *faa = aux;
	int i;

	if (faa->fa_nreg < 2) {
		printf(": no registers\n");
		return;
	}

	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}
	if (bus_space_map(sc->sc_iot, faa->fa_reg[1].addr,
	    faa->fa_reg[1].size, 0, &sc->sc_sw_ioh)) {
		bus_space_unmap(sc->sc_iot, sc->sc_ioh, faa->fa_reg[0].size);
		printf(": can't map registers\n");
		return;
	}

	sc->sc_node = faa->fa_node;
	sc->sc_phandle = OF_getpropint(faa->fa_node, "phandle", 0);

	sc->sc_nclusters = OF_getpropint(faa->fa_node, "apple,nclusters", 6);
	sc->sc_ad = mallocarray(sc->sc_nclusters, sizeof(*sc->sc_ad),
	    M_DEVBUF, M_WAITOK | M_ZERO);

	for (i = 0; i < sc->sc_nclusters; i++) {
		sc->sc_ad[i].ad_cluster = i;
		sc->sc_ad[i].ad_sc = sc;
		sc->sc_ad[i].ad_dai.dd_node = sc->sc_node;
		sc->sc_ad[i].ad_dai.dd_cookie = &sc->sc_ad[i];
		sc->sc_ad[i].ad_dai.dd_hw_if = &aplmca_hw_if;
		sc->sc_ad[i].ad_dai.dd_set_format = aplmca_set_format;
		sc->sc_ad[i].ad_dai.dd_set_sysclk = aplmca_set_sysclk;
	}

	printf("\n");

	power_domain_enable_idx(sc->sc_node, 0);

	for (i = 0; i < sc->sc_nclusters; i++) {
		HCLR4(sc, MCA_SERDES_STATUS(i, MCA_SERDES_TXA),
		    MCA_SERDES_STATUS_EN);
		HCLR4(sc, MCA_SYNCGEN_STATUS(i), MCA_SYNCGEN_STATUS_EN);
		HCLR4(sc, MCA_STATUS(i), MCA_STATUS_MCLK_EN);
	}
}

int
aplmca_activate(struct device *self, int act)
{
	struct aplmca_softc *sc = (struct aplmca_softc *)self;
	int i;

	switch (act) {
	case DVACT_SUSPEND:
		for (i = 0; i < sc->sc_nclusters; i++) {
			if (sc->sc_ad[i].ad_ac)
				power_domain_disable_idx(sc->sc_node, i + 1);
		}
		power_domain_disable_idx(sc->sc_node, 0);
		break;
	case DVACT_RESUME:
		power_domain_enable_idx(sc->sc_node, 0);
		for (i = 0; i < sc->sc_nclusters; i++) {
			if (sc->sc_ad[i].ad_ac)
				power_domain_enable_idx(sc->sc_node, i + 1);
		}
		break;
	}

	return 0;
}

int
aplmca_dai_init(struct aplmca_softc *sc, int port)
{
	struct aplmca_dai *ad = &sc->sc_ad[port];
	uint32_t conf;
	char name[5];
	int idx;

	/* Allocate DMA channel. */
	snprintf(name, sizeof(name), "tx%da", ad->ad_cluster);
	idx = OF_getindex(sc->sc_node, name, "dma-names");
	if (idx == -1)
		return ENOENT;
	ad->ad_ac = apldma_alloc_channel(idx);
	if (ad->ad_ac == NULL)
		return ENOENT;

	power_domain_enable_idx(sc->sc_node, port + 1);

	/* Basic SERDES configuration. */
	conf = HREAD4(sc, MCA_SERDES_CONF(ad->ad_cluster, MCA_SERDES_TXA));
	conf &= ~MCA_SERDES_CONF_SYNC_SEL_MASK;
	conf |= (ad->ad_cluster + 1) << MCA_SERDES_CONF_SYNC_SEL_SHIFT;
	conf |= MCA_SERDES_CONF_MAGIC;
	HWRITE4(sc, MCA_SERDES_CONF(ad->ad_cluster, MCA_SERDES_TXA), conf);

	/* Output port configuration. */
	HWRITE4(sc, MCA_PORT_CLOCK_SEL(port),
	    (ad->ad_cluster + 1) << MCA_PORT_CLOCK_SEL_SHIFT);
	HWRITE4(sc, MCA_PORT_DATA_SEL(port),
	    MCA_PORT_DATA_SEL_TXA(ad->ad_cluster));
	HWRITE4(sc, MCA_PORT_ENABLE(port),
	    MCA_PORT_ENABLE_CLOCKS | MCA_PORT_ENABLE_TX_DATA);

	return 0;
}

void
aplmca_dai_link(struct aplmca_softc *sc, int master, int port)
{
	struct aplmca_dai *ad = &sc->sc_ad[master];

	HWRITE4(sc, MCA_PORT_CLOCK_SEL(port),
	    (ad->ad_cluster + 1) << MCA_PORT_CLOCK_SEL_SHIFT);
	HWRITE4(sc, MCA_PORT_DATA_SEL(port),
	    MCA_PORT_DATA_SEL_TXA(ad->ad_cluster));
	HWRITE4(sc, MCA_PORT_ENABLE(port),
	    MCA_PORT_ENABLE_CLOCKS | MCA_PORT_ENABLE_TX_DATA);
}

uint32_t *
aplmca_dai_next_dai(uint32_t *cells)
{
	uint32_t phandle = cells[0];
	int node, ncells;

	node = OF_getnodebyphandle(phandle);
	if (node == 0)
		return NULL;

	ncells = OF_getpropint(node, "#sound-dai-cells", 0);
	return cells + ncells + 1;
}

struct dai_device *
aplmca_alloc_cluster(int node)
{
	struct aplmca_softc *sc = aplmca_cd.cd_devs[0];
	uint32_t *dais;
	uint32_t *dai;
	uint32_t ports[2];
	int nports = 0;
	int len, i;

	len = OF_getproplen(node, "sound-dai");
	if (len != 2 * sizeof(uint32_t) && len != 4 * sizeof(uint32_t))
		return NULL;

	dais = malloc(len, M_TEMP, M_WAITOK);
	OF_getpropintarray(node, "sound-dai", dais, len);

	dai = dais;
	while (dai && dai < dais + (len / sizeof(uint32_t))) {
		if (dai[0] == sc->sc_phandle && nports < nitems(ports))
			ports[nports++] = dai[1];
		dai = aplmca_dai_next_dai(dai);
	}

	free(dais, M_TEMP, len);

	if (nports == 0)
		return NULL;
	for (i = 0; i < nports; i++) {
		if (ports[i] >= sc->sc_nclusters)
			return NULL;
	}

	if (sc->sc_ad[ports[0]].ad_ac != NULL)
		return NULL;

	/* Setup the primary cluster. */
	if (aplmca_dai_init(sc, ports[0]))
		return NULL;

	/*
	 * Additional interfaces receive the same output as the
	 * primary interface by linking the output port to the primary
	 * cluster.
	 */
	for (i = 1; i < nports; i++)
		aplmca_dai_link(sc, ports[0], ports[i]);

	return &sc->sc_ad[ports[0]].ad_dai;
}

int
aplmca_set_format(void *cookie, uint32_t fmt, uint32_t pol,
    uint32_t clk)
{
	struct aplmca_dai *ad = cookie;
	struct aplmca_softc *sc = ad->ad_sc;
	uint32_t conf;

	conf = HREAD4(sc, MCA_SERDES_CONF(ad->ad_cluster, MCA_SERDES_TXA));
	conf &= ~MCA_SERDES_CONF_WIDTH_MASK;
	conf |= MCA_SERDES_CONF_WIDTH_32BIT;

	switch (fmt) {
	case DAI_FORMAT_I2S:
		conf &= ~MCA_SERDES_CONF_BCLK_POL;
		break;
	case DAI_FORMAT_RJ:
	case DAI_FORMAT_LJ:
		conf |= MCA_SERDES_CONF_BCLK_POL;
		break;
	default:
		return EINVAL;
	}

	if (pol & DAI_POLARITY_IB)
		conf ^= MCA_SERDES_CONF_BCLK_POL;
	if (pol & DAI_POLARITY_IF)
		return EINVAL;

	if (!(clk & DAI_CLOCK_CBM) || !(clk & DAI_CLOCK_CFM))
		return EINVAL;

	HWRITE4(sc, MCA_SERDES_CONF(ad->ad_cluster, MCA_SERDES_TXA), conf);

	return 0;
}

int
aplmca_set_sysclk(void *cookie, uint32_t rate)
{
	struct aplmca_dai *ad = cookie;
	struct aplmca_softc *sc = ad->ad_sc;

	return clock_set_frequency_idx(sc->sc_node, ad->ad_cluster, rate);
}

int
aplmca_open(void *cookie, int flags)
{
	if ((flags & (FWRITE | FREAD)) == (FWRITE | FREAD))
		return ENXIO;

	return 0;
}

int
aplmca_set_params(void *cookie, int setmode, int usemode,
    struct audio_params *play, struct audio_params *rec)
{
	if (setmode & AUMODE_PLAY) {
		play->sample_rate = 48000;
		play->encoding = AUDIO_ENCODING_SLINEAR_LE;
		play->precision = 24;
		play->bps = 4;
		play->msb = 0;
		play->channels = 2;
	}

	return 0;
}

void *
aplmca_allocm(void *cookie, int direction, size_t size, int type,
    int flags)
{
	struct aplmca_dai *ad = cookie;

	if (direction == AUMODE_PLAY) {
		ad->ad_pbuf = apldma_allocm(ad->ad_ac, size, flags);
		return ad->ad_pbuf;
	}

	return malloc(size, type, flags | M_ZERO);
}

void
aplmca_freem(void *cookie, void *addr, int type)
{
	struct aplmca_dai *ad = cookie;

	if (addr == ad->ad_pbuf) {
		apldma_freem(ad->ad_ac);
		return;
	}

	free(addr, type, 0);
}

int
aplmca_trigger_output(void *cookie, void *start, void *end, int blksize,
    void (*intr)(void *), void *intrarg, struct audio_params *params)
{
	struct aplmca_dai *ad = cookie;
	struct aplmca_softc *sc = ad->ad_sc;
	uint32_t conf, period;
	int pad;

	if (params->channels > 16)
		return EINVAL;

	/* Finalize SERDES configuration. */
	conf = HREAD4(sc, MCA_SERDES_CONF(ad->ad_cluster, MCA_SERDES_TXA));
	conf &= ~MCA_SERDES_CONF_NSLOTS_MASK;
	conf |= ((params->channels - 1) << MCA_SERDES_CONF_NSLOTS_SHIFT);
	HWRITE4(sc, MCA_SERDES_CONF(ad->ad_cluster, MCA_SERDES_TXA), conf);
	HWRITE4(sc, MCA_SERDES_CHANMASK0(ad->ad_cluster, MCA_SERDES_TXA),
	    0xffffffff);
	HWRITE4(sc, MCA_SERDES_CHANMASK1(ad->ad_cluster, MCA_SERDES_TXA),
	    0xffffffff << params->channels);
	HWRITE4(sc, MCA_SERDES_CHANMASK2(ad->ad_cluster, MCA_SERDES_TXA),
	    0xffffffff);
	HWRITE4(sc, MCA_SERDES_CHANMASK3(ad->ad_cluster, MCA_SERDES_TXA),
	    0xffffffff << params->channels);

	period = params->channels * 32;
	HWRITE4(sc, MCA_SYNCGEN_HI_PERIOD(ad->ad_cluster), period - 2);
	HWRITE4(sc, MCA_SYNCGEN_LO_PERIOD(ad->ad_cluster), 0);
	HWRITE4(sc, MCA_MCLK_CONF(ad->ad_cluster),
	    1 << MCA_MCLK_CONF_DIV_SHIFT);

	clock_enable_idx(sc->sc_node, ad->ad_cluster);

	HWRITE4(sc, MCA_SYNCGEN_MCLK_SEL(ad->ad_cluster),
	    ad->ad_cluster + 1);

	HSET4(sc, MCA_STATUS(ad->ad_cluster), MCA_STATUS_MCLK_EN);
	HSET4(sc, MCA_SYNCGEN_STATUS(ad->ad_cluster),
	    MCA_SYNCGEN_STATUS_EN);
	HSET4(sc, MCA_SERDES_STATUS(ad->ad_cluster, MCA_SERDES_TXA),
	    MCA_SERDES_STATUS_EN);

	pad = params->bps * 8 - params->precision;
	bus_space_write_4(sc->sc_iot, sc->sc_sw_ioh,
	    MCA_DMA_ADAPTER_A(ad->ad_cluster),
	    pad << MCA_DMA_ADAPTER_TX_LSB_PAD_SHIFT |
	    2 << MCA_DMA_ADAPTER_TX_NCHANS_SHIFT |
	    2 << MCA_DMA_ADAPTER_NCHANS_SHIFT);

	return apldma_trigger_output(ad->ad_ac, start, end, blksize,
	    intr, intrarg, params);
}

int
aplmca_trigger_input(void *cookie, void *start, void *end, int blksize,
    void (*intr)(void *), void *intrarg, struct audio_params *params)
{
	printf("%s\n", __func__);
	return EIO;
}

int
aplmca_halt_output(void *cookie)
{
	struct aplmca_dai *ad = cookie;
	struct aplmca_softc *sc = ad->ad_sc;
	int error;

	error = apldma_halt_output(ad->ad_ac);

	HCLR4(sc, MCA_SERDES_STATUS(ad->ad_cluster, MCA_SERDES_TXA),
	    MCA_SERDES_STATUS_EN);
	HCLR4(sc, MCA_SYNCGEN_STATUS(ad->ad_cluster),
	    MCA_SYNCGEN_STATUS_EN);
	HCLR4(sc, MCA_STATUS(ad->ad_cluster), MCA_STATUS_MCLK_EN);

	clock_disable_idx(sc->sc_node, ad->ad_cluster);

	return error;
}

int
aplmca_halt_input(void *cookie)
{
	printf("%s\n", __func__);
	return 0;
}
