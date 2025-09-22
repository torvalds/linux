/*	$OpenBSD: esa.c,v 1.44 2024/08/18 14:42:56 deraadt Exp $	*/
/* $NetBSD: esa.c,v 1.12 2002/03/24 14:17:35 jmcneill Exp $ */

/*
 * Copyright (c) 2001, 2002 Jared D. McNeill <jmcneill@invisible.ca>
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
 * Shamelessly stolen from NetBSD who based it on FreeBSD's who in turn
 * based it on Linux's driver.  What a wonderful world.
 *
 *
 * ESS Allegro-1 / Maestro3 Audio Driver
 *
 * Based on the FreeBSD maestro3 driver and the NetBSD eap driver.
 * Original driver by Don Kim.
 *
 * The list management code could possibly be written better, but what
 * we have right now does the job nicely. Thanks to Zach Brown <zab@zabbo.net>
 * and Andrew MacDonald <amac@epsilon.yi.org> for helping me debug the
 * problems with the original list management code present in the Linux
 * driver.
 */

#include <sys/errno.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/audioio.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/pci/pcidevs.h>
#include <dev/pci/pcivar.h>

#include <dev/audio_if.h>
#include <dev/ic/ac97.h>

#include <dev/pci/esareg.h>
#include <dev/pci/esavar.h>
#include <dev/microcode/esa/esadsp.h>

#define PCI_CBIO	0x10

#define ESA_DAC_DATA	0x1100

enum {
	ESS_ALLEGRO1,
	ESS_MAESTRO3
};

static struct esa_card_type {
	u_int16_t pci_vendor_id;
	u_int16_t pci_product_id;
	int type;
	int delay1, delay2;
} esa_card_types[] = {
	{ PCI_VENDOR_ESSTECH, PCI_PRODUCT_ESSTECH_ES1989,
	  ESS_ALLEGRO1, 50, 800 },
	{ PCI_VENDOR_ESSTECH, PCI_PRODUCT_ESSTECH_MAESTRO3,
	  ESS_MAESTRO3, 20, 500 },
	{ PCI_VENDOR_ESSTECH, PCI_PRODUCT_ESSTECH_MAESTRO3_2,
	  ESS_MAESTRO3, 20, 500 },
	{ 0, 0, 0, 0, 0 }
};

int		esa_match(struct device *, void *, void *);
void		esa_attach(struct device *, struct device *, void *);
int		esa_detach(struct device *, int);
int		esa_activate(struct device *, int);

/* audio(9) functions */
int		esa_open(void *, int);
void		esa_close(void *);
int		esa_set_params(void *, int, int, struct audio_params *,
			       struct audio_params *);
int		esa_round_blocksize(void *, int);
int		esa_commit_settings(void *);
int		esa_halt_output(void *);
int		esa_halt_input(void *);
int		esa_set_port(void *, mixer_ctrl_t *);
int		esa_get_port(void *, mixer_ctrl_t *);
int		esa_query_devinfo(void *, mixer_devinfo_t *);
void *		esa_malloc(void *, int, size_t, int, int);
void		esa_free(void *, void *, int);
size_t		esa_round_buffersize(void *, int, size_t);
int		esa_trigger_output(void *, void *, void *, int,
				   void (*)(void *), void *,
				   struct audio_params *);
int		esa_trigger_input(void *, void *, void *, int,
				  void (*)(void *), void *,
				  struct audio_params *);

int		esa_intr(void *);
int		esa_allocmem(struct esa_softc *, size_t, size_t,
			     struct esa_dma *);
int		esa_freemem(struct esa_softc *, struct esa_dma *);

/* Supporting subroutines */
u_int16_t	esa_read_assp(struct esa_softc *, u_int16_t, u_int16_t);
void		esa_write_assp(struct esa_softc *, u_int16_t, u_int16_t,
			       u_int16_t);
int		esa_init_codec(struct esa_softc *);
int		esa_attach_codec(void *, struct ac97_codec_if *);
int		esa_read_codec(void *, u_int8_t, u_int16_t *);
int		esa_write_codec(void *, u_int8_t, u_int16_t);
void		esa_reset_codec(void *);
enum ac97_host_flags	esa_flags_codec(void *);
int		esa_wait(struct esa_softc *);
int		esa_init(struct esa_softc *);
void		esa_config(struct esa_softc *);
u_int8_t	esa_assp_halt(struct esa_softc *);
void		esa_codec_reset(struct esa_softc *);
int		esa_amp_enable(struct esa_softc *);
void		esa_enable_interrupts(struct esa_softc *);
u_int32_t	esa_get_pointer(struct esa_softc *, struct esa_channel *);

/* list management */
int		esa_add_list(struct esa_voice *, struct esa_list *, u_int16_t,
			     int);
void		esa_remove_list(struct esa_voice *, struct esa_list *, int);

/* power management */
void		esa_suspend(struct esa_softc *);
void		esa_resume(struct esa_softc *);

const struct audio_hw_if esa_hw_if = {
	.open = esa_open,
	.close = esa_close,
	.set_params = esa_set_params,
	.round_blocksize = esa_round_blocksize,
	.commit_settings = esa_commit_settings,
	.halt_output = esa_halt_output,
	.halt_input = esa_halt_input,
	.set_port = esa_set_port,
	.get_port = esa_get_port,
	.query_devinfo = esa_query_devinfo,
	.allocm = esa_malloc,
	.freem = esa_free,
	.round_buffersize = esa_round_buffersize,
	.trigger_output = esa_trigger_output,
	.trigger_input = esa_trigger_input,
};

struct cfdriver esa_cd = {
	NULL, "esa", DV_DULL
};

const struct cfattach esa_ca = {
	sizeof(struct esa_softc), esa_match, esa_attach,
	esa_detach, esa_activate
};

/*
 * audio(9) functions
 */

int
esa_open(void *hdl, int flags)
{

	return (0);
}

void
esa_close(void *hdl)
{

	return;
}

int
esa_set_params(void *hdl, int setmode, int usemode, struct audio_params *play,
	       struct audio_params *rec)
{
	struct esa_voice *vc = hdl;
	struct esa_channel *ch;
	struct audio_params *p;
	int mode;

	for (mode = AUMODE_RECORD; mode != -1;
	     mode = (mode == AUMODE_RECORD) ? AUMODE_PLAY : -1) {
		if ((setmode & mode) == 0)
			continue;

		switch (mode) {
		case AUMODE_PLAY:
			p = play;
			ch = &vc->play;
			break;
		case AUMODE_RECORD:
			p = rec;
			ch = &vc->rec;
			break;
		}

		if (p->sample_rate < ESA_MINRATE)
			p->sample_rate = ESA_MINRATE;
		if (p->sample_rate > ESA_MAXRATE)
			p->sample_rate = ESA_MAXRATE;
		if (p->precision > 16)
			p->precision = 16;
		if (p->channels > 2)
			p->channels = 2;

		switch(p->encoding) {
		case AUDIO_ENCODING_SLINEAR_LE:
			if (p->precision != 16)
				return EINVAL;
			break;
		case AUDIO_ENCODING_ULINEAR_LE:
		case AUDIO_ENCODING_ULINEAR_BE:
			if (p->precision != 8)
				return EINVAL;
			break;
		default:
			return (EINVAL);
		}
		p->bps = AUDIO_BPS(p->precision);
		p->msb = 1;

		ch->mode = *p;
	}

