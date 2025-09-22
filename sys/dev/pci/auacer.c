/*	$OpenBSD: auacer.c,v 1.30 2024/05/24 06:02:53 jsg Exp $	*/
/*	$NetBSD: auacer.c,v 1.3 2004/11/10 04:20:26 kent Exp $	*/

/*-
 * Copyright (c) 2004 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Acer Labs M5455 audio driver
 *
 * Acer provides data sheets after signing an NDA.
 * The chip behaves somewhat like the Intel i8x0, so this driver
 * is loosely based on the auich driver.  Additional information taken from
 * the ALSA intel8x0.c driver (which handles M5455 as well).
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/device.h>

#include <dev/pci/pcidevs.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/auacerreg.h>

#include <sys/audioio.h>
#include <dev/audio_if.h>

#include <machine/bus.h>

#include <dev/ic/ac97.h>

struct auacer_dma {
	bus_dmamap_t map;
	caddr_t addr;
	bus_dma_segment_t segs[1];
	int nsegs;
	size_t size;
	struct auacer_dma *next;
};

#define	DMAADDR(p)	((p)->map->dm_segs[0].ds_addr)
#define	KERNADDR(p)	((void *)((p)->addr))

const struct pci_matchid auacer_pci_devices[] = {
	{ PCI_VENDOR_ALI, PCI_PRODUCT_ALI_M5455 }
};

struct auacer_cdata {
	struct auacer_dmalist ic_dmalist_pcmo[ALI_DMALIST_MAX];
};

struct auacer_chan {
	uint32_t ptr;
	uint32_t start, p, end;
	uint32_t blksize, fifoe;
	uint32_t ack;
	uint32_t port;
	struct auacer_dmalist *dmalist;
	void (*intr)(void *);
	void *arg;
};

struct auacer_softc {
	struct device sc_dev;
	void *sc_ih;

	bus_space_tag_t iot;
	bus_space_handle_t mix_ioh;
	bus_space_handle_t aud_ioh;
	bus_dma_tag_t dmat;

	struct ac97_codec_if *codec_if;
	struct ac97_host_if host_if;

	/* DMA scatter-gather lists. */
	bus_dmamap_t sc_cddmamap;
#define	sc_cddma	sc_cddmamap->dm_segs[0].ds_addr

	struct auacer_cdata *sc_cdata;

	struct auacer_chan sc_pcmo;

	struct auacer_dma *sc_dmas;

	pci_chipset_tag_t sc_pc;
	pcitag_t sc_pt;

	int sc_dmamap_flags;
};

#define READ1(sc, a) bus_space_read_1(sc->iot, sc->aud_ioh, a)
#define READ2(sc, a) bus_space_read_2(sc->iot, sc->aud_ioh, a)
#define READ4(sc, a) bus_space_read_4(sc->iot, sc->aud_ioh, a)
#define WRITE1(sc, a, v) bus_space_write_1(sc->iot, sc->aud_ioh, a, v)
#define WRITE2(sc, a, v) bus_space_write_2(sc->iot, sc->aud_ioh, a, v)
#define WRITE4(sc, a, v) bus_space_write_4(sc->iot, sc->aud_ioh, a, v)

/* Debug */
#ifdef AUACER_DEBUG
#define	DPRINTF(l,x)	do { if (auacer_debug & (l)) printf x; } while(0)
int auacer_debug = 0;
#define	ALI_DEBUG_CODECIO	0x0001
#define	ALI_DEBUG_DMA		0x0002
#define	ALI_DEBUG_INTR		0x0004
#define ALI_DEBUG_API		0x0008
#define ALI_DEBUG_MIXERAPI	0x0010
#else
#define	DPRINTF(x,y)	/* nothing */
#endif

struct cfdriver auacer_cd = {
        NULL, "auacer", DV_DULL
};

int	auacer_match(struct device *, void *, void *);
void	auacer_attach(struct device *, struct device *, void *); 
int	auacer_activate(struct device *, int);
int	auacer_intr(void *); 

const struct cfattach auacer_ca = {
        sizeof(struct auacer_softc), auacer_match, auacer_attach, NULL,
	auacer_activate
};

