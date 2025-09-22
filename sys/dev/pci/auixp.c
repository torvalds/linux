/* $OpenBSD: auixp.c,v 1.56 2024/09/20 02:20:44 jsg Exp $ */
/* $NetBSD: auixp.c,v 1.9 2005/06/27 21:13:09 thorpej Exp $ */

/*
 * Copyright (c) 2004, 2005 Reinoud Zandijk <reinoud@netbsd.org>
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
/*
 * Audio driver for ATI IXP-{150,200,...} audio driver hardware.
 *
 * Recording and playback has been tested OK on various sample rates and
 * encodings.
 *
 * Known problems and issues :
 * - SPDIF is untested and needs some work still (LED stays off)
 * - 32 bit audio playback failed last time i tried but that might an AC'97
 *   codec support problem.
 * - 32 bit recording works but can't try out playing: see above.
 * - no suspend/resume support yet.
 * - multiple codecs are `supported' but not tested; the implementation needs
 *   some cleaning up.
 */

/*#define DEBUG_AUIXP*/

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/audioio.h>
#include <sys/queue.h>

#include <machine/bus.h>

#include <dev/pci/pcidevs.h>
#include <dev/pci/pcivar.h>

#include <dev/audio_if.h>
#include <dev/ic/ac97.h>

#include <dev/pci/auixpreg.h>
#include <dev/pci/auixpvar.h>

/* codec detection constant indicating the interrupt flags */
#define ALL_CODECS_NOT_READY \
    (ATI_REG_ISR_CODEC0_NOT_READY | ATI_REG_ISR_CODEC1_NOT_READY |\
     ATI_REG_ISR_CODEC2_NOT_READY)
#define CODEC_CHECK_BITS (ALL_CODECS_NOT_READY|ATI_REG_ISR_NEW_FRAME)

/* why isn't this base address register not in the headerfile? */
#define PCI_CBIO 0x10

/* macro's used */
#define KERNADDR(p)	((void *)((p)->addr))
#define	DMAADDR(p)	((p)->map->dm_segs[0].ds_addr)

const struct pci_matchid auixp_pci_devices[] = {
	{ PCI_VENDOR_ATI, PCI_PRODUCT_ATI_SB200_AUDIO },
	{ PCI_VENDOR_ATI, PCI_PRODUCT_ATI_SB300_AUDIO },
	{ PCI_VENDOR_ATI, PCI_PRODUCT_ATI_SB400_AUDIO },
	{ PCI_VENDOR_ATI, PCI_PRODUCT_ATI_SB600_AUDIO }
};

struct cfdriver auixp_cd = {
	NULL, "auixp", DV_DULL
};

int	auixp_match( struct device *, void *, void *);
void	auixp_attach(struct device *, struct device *, void *);
int	auixp_detach(struct device *, int);

int	auixp_activate(struct device *, int);

const struct cfattach auixp_ca = {
	sizeof(struct auixp_softc), auixp_match, auixp_attach,
	NULL, auixp_activate
};

int	auixp_open(void *v, int flags);
void	auixp_close(void *v);
int	auixp_set_params(void *, int, int, struct audio_params *,
    struct audio_params *);
int	auixp_commit_settings(void *);
int	auixp_round_blocksize(void *, int);
int	auixp_trigger_output(void *, void *, void *, int,
    void (*)(void *), void *, struct audio_params *);
int	auixp_trigger_input(void *, void *, void *, int,
    void (*)(void *), void *, struct audio_params *);
int	auixp_halt_output(void *);
int	auixp_halt_input(void *);
int	auixp_set_port(void *, mixer_ctrl_t *);
int	auixp_get_port(void *, mixer_ctrl_t *);
int	auixp_query_devinfo(void *, mixer_devinfo_t *);
void *	auixp_malloc(void *, int, size_t, int, int);
void	auixp_free(void *, void *, int);
int	auixp_intr(void *);
int	auixp_allocmem(struct auixp_softc *, size_t, size_t,
    struct auixp_dma *);
int	auixp_freemem(struct auixp_softc *, struct auixp_dma *);

/* Supporting subroutines */
int	auixp_init(struct auixp_softc *);
void	auixp_autodetect_codecs(struct auixp_softc *);
void	auixp_post_config(struct device *);

void	auixp_reset_aclink(struct auixp_softc *);
int	auixp_attach_codec(void *, struct ac97_codec_if *);
int	auixp_read_codec(void *, u_int8_t, u_int16_t *);
int	auixp_write_codec(void *, u_int8_t, u_int16_t);
int	auixp_wait_for_codecs(struct auixp_softc *, const char *);
void	auixp_reset_codec(void *);
enum ac97_host_flags	auixp_flags_codec(void *);

void	auixp_enable_dma(struct auixp_softc *, struct auixp_dma *);
void	auixp_disable_dma(struct auixp_softc *, struct auixp_dma *);
void	auixp_enable_interrupts(struct auixp_softc *);
void	auixp_disable_interrupts(struct auixp_softc *);

void	auixp_link_daisychain(struct auixp_softc *,
    struct auixp_dma *, struct auixp_dma *, int, int);
int	auixp_allocate_dma_chain(struct auixp_softc *, struct auixp_dma **);
void	auixp_program_dma_chain(struct auixp_softc *, struct auixp_dma *);
void	auixp_dma_update(struct auixp_softc *, struct auixp_dma *);
void	auixp_update_busbusy(struct auixp_softc *);

#ifdef DEBUG_AUIXP
#define DPRINTF(x)	printf x
#else
#define DPRINTF(x)
#endif

const struct audio_hw_if auixp_hw_if = {
	.open = auixp_open,
	.close = auixp_close,
	.set_params = auixp_set_params,
	.round_blocksize = auixp_round_blocksize,
	.commit_settings = auixp_commit_settings,
	.halt_output = auixp_halt_output,
	.halt_input = auixp_halt_input,
	.set_port = auixp_set_port,
	.get_port = auixp_get_port,
	.query_devinfo = auixp_query_devinfo,
	.allocm = auixp_malloc,
	.freem = auixp_free,
	.trigger_output = auixp_trigger_output,
	.trigger_input = auixp_trigger_input,
};