	return (0);
}

int
esa_commit_settings(void *hdl)
{
	struct esa_voice *vc = hdl;
	struct esa_softc *sc = (struct esa_softc *)vc->parent;
	struct audio_params *p = &vc->play.mode;
	struct audio_params *r = &vc->rec.mode;
	u_int32_t data;
	u_int32_t freq;
	int data_bytes = (((ESA_MINISRC_TMP_BUFFER_SIZE & ~1) +
			   (ESA_MINISRC_IN_BUFFER_SIZE & ~1) +
			   (ESA_MINISRC_OUT_BUFFER_SIZE & ~1) + 4) + 255)
			   &~ 255;

	/* playback */
	vc->play.data_offset = ESA_DAC_DATA + (data_bytes * vc->index);
	if (p->channels == 1)
		data = 1;
	else
		data = 0;
	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA,
		       vc->play.data_offset + ESA_SRC3_MODE_OFFSET,
		       data);
	if (p->precision == 8)
		data = 1;
	else
		data = 0;
	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA,
		       vc->play.data_offset + ESA_SRC3_WORD_LENGTH_OFFSET,
		       data);
	if ((freq = ((p->sample_rate << 15) + 24000) / 48000) != 0) {
		freq--;
	}
	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA,
		       vc->play.data_offset + ESA_CDATA_FREQUENCY, freq);

	/* recording */
	vc->rec.data_offset = ESA_DAC_DATA + (data_bytes * vc->index) +
			      (data_bytes / 2);
	if (r->channels == 1)
		data = 1;
	else
		data = 0;
	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA,
		       vc->rec.data_offset + ESA_SRC3_MODE_OFFSET,
		       data);
	if (r->precision == 8)
		data = 1;
	else
		data = 0;
	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA,
		       vc->rec.data_offset + ESA_SRC3_WORD_LENGTH_OFFSET,
		       data);
	if ((freq = ((r->sample_rate << 15) + 24000) / 48000) != 0) {
		freq--;
	}
	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA,
		       vc->rec.data_offset + ESA_CDATA_FREQUENCY, freq);

	return (0);
};

int
esa_round_blocksize(void *hdl, int bs)
{
	struct esa_voice *vc = hdl;

	/*
	 * Surely there has to be a better solution...
	 */
	vc->play.blksize = vc->rec.blksize = 4096;

	return (vc->play.blksize);
}

int
esa_halt_output(void *hdl)
{
	struct esa_voice *vc = hdl;
	struct esa_softc *sc = (struct esa_softc *)vc->parent;
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	u_int16_t data;

	if (vc->play.active == 0)
		return (0);

	mtx_enter(&audio_lock);
	vc->play.active = 0;

	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA,
		       ESA_CDATA_INSTANCE_READY + vc->play.data_offset, 0);

	sc->sc_ntimers--;
	if (sc->sc_ntimers == 0) {
		esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA,
			       ESA_KDATA_TIMER_COUNT_RELOAD, 0);
		esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA,
			       ESA_KDATA_TIMER_COUNT_CURRENT, 0);
		data = bus_space_read_2(iot, ioh, ESA_HOST_INT_CTRL);
		bus_space_write_2(iot, ioh, ESA_HOST_INT_CTRL,
		    data & ~ESA_CLKRUN_GEN_ENABLE);
	}

	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA,
		       ESA_KDATA_MIXER_TASK_NUMBER,
		       sc->mixer_list.indexmap[vc->index]);
	/* remove ourselves from the packed lists */
	esa_remove_list(vc, &sc->mixer_list, vc->index);
	esa_remove_list(vc, &sc->dma_list, vc->index);
	esa_remove_list(vc, &sc->msrc_list, vc->index);
	mtx_leave(&audio_lock);
	return (0);
}

int
esa_halt_input(void *hdl)
{
	struct esa_voice *vc = hdl;
	struct esa_softc *sc = (struct esa_softc *)vc->parent;
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	u_int32_t data;

	if (vc->rec.active == 0)
		return (0);

	mtx_enter(&audio_lock);
	vc->rec.active = 0;

	sc->sc_ntimers--;
	if (sc->sc_ntimers == 0) {
		esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA,
			       ESA_KDATA_TIMER_COUNT_RELOAD, 0);
		esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA,
			       ESA_KDATA_TIMER_COUNT_CURRENT, 0);
		data = bus_space_read_2(iot, ioh, ESA_HOST_INT_CTRL);
		bus_space_write_2(iot, ioh, ESA_HOST_INT_CTRL,
				  data & ~ESA_CLKRUN_GEN_ENABLE);
	}

	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA, vc->rec.data_offset +
		       ESA_CDATA_INSTANCE_READY, 0);
	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA, ESA_KDATA_ADC1_REQUEST,
		       0);

	/* remove ourselves from the packed lists */
	esa_remove_list(vc, &sc->adc1_list, vc->index + ESA_NUM_VOICES);
	esa_remove_list(vc, &sc->dma_list, vc->index + ESA_NUM_VOICES);
	esa_remove_list(vc, &sc->msrc_list, vc->index + ESA_NUM_VOICES);
	mtx_leave(&audio_lock);
	return (0);
}

void *
esa_malloc(void *hdl, int direction, size_t size, int type, int flags)
{
	struct esa_voice *vc = hdl;
	struct esa_softc *sc = (struct esa_softc *)vc->parent;
	struct esa_dma *p;
	int error;

	p = malloc(sizeof(*p), type, flags);
	if (!p)
		return (0);
	error = esa_allocmem(sc, size, 16, p);
	if (error) {
		free(p, type, 0);
		printf("%s: esa_malloc: not enough memory\n",
		    sc->sc_dev.dv_xname);
		return (0);
	}
	p->next = vc->dma;
	vc->dma = p;

	return (KERNADDR(p));
}

void
esa_free(void *hdl, void *addr, int type)
{
	struct esa_voice *vc = hdl;
	struct esa_softc *sc = (struct esa_softc *)vc->parent;
	struct esa_dma *p;
	struct esa_dma **pp;

	for (pp = &vc->dma; (p = *pp) != NULL; pp = &p->next)
		if (KERNADDR(p) == addr) {
			esa_freemem(sc, p);
			*pp = p->next;
			free(p, type, 0);
			return;
		}
}

int
esa_set_port(void *hdl, mixer_ctrl_t *mc)
{
	struct esa_voice *vc = hdl;
	struct esa_softc *sc = (struct esa_softc *)vc->parent;

	return (sc->codec_if->vtbl->mixer_set_port(sc->codec_if, mc));
}

int
esa_get_port(void *hdl, mixer_ctrl_t *mc)
{
	struct esa_voice *vc = hdl;
	struct esa_softc *sc = (struct esa_softc *)vc->parent;

	return (sc->codec_if->vtbl->mixer_get_port(sc->codec_if, mc));
}