int	auacer_open(void *, int);
void	auacer_close(void *);
int	auacer_set_params(void *, int, int, struct audio_params *,
	    struct audio_params *);
int	auacer_round_blocksize(void *, int);
int	auacer_halt_output(void *);
int	auacer_halt_input(void *);
int	auacer_set_port(void *, mixer_ctrl_t *);
int	auacer_get_port(void *, mixer_ctrl_t *);
int	auacer_query_devinfo(void *, mixer_devinfo_t *);
void	*auacer_allocm(void *, int, size_t, int, int);
void	auacer_freem(void *, void *, int);
size_t	auacer_round_buffersize(void *, int, size_t);
int	auacer_trigger_output(void *, void *, void *, int, void (*)(void *),
	    void *, struct audio_params *);
int	auacer_trigger_input(void *, void *, void *, int, void (*)(void *),
	    void *, struct audio_params *);

int	auacer_alloc_cdata(struct auacer_softc *);

int	auacer_allocmem(struct auacer_softc *, size_t, size_t,
	    struct auacer_dma *);
int	auacer_freemem(struct auacer_softc *, struct auacer_dma *);

int	auacer_set_rate(struct auacer_softc *, int, u_long);

static	void auacer_reset(struct auacer_softc *sc);

const struct audio_hw_if auacer_hw_if = {
	.open = auacer_open,
	.close = auacer_close,
	.set_params = auacer_set_params,
	.round_blocksize = auacer_round_blocksize,
	.halt_output = auacer_halt_output,
	.halt_input = auacer_halt_input,
	.set_port = auacer_set_port,
	.get_port = auacer_get_port,
	.query_devinfo = auacer_query_devinfo,
	.allocm = auacer_allocm,
	.freem = auacer_freem,
	.round_buffersize = auacer_round_buffersize,
	.trigger_output = auacer_trigger_output,
	.trigger_input = auacer_trigger_input,
};

int	auacer_attach_codec(void *, struct ac97_codec_if *);
int	auacer_read_codec(void *, u_int8_t, u_int16_t *);
int	auacer_write_codec(void *, u_int8_t, u_int16_t);
void	auacer_reset_codec(void *);

int
auacer_match(struct device *parent, void *match, void *aux)
{
	return (pci_matchbyid((struct pci_attach_args *)aux, auacer_pci_devices,
	    nitems(auacer_pci_devices)));
}

void
auacer_attach(struct device *parent, struct device *self, void *aux)
{
	struct auacer_softc *sc = (struct auacer_softc *)self;
	struct pci_attach_args *pa = aux;
	pci_intr_handle_t ih;
	bus_size_t aud_size;
	const char *intrstr;

	if (pci_mapreg_map(pa, PCI_MAPREG_START, PCI_MAPREG_TYPE_IO, 0,
		&sc->iot, &sc->aud_ioh, NULL, &aud_size, 0)) {
		printf(": can't map i/o space\n");
		return;
	}

	sc->sc_pc = pa->pa_pc;
	sc->sc_pt = pa->pa_tag;
	sc->dmat = pa->pa_dmat;

	sc->sc_dmamap_flags = BUS_DMA_COHERENT;	/* XXX remove */

	/* Map and establish the interrupt. */
	if (pci_intr_map(pa, &ih)) {
		printf("%s: can't map interrupt\n", sc->sc_dev.dv_xname);
		return;
	}
	intrstr = pci_intr_string(pa->pa_pc, ih);
	sc->sc_ih = pci_intr_establish(pa->pa_pc, ih, IPL_AUDIO | IPL_MPSAFE,
	    auacer_intr, sc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf("%s: can't establish interrupt",
		    sc->sc_dev.dv_xname);
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		return;
	}

	printf(": %s\n", intrstr);

	/* Set up DMA lists. */
	auacer_alloc_cdata(sc);
	sc->sc_pcmo.dmalist = sc->sc_cdata->ic_dmalist_pcmo;
	sc->sc_pcmo.ptr = 0;
	sc->sc_pcmo.port = ALI_BASE_PO;

	DPRINTF(ALI_DEBUG_DMA, ("auacer_attach: lists %p\n",
	    sc->sc_pcmo.dmalist));

	sc->host_if.arg = sc;
	sc->host_if.attach = auacer_attach_codec;
	sc->host_if.read = auacer_read_codec;
	sc->host_if.write = auacer_write_codec;
	sc->host_if.reset = auacer_reset_codec;

	if (ac97_attach(&sc->host_if) != 0)
		return;

	audio_attach_mi(&auacer_hw_if, sc, NULL, &sc->sc_dev);

	auacer_reset(sc);
}