int
auixp_open(void *v, int flags)
{

	return 0;
}

void
auixp_close(void *v)
{
}

/* commit setting and program ATI IXP chip */
int
auixp_commit_settings(void *hdl)
{
	struct auixp_codec *co;
	struct auixp_softc *sc;
	bus_space_tag_t    iot;
	bus_space_handle_t ioh;
	struct audio_params *params;
	u_int32_t value;

	/* XXX would it be better to stop interrupts first? XXX */
	co = (struct auixp_codec *) hdl;
	sc = co->sc;
	iot = sc->sc_iot;
	ioh = sc->sc_ioh;

	/* process input settings */
	params = &sc->sc_play_params;

	/* set input interleaving (precision) */
	value  =  bus_space_read_4(iot, ioh, ATI_REG_CMD);
	value &= ~ATI_REG_CMD_INTERLEAVE_IN;
	if (params->precision <= 16)
		value |= ATI_REG_CMD_INTERLEAVE_IN;
	bus_space_write_4(iot, ioh, ATI_REG_CMD, value);

	/* process output settings */
	params = &sc->sc_play_params;

	value  =  bus_space_read_4(iot, ioh, ATI_REG_OUT_DMA_SLOT);
	value &= ~ATI_REG_OUT_DMA_SLOT_MASK;

	/* TODO SPDIF case for 8 channels */
	switch (params->channels) {
	case 6:
		value |= ATI_REG_OUT_DMA_SLOT_BIT(7) |
			 ATI_REG_OUT_DMA_SLOT_BIT(8);
		/* FALLTHROUGH */
	case 4:
		value |= ATI_REG_OUT_DMA_SLOT_BIT(6) |
			 ATI_REG_OUT_DMA_SLOT_BIT(9);
		/* FALLTHROUGH */
	default:
		value |= ATI_REG_OUT_DMA_SLOT_BIT(3) |
			 ATI_REG_OUT_DMA_SLOT_BIT(4);
		break;
	}
	/* set output threshold */
	value |= 0x04 << ATI_REG_OUT_DMA_THRESHOLD_SHIFT;
	bus_space_write_4(iot, ioh, ATI_REG_OUT_DMA_SLOT, value);

	/* set output interleaving (precision) */
	value  =  bus_space_read_4(iot, ioh, ATI_REG_CMD);
	value &= ~ATI_REG_CMD_INTERLEAVE_OUT;
	if (params->precision <= 16)
		value |= ATI_REG_CMD_INTERLEAVE_OUT;
	bus_space_write_4(iot, ioh, ATI_REG_CMD, value);

	/* enable 6 channel reordering */
	value  =  bus_space_read_4(iot, ioh, ATI_REG_6CH_REORDER);
	value &= ~ATI_REG_6CH_REORDER_EN;
	if (params->channels == 6)
		value |= ATI_REG_6CH_REORDER_EN;
	bus_space_write_4(iot, ioh, ATI_REG_6CH_REORDER, value);

	if (sc->has_spdif) {
		/* set SPDIF (if present) */
		value  =  bus_space_read_4(iot, ioh, ATI_REG_CMD);
		value &= ~ATI_REG_CMD_SPDF_CONFIG_MASK;
		value |=  ATI_REG_CMD_SPDF_CONFIG_34; /* NetBSD AC'97 default */

		/* XXX this is probably not necessary unless split XXX */
		value &= ~ATI_REG_CMD_INTERLEAVE_SPDF;
		if (params->precision <= 16)
			value |= ATI_REG_CMD_INTERLEAVE_SPDF;
		bus_space_write_4(iot, ioh, ATI_REG_CMD, value);
	}

	return 0;
}


/* set audio properties in desired setting */
int
auixp_set_params(void *hdl, int setmode, int usemode,
    struct audio_params *play, struct audio_params *rec)
{
	struct auixp_codec *co;
	int error;
	u_int temprate;

	co = (struct auixp_codec *) hdl;
	if (setmode & AUMODE_PLAY) {
		play->channels = 2;
		play->precision = 16;
		switch(play->encoding) {
		case AUDIO_ENCODING_SLINEAR_LE:
			break;
		default:
			return (EINVAL);
		}
		play->bps = AUDIO_BPS(play->precision);
		play->msb = 1;

		temprate = play->sample_rate;
		error = ac97_set_rate(co->codec_if,
		    AC97_REG_PCM_LFE_DAC_RATE, &play->sample_rate);
		if (error)
			return (error);

		play->sample_rate = temprate;
		error = ac97_set_rate(co->codec_if,
		    AC97_REG_PCM_SURR_DAC_RATE, &play->sample_rate);
		if (error)
			return (error);

		play->sample_rate = temprate;
		error = ac97_set_rate(co->codec_if,
		    AC97_REG_PCM_FRONT_DAC_RATE, &play->sample_rate);
		if (error)
			return (error);

	}

	if (setmode & AUMODE_RECORD) {		
		rec->channels = 2;
		rec->precision = 16;
		switch(rec->encoding) {
		case AUDIO_ENCODING_SLINEAR_LE:
			break;
		default:
			return (EINVAL);
		}
		rec->bps = AUDIO_BPS(rec->precision);
		rec->msb = 1;

		error = ac97_set_rate(co->codec_if, AC97_REG_PCM_LR_ADC_RATE,
		    &rec->sample_rate);
		if (error)
			return (error);
	}

	return (0);
}


/* called to translate a requested blocksize to a hw-possible one */
int
auixp_round_blocksize(void *v, int blk)
{

	blk = (blk + 0x1f) & ~0x1f;
	/* Be conservative; align to 32 bytes and maximise it to 64 kb */
	if (blk > 0x10000)
		blk = 0x10000;

	return blk;
}


/*
 * allocate dma capable memory and record its information for later retrieval
 * when we program the dma chain itself. The trigger routines passes on the
 * kernel virtual address we return here as a reference to the mapping.
 */