int
esa_query_devinfo(void *hdl, mixer_devinfo_t *di)
{
	struct esa_voice *vc = hdl;
	struct esa_softc *sc = (struct esa_softc *)vc->parent;

	return (sc->codec_if->vtbl->query_devinfo(sc->codec_if, di));
}

size_t
esa_round_buffersize(void *hdl, int direction, size_t bufsize)
{
	struct esa_voice *vc = hdl;

	/*
	 * We must be able to do better than this...
	 */
	vc->play.bufsize = vc->rec.bufsize = 65536;

	return (vc->play.bufsize);
}

int
esa_trigger_output(void *hdl, void *start, void *end, int blksize,
			void (*intr)(void *), void *intrarg,
			struct audio_params *param)
{
	struct esa_voice *vc = hdl;
	struct esa_softc *sc = (struct esa_softc *)vc->parent;
	struct esa_dma *p;
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	u_int32_t data;
	u_int32_t bufaddr;
	u_int32_t i;
	size_t size;

	int data_bytes = (((ESA_MINISRC_TMP_BUFFER_SIZE & ~1) +
			   (ESA_MINISRC_IN_BUFFER_SIZE & ~1) +
			   (ESA_MINISRC_OUT_BUFFER_SIZE & ~1) + 4) + 255)
			   &~ 255;
	int dac_data = ESA_DAC_DATA + (data_bytes * vc->index);
	int dsp_in_size = ESA_MINISRC_IN_BUFFER_SIZE - (0x20 * 2);
	int dsp_out_size = ESA_MINISRC_OUT_BUFFER_SIZE - (0x20 * 2);
	int dsp_in_buf = dac_data + (ESA_MINISRC_TMP_BUFFER_SIZE / 2);
	int dsp_out_buf = dsp_in_buf + (dsp_in_size / 2) + 1;

	if (vc->play.active)
		return (EINVAL);

	for (p = vc->dma; p && KERNADDR(p) != start; p = p->next)
		;
	if (!p) {
		printf("%s: esa_trigger_output: bad addr %p\n",
		    sc->sc_dev.dv_xname, start);
		return (EINVAL);
	}

	vc->play.active = 1;
	vc->play.intr = intr;
	vc->play.arg = intrarg;
	vc->play.pos = 0;
	vc->play.count = 0;
	vc->play.buf = start;
	size = (size_t)(((caddr_t)end - (caddr_t)start));
	bufaddr = DMAADDR(p);
	vc->play.start = bufaddr;

#define LO(x) ((x) & 0x0000ffff)
#define HI(x) ((x) >> 16)

	mtx_enter(&audio_lock);
	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA, dac_data +
	    ESA_CDATA_HOST_SRC_ADDRL, LO(bufaddr));
	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA, dac_data +
	    ESA_CDATA_HOST_SRC_ADDRH, HI(bufaddr));
	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA, dac_data +
	    ESA_CDATA_HOST_SRC_END_PLUS_1L, LO(bufaddr + size));
	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA, dac_data +
	    ESA_CDATA_HOST_SRC_END_PLUS_1H, HI(bufaddr + size));
	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA, dac_data +
	    ESA_CDATA_HOST_SRC_CURRENTL, LO(bufaddr));
	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA, dac_data +
	    ESA_CDATA_HOST_SRC_CURRENTH, HI(bufaddr));

	/* DSP buffers */
	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA, dac_data +
	    ESA_CDATA_IN_BUF_BEGIN, dsp_in_buf);
	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA, dac_data +
	    ESA_CDATA_IN_BUF_END_PLUS_1, dsp_in_buf + (dsp_in_size / 2));
	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA, dac_data +
	    ESA_CDATA_IN_BUF_HEAD, dsp_in_buf);
	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA, dac_data +
	    ESA_CDATA_IN_BUF_TAIL, dsp_in_buf);
	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA, dac_data +
	    ESA_CDATA_OUT_BUF_BEGIN, dsp_out_buf);
	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA, dac_data +
	    ESA_CDATA_OUT_BUF_END_PLUS_1, dsp_out_buf + (dsp_out_size / 2));
	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA, dac_data +
	    ESA_CDATA_OUT_BUF_HEAD, dsp_out_buf);
	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA, dac_data +
	    ESA_CDATA_OUT_BUF_TAIL, dsp_out_buf);

	/* Some per-client initializers */
	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA, dac_data +
	    ESA_SRC3_DIRECTION_OFFSET + 12, dac_data + 40 + 8);
	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA, dac_data +
	    ESA_SRC3_DIRECTION_OFFSET + 19, 0x400 + ESA_MINISRC_COEF_LOC);
	/* Enable or disable low-pass filter? (0xff if rate > 45000) */
	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA, dac_data +
	    ESA_SRC3_DIRECTION_OFFSET + 22,
	    vc->play.mode.sample_rate > 45000 ? 0xff : 0);
	/* Tell it which way DMA is going */
	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA, dac_data +
	    ESA_CDATA_DMA_CONTROL,
	    ESA_DMACONTROL_AUTOREPEAT + ESA_DMAC_PAGE3_SELECTOR +
	    ESA_DMAC_BLOCKF_SELECTOR);

	/* Set an armload of static initializers */
	for (i = 0; i < nitems(esa_playvals); i++)
		esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA, dac_data +
		    esa_playvals[i].addr, esa_playvals[i].val);

	/* Put us in the packed task lists */
	esa_add_list(vc, &sc->msrc_list, dac_data >> ESA_DP_SHIFT_COUNT,
		     vc->index);
	esa_add_list(vc, &sc->dma_list, dac_data >> ESA_DP_SHIFT_COUNT,
		     vc->index);
	esa_add_list(vc, &sc->mixer_list, dac_data >> ESA_DP_SHIFT_COUNT,
		     vc->index);
#undef LO
#undef HI

	sc->sc_ntimers++;

	if (sc->sc_ntimers == 1) {
		esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA,
		    ESA_KDATA_TIMER_COUNT_RELOAD, 240);
		esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA,
		    ESA_KDATA_TIMER_COUNT_CURRENT, 240);
		data = bus_space_read_2(iot, ioh, ESA_HOST_INT_CTRL);
		bus_space_write_2(iot, ioh, ESA_HOST_INT_CTRL,
		    data | ESA_CLKRUN_GEN_ENABLE);
	}

	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA, dac_data +
	    ESA_CDATA_INSTANCE_READY, 1);
	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA,
	    ESA_KDATA_MIXER_TASK_NUMBER,
	    sc->mixer_list.indexmap[vc->index]);
	mtx_leave(&audio_lock);
	return (0);
}