static int
auacer_ready_codec(struct auacer_softc *sc, int mask)
{
	int count = 0;

	for (count = 0; count < 0x7f; count++) {
		int val = READ1(sc, ALI_CSPSR);
		if (val & mask)
			return 0;
	}

	printf("auacer_ready_codec: AC97 codec ready timeout.\n");
	return EBUSY;
}

static int
auacer_sema_codec(struct auacer_softc *sc)
{
	int ttime = 100;

	while (ttime-- && (READ4(sc, ALI_CAS) & ALI_CAS_SEM_BUSY))
		delay(1);
	if (!ttime)
		printf("auacer_sema_codec: timeout\n");
	return auacer_ready_codec(sc, ALI_CSPSR_CODEC_READY);
}

int
auacer_read_codec(void *v, u_int8_t reg, u_int16_t *val)
{
	struct auacer_softc *sc = v;

	if (auacer_sema_codec(sc))
		return EIO;

	reg |= ALI_CPR_ADDR_READ;
#if 0
	if (ac97->num)
		reg |= ALI_CPR_ADDR_SECONDARY;
#endif
	WRITE2(sc, ALI_CPR_ADDR, reg);
	if (auacer_ready_codec(sc, ALI_CSPSR_READ_OK))
		return EIO;
	*val = READ2(sc, ALI_SPR);

	DPRINTF(ALI_DEBUG_CODECIO, ("auacer_read_codec: reg=0x%x val=0x%x\n",
				    reg, *val));

	return 0;
}

int
auacer_write_codec(void *v, u_int8_t reg, u_int16_t val)
{
	struct auacer_softc *sc = v;

	DPRINTF(ALI_DEBUG_CODECIO, ("auacer_write_codec: reg=0x%x val=0x%x\n",
				    reg, val));

	if (auacer_sema_codec(sc))
		return EIO;
	WRITE2(sc, ALI_CPR, val);
#if 0
	if (ac97->num)
		reg |= ALI_CPR_ADDR_SECONDARY;
#endif
	WRITE2(sc, ALI_CPR_ADDR, reg);
	auacer_ready_codec(sc, ALI_CSPSR_WRITE_OK);
	return 0;
}

int
auacer_attach_codec(void *v, struct ac97_codec_if *cif)
{
	struct auacer_softc *sc = v;

	sc->codec_if = cif;
	return 0;
}

void
auacer_reset_codec(void *v)
{
	struct auacer_softc *sc = v;
	u_int32_t reg;
	int i = 0;

	reg = READ4(sc, ALI_SCR);
	if ((reg & 2) == 0)	/* Cold required */
		reg |= 2;
	else
		reg |= 1;	/* Warm */
	reg &= ~0x80000000;	/* ACLink on */
	WRITE4(sc, ALI_SCR, reg);

	while (i < 10) {
		if ((READ4(sc, ALI_INTERRUPTSR) & ALI_INT_GPIO) == 0)
			break;
		delay(50000);	/* XXX */
		i++;
	}
	if (i == 10) {
		return;
	}

	for (i = 0; i < 10; i++) {
		reg = READ4(sc, ALI_RTSR);
		if (reg & 0x80) /* primary codec */
			break;
		WRITE4(sc, ALI_RTSR, reg | 0x80);
		delay(50000);	/* XXX */
	}
}

static void
auacer_reset(struct auacer_softc *sc)
{
	WRITE4(sc, ALI_SCR, ALI_SCR_RESET);
	WRITE4(sc, ALI_FIFOCR1, 0x83838383);
	WRITE4(sc, ALI_FIFOCR2, 0x83838383);
	WRITE4(sc, ALI_FIFOCR3, 0x83838383);
	WRITE4(sc, ALI_INTERFACECR, ALI_IF_PO); /* XXX pcm out only */
	WRITE4(sc, ALI_INTERRUPTCR, 0x00000000);
	WRITE4(sc, ALI_INTERRUPTSR, 0x00000000);
}