void *
auixp_malloc(void *hdl, int direction, size_t size, int pool, int flags)
{
	struct auixp_codec *co;
	struct auixp_softc *sc;
	struct auixp_dma *dma;
	int error;

	co = (struct auixp_codec *) hdl;
	sc = co->sc;
	/* get us a auixp_dma structure */
	dma = malloc(sizeof(*dma), pool, flags);
	if (!dma)
		return NULL;

	/* get us a dma buffer itself */
	error = auixp_allocmem(sc, size, 16, dma);
	if (error) {
		free(dma, pool, sizeof(*dma));
		printf("%s: auixp_malloc: not enough memory\n",
		    sc->sc_dev.dv_xname);
		return NULL;
	}
	SLIST_INSERT_HEAD(&sc->sc_dma_list, dma, dma_chain);

	DPRINTF(("auixp_malloc: returning kern %p,   hw 0x%08x for %zu bytes "
	    "in %d segs\n", KERNADDR(dma), (u_int32_t) DMAADDR(dma), dma->size,
	    dma->nsegs)
	);

	return KERNADDR(dma);
}

/*
 * free and release dma capable memory we allocated before and remove its
 * recording
 */
void
auixp_free(void *hdl, void *addr, int pool)
{
	struct auixp_codec *co;
	struct auixp_softc *sc;
	struct auixp_dma *dma;

	co = (struct auixp_codec *) hdl;
	sc = co->sc;
	SLIST_FOREACH(dma, &sc->sc_dma_list, dma_chain) {
		if (KERNADDR(dma) == addr) {
			SLIST_REMOVE(&sc->sc_dma_list, dma, auixp_dma,
			    dma_chain);
			auixp_freemem(sc, dma);
			free(dma, pool, sizeof(*dma));
			return;
		}
	}
}

/* pass request to AC'97 codec code */
int
auixp_set_port(void *hdl, mixer_ctrl_t *mc)
{
	struct auixp_codec *co;

	co = (struct auixp_codec *) hdl;
	return co->codec_if->vtbl->mixer_set_port(co->codec_if, mc);
}


/* pass request to AC'97 codec code */
int
auixp_get_port(void *hdl, mixer_ctrl_t *mc)
{
	struct auixp_codec *co;

	co = (struct auixp_codec *) hdl;
	return co->codec_if->vtbl->mixer_get_port(co->codec_if, mc);
}

/* pass request to AC'97 codec code */
int
auixp_query_devinfo(void *hdl, mixer_devinfo_t *di)
{
	struct auixp_codec *co;

	co = (struct auixp_codec *) hdl;
	return co->codec_if->vtbl->query_devinfo(co->codec_if, di);
}


/*
 * A dma descriptor has dma->nsegs segments defined in dma->segs set up when
 * we claimed the memory.
 *
 * Due to our demand for one contiguous DMA area, we only have one segment. A
 * c_dma structure is about 3 kb for the 256 entries we maximally program
 * -arbitrary limit AFAIK- so all is most likely to be in one segment/page
 * anyway.
 *
 * XXX ought to implement fragmented dma area XXX
 *
 * Note that _v variables depict kernel virtual addresses, _p variables depict
 * physical addresses.
 */
void
auixp_link_daisychain(struct auixp_softc *sc,
		struct auixp_dma *c_dma, struct auixp_dma *s_dma,
		int blksize, int blocks)
{
	atiixp_dma_desc_t *caddr_v, *next_caddr_v;
	u_int32_t caddr_p, next_caddr_p, saddr_p;
	int i;

	/* just make sure we are not changing when its running */
	auixp_disable_dma(sc, c_dma);

	/* setup dma chain start addresses */
	caddr_v = KERNADDR(c_dma);
	caddr_p = DMAADDR(c_dma);
	saddr_p = DMAADDR(s_dma);

	/* program the requested number of blocks */
	for (i = 0; i < blocks; i++) {
		/* clear the block just in case */
		bzero(caddr_v, sizeof(atiixp_dma_desc_t));

		/* round robin the chain dma addresses for its successor */
		next_caddr_v = caddr_v + 1;
		next_caddr_p = caddr_p + sizeof(atiixp_dma_desc_t);

		if (i == blocks-1) {
			next_caddr_v = KERNADDR(c_dma);
			next_caddr_p = DMAADDR(c_dma);
		}

		/* fill in the hardware dma chain descriptor in little-endian */
		caddr_v->addr   = htole32(saddr_p);
		caddr_v->status = htole16(0);
		caddr_v->size   = htole16((blksize >> 2)); /* in dwords (!!!) */
		caddr_v->next   = htole32(next_caddr_p);

		/* advance slot */
		saddr_p += blksize;	/* XXX assuming contiguous XXX */
		caddr_v  = next_caddr_v;
		caddr_p  = next_caddr_p;
	}
}