int
esa_trigger_input(void *hdl, void *start, void *end, int blksize,
			void (*intr)(void *), void *intrarg,
			struct audio_params *param)
{
	struct esa_voice *vc = hdl;
	struct esa_softc *sc = (struct esa_softc *)vc->parent;
	struct esa_dma *p;
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	u_int32_t data;
	u_int32_t bufaddr;
	u_int32_t i;
	size_t size;
	int data_bytes = (((ESA_MINISRC_TMP_BUFFER_SIZE & ~1) +
			   (ESA_MINISRC_IN_BUFFER_SIZE & ~1) +
			   (ESA_MINISRC_OUT_BUFFER_SIZE & ~1) + 4) + 255)
			   &~ 255;
	int adc_data = ESA_DAC_DATA + (data_bytes * vc->index) +
		       (data_bytes / 2);
	int dsp_in_size = ESA_MINISRC_IN_BUFFER_SIZE - (0x10 * 2);
	int dsp_out_size = ESA_MINISRC_OUT_BUFFER_SIZE - (0x10 * 2);
	int dsp_in_buf = adc_data + (ESA_MINISRC_TMP_BUFFER_SIZE / 2);
	int dsp_out_buf = dsp_in_buf + (dsp_in_size / 2) + 1;
	vc->rec.data_offset = adc_data;

	/* We only support 1 recording channel */
	if (vc->index > 0)
		return (ENODEV);

	if (vc->rec.active)
		return (EINVAL);

	for (p = vc->dma; p && KERNADDR(p) != start; p = p->next)
		;
	if (!p) {
		printf("%s: esa_trigger_input: bad addr %p\n",
		    sc->sc_dev.dv_xname, start);
		return (EINVAL);
	}

	vc->rec.active = 1;
	vc->rec.intr = intr;
	vc->rec.arg = intrarg;
	vc->rec.pos = 0;
	vc->rec.count = 0;
	vc->rec.buf = start;
	size = (size_t)(((caddr_t)end - (caddr_t)start));
	bufaddr = DMAADDR(p);
	vc->rec.start = bufaddr;

#define LO(x) ((x) & 0x0000ffff)
#define HI(x) ((x) >> 16)
	mtx_enter(&audio_lock);
	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA, adc_data +
	    ESA_CDATA_HOST_SRC_ADDRL, LO(bufaddr));
	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA, adc_data +
	    ESA_CDATA_HOST_SRC_ADDRH, HI(bufaddr));
	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA, adc_data +
	    ESA_CDATA_HOST_SRC_END_PLUS_1L, LO(bufaddr + size));
	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA, adc_data +
	    ESA_CDATA_HOST_SRC_END_PLUS_1H, HI(bufaddr + size));
	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA, adc_data +
	    ESA_CDATA_HOST_SRC_CURRENTL, LO(bufaddr));
	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA, adc_data +
	    ESA_CDATA_HOST_SRC_CURRENTH, HI(bufaddr));

	/* DSP buffers */
	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA, adc_data +
	    ESA_CDATA_IN_BUF_BEGIN, dsp_in_buf);
	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA, adc_data +
	    ESA_CDATA_IN_BUF_END_PLUS_1, dsp_in_buf + (dsp_in_size / 2));
	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA, adc_data +
	    ESA_CDATA_IN_BUF_HEAD, dsp_in_buf);
	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA, adc_data +
	    ESA_CDATA_IN_BUF_TAIL, dsp_in_buf);
	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA, adc_data +
	    ESA_CDATA_OUT_BUF_BEGIN, dsp_out_buf);
	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA, adc_data +
	    ESA_CDATA_OUT_BUF_END_PLUS_1, dsp_out_buf + (dsp_out_size / 2));
	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA, adc_data +
	    ESA_CDATA_OUT_BUF_HEAD, dsp_out_buf);
	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA, adc_data +
	    ESA_CDATA_OUT_BUF_TAIL, dsp_out_buf);

	/* Some per-client initializers */
	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA, adc_data +
	    ESA_SRC3_DIRECTION_OFFSET + 12, adc_data + 40 + 8);
	/* Tell it which way DMA is going */
	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA, adc_data +
	    ESA_CDATA_DMA_CONTROL,
	    ESA_DMACONTROL_DIRECTION + ESA_DMACONTROL_AUTOREPEAT +
	    ESA_DMAC_PAGE3_SELECTOR + ESA_DMAC_BLOCKF_SELECTOR);

	/* Set an armload of static initializers */
	for (i = 0; i < nitems(esa_recvals); i++)
		esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA, adc_data +
		    esa_recvals[i].addr, esa_recvals[i].val);

	/* Put us in the packed task lists */
	esa_add_list(vc, &sc->adc1_list, adc_data >> ESA_DP_SHIFT_COUNT,
		     vc->index + ESA_NUM_VOICES);
	esa_add_list(vc, &sc->msrc_list, adc_data >> ESA_DP_SHIFT_COUNT,
		     vc->index + ESA_NUM_VOICES);
	esa_add_list(vc, &sc->dma_list, adc_data >> ESA_DP_SHIFT_COUNT,
		     vc->index + ESA_NUM_VOICES);
#undef LO
#undef HI

	sc->sc_ntimers++;
	if (sc->sc_ntimers == 1) {
		esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA,
		    ESA_KDATA_TIMER_COUNT_RELOAD, 240);
		esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA,
		    ESA_KDATA_TIMER_COUNT_CURRENT, 240);
		data = bus_space_read_2(iot, ioh, ESA_HOST_INT_CTRL);
		bus_space_write_2(iot, ioh, ESA_HOST_INT_CTRL,
		    data | ESA_CLKRUN_GEN_ENABLE);
	}

	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA, adc_data +
	    ESA_CDATA_INSTANCE_READY, 1);
	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA, ESA_KDATA_ADC1_REQUEST,
	    1);
	mtx_leave(&audio_lock);
	return (0);
}

/* Interrupt handler */

int
esa_intr(void *hdl)
{
	struct esa_softc *sc = hdl;
	struct esa_voice *vc;
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	u_int8_t status, ctl;
	u_int32_t pos;
	u_int32_t diff;
	u_int32_t play_blksize, play_bufsize;
	u_int32_t rec_blksize, rec_bufsize;
	int i, claimed = 0;

	mtx_enter(&audio_lock);
	status = bus_space_read_1(iot, ioh, ESA_HOST_INT_STATUS);
	if (status == 0xff) {
		mtx_leave(&audio_lock);
		return (0);
	}

	/* ack the interrupt */
	bus_space_write_1(iot, ioh, ESA_HOST_INT_STATUS, status);

	if (status & ESA_HV_INT_PENDING) {
		u_int8_t event;

		printf("%s: hardware volume interrupt\n", sc->sc_dev.dv_xname);
		event = bus_space_read_1(iot, ioh, ESA_HW_VOL_COUNTER_MASTER);
		switch(event) {
		case 0x99:
		case 0xaa:
		case 0x66:
		case 0x88:
			printf("%s: esa_intr: FIXME\n", sc->sc_dev.dv_xname);
			break;
		default:
			printf("%s: unknown hwvol event 0x%02x\n",
			    sc->sc_dev.dv_xname, event);
			break;
		}
		bus_space_write_1(iot, ioh, ESA_HW_VOL_COUNTER_MASTER, 0x88);
		claimed = 1;
	}

	if (status & ESA_ASSP_INT_PENDING) {
		ctl = bus_space_read_1(iot, ioh, ESA_ASSP_CONTROL_B);
		if (!(ctl & ESA_STOP_ASSP_CLOCK)) {
			ctl = bus_space_read_1(iot, ioh,
					       ESA_ASSP_HOST_INT_STATUS);
			if (ctl & ESA_DSP2HOST_REQ_TIMER) {
				bus_space_write_1(iot, ioh,
				    ESA_ASSP_HOST_INT_STATUS,
				    ESA_DSP2HOST_REQ_TIMER);
				for (i = 0; i < ESA_NUM_VOICES; i++) {
					vc = &sc->voice[i];
					if (vc->play.active) {
						play_blksize = vc->play.blksize;
						play_bufsize = vc->play.bufsize;
						pos = esa_get_pointer(sc, &vc->play)
						    % play_bufsize;
						diff = (play_bufsize + pos - vc->play.pos)
						    % play_bufsize;
						vc->play.pos = pos;
						vc->play.count += diff;
						while(vc->play.count >= play_blksize) {
							vc->play.count -= play_blksize;
							(*vc->play.intr)(vc->play.arg);
						}
					}
					if (vc->rec.active) {
						rec_blksize = vc->rec.blksize;
						rec_bufsize = vc->rec.bufsize;
						pos = esa_get_pointer(sc, &vc->rec)
						    % rec_bufsize;
						diff = (rec_bufsize + pos - vc->rec.pos)
						    % rec_bufsize;
						vc->rec.pos = pos;
						vc->rec.count += diff;
						while(vc->rec.count >= rec_blksize) {
							vc->rec.count -= rec_blksize;
							(*vc->rec.intr)(vc->rec.arg);
						}
					}
				}
			}
		}
		claimed = 1;
	}
	mtx_leave(&audio_lock);
	return (claimed);
}