int
auacer_open(void *v, int flags)
{
	DPRINTF(ALI_DEBUG_API, ("auacer_open: flags=%d\n", flags));
	return 0;
}

void
auacer_close(void *v)
{
	DPRINTF(ALI_DEBUG_API, ("auacer_close\n"));
}

int
auacer_set_rate(struct auacer_softc *sc, int mode, u_long srate)
{
	int ret;
	u_long ratetmp;

	DPRINTF(ALI_DEBUG_API, ("auacer_set_rate: srate=%lu\n", srate));

	ratetmp = srate;
	if (mode == AUMODE_RECORD)
		return sc->codec_if->vtbl->set_rate(sc->codec_if,
		    AC97_REG_PCM_LR_ADC_RATE, &ratetmp);
	ret = sc->codec_if->vtbl->set_rate(sc->codec_if,
	    AC97_REG_PCM_FRONT_DAC_RATE, &ratetmp);
	if (ret)
		return ret;
	ratetmp = srate;
	ret = sc->codec_if->vtbl->set_rate(sc->codec_if,
	    AC97_REG_PCM_SURR_DAC_RATE, &ratetmp);
	if (ret)
		return ret;
	ratetmp = srate;
	ret = sc->codec_if->vtbl->set_rate(sc->codec_if,
	    AC97_REG_PCM_LFE_DAC_RATE, &ratetmp);
	return ret;
}

static int
auacer_fixup_rate(int rate)
{
	int i;
	int rates[] = {
		8000, 11025, 12000, 16000, 22050, 32000, 44100, 48000
	};

	for (i = 0; i < nitems(rates) - 1; i++)
		if (rate <= (rates[i] + rates[i+1]) / 2)
			return (rates[i]);
	return (rates[i]);
}

int
auacer_set_params(void *v, int setmode, int usemode, struct audio_params *play,
    struct audio_params *rec)
{
	struct auacer_softc *sc = v;
	struct audio_params *p;
	uint32_t control;
	int mode;

	DPRINTF(ALI_DEBUG_API, ("auacer_set_params\n"));

	for (mode = AUMODE_RECORD; mode != -1;
	     mode = mode == AUMODE_RECORD ? AUMODE_PLAY : -1) {
		if ((setmode & mode) == 0)
			continue;

		p = mode == AUMODE_PLAY ? play : rec;
		if (p == NULL)
			continue;

		p->sample_rate = auacer_fixup_rate(p->sample_rate);
		p->precision = 16;
		p->encoding = AUDIO_ENCODING_SLINEAR_LE;
		if (mode == AUMODE_RECORD) {
			if (p->channels > 2)
				p->channels = 2;
		}
		p->bps = AUDIO_BPS(p->precision);
		p->msb = 1;

		if (AC97_IS_FIXED_RATE(sc->codec_if))
			p->sample_rate = AC97_SINGLE_RATE;
		else if (auacer_set_rate(sc, mode, p->sample_rate))
			return EINVAL;

		if (mode == AUMODE_PLAY) {
			control = READ4(sc, ALI_SCR);
			control &= ~ALI_SCR_PCM_246_MASK;
 			if (p->channels == 4)
				control |= ALI_SCR_PCM_4;
			else if (p->channels == 6)
				control |= ALI_SCR_PCM_6;
			WRITE4(sc, ALI_SCR, control);
		}
	}

	return (0);
}

int
auacer_round_blocksize(void *v, int blk)
{
	return ((blk + 0x3f) & ~0x3f);		/* keep good alignment */
}

static void
auacer_halt(struct auacer_softc *sc, struct auacer_chan *chan)
{
	uint32_t val;
	uint8_t port = chan->port;
	uint32_t slot;

	DPRINTF(ALI_DEBUG_API, ("auacer_halt: port=0x%x\n", port));

	chan->intr = 0;

	slot = ALI_PORT2SLOT(port);

	val = READ4(sc, ALI_DMACR);
	val |= 1 << (slot+16); /* pause */
	val &= ~(1 << slot); /* no start */
	WRITE4(sc, ALI_DMACR, val);
	WRITE1(sc, port + ALI_OFF_CR, 0);
	while (READ1(sc, port + ALI_OFF_CR))
		;
	/* reset whole DMA things */
	WRITE1(sc, port + ALI_OFF_CR, ALI_CR_RR);
	/* clear interrupts */
	WRITE1(sc, port + ALI_OFF_SR, READ1(sc, port+ALI_OFF_SR) | ALI_SR_W1TC);
	WRITE4(sc, ALI_INTERRUPTSR, ALI_PORT2INTR(port));
}