int
auixp_allocate_dma_chain(struct auixp_softc *sc, struct auixp_dma **dmap)
{
	struct auixp_dma *dma;
	int error;

	/* allocate keeper of dma area */
	*dmap = NULL;
	dma = malloc(sizeof(*dma), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (!dma)
		return ENOMEM;

	/* allocate for daisychain of IXP hardware-dma descriptors */
	error = auixp_allocmem(sc, DMA_DESC_CHAIN * sizeof(atiixp_dma_desc_t),
	    16, dma);
	if (error) {
		printf("%s: can't malloc dma descriptor chain\n",
		    sc->sc_dev.dv_xname);
		free(dma, M_DEVBUF, sizeof(*dma));
		return ENOMEM;
	}

	/* return info and initialise structure */
	dma->intr    = NULL;
	dma->intrarg = NULL;

	*dmap = dma;
	return 0;
}


/* program dma chain in its link address descriptor */
void
auixp_program_dma_chain(struct auixp_softc *sc, struct auixp_dma *dma)
{
	bus_space_tag_t    iot;
	bus_space_handle_t ioh;
	u_int32_t value;

	iot = sc->sc_iot;
	ioh = sc->sc_ioh;
	/* get hardware start address of DMA chain and set valid-flag in it */
	/* XXX always at start? XXX */
	value = DMAADDR(dma);
	value = value | ATI_REG_LINKPTR_EN;

	/* reset linkpointer */
	bus_space_write_4(iot, ioh, dma->linkptr, 0);

	/* reset this DMA engine */
	auixp_disable_dma(sc, dma);
	auixp_enable_dma(sc, dma);

	/* program new DMA linkpointer */
	bus_space_write_4(iot, ioh, dma->linkptr, value);
}


/* called from interrupt code to signal end of one dma-slot */
void
auixp_dma_update(struct auixp_softc *sc, struct auixp_dma *dma)
{

	/* be very paranoid */
	if (!dma)
		panic("auixp: update: dma = NULL");
	if (!dma->intr)
		panic("auixp: update: dma->intr = NULL");

	/* request more input from upper layer */
	(*dma->intr)(dma->intrarg);
}


/*
 * The magic `busbusy' bit that needs to be set when dma is active; allowing
 * busmastering?
 */
void
auixp_update_busbusy(struct auixp_softc *sc)
{
	bus_space_tag_t    iot;
	bus_space_handle_t ioh;
	u_int32_t value;
	int running;

	iot = sc->sc_iot;
	ioh = sc->sc_ioh;
	/* set bus-busy flag when either recording or playing is performed */
	value  = bus_space_read_4(iot, ioh, ATI_REG_IER);
	value &= ~ATI_REG_IER_SET_BUS_BUSY;

	running = ((sc->sc_output_dma->running) || (sc->sc_input_dma->running));
	if (running)
		value |= ATI_REG_IER_SET_BUS_BUSY;

	bus_space_write_4(iot, ioh, ATI_REG_IER, value);

}


/*
 * Called from upper audio layer to request playing audio, only called once;
 * audio is refilled by calling the intr() function when space is available
 * again.
 */
/* XXX almost literally a copy of trigger-input; could be factorised XXX */
int
auixp_trigger_output(void *hdl, void *start, void *end, int blksize,
    void (*intr)(void *), void *intrarg, struct audio_params *param)
{
	struct auixp_codec *co;
	struct auixp_softc *sc;
	struct auixp_dma   *chain_dma;
	struct auixp_dma   *sound_dma;
	u_int32_t blocks;

	co = (struct auixp_codec *) hdl;
	sc = co->sc;
	chain_dma = sc->sc_output_dma;
	/* add functions to call back */
	chain_dma->intr    = intr;
	chain_dma->intrarg = intrarg;

	/*
	 * Program output DMA chain with blocks from [start...end] with
	 * blksize fragments.
	 *
	 * NOTE, we can assume its in one block since we asked for it to be in
	 * one contiguous blob; XXX change this? XXX
	 */
	blocks = (size_t) (((caddr_t) end) - ((caddr_t) start)) / blksize;

	/* lookup `start' address in our list of DMA area's */
	SLIST_FOREACH(sound_dma, &sc->sc_dma_list, dma_chain) {
		if (KERNADDR(sound_dma) == start)
			break;
	}

	/* not ours ? then bail out */
	if (!sound_dma) {
		printf("%s: auixp_trigger_output: bad sound addr %p\n",
		    sc->sc_dev.dv_xname, start);
		return EINVAL;
	}

	/* link round-robin daisychain and program hardware */
	auixp_link_daisychain(sc, chain_dma, sound_dma, blksize, blocks);
	auixp_program_dma_chain(sc, chain_dma);

	/* mark we are now able to run now */
	mtx_enter(&audio_lock);
	chain_dma->running = 1;

	/* update bus-flags; XXX programs more flags XXX */
	auixp_update_busbusy(sc);
	mtx_leave(&audio_lock);

	/* callbacks happen in interrupt routine */
	return 0;
}


/* halt output of audio, just disable its dma and update bus state */
int
auixp_halt_output(void *hdl)
{
	struct auixp_codec *co;
	struct auixp_softc *sc;
	struct auixp_dma   *dma;

	mtx_enter(&audio_lock);
	co  = (struct auixp_codec *) hdl;
	sc  = co->sc;
	dma = sc->sc_output_dma;
	auixp_disable_dma(sc, dma);

	dma->running = 0;
	auixp_update_busbusy(sc);
	mtx_leave(&audio_lock);
	return 0;
}


/* XXX almost literally a copy of trigger-output; could be factorised XXX */
int
auixp_trigger_input(void *hdl, void *start, void *end, int blksize,
    void (*intr)(void *), void *intrarg, struct audio_params *param)
{
	struct auixp_codec *co;
	struct auixp_softc *sc;
	struct auixp_dma   *chain_dma;
	struct auixp_dma   *sound_dma;
	u_int32_t blocks;

	co = (struct auixp_codec *) hdl;
	sc = co->sc;
	chain_dma = sc->sc_input_dma;
	/* add functions to call back */
	chain_dma->intr    = intr;
	chain_dma->intrarg = intrarg;

	/*
	 * Program output DMA chain with blocks from [start...end] with
	 * blksize fragments.
	 *
	 * NOTE, we can assume its in one block since we asked for it to be in
	 * one contiguous blob; XXX change this? XXX
	 */
	blocks = (size_t) (((caddr_t) end) - ((caddr_t) start)) / blksize;

	/* lookup `start' address in our list of DMA area's */
	SLIST_FOREACH(sound_dma, &sc->sc_dma_list, dma_chain) {
		if (KERNADDR(sound_dma) == start)
			break;
	}

	/* not ours ? then bail out */
	if (!sound_dma) {
		printf("%s: auixp_trigger_input: bad sound addr %p\n",
		    sc->sc_dev.dv_xname, start);
		return EINVAL;
	}

	/* link round-robin daisychain and program hardware */
	auixp_link_daisychain(sc, chain_dma, sound_dma, blksize, blocks);
	auixp_program_dma_chain(sc, chain_dma);

	/* mark we are now able to run now */
	mtx_enter(&audio_lock);
	chain_dma->running = 1;

	/* update bus-flags; XXX programs more flags XXX */
	auixp_update_busbusy(sc);
	mtx_leave(&audio_lock);

	/* callbacks happen in interrupt routine */
	return 0;
}


/* halt sampling audio, just disable its dma and update bus state */
int
auixp_halt_input(void *hdl)
{
	struct auixp_codec *co;
	struct auixp_softc *sc;
	struct auixp_dma   *dma;

	mtx_enter(&audio_lock);
	co = (struct auixp_codec *) hdl;
	sc = co->sc;
	dma = sc->sc_input_dma;
	auixp_disable_dma(sc, dma);

	dma->running = 0;
	auixp_update_busbusy(sc);

	mtx_leave(&audio_lock);
	return 0;
}


/*
 * IXP audio interrupt handler
 *
 * note that we return the number of bits handled; the return value is not
 * documented but I saw it implemented in other drivers. Probably returning a
 * value > 0 means "I've dealt with it"
 *
 */
int
auixp_intr(void *softc)
{
	struct auixp_softc *sc;
	bus_space_tag_t    iot;
	bus_space_handle_t ioh;
	u_int32_t status, enable, detected_codecs;
	int ret;

	mtx_enter(&audio_lock);
	sc = softc;
	iot = sc->sc_iot;
	ioh = sc->sc_ioh;
	ret = 0;
	/* get status from the interrupt status register */
	status = bus_space_read_4(iot, ioh, ATI_REG_ISR);

	if (status == 0) {
		mtx_leave(&audio_lock);
		return 0;
	}

	DPRINTF(("%s: (status = %x)\n", sc->sc_dev.dv_xname, status));

	/* check DMA UPDATE flags for input & output */
	if (status & ATI_REG_ISR_IN_STATUS) {
		ret++; DPRINTF(("IN_STATUS\n"));
		auixp_dma_update(sc, sc->sc_input_dma);
	}
	if (status & ATI_REG_ISR_OUT_STATUS) {
		ret++; DPRINTF(("OUT_STATUS\n"));
		auixp_dma_update(sc, sc->sc_output_dma);
	}

	/* XXX XRUN flags not used/needed yet; should i implement it? XXX */
	/* acknowledge the interrupts nevertheless */
	if (status & ATI_REG_ISR_IN_XRUN) {
		ret++; DPRINTF(("IN_XRUN\n"));
		/* auixp_dma_xrun(sc, sc->sc_input_dma);  */
	}
	if (status & ATI_REG_ISR_OUT_XRUN) {
		ret++; DPRINTF(("OUT_XRUN\n"));
		/* auixp_dma_xrun(sc, sc->sc_output_dma); */
	}

	/* check if we are looking for codec detection */
	if (status & CODEC_CHECK_BITS) {
		ret++;
		/* mark missing codecs as not ready */
		detected_codecs = status & CODEC_CHECK_BITS;
		sc->sc_codec_not_ready_bits |= detected_codecs;

		/* disable detected interrupt sources */
		enable  = bus_space_read_4(iot, ioh, ATI_REG_IER);
		enable &= ~detected_codecs;
		bus_space_write_4(iot, ioh, ATI_REG_IER, enable);
	}

	/* acknowledge interrupt sources */
	bus_space_write_4(iot, ioh, ATI_REG_ISR, status);
	mtx_leave(&audio_lock);
	return ret;
}


/* allocate memory for dma purposes; on failure of any of the steps, roll back */
int
auixp_allocmem(struct auixp_softc *sc, size_t size,
	       size_t align, struct auixp_dma *dma)
{
	int error;

	/* remember size */
	dma->size = size;

	/* allocate DMA safe memory but in just one segment for now :( */
	error = bus_dmamem_alloc(sc->sc_dmat, dma->size, align, 0,
	    dma->segs, sizeof(dma->segs) / sizeof(dma->segs[0]), &dma->nsegs,
	    BUS_DMA_NOWAIT);
	if (error)
		return error;

	/*
	 * map allocated memory into kernel virtual address space and keep it
	 * coherent with the CPU.
	 */
	error = bus_dmamem_map(sc->sc_dmat, dma->segs, dma->nsegs, dma->size,
				&dma->addr, BUS_DMA_NOWAIT | BUS_DMA_COHERENT);
	if (error)
		goto free;

	/* allocate associated dma handle and initialize it. */
	error = bus_dmamap_create(sc->sc_dmat, dma->size, 1, dma->size, 0,
				  BUS_DMA_NOWAIT, &dma->map);
	if (error)
		goto unmap;

	/*
	 * load the dma handle with mappings for a dma transfer; all pages
	 * need to be wired.
	 */
	error = bus_dmamap_load(sc->sc_dmat, dma->map, dma->addr, dma->size, NULL,
				BUS_DMA_NOWAIT);
	if (error)
		goto destroy;

	return 0;

destroy:
	bus_dmamap_destroy(sc->sc_dmat, dma->map);
unmap:
	bus_dmamem_unmap(sc->sc_dmat, dma->addr, dma->size);
free:
	bus_dmamem_free(sc->sc_dmat, dma->segs, dma->nsegs);

	return error;
}


/* undo dma mapping and release memory allocated */
int
auixp_freemem(struct auixp_softc *sc, struct auixp_dma *p)
{

	bus_dmamap_unload(sc->sc_dmat, p->map);
	bus_dmamap_destroy(sc->sc_dmat, p->map);
	bus_dmamem_unmap(sc->sc_dmat, p->addr, p->size);
	bus_dmamem_free(sc->sc_dmat, p->segs, p->nsegs);

	return 0;
}

int
auixp_match(struct device *dev, void *match, void *aux)
{
	return (pci_matchbyid((struct pci_attach_args *)aux, auixp_pci_devices,
	    sizeof(auixp_pci_devices)/sizeof(auixp_pci_devices[0])));
}

int
auixp_activate(struct device *self, int act)
{
	struct auixp_softc *sc = (struct auixp_softc *)self;
	int rv = 0;

	switch (act) {
	case DVACT_SUSPEND:
		rv = config_activate_children(self, act);
		auixp_disable_interrupts(sc);
		break;
	case DVACT_RESUME:
		auixp_init(sc);
		ac97_resume(&sc->sc_codec.host_if, sc->sc_codec.codec_if);
		rv = config_activate_children(self, act);
		break;
	default:
		rv = config_activate_children(self, act);
		break;
	}
	return (rv);
}

void
auixp_attach(struct device *parent, struct device *self, void *aux)
{
	struct auixp_softc *sc;
	struct pci_attach_args *pa;
	pcitag_t tag;
	pci_chipset_tag_t pc;
	pci_intr_handle_t ih;
	const char *intrstr;

	sc = (struct auixp_softc *)self;
	pa = (struct pci_attach_args *)aux;
	tag = pa->pa_tag;
	pc = pa->pa_pc;

	/* map memory; its not sized -> what is the size? max PCI slot size? */
	if (pci_mapreg_map(pa, PCI_CBIO, PCI_MAPREG_TYPE_MEM, 0,
	    &sc->sc_iot, &sc->sc_ioh, &sc->sc_iob, &sc->sc_ios, 0)) {
		printf(": can't map mem space\n");
		return;
	}

	/* Initialize softc */
	sc->sc_tag = tag;
	sc->sc_pct = pc;
	sc->sc_dmat = pa->pa_dmat;
	SLIST_INIT(&sc->sc_dma_list);

	/* get us the auixp_dma structures */
	auixp_allocate_dma_chain(sc, &sc->sc_output_dma);
	auixp_allocate_dma_chain(sc, &sc->sc_input_dma);

	/* when that fails we are dead in the water */
	if (!sc->sc_output_dma || !sc->sc_input_dma)
		return;

#if 0
	/* could preliminary program DMA chain */
	auixp_program_dma_chain(sc, sc->sc_output_dma);
	auixp_program_dma_chain(sc, sc->sc_input_dma);
#endif

	if (pci_intr_map(pa, &ih)) {
		printf(": can't map interrupt\n");
		return;
	}
	intrstr = pci_intr_string(pc, ih);
	sc->sc_ih = pci_intr_establish(pc, ih, IPL_AUDIO | IPL_MPSAFE,
	    auixp_intr, sc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		return;
	}
	printf(": %s\n", intrstr);

	/* power up chip */
	pci_set_powerstate(pc, tag, PCI_PMCSR_STATE_D0);

	/* init chip */
	if (auixp_init(sc) == -1) {
		printf("%s: auixp_attach: unable to initialize the card\n",
		    sc->sc_dev.dv_xname);
		return;
	}

	/*
	 * delay further configuration of codecs and audio after interrupts
	 * are enabled.
	 */
	config_mountroot(self, auixp_post_config);
}

/* called from autoconfigure system when interrupts are enabled */
void
auixp_post_config(struct device *self)
{
	struct auixp_softc *sc = (struct auixp_softc *)self;

	/* detect the AC97 codecs */
	auixp_autodetect_codecs(sc);

	/* Bail if no codecs attached. */
	if (!sc->sc_codec.present) {
		printf("%s: no codecs detected or initialised\n",
		    sc->sc_dev.dv_xname);
		return;
	}

	audio_attach_mi(&auixp_hw_if, &sc->sc_codec, NULL, &sc->sc_dev);

	if (sc->has_spdif)
		sc->has_spdif = 0;

	/* fill in the missing details about the dma channels. */
	/* for output */
	sc->sc_output_dma->linkptr        = ATI_REG_OUT_DMA_LINKPTR;
	sc->sc_output_dma->dma_enable_bit = ATI_REG_CMD_OUT_DMA_EN |
					    ATI_REG_CMD_SEND_EN;
	/* have spdif? then this too! XXX not seeing LED yet! XXX */
	if (sc->has_spdif)
		sc->sc_output_dma->dma_enable_bit |= ATI_REG_CMD_SPDF_OUT_EN;

	/* and for input */
	sc->sc_input_dma->linkptr         = ATI_REG_IN_DMA_LINKPTR;
	sc->sc_input_dma->dma_enable_bit  = ATI_REG_CMD_IN_DMA_EN  |
					    ATI_REG_CMD_RECEIVE_EN;

	/* done! now enable all interrupts we can service */
	auixp_enable_interrupts(sc);
}

void
auixp_enable_interrupts(struct auixp_softc *sc)
{
	bus_space_tag_t     iot;
	bus_space_handle_t  ioh;
	u_int32_t value;

	iot = sc->sc_iot;
	ioh = sc->sc_ioh;
	/* clear all pending */
	bus_space_write_4(iot, ioh, ATI_REG_ISR, 0xffffffff);

	/* enable all relevant interrupt sources we can handle */
	value = bus_space_read_4(iot, ioh, ATI_REG_IER);

	value |= ATI_REG_IER_IO_STATUS_EN;

	bus_space_write_4(iot, ioh, ATI_REG_IER, value);
}

void
auixp_disable_interrupts(struct auixp_softc *sc)
{
	bus_space_tag_t     iot;
	bus_space_handle_t  ioh;

	iot = sc->sc_iot;
	ioh = sc->sc_ioh;
	/* disable all interrupt sources */
	bus_space_write_4(iot, ioh, ATI_REG_IER, 0);

	/* clear all pending */
	bus_space_write_4(iot, ioh, ATI_REG_ISR, 0xffffffff);
}

/* dismantle what we've set up by undoing setup */
int
auixp_detach(struct device *self, int flags)
{
	struct auixp_softc *sc;

	sc = (struct auixp_softc *)self;
	/* XXX shouldn't we just reset the chip? XXX */
	/*
	 * should we explicitly disable interrupt generation and acknowledge
	 * what's left on? better be safe than sorry.
	 */
	auixp_disable_interrupts(sc);

	/* tear down .... */
	config_detach(&sc->sc_dev, flags);	/* XXX OK? XXX */

	if (sc->sc_ih != NULL)
		pci_intr_disestablish(sc->sc_pct, sc->sc_ih);
	if (sc->sc_ios)
		bus_space_unmap(sc->sc_iot, sc->sc_ioh, sc->sc_ios);
	return 0;
}


/*
 * codec handling
 *
 * IXP audio support can have upto 3 codecs! are they chained ? or
 * alternative outlets with the same audio feed i.e. with different mixer
 * settings? XXX does NetBSD support more than one audio codec? XXX
 */


int
auixp_attach_codec(void *aux, struct ac97_codec_if *codec_if)
{
	struct auixp_codec *ixp_codec;

	ixp_codec = aux;
	ixp_codec->codec_if = codec_if;

	return 0;
}

int
auixp_read_codec(void *aux, u_int8_t reg, u_int16_t *result)
{
	struct auixp_codec *co;
	struct auixp_softc *sc;
	bus_space_tag_t     iot;
	bus_space_handle_t  ioh;
	u_int32_t data;
	int timeout;

	co  = aux;
	sc  = co->sc;
	iot = sc->sc_iot;
	ioh = sc->sc_ioh;
	if (auixp_wait_for_codecs(sc, "read_codec"))
		return 0xffff;

	/* build up command for reading codec register */
	data = (reg << ATI_REG_PHYS_OUT_ADDR_SHIFT) |
		ATI_REG_PHYS_OUT_ADDR_EN |
		ATI_REG_PHYS_OUT_RW |
		co->codec_nr;

	bus_space_write_4(iot, ioh, ATI_REG_PHYS_OUT_ADDR, data);

	if (auixp_wait_for_codecs(sc, "read_codec"))
		return 0xffff;

	/* wait until codec info is clocked in */
	timeout = 500;		/* 500*2 usec -> 0.001 sec */
	do {
		data = bus_space_read_4(iot, ioh, ATI_REG_PHYS_IN_ADDR);
		if (data & ATI_REG_PHYS_IN_READ_FLAG) {
			DPRINTF(("read ac'97 codec reg 0x%x = 0x%08x\n",
				reg, data >> ATI_REG_PHYS_IN_DATA_SHIFT));
			*result = data >> ATI_REG_PHYS_IN_DATA_SHIFT;
			return 0;
		}
		DELAY(2);
		timeout--;
	} while (timeout > 0);

	if (reg < 0x7c)
		printf("%s: codec read timeout! (reg %x)\n",
		    sc->sc_dev.dv_xname, reg);

	return 0xffff;
}

int
auixp_write_codec(void *aux, u_int8_t reg, u_int16_t data)
{
	struct auixp_codec *co;
	struct auixp_softc *sc;
	bus_space_tag_t     iot;
	bus_space_handle_t  ioh;
	u_int32_t value;

	DPRINTF(("write ac'97 codec reg 0x%x = 0x%08x\n", reg, data));
	co  = aux;
	sc  = co->sc;
	iot = sc->sc_iot;
	ioh = sc->sc_ioh;
	if (auixp_wait_for_codecs(sc, "write_codec"))
		return -1;

	/* build up command for writing codec register */
	value = (((u_int32_t) data) << ATI_REG_PHYS_OUT_DATA_SHIFT) |
		(((u_int32_t)  reg) << ATI_REG_PHYS_OUT_ADDR_SHIFT) |
		ATI_REG_PHYS_OUT_ADDR_EN |
		co->codec_nr;

	bus_space_write_4(iot, ioh, ATI_REG_PHYS_OUT_ADDR, value);

	return 0;
}

void
auixp_reset_codec(void *aux)
{

	/* nothing to be done? */
}

enum ac97_host_flags
auixp_flags_codec(void *aux)
{
	struct auixp_codec *ixp_codec;

	ixp_codec = aux;
	return ixp_codec->codec_flags;
}

int
auixp_wait_for_codecs(struct auixp_softc *sc, const char *func)
{
	bus_space_tag_t      iot;
	bus_space_handle_t   ioh;
	u_int32_t value;
	int timeout;

	iot = sc->sc_iot;
	ioh = sc->sc_ioh;
	/* wait until all codec transfers are done */
	timeout = 500;		/* 500*2 usec -> 0.001 sec */
	do {
		value = bus_space_read_4(iot, ioh, ATI_REG_PHYS_OUT_ADDR);
		if ((value & ATI_REG_PHYS_OUT_ADDR_EN) == 0)
			return 0;

		DELAY(2);
		timeout--;
	} while (timeout > 0);

	printf("%s: %s: timed out\n", func, sc->sc_dev.dv_xname);
	return -1;
}

void
auixp_autodetect_codecs(struct auixp_softc *sc)
{
	bus_space_tag_t      iot;
	bus_space_handle_t   ioh;
	pcireg_t subdev;
	struct auixp_codec  *codec;
	int timeout;

	iot = sc->sc_iot;
	ioh = sc->sc_ioh;
	subdev = pci_conf_read(sc->sc_pct, sc->sc_tag, PCI_SUBSYS_ID_REG);

	/* ATI IXP can have upto 3 codecs; mark all codecs as not existing */
	sc->sc_codec_not_ready_bits = 0;

	/* enable all codecs to interrupt as well as the new frame interrupt */
	bus_space_write_4(iot, ioh, ATI_REG_IER, CODEC_CHECK_BITS);

	/* wait for the interrupts to happen */
	timeout = 100;		/* 100.000 usec -> 0.1 sec */

	while (timeout > 0) {
		DELAY(1000);
		if (sc->sc_codec_not_ready_bits)
			break;
		timeout--;
	}

	if (timeout == 0)
		printf("%s: WARNING: timeout during codec detection; "
			"codecs might be present but haven't interrupted\n",
			sc->sc_dev.dv_xname);

	/* disable all interrupts for now */
	auixp_disable_interrupts(sc);

	/* Attach AC97 host interfaces */
	codec = &sc->sc_codec;
	bzero(codec, sizeof(struct auixp_codec));

	codec->sc       = sc;

	codec->host_if.arg    = codec;
	codec->host_if.attach = auixp_attach_codec;
	codec->host_if.read   = auixp_read_codec;
	codec->host_if.write  = auixp_write_codec;
	codec->host_if.reset  = auixp_reset_codec;
	codec->host_if.flags  = auixp_flags_codec;
	switch (subdev) {
	case 0x1311462: /* MSI S270 */
	case 0x1611462: /* LG K1 Express */
	case 0x3511462: /* MSI L725 */
	case 0x4711462: /* MSI L720 */
	case 0x0611462: /* MSI S250 */
		codec->codec_flags = AC97_HOST_ALC650_PIN47_IS_EAPD;
		break;
	}

	if (!(sc->sc_codec_not_ready_bits & ATI_REG_ISR_CODEC0_NOT_READY)) {
		/* codec 0 present */
		DPRINTF(("auixp : YAY! codec 0 present!\n"));
		if (ac97_attach(&sc->sc_codec.host_if) == 0) {
			sc->sc_codec.codec_nr = 0;
			sc->sc_codec.present = 1;
			return;
		}
	}

	if (!(sc->sc_codec_not_ready_bits & ATI_REG_ISR_CODEC1_NOT_READY)) {
		/* codec 1 present */
		DPRINTF(("auixp : YAY! codec 1 present!\n"));
		if (ac97_attach(&sc->sc_codec.host_if) == 0) {
			sc->sc_codec.codec_nr = 1;
			sc->sc_codec.present = 1;
			return;
		}
	}

	if (!(sc->sc_codec_not_ready_bits & ATI_REG_ISR_CODEC2_NOT_READY)) {
		/* codec 2 present */
		DPRINTF(("auixp : YAY! codec 2 present!\n"));
		if (ac97_attach(&sc->sc_codec.host_if) == 0) {
			sc->sc_codec.codec_nr = 2;
			sc->sc_codec.present = 1;
			return;
		}
	}
}

void
auixp_disable_dma(struct auixp_softc *sc, struct auixp_dma *dma)
{
	bus_space_tag_t      iot;
	bus_space_handle_t   ioh;
	u_int32_t value;

	iot = sc->sc_iot;
	ioh = sc->sc_ioh;
	/* lets not stress the DMA engine more than necessary */
	value = bus_space_read_4(iot, ioh, ATI_REG_CMD);
	if (value & dma->dma_enable_bit) {
		value &= ~dma->dma_enable_bit;
		bus_space_write_4(iot, ioh, ATI_REG_CMD, value);
	}
}

void
auixp_enable_dma(struct auixp_softc *sc, struct auixp_dma *dma)
{
	bus_space_tag_t      iot;
	bus_space_handle_t   ioh;
	u_int32_t value;

	iot = sc->sc_iot;
	ioh = sc->sc_ioh;
	/* lets not stress the DMA engine more than necessary */
	value = bus_space_read_4(iot, ioh, ATI_REG_CMD);
	if (!(value & dma->dma_enable_bit)) {
		value |= dma->dma_enable_bit;
		bus_space_write_4(iot, ioh, ATI_REG_CMD, value);
	}
}

void
auixp_reset_aclink(struct auixp_softc *sc)
{
	bus_space_tag_t      iot;
	bus_space_handle_t   ioh;
	u_int32_t value, timeout;

	iot = sc->sc_iot;
	ioh = sc->sc_ioh;

	/* if power is down, power it up */
	value = bus_space_read_4(iot, ioh, ATI_REG_CMD);
	if (value & ATI_REG_CMD_POWERDOWN) {
		printf("%s: powering up\n", sc->sc_dev.dv_xname);

		/* explicitly enable power */
		value &= ~ATI_REG_CMD_POWERDOWN;
		bus_space_write_4(iot, ioh, ATI_REG_CMD, value);

		/* have to wait at least 10 usec for it to initialise */
		DELAY(20);
	}

	printf("%s: soft resetting aclink\n", sc->sc_dev.dv_xname);

	/* perform a soft reset */
	value  = bus_space_read_4(iot, ioh, ATI_REG_CMD);
	value |= ATI_REG_CMD_AC_SOFT_RESET;
	bus_space_write_4(iot, ioh, ATI_REG_CMD, value);

	/* need to read the CMD reg and wait aprox. 10 usec to init */
	value  = bus_space_read_4(iot, ioh, ATI_REG_CMD);
	DELAY(20);

	/* clear soft reset flag again */
	value  = bus_space_read_4(iot, ioh, ATI_REG_CMD);
	value &= ~ATI_REG_CMD_AC_SOFT_RESET;
	bus_space_write_4(iot, ioh, ATI_REG_CMD, value);

	/* check if the ac-link is working; reset device otherwise */
	timeout = 10;
	value = bus_space_read_4(iot, ioh, ATI_REG_CMD);
	while (!(value & ATI_REG_CMD_ACLINK_ACTIVE)) {
		printf("%s: not up; resetting aclink hardware\n",
				sc->sc_dev.dv_xname);

		/* dip aclink reset but keep the acsync */
		value &= ~ATI_REG_CMD_AC_RESET;
		value |=  ATI_REG_CMD_AC_SYNC;
		bus_space_write_4(iot, ioh, ATI_REG_CMD, value);

		/* need to read CMD again and wait again (clocking in issue?) */
		value = bus_space_read_4(iot, ioh, ATI_REG_CMD);
		DELAY(20);

		/* assert aclink reset again */
		value = bus_space_read_4(iot, ioh, ATI_REG_CMD);
		value |=  ATI_REG_CMD_AC_RESET;
		bus_space_write_4(iot, ioh, ATI_REG_CMD, value);

		/* check if its active now */
		value = bus_space_read_4(iot, ioh, ATI_REG_CMD);

		timeout--;
		if (timeout == 0) break;
	}

	if (timeout == 0) {
		printf("%s: giving up aclink reset\n", sc->sc_dev.dv_xname);
	}
	if (timeout != 10) {
		printf("%s: aclink hardware reset successful\n",
			sc->sc_dev.dv_xname);
	}

	/* assert reset and sync for safety */
	value  = bus_space_read_4(iot, ioh, ATI_REG_CMD);
	value |= ATI_REG_CMD_AC_SYNC | ATI_REG_CMD_AC_RESET;
	bus_space_write_4(iot, ioh, ATI_REG_CMD, value);
}

/* chip hard init */
int
auixp_init(struct auixp_softc *sc)
{
	bus_space_tag_t      iot;
	bus_space_handle_t   ioh;
	u_int32_t value;

	iot = sc->sc_iot;
	ioh = sc->sc_ioh;
	/* disable all interrupts and clear all sources */
	auixp_disable_interrupts(sc);

	/* clear all DMA enables (preserving rest of settings) */
	value = bus_space_read_4(iot, ioh, ATI_REG_CMD);
	value &= ~( ATI_REG_CMD_IN_DMA_EN  |
		    ATI_REG_CMD_OUT_DMA_EN |
		    ATI_REG_CMD_SPDF_OUT_EN );
	bus_space_write_4(iot, ioh, ATI_REG_CMD, value);

	/* Reset AC-link */
	auixp_reset_aclink(sc);

	/*
	 * codecs get auto-detected later
	 *
	 * note: we are NOT enabling interrupts yet, no codecs have been
	 * detected yet nor is anything else set up
	 */

	return 0;
}