int
esa_allocmem(struct esa_softc *sc, size_t size, size_t align,
		struct esa_dma *p)
{
	int error;

	p->size = size;
	error = bus_dmamem_alloc(sc->sc_dmat, p->size, align, 0,
				 p->segs, sizeof(p->segs) / sizeof(p->segs[0]),
				 &p->nsegs, BUS_DMA_NOWAIT);
	if (error)
		return (error);

	error = bus_dmamem_map(sc->sc_dmat, p->segs, p->nsegs, p->size,
				&p->addr, BUS_DMA_NOWAIT | BUS_DMA_COHERENT);
	if (error)
		goto free;

	error = bus_dmamap_create(sc->sc_dmat, p->size, 1, p->size, 0,
				  BUS_DMA_NOWAIT, &p->map);
	if (error)
		goto unmap;

	error = bus_dmamap_load(sc->sc_dmat, p->map, p->addr, p->size, NULL,
				BUS_DMA_NOWAIT);
	if (error)
		goto destroy;

	return (0);

destroy:
	bus_dmamap_destroy(sc->sc_dmat, p->map);
unmap:
	bus_dmamem_unmap(sc->sc_dmat, p->addr, p->size);
free:
	bus_dmamem_free(sc->sc_dmat, p->segs, p->nsegs);

	return (error);
}

int
esa_freemem(struct esa_softc *sc, struct esa_dma *p)
{

	bus_dmamap_unload(sc->sc_dmat, p->map);
	bus_dmamap_destroy(sc->sc_dmat, p->map);
	bus_dmamem_unmap(sc->sc_dmat, p->addr, p->size);
	bus_dmamem_free(sc->sc_dmat, p->segs, p->nsegs);

	return (0);
}

/*
 * Supporting Subroutines
 */
const struct pci_matchid esa_devices[] = {
	{ PCI_VENDOR_ESSTECH, PCI_PRODUCT_ESSTECH_ES1989 },
	{ PCI_VENDOR_ESSTECH, PCI_PRODUCT_ESSTECH_MAESTRO3 },
	{ PCI_VENDOR_ESSTECH, PCI_PRODUCT_ESSTECH_MAESTRO3_2 },
};

int
esa_match(struct device *dev, void *match, void *aux)
{
	return (pci_matchbyid((struct pci_attach_args *)aux, esa_devices,
	    nitems(esa_devices)));
}

void
esa_attach(struct device *parent, struct device *self, void *aux)
{
	struct esa_softc *sc = (struct esa_softc *)self;
	struct pci_attach_args *pa = (struct pci_attach_args *)aux;
	pcitag_t tag = pa->pa_tag;
	pci_chipset_tag_t pc = pa->pa_pc;
	pci_intr_handle_t ih;
	struct esa_card_type *card;
	const char *intrstr;
	int i, len;

	for (card = esa_card_types; card->pci_vendor_id; card++)
		if (PCI_VENDOR(pa->pa_id) == card->pci_vendor_id &&
		    PCI_PRODUCT(pa->pa_id) == card->pci_product_id) {
			sc->type = card->type;
			sc->delay1 = card->delay1;
			sc->delay2 = card->delay2;
			break;
		}

	/* Map I/O register */
	if (pci_mapreg_map(pa, PCI_CBIO, PCI_MAPREG_TYPE_IO, 0,
	    &sc->sc_iot, &sc->sc_ioh, &sc->sc_iob, &sc->sc_ios, 0)) {
		printf(": can't map i/o space\n");
		return;
	}

	/* Initialize softc */
	sc->sc_tag = tag;
	sc->sc_pct = pc;
	sc->sc_dmat = pa->pa_dmat;

	/* Map and establish an interrupt */
	if (pci_intr_map(pa, &ih)) {
		printf(": can't map interrupt\n");
		bus_space_unmap(sc->sc_iot, sc->sc_ioh, sc->sc_ios);
		return;
	}
	intrstr = pci_intr_string(pc, ih);
	sc->sc_ih = pci_intr_establish(pc, ih, IPL_AUDIO | IPL_MPSAFE,
	    esa_intr, self, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		bus_space_unmap(sc->sc_iot, sc->sc_ioh, sc->sc_ios);
		return;
	}
	printf(": %s\n", intrstr);

	/* Power up chip */
	pci_set_powerstate(pc, tag, PCI_PMCSR_STATE_D0);

	/* Init chip */
	if (esa_init(sc) == -1) {
		printf("%s: esa_attach: unable to initialize the card\n",
		    sc->sc_dev.dv_xname);
		pci_intr_disestablish(pc, sc->sc_ih);
		bus_space_unmap(sc->sc_iot, sc->sc_ioh, sc->sc_ios);
		return;
	}

	/* create suspend save area */
	len = sizeof(u_int16_t) * (ESA_REV_B_CODE_MEMORY_LENGTH
	    + ESA_REV_B_DATA_MEMORY_LENGTH + 1);
	sc->savemem = malloc(len, M_DEVBUF, M_NOWAIT | M_ZERO);
	if (sc->savemem == NULL) {
		printf("%s: unable to allocate suspend buffer\n",
		    sc->sc_dev.dv_xname);
		pci_intr_disestablish(pc, sc->sc_ih);
		bus_space_unmap(sc->sc_iot, sc->sc_ioh, sc->sc_ios);
		return;
	}

	/*
	 * Every card I've seen has had their channels swapped with respect
	 * to the mixer. Ie:
	 *  $ mixerctl -w outputs.master=0,191
	 * Would result in the _right_ speaker being turned off.
	 *
	 * So, we will swap the left and right mixer channels to compensate
	 * for this.
	 */
	sc->codec_flags |= AC97_HOST_SWAPPED_CHANNELS;
	sc->codec_flags |= AC97_HOST_DONT_READ;

	/* Attach AC97 host interface */
	sc->host_if.arg = self;
	sc->host_if.attach = esa_attach_codec;
	sc->host_if.read = esa_read_codec;
	sc->host_if.write = esa_write_codec;
	sc->host_if.reset = esa_reset_codec;
	sc->host_if.flags = esa_flags_codec;

	if (ac97_attach(&sc->host_if) != 0) {
		pci_intr_disestablish(pc, sc->sc_ih);
		bus_space_unmap(sc->sc_iot, sc->sc_ioh, sc->sc_ios);
		free(sc->savemem, M_DEVBUF, 0);
		return;
	}

	/* initialize list management structures */
	sc->mixer_list.mem_addr = ESA_KDATA_MIXER_XFER0;
	sc->mixer_list.max = ESA_MAX_VIRTUAL_MIXER_CHANNELS;
	sc->adc1_list.mem_addr = ESA_KDATA_ADC1_XFER0;
	sc->adc1_list.max = ESA_MAX_VIRTUAL_ADC1_CHANNELS;
	sc->dma_list.mem_addr = ESA_KDATA_DMA_XFER0;
	sc->dma_list.max = ESA_MAX_VIRTUAL_DMA_CHANNELS;
	sc->msrc_list.mem_addr = ESA_KDATA_INSTANCE0_MINISRC;
	sc->msrc_list.max = ESA_MAX_INSTANCE_MINISRC;

	/* initialize index maps */
	for (i = 0; i < ESA_NUM_VOICES * 2; i++) {
		sc->mixer_list.indexmap[i] = -1;
		sc->msrc_list.indexmap[i] = -1;
		sc->dma_list.indexmap[i] = -1;
		sc->adc1_list.indexmap[i] = -1;
	}
	for (i = 0; i < ESA_NUM_VOICES; i++) {
		sc->voice[i].parent = (struct device *)sc;
		sc->voice[i].index = i;
		sc->sc_audiodev[i] =
		    audio_attach_mi(&esa_hw_if, &sc->voice[i], NULL, &sc->sc_dev);
	}
}