int
auacer_halt_output(void *v)
{
	struct auacer_softc *sc = v;

	DPRINTF(ALI_DEBUG_DMA, ("auacer_halt_output\n"));
	mtx_enter(&audio_lock);
	auacer_halt(sc, &sc->sc_pcmo);
	mtx_leave(&audio_lock);
	return (0);
}

int
auacer_halt_input(void *v)
{
	DPRINTF(ALI_DEBUG_DMA, ("auacer_halt_input\n"));
	return (0);
}

int
auacer_set_port(void *v, mixer_ctrl_t *cp)
{
	struct auacer_softc *sc = v;

	DPRINTF(ALI_DEBUG_MIXERAPI, ("auacer_set_port\n"));
	return (sc->codec_if->vtbl->mixer_set_port(sc->codec_if, cp));
}

int
auacer_get_port(void *v, mixer_ctrl_t *cp)
{
	struct auacer_softc *sc = v;

	DPRINTF(ALI_DEBUG_MIXERAPI, ("auacer_get_port\n"));
	return (sc->codec_if->vtbl->mixer_get_port(sc->codec_if, cp));
}

int
auacer_query_devinfo(void *v, mixer_devinfo_t *dp)
{
	struct auacer_softc *sc = v;

	DPRINTF(ALI_DEBUG_MIXERAPI, ("auacer_query_devinfo\n"));
	return (sc->codec_if->vtbl->query_devinfo(sc->codec_if, dp));
}

void *
auacer_allocm(void *v, int direction, size_t size, int pool, int flags)
{
	struct auacer_softc *sc = v;
	struct auacer_dma *p;
	int error;

	if (size > (ALI_DMALIST_MAX * ALI_DMASEG_MAX))
		return (NULL);

	p = malloc(sizeof(*p), pool, flags | M_ZERO);
	if (p == NULL)
		return (NULL);

	error = auacer_allocmem(sc, size, PAGE_SIZE, p);
	if (error) {
		free(p, pool, sizeof(*p));
		return (NULL);
	}

	p->next = sc->sc_dmas;
	sc->sc_dmas = p;

	return (KERNADDR(p));
}

void
auacer_freem(void *v, void *ptr, int pool)
{
	struct auacer_softc *sc = v;
	struct auacer_dma *p, **pp;

	for (pp = &sc->sc_dmas; (p = *pp) != NULL; pp = &p->next) {
		if (KERNADDR(p) == ptr) {
			auacer_freemem(sc, p);
			*pp = p->next;
			free(p, pool, sizeof(*p));
			return;
		}
	}
}

size_t
auacer_round_buffersize(void *v, int direction, size_t size)
{

	if (size > (ALI_DMALIST_MAX * ALI_DMASEG_MAX))
		size = ALI_DMALIST_MAX * ALI_DMASEG_MAX;

	return size;
}

static void
auacer_add_entry(struct auacer_chan *chan)
{
	struct auacer_dmalist *q;

	q = &chan->dmalist[chan->ptr];

	DPRINTF(ALI_DEBUG_INTR,
		("auacer_add_entry: %p = %x @ 0x%x\n",
		 q, chan->blksize / 2, chan->p));

	q->base = htole32(chan->p);
	q->len = htole32((chan->blksize / ALI_SAMPLE_SIZE) | ALI_DMAF_IOC);
	chan->p += chan->blksize;
	if (chan->p >= chan->end)
		chan->p = chan->start;
	
	if (++chan->ptr >= ALI_DMALIST_MAX)
		chan->ptr = 0;
}

static void
auacer_upd_chan(struct auacer_softc *sc, struct auacer_chan *chan)
{
	uint32_t sts;
	uint32_t civ;

	sts = READ2(sc, chan->port + ALI_OFF_SR);
	/* intr ack */
	WRITE2(sc, chan->port + ALI_OFF_SR, sts & ALI_SR_W1TC);
	WRITE4(sc, ALI_INTERRUPTSR, ALI_PORT2INTR(chan->port));

	DPRINTF(ALI_DEBUG_INTR, ("auacer_upd_chan: sts=0x%x\n", sts));

	if (sts & ALI_SR_DMA_INT_FIFO) {
		printf("%s: fifo underrun # %u\n",
		       sc->sc_dev.dv_xname, ++chan->fifoe);
	}

	civ = READ1(sc, chan->port + ALI_OFF_CIV);
	
	DPRINTF(ALI_DEBUG_INTR,("auacer_intr: civ=%u ptr=%u\n",civ,chan->ptr));
			
	/* XXX */
	while (chan->ptr != civ) {
		auacer_add_entry(chan);
	}

	WRITE1(sc, chan->port + ALI_OFF_LVI, (chan->ptr - 1) & ALI_LVI_MASK);

	while (chan->ack != civ) {
		if (chan->intr) {
			DPRINTF(ALI_DEBUG_INTR,("auacer_upd_chan: callback\n"));
			chan->intr(chan->arg);
		}
		chan->ack++;
		if (chan->ack >= ALI_DMALIST_MAX)
			chan->ack = 0;
	}
}

int
auacer_intr(void *v)
{
	struct auacer_softc *sc = v;
	int ret, intrs;

	mtx_enter(&audio_lock);
	intrs = READ4(sc, ALI_INTERRUPTSR);
	DPRINTF(ALI_DEBUG_INTR, ("auacer_intr: intrs=0x%x\n", intrs));

	ret = 0;
	if (intrs & ALI_INT_PCMOUT) {
		auacer_upd_chan(sc, &sc->sc_pcmo);
		ret++;
	}
	mtx_leave(&audio_lock);
	return ret != 0;
}

static void
auacer_setup_chan(struct auacer_softc *sc, struct auacer_chan *chan,
    uint32_t start, uint32_t size, uint32_t blksize, void (*intr)(void *),
    void *arg)
{
	uint32_t port, slot;
	uint32_t offs, val;

	chan->start = start;
	chan->ptr = 0;
	chan->p = chan->start;
	chan->end = chan->start + size;
	chan->blksize = blksize;
	chan->ack = 0;
	chan->intr = intr;
	chan->arg = arg;

	auacer_add_entry(chan);
	auacer_add_entry(chan);

	port = chan->port;
	slot = ALI_PORT2SLOT(port);

	WRITE1(sc, port + ALI_OFF_CIV, 0);
	WRITE1(sc, port + ALI_OFF_LVI, (chan->ptr - 1) & ALI_LVI_MASK);
	offs = (char *)chan->dmalist - (char *)sc->sc_cdata;
	WRITE4(sc, port + ALI_OFF_BDBAR, sc->sc_cddma + offs);
	WRITE1(sc, port + ALI_OFF_CR,
	       ALI_CR_IOCE | ALI_CR_FEIE | ALI_CR_LVBIE | ALI_CR_RPBM);
	val = READ4(sc, ALI_DMACR);
	val &= ~(1 << (slot+16)); /* no pause */
	val |= 1 << slot;	/* start */
	WRITE4(sc, ALI_DMACR, val);
}

int
auacer_trigger_output(void *v, void *start, void *end, int blksize,
    void (*intr)(void *), void *arg, struct audio_params *param)
{
	struct auacer_softc *sc = v;
	struct auacer_dma *p;
	uint32_t size;

	DPRINTF(ALI_DEBUG_DMA,
		("auacer_trigger_output(%p, %p, %d, %p, %p, %p)\n",
		 start, end, blksize, intr, arg, param));

	for (p = sc->sc_dmas; p && KERNADDR(p) != start; p = p->next)
		;
	if (!p) {
		printf("auacer_trigger_output: bad addr %p\n", start);
		return (EINVAL);
	}

	size = (char *)end - (char *)start;
	mtx_enter(&audio_lock);
	auacer_setup_chan(sc, &sc->sc_pcmo, DMAADDR(p), size, blksize,
			  intr, arg);
	mtx_leave(&audio_lock);
	return 0;
}