int
esa_detach(struct device *self, int flags)
{
	struct esa_softc *sc = (struct esa_softc *)self;
	int i;

	for (i = 0; i < ESA_NUM_VOICES; i++) {
		if (sc->sc_audiodev[i] != NULL)
			config_detach(sc->sc_audiodev[i], flags);
	}

	if (sc->sc_ih != NULL)
		pci_intr_disestablish(sc->sc_pct, sc->sc_ih);
	if (sc->sc_ios)
		bus_space_unmap(sc->sc_iot, sc->sc_ioh, sc->sc_ios);

	free(sc->savemem, M_DEVBUF, 0);

	return (0);
}

u_int16_t
esa_read_assp(struct esa_softc *sc, u_int16_t region, u_int16_t index)
{
	u_int16_t data;
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;

	bus_space_write_2(iot, ioh, ESA_DSP_PORT_MEMORY_TYPE,
	    region & ESA_MEMTYPE_MASK);
	bus_space_write_2(iot, ioh, ESA_DSP_PORT_MEMORY_INDEX, index);
	data = bus_space_read_2(iot, ioh, ESA_DSP_PORT_MEMORY_DATA);

	return (data);
}

void
esa_write_assp(struct esa_softc *sc, u_int16_t region, u_int16_t index,
		u_int16_t data)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;

	bus_space_write_2(iot, ioh, ESA_DSP_PORT_MEMORY_TYPE,
	    region & ESA_MEMTYPE_MASK);
	bus_space_write_2(iot, ioh, ESA_DSP_PORT_MEMORY_INDEX, index);
	bus_space_write_2(iot, ioh, ESA_DSP_PORT_MEMORY_DATA, data);

	return;
}

int
esa_init_codec(struct esa_softc *sc)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	u_int32_t data;

	data = bus_space_read_1(iot, ioh, ESA_CODEC_COMMAND);

	return ((data & 0x1) ? 0 : 1);
}

int
esa_attach_codec(void *aux, struct ac97_codec_if *codec_if)
{
	struct esa_softc *sc = aux;

	sc->codec_if = codec_if;

	return (0);
}

int
esa_read_codec(void *aux, u_int8_t reg, u_int16_t *result)
{
	struct esa_softc *sc = aux;
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;

	if (esa_wait(sc))
		printf("%s: esa_read_codec: timed out\n", sc->sc_dev.dv_xname);
	bus_space_write_1(iot, ioh, ESA_CODEC_COMMAND, (reg & 0x7f) | 0x80);
	delay(50);
	if (esa_wait(sc))
		printf("%s: esa_read_codec: timed out\n", sc->sc_dev.dv_xname);
	*result = bus_space_read_2(iot, ioh, ESA_CODEC_DATA);

	return (0);
}

int
esa_write_codec(void *aux, u_int8_t reg, u_int16_t data)
{
	struct esa_softc *sc = aux;
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;

	if (esa_wait(sc)) {
		printf("%s: esa_write_codec: timed out\n", sc->sc_dev.dv_xname);
		return (-1);
	}
	bus_space_write_2(iot, ioh, ESA_CODEC_DATA, data);
	bus_space_write_1(iot, ioh, ESA_CODEC_COMMAND, reg & 0x7f);
	delay(50);

	return (0);
}

void
esa_reset_codec(void *aux)
{

	return;
}

enum ac97_host_flags
esa_flags_codec(void *aux)
{
	struct esa_softc *sc = aux;

	return (sc->codec_flags);
}

int
esa_wait(struct esa_softc *sc)
{
	int i, val;
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;

	for (i = 0; i < 20; i++) {
		val = bus_space_read_1(iot, ioh, ESA_CODEC_STATUS);
		if ((val & 1) == 0)
			return (0);
		delay(2);
	}

	return (-1);
}

int
esa_init(struct esa_softc *sc)
{
	struct esa_voice *vc;
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	pcitag_t tag = sc->sc_tag;
	pci_chipset_tag_t pc = sc->sc_pct;
	u_int32_t data, i, size;
	u_int8_t reset_state;
	int data_bytes = (((ESA_MINISRC_TMP_BUFFER_SIZE & ~1) +
			   (ESA_MINISRC_IN_BUFFER_SIZE & ~1) +
			   (ESA_MINISRC_OUT_BUFFER_SIZE & ~1) + 4) + 255)
			   &~ 255;

	/* Disable legacy emulation */
	data = pci_conf_read(pc, tag, PCI_LEGACY_AUDIO_CTRL);
	data |= DISABLE_LEGACY;
	pci_conf_write(pc, tag, PCI_LEGACY_AUDIO_CTRL, data);

	esa_config(sc);

	reset_state = esa_assp_halt(sc);

	esa_init_codec(sc);
	esa_codec_reset(sc);

	/* Zero kernel and mixer data */
	size = ESA_REV_B_DATA_MEMORY_UNIT_LENGTH * ESA_NUM_UNITS_KERNEL_DATA;
	for (i = 0; i < size / 2; i++) {
		esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA,
		    ESA_KDATA_BASE_ADDR + i, 0);
		esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA,
		    ESA_KDATA_BASE_ADDR2 + i, 0);
	}

	/* Init DMA pointer */
	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA, ESA_KDATA_CURRENT_DMA,
	    ESA_KDATA_DMA_XFER0);

	/* Write kernel code into memory */
	size = nitems(esa_assp_kernel_image);
	for (i = 0; i < size; i++)
		esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_CODE,
		    ESA_REV_B_CODE_MEMORY_BEGIN + i, esa_assp_kernel_image[i]);

	size = nitems(esa_assp_minisrc_image);
	for (i = 0; i < size; i++)
		esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_CODE, 0x400 + i,
		    esa_assp_minisrc_image[i]);

	/* Write the coefficients for the low pass filter */
	size = nitems(esa_minisrc_lpf_image);
	for (i = 0; i < size; i++)
		esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_CODE,
		    0x400 + ESA_MINISRC_COEF_LOC + i, esa_minisrc_lpf_image[i]);
	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_CODE,
	    0x400 + ESA_MINISRC_COEF_LOC + size, 0x8000);
	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA, ESA_KDATA_TASK0, 0x400);
	/* Init the mixer number */
	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA,
             ESA_KDATA_MIXER_TASK_NUMBER, 0);
	/* Extreme kernel master volume */
	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA,
	    ESA_KDATA_DAC_LEFT_VOLUME, ESA_ARB_VOLUME);
	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA,
            ESA_KDATA_DAC_RIGHT_VOLUME, ESA_ARB_VOLUME);

	if (esa_amp_enable(sc))
		return (-1);

	/* Zero entire DAC/ADC area */
	for (i = 0x1100; i < 0x1c00; i++)
		esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA, i, 0);

	/* set some sane defaults */
	for (i = 0; i < ESA_NUM_VOICES; i++) {
		vc = &sc->voice[i];
		vc->play.data_offset = ESA_DAC_DATA + (data_bytes * i);
		vc->rec.data_offset = ESA_DAC_DATA + (data_bytes * i * 2);
	}

	esa_enable_interrupts(sc);

	bus_space_write_1(iot, ioh, ESA_DSP_PORT_CONTROL_REG_B,
	    reset_state | ESA_REGB_ENABLE_RESET);

	return (0);
}

void
esa_config(struct esa_softc *sc)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	pcitag_t tag = sc->sc_tag;
	pci_chipset_tag_t pc = sc->sc_pct;
	u_int32_t data;

	data = pci_conf_read(pc, tag, ESA_PCI_ALLEGRO_CONFIG);
	data &= ESA_REDUCED_DEBOUNCE;
	data |= ESA_PM_CTRL_ENABLE | ESA_CLK_DIV_BY_49 | ESA_USE_PCI_TIMING;
	pci_conf_write(pc, tag, ESA_PCI_ALLEGRO_CONFIG, data);

	bus_space_write_1(iot, ioh, ESA_ASSP_CONTROL_B, ESA_RESET_ASSP);
	data = pci_conf_read(pc, tag, ESA_PCI_ALLEGRO_CONFIG);
	data &= ~ESA_INT_CLK_SELECT;
	if (sc->type == ESS_MAESTRO3) {
		data &= ~ESA_INT_CLK_MULT_ENABLE;
		data |= ESA_INT_CLK_SRC_NOT_PCI;
	}
	data &= ~(ESA_CLK_MULT_MODE_SELECT | ESA_CLK_MULT_MODE_SELECT_2);
	pci_conf_write(pc, tag, ESA_PCI_ALLEGRO_CONFIG, data);

	if (sc->type == ESS_ALLEGRO1) {
		data = pci_conf_read(pc, tag, ESA_PCI_USER_CONFIG);
		data |= ESA_IN_CLK_12MHZ_SELECT;
		pci_conf_write(pc, tag, ESA_PCI_USER_CONFIG, data);
	}

	data = bus_space_read_1(iot, ioh, ESA_ASSP_CONTROL_A);
	data &= ~(ESA_DSP_CLK_36MHZ_SELECT | ESA_ASSP_CLK_49MHZ_SELECT);
	data |= ESA_ASSP_CLK_49MHZ_SELECT;	/* XXX: Assumes 49MHz DSP */
	data |= ESA_ASSP_0_WS_ENABLE;
	bus_space_write_1(iot, ioh, ESA_ASSP_CONTROL_A, data);

	bus_space_write_1(iot, ioh, ESA_ASSP_CONTROL_B, ESA_RUN_ASSP);

	return;
}

u_int8_t
esa_assp_halt(struct esa_softc *sc)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	u_int8_t data, reset_state;

	data = bus_space_read_1(iot, ioh, ESA_DSP_PORT_CONTROL_REG_B);
	reset_state = data & ~ESA_REGB_STOP_CLOCK;
	delay(10000);		/* XXX use tsleep */
	bus_space_write_1(iot, ioh, ESA_DSP_PORT_CONTROL_REG_B,
			reset_state & ~ESA_REGB_ENABLE_RESET);
	delay(10000);		/* XXX use tsleep */

	return (reset_state);
}

void
esa_codec_reset(struct esa_softc *sc)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	u_int16_t data, dir;
	int retry = 0;

	do {
		data = bus_space_read_2(iot, ioh, ESA_GPIO_DIRECTION);
		dir = data | 0x10; /* assuming pci bus master? */

		/* remote codec config */
		data = bus_space_read_2(iot, ioh, ESA_RING_BUS_CTRL_B);
		bus_space_write_2(iot, ioh, ESA_RING_BUS_CTRL_B,
		    data & ~ESA_SECOND_CODEC_ID_MASK);
		data = bus_space_read_2(iot, ioh, ESA_SDO_OUT_DEST_CTRL);
		bus_space_write_2(iot, ioh, ESA_SDO_OUT_DEST_CTRL,
		    data & ~ESA_COMMAND_ADDR_OUT);
		data = bus_space_read_2(iot, ioh, ESA_SDO_IN_DEST_CTRL);
		bus_space_write_2(iot, ioh, ESA_SDO_IN_DEST_CTRL,
		    data & ~ESA_STATUS_ADDR_IN);

		bus_space_write_2(iot, ioh, ESA_RING_BUS_CTRL_A,
				  ESA_IO_SRAM_ENABLE);
		delay(20);

		bus_space_write_2(iot, ioh, ESA_GPIO_DIRECTION,
		    dir & ~ESA_GPO_PRIMARY_AC97);
		bus_space_write_2(iot, ioh, ESA_GPIO_MASK,
				  ~ESA_GPO_PRIMARY_AC97);
		bus_space_write_2(iot, ioh, ESA_GPIO_DATA, 0);
		bus_space_write_2(iot, ioh, ESA_GPIO_DIRECTION,
		    dir | ESA_GPO_PRIMARY_AC97);
		delay(sc->delay1 * 1000);
		bus_space_write_2(iot, ioh, ESA_GPIO_DATA,
				  ESA_GPO_PRIMARY_AC97);
		delay(5);
		bus_space_write_2(iot, ioh, ESA_RING_BUS_CTRL_A,
		    ESA_IO_SRAM_ENABLE | ESA_SERIAL_AC_LINK_ENABLE);
		bus_space_write_2(iot, ioh, ESA_GPIO_MASK, ~0);
		delay(sc->delay2 * 1000);

		esa_read_codec(sc, 0x7c, &data);
		if ((data == 0) || (data == 0xffff)) {
			retry++;
			if (retry > 3) {
				printf("%s: esa_codec_reset: failed\n",
				    sc->sc_dev.dv_xname);
				break;
			}
			printf("%s: esa_codec_reset: retrying\n",
			    sc->sc_dev.dv_xname);
		} else
			retry = 0;
	} while (retry);

	return;
}