int
auacer_trigger_input(void *v, void *start, void *end, int blksize,
    void (*intr)(void *), void *arg, struct audio_params *param)
{
	return (EINVAL);
}

int
auacer_allocmem(struct auacer_softc *sc, size_t size, size_t align,
    struct auacer_dma *p)
{
	int error;

	p->size = size;
	error = bus_dmamem_alloc(sc->dmat, p->size, align, 0, p->segs,
	    nitems(p->segs), &p->nsegs, BUS_DMA_NOWAIT);
	if (error)
		return (error);

	error = bus_dmamem_map(sc->dmat, p->segs, p->nsegs, p->size, &p->addr,
	    BUS_DMA_NOWAIT | sc->sc_dmamap_flags);
	if (error)
		goto free;

	error = bus_dmamap_create(sc->dmat, p->size, 1, p->size, 0,
	    BUS_DMA_NOWAIT, &p->map);
	if (error)
		goto unmap;

	error = bus_dmamap_load(sc->dmat, p->map, p->addr, p->size, NULL,
	    BUS_DMA_NOWAIT);
	if (error)
		goto destroy;
	return (0);

 destroy:
	bus_dmamap_destroy(sc->dmat, p->map);
 unmap:
	bus_dmamem_unmap(sc->dmat, p->addr, p->size);
 free:
	bus_dmamem_free(sc->dmat, p->segs, p->nsegs);
	return (error);
}

int
auacer_freemem(struct auacer_softc *sc, struct auacer_dma *p)
{

	bus_dmamap_unload(sc->dmat, p->map);
	bus_dmamap_destroy(sc->dmat, p->map);
	bus_dmamem_unmap(sc->dmat, p->addr, p->size);
	bus_dmamem_free(sc->dmat, p->segs, p->nsegs);
	return (0);
}

int
auacer_alloc_cdata(struct auacer_softc *sc)
{
	bus_dma_segment_t seg;
	int error, rseg;

	/*
	 * Allocate the control data structure, and create and load the
	 * DMA map for it.
	 */
	if ((error = bus_dmamem_alloc(sc->dmat, sizeof(struct auacer_cdata),
	    PAGE_SIZE, 0, &seg, 1, &rseg, 0)) != 0) {
		printf("%s: unable to allocate control data, error = %d\n",
		    sc->sc_dev.dv_xname, error);
		goto fail_0;
	}

	if ((error = bus_dmamem_map(sc->dmat, &seg, rseg,
            sizeof(struct auacer_cdata), (caddr_t *) &sc->sc_cdata,
	    sc->sc_dmamap_flags)) != 0) {
		printf("%s: unable to map control data, error = %d\n",
		    sc->sc_dev.dv_xname, error);
		goto fail_1;
	}

	if ((error = bus_dmamap_create(sc->dmat, sizeof(struct auacer_cdata), 1,
	    sizeof(struct auacer_cdata), 0, 0, &sc->sc_cddmamap)) != 0) {
		printf("%s: unable to create control data DMA map, "
		    "error = %d\n", sc->sc_dev.dv_xname, error);
		goto fail_2;
	}

	if ((error = bus_dmamap_load(sc->dmat, sc->sc_cddmamap, sc->sc_cdata,
	    sizeof(struct auacer_cdata), NULL, 0)) != 0) {
		printf("%s: unable to load control data DMA map, error = %d\n",
		    sc->sc_dev.dv_xname, error);
		goto fail_3;
	}

	return (0);

 fail_3:
	bus_dmamap_destroy(sc->dmat, sc->sc_cddmamap);
 fail_2:
	bus_dmamem_unmap(sc->dmat, (caddr_t) sc->sc_cdata,
	    sizeof(struct auacer_cdata));
 fail_1:
	bus_dmamem_free(sc->dmat, &seg, rseg);
 fail_0:
	return (error);
}

int
auacer_activate(struct device *self, int act)
{
	struct auacer_softc *sc = (struct auacer_softc *)self;

	if (act == DVACT_RESUME)
		ac97_resume(&sc->host_if, sc->codec_if);
	return (config_activate_children(self, act));
}