int
esa_amp_enable(struct esa_softc *sc)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	u_int32_t gpo, polarity_port, polarity;
	u_int16_t data;

	switch (sc->type) {
	case ESS_ALLEGRO1:
		polarity_port = 0x1800;
		break;
	case ESS_MAESTRO3:
		polarity_port = 0x1100;
		break;
	default:
		printf("%s: esa_amp_enable: Unknown chip type!!!\n",
		    sc->sc_dev.dv_xname);
		return (1);
	}

	gpo = (polarity_port >> 8) & 0x0f;
	polarity = polarity_port >> 12;
	polarity = !polarity;	/* Enable */
	polarity = polarity << gpo;
	gpo = 1 << gpo;
	bus_space_write_2(iot, ioh, ESA_GPIO_MASK, ~gpo);
	data = bus_space_read_2(iot, ioh, ESA_GPIO_DIRECTION);
	bus_space_write_2(iot, ioh, ESA_GPIO_DIRECTION, data | gpo);
	data = ESA_GPO_SECONDARY_AC97 | ESA_GPO_PRIMARY_AC97 | polarity;
	bus_space_write_2(iot, ioh, ESA_GPIO_DATA, data);
	bus_space_write_2(iot, ioh, ESA_GPIO_MASK, ~0);

	return (0);
}

void
esa_enable_interrupts(struct esa_softc *sc)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	u_int8_t data;

	bus_space_write_2(iot, ioh, ESA_HOST_INT_CTRL,
	    ESA_ASSP_INT_ENABLE | ESA_HV_INT_ENABLE);
	data = bus_space_read_1(iot, ioh, ESA_ASSP_CONTROL_C);
	bus_space_write_1(iot, ioh, ESA_ASSP_CONTROL_C,
	    data | ESA_ASSP_HOST_INT_ENABLE);
}

/*
 * List management
 */
int
esa_add_list(struct esa_voice *vc, struct esa_list *el,
	     u_int16_t val, int index)
{
	struct esa_softc *sc = (struct esa_softc *)vc->parent;

	el->indexmap[index] = el->currlen;
	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA,
		       el->mem_addr + el->currlen,
		       val);

	return (el->currlen++);
}

void
esa_remove_list(struct esa_voice *vc, struct esa_list *el, int index)
{
	struct esa_softc *sc = (struct esa_softc *)vc->parent;
	u_int16_t val;
	int lastindex = el->currlen - 1;
	int vindex = el->indexmap[index];
	int i;

	/* reset our virtual index */
	el->indexmap[index] = -1;

	if (vindex != lastindex) {
		val = esa_read_assp(sc, ESA_MEMTYPE_INTERNAL_DATA,
				    el->mem_addr + lastindex);
		esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA,
			       el->mem_addr + vindex,
			       val);
		for (i = 0; i < ESA_NUM_VOICES * 2; i++)
			if (el->indexmap[i] == lastindex)
				break;
		if (i >= ESA_NUM_VOICES * 2)
			printf("%s: esa_remove_list: invalid task index\n",
			       sc->sc_dev.dv_xname);
		else
			el->indexmap[i] = vindex;
	}

	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA,
		       el->mem_addr + lastindex, 0);
	el->currlen--;

	return;
}

int
esa_activate(struct device *self, int act)
{
	struct esa_softc *sc = (struct esa_softc *)self;
	int rv;

	switch (act) {
	case DVACT_SUSPEND:
		rv = config_activate_children(self, act);
		esa_suspend(sc);
		break;
	case DVACT_RESUME:
		esa_resume(sc);
		rv = config_activate_children(self, act);
		break;
	default:
		rv = config_activate_children(self, act);
		break;
	}
	return rv;
}

void
esa_suspend(struct esa_softc *sc)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	int i, index;

	index = 0;

	bus_space_write_2(iot, ioh, ESA_HOST_INT_CTRL, 0);
	bus_space_write_1(iot, ioh, ESA_ASSP_CONTROL_C, 0);

	esa_assp_halt(sc);

	/* Save ASSP state */
	for (i = ESA_REV_B_CODE_MEMORY_BEGIN; i <= ESA_REV_B_CODE_MEMORY_END;
	    i++)
		sc->savemem[index++] = esa_read_assp(sc,
		    ESA_MEMTYPE_INTERNAL_CODE, i);
	for (i = ESA_REV_B_DATA_MEMORY_BEGIN; i <= ESA_REV_B_DATA_MEMORY_END;
	    i++)
		sc->savemem[index++] = esa_read_assp(sc,
		    ESA_MEMTYPE_INTERNAL_DATA, i);
}

void
esa_resume(struct esa_softc *sc)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	int i, index;
	u_int8_t reset_state;

	index = 0;

	esa_config(sc);

	reset_state = esa_assp_halt(sc);

	esa_codec_reset(sc);

	/* restore ASSP */
	for (i = ESA_REV_B_CODE_MEMORY_BEGIN; i <= ESA_REV_B_CODE_MEMORY_END;
	    i++)
		esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_CODE, i,
		    sc->savemem[index++]);
	for (i = ESA_REV_B_DATA_MEMORY_BEGIN; i <= ESA_REV_B_DATA_MEMORY_END;
	    i++)
		esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA, i,
		    sc->savemem[index++]);

	esa_write_assp(sc, ESA_MEMTYPE_INTERNAL_DATA, ESA_KDATA_DMA_ACTIVE, 0);
	bus_space_write_1(iot, ioh, ESA_DSP_PORT_CONTROL_REG_B,
	    reset_state | ESA_REGB_ENABLE_RESET);

	esa_enable_interrupts(sc);
	esa_amp_enable(sc);
}

u_int32_t
esa_get_pointer(struct esa_softc *sc, struct esa_channel *ch)
{
	u_int16_t hi = 0, lo = 0;
	u_int32_t addr;
	int data_offset = ch->data_offset;

	hi = esa_read_assp(sc, ESA_MEMTYPE_INTERNAL_DATA, data_offset +
	    ESA_CDATA_HOST_SRC_CURRENTH);
	lo = esa_read_assp(sc, ESA_MEMTYPE_INTERNAL_DATA, data_offset +
	    ESA_CDATA_HOST_SRC_CURRENTL);

	addr = lo | ((u_int32_t)hi << 16);
	return (addr - ch->start);
}
