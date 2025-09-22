/*	$OpenBSD: autri.c,v 1.51 2024/05/24 06:02:53 jsg Exp $	*/

/*
 * Copyright (c) 2001 SOMEYA Yoshihiko and KUROSAWA Takahiro.
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
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Trident 4DWAVE-DX/NX, SiS 7018, ALi M5451 Sound Driver
 *
 * The register information is taken from the ALSA driver.
 *
 * Documentation links:
 * - ftp://ftp.alsa-project.org/pub/manuals/trident/
 */

#include "midi.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/fcntl.h>
#include <sys/malloc.h>
#include <sys/device.h>

#include <dev/pci/pcidevs.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <sys/audioio.h>
#include <dev/audio_if.h>
#include <dev/midi_if.h>
#include <dev/ic/ac97.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/pci/autrireg.h>
#include <dev/pci/autrivar.h>

#ifdef AUDIO_DEBUG
# define DPRINTF(x)	if (autridebug) printf x
# define DPRINTFN(n,x)	if (autridebug > (n)) printf x
int autridebug = 0;
#else
# define DPRINTF(x)
# define DPRINTFN(n,x)
#endif

int	autri_match(struct device *, void *, void *);
void	autri_attach(struct device *, struct device *, void *);
int	autri_activate(struct device *, int);
int	autri_intr(void *);

#define DMAADDR(p) ((p)->map->dm_segs[0].ds_addr)
#define KERNADDR(p) ((void *)((p)->addr))

int autri_allocmem(struct autri_softc *, size_t, size_t, struct autri_dma *);
int autri_freemem(struct autri_softc *, struct autri_dma *);

#define TWRITE1(sc, r, x) bus_space_write_1((sc)->memt, (sc)->memh, (r), (x))
#define TWRITE2(sc, r, x) bus_space_write_2((sc)->memt, (sc)->memh, (r), (x))
#define TWRITE4(sc, r, x) bus_space_write_4((sc)->memt, (sc)->memh, (r), (x))
#define TREAD1(sc, r) bus_space_read_1((sc)->memt, (sc)->memh, (r))
#define TREAD2(sc, r) bus_space_read_2((sc)->memt, (sc)->memh, (r))
#define TREAD4(sc, r) bus_space_read_4((sc)->memt, (sc)->memh, (r))

static __inline void autri_reg_set_1(struct autri_softc *, int, uint8_t);
static __inline void autri_reg_clear_1(struct autri_softc *, int, uint8_t);
static __inline void autri_reg_set_4(struct autri_softc *, int, uint32_t);
static __inline void autri_reg_clear_4(struct autri_softc *, int, uint32_t);

int	autri_attach_codec(void *sc, struct ac97_codec_if *);
int	autri_read_codec(void *sc, u_int8_t a, u_int16_t *d);
int	autri_write_codec(void *sc, u_int8_t a, u_int16_t d);
void	autri_reset_codec(void *sc);
enum ac97_host_flags	autri_flags_codec(void *);

int  autri_init(void *sc);
struct autri_dma *autri_find_dma(struct autri_softc *, void *);
void autri_setup_channel(struct autri_softc *sc,int mode,
				    struct audio_params *param);
void autri_enable_interrupt(struct autri_softc *sc, int ch);
void autri_disable_interrupt(struct autri_softc *sc, int ch);
void autri_startch(struct autri_softc *sc, int ch, int ch_intr);
void autri_stopch(struct autri_softc *sc, int ch, int ch_intr);
void autri_enable_loop_interrupt(void *sc);
#if 0
void autri_disable_loop_interrupt(void *sc);
#endif

struct cfdriver autri_cd = {
	NULL, "autri", DV_DULL
};

const struct cfattach autri_ca = {
	sizeof(struct autri_softc), autri_match, autri_attach, NULL,
	autri_activate
};

int	autri_open(void *, int);
void	autri_close(void *);
int	autri_set_params(void *, int, int, struct audio_params *,
	    struct audio_params *);
int	autri_round_blocksize(void *, int);
int	autri_trigger_output(void *, void *, void *, int, void (*)(void *),
	    void *, struct audio_params *);
int	autri_trigger_input(void *, void *, void *, int, void (*)(void *),
	    void *, struct audio_params *);
int	autri_halt_output(void *);
int	autri_halt_input(void *);
int	autri_mixer_set_port(void *, mixer_ctrl_t *);
int	autri_mixer_get_port(void *, mixer_ctrl_t *);
void   *autri_malloc(void *, int, size_t, int, int);
void	autri_free(void *, void *, int);
int	autri_query_devinfo(void *addr, mixer_devinfo_t *dip);

int	autri_get_portnum_by_name(struct autri_softc *, char *, char *, char *);

const struct audio_hw_if autri_hw_if = {
	.open = autri_open,
	.close = autri_close,
	.set_params = autri_set_params,
	.round_blocksize = autri_round_blocksize,
	.halt_output = autri_halt_output,
	.halt_input = autri_halt_input,
	.set_port = autri_mixer_set_port,
	.get_port = autri_mixer_get_port,
	.query_devinfo = autri_query_devinfo,
	.allocm = autri_malloc,
	.freem = autri_free,
	.trigger_output = autri_trigger_output,
	.trigger_input = autri_trigger_input,
};

#if NMIDI > 0
void	autri_midi_close(void *);
void	autri_midi_getinfo(void *, struct midi_info *);
int	autri_midi_open(void *, int, void (*)(void *, int),
			   void (*)(void *), void *);
int	autri_midi_output(void *, int);

const struct midi_hw_if autri_midi_hw_if = {
	autri_midi_open,
	autri_midi_close,
	autri_midi_output,
	NULL,			/* flush */
	autri_midi_getinfo,
	NULL,			/* ioctl */
};
#endif

/*
 * register set/clear bit
 */
static __inline void
autri_reg_set_1(struct autri_softc *sc, int no, uint8_t mask)
{
	bus_space_write_1(sc->memt, sc->memh, no,
	    (bus_space_read_1(sc->memt, sc->memh, no) | mask));
}

static __inline void
autri_reg_clear_1(struct autri_softc *sc, int no, uint8_t mask)
{
	bus_space_write_1(sc->memt, sc->memh, no,
	    (bus_space_read_1(sc->memt, sc->memh, no) & ~mask));
}

static __inline void
autri_reg_set_4(struct autri_softc *sc, int no, uint32_t mask)
{
	bus_space_write_4(sc->memt, sc->memh, no,
	    (bus_space_read_4(sc->memt, sc->memh, no) | mask));
}

static __inline void
autri_reg_clear_4(struct autri_softc *sc, int no, uint32_t mask)
{
	bus_space_write_4(sc->memt, sc->memh, no,
	    (bus_space_read_4(sc->memt, sc->memh, no) & ~mask));
}

/*
 * AC97 codec
 */
int
autri_attach_codec(void *sc_, struct ac97_codec_if *codec_if)
{
	struct autri_codec_softc *sc = sc_;

	DPRINTF(("autri_attach_codec()\n"));

	sc->codec_if = codec_if;
	return 0;
}

int
autri_read_codec(void *sc_, u_int8_t index, u_int16_t *data)
{
	struct autri_codec_softc *codec = sc_;
	struct autri_softc *sc = codec->sc;
	u_int32_t status, addr, cmd, busy;
	u_int16_t count;

	/*DPRINTF(("sc->sc->type : 0x%X",sc->sc->type));*/

	switch (sc->sc_devid) {
	case AUTRI_DEVICE_ID_4DWAVE_DX:
		addr = AUTRI_DX_ACR1;
		cmd  = AUTRI_DX_ACR1_CMD_READ;
		busy = AUTRI_DX_ACR1_BUSY_READ;
		break;
	case AUTRI_DEVICE_ID_4DWAVE_NX:
		addr = AUTRI_NX_ACR2;
		cmd  = AUTRI_NX_ACR2_CMD_READ;
		busy = AUTRI_NX_ACR2_BUSY_READ | AUTRI_NX_ACR2_RECV_WAIT;
		break;
	case AUTRI_DEVICE_ID_SIS_7018:
		addr = AUTRI_SIS_ACRD;
		cmd  = AUTRI_SIS_ACRD_CMD_READ;
		busy = AUTRI_SIS_ACRD_BUSY_READ | AUTRI_SIS_ACRD_AUDIO_BUSY;
		break;
	case AUTRI_DEVICE_ID_ALI_M5451:
		if (sc->sc_revision > 0x01)
			addr = AUTRI_ALI_ACWR;
		else
			addr = AUTRI_ALI_ACRD;
		cmd  = AUTRI_ALI_ACRD_CMD_READ;
		busy = AUTRI_ALI_ACRD_BUSY_READ;
		break;
	default:
		printf("%s: autri_read_codec : unknown device\n",
		    sc->sc_dev.dv_xname);
		return -1;
	}

	/* wait for 'Ready to Read' */
	for (count=0; count < 0xffff; count++) {
		if ((TREAD4(sc, addr) & busy) == 0)
			break;
		DELAY(1);
	}

	if (count == 0xffff) {
		printf("%s: Codec timeout. Busy reading AC97 codec.\n",
		    sc->sc_dev.dv_xname);
		return -1;
	}

	/* send Read Command to AC97 */
	TWRITE4(sc, addr, (index & 0x7f) | cmd);

	/* wait for 'Returned data is available' */
	for (count=0; count < 0xffff; count++) {
		status = TREAD4(sc, addr);
		if ((status & busy) == 0)
			break;
		DELAY(1);
	}

	if (count == 0xffff) {
		printf("%s: Codec timeout. Busy reading AC97 codec.\n",
		    sc->sc_dev.dv_xname);
		return -1;
	}

	*data =  (status >> 16) & 0x0000ffff;
	/*DPRINTF(("autri_read_codec(0x%X) return 0x%X\n",reg,*data));*/
	return 0;
}

int
autri_write_codec(void *sc_, u_int8_t index, u_int16_t data)
{
	struct autri_codec_softc *codec = sc_;
	struct autri_softc *sc = codec->sc;
	u_int32_t addr, cmd, busy;
	u_int16_t count;

	/*DPRINTF(("autri_write_codec(0x%X,0x%X)\n",index,data));*/

	switch (sc->sc_devid) {
	case AUTRI_DEVICE_ID_4DWAVE_DX:
		addr = AUTRI_DX_ACR0;
		cmd  = AUTRI_DX_ACR0_CMD_WRITE;
		busy = AUTRI_DX_ACR0_BUSY_WRITE;
		break;
	case AUTRI_DEVICE_ID_4DWAVE_NX:
		addr = AUTRI_NX_ACR1;
		cmd  = AUTRI_NX_ACR1_CMD_WRITE;
		busy = AUTRI_NX_ACR1_BUSY_WRITE;
		break;
	case AUTRI_DEVICE_ID_SIS_7018:
		addr = AUTRI_SIS_ACWR;
		cmd  = AUTRI_SIS_ACWR_CMD_WRITE;
		busy = AUTRI_SIS_ACWR_BUSY_WRITE | AUTRI_SIS_ACWR_AUDIO_BUSY;
		break;
	case AUTRI_DEVICE_ID_ALI_M5451:
		addr = AUTRI_ALI_ACWR;
		cmd  = AUTRI_ALI_ACWR_CMD_WRITE;
		if (sc->sc_revision > 0x01)
			cmd  |= 0x0100;
		busy = AUTRI_ALI_ACWR_BUSY_WRITE;
		break;
	default:
		printf("%s: autri_write_codec : unknown device.\n",
		    sc->sc_dev.dv_xname);
		return -1;
	}

	/* wait for 'Ready to Write' */
	for (count=0; count < 0xffff; count++) {
		if ((TREAD4(sc, addr) & busy) == 0)
			break;
		DELAY(1);
	}

	if (count == 0xffff) {
		printf("%s: Codec timeout. Busy writing AC97 codec\n",
		    sc->sc_dev.dv_xname);
		return -1;
	}

	/* send Write Command to AC97 */
	TWRITE4(sc, addr, (data << 16) | (index & 0x7f) | cmd);

	return 0;
}

void
autri_reset_codec(void *sc_)
{
	struct autri_codec_softc *codec = sc_;
	struct autri_softc *sc = codec->sc;
	u_int32_t reg, ready;
	int addr, count = 200;

	DPRINTF(("autri_reset_codec(codec=%p,sc=%p)\n",codec,sc));
	DPRINTF(("sc->sc_devid=%X\n",sc->sc_devid));

	switch (sc->sc_devid) {
	case AUTRI_DEVICE_ID_4DWAVE_DX:
		/* warm reset AC97 codec */
		autri_reg_set_4(sc, AUTRI_DX_ACR2, 1);
		delay(100);
		/* release reset */
		autri_reg_clear_4(sc, AUTRI_DX_ACR2, 1);
		delay(100);

		addr = AUTRI_DX_ACR2;
		ready = AUTRI_DX_ACR2_CODEC_READY;
		break;
	case AUTRI_DEVICE_ID_4DWAVE_NX:
		/* warm reset AC97 codec */
		autri_reg_set_4(sc, AUTRI_NX_ACR0, 1);
		delay(100);
		/* release reset */
		autri_reg_clear_4(sc, AUTRI_NX_ACR0, 1);
		delay(100);

		addr = AUTRI_NX_ACR0;
		ready = AUTRI_NX_ACR0_CODEC_READY;
		break;
	case AUTRI_DEVICE_ID_SIS_7018:
		/* warm reset AC97 codec */
		autri_reg_set_4(sc, AUTRI_SIS_SCTRL, 2);
		delay(1000);
		/* release reset (warm & cold) */
		autri_reg_clear_4(sc, AUTRI_SIS_SCTRL, 3);
		delay(2000);

		addr = AUTRI_SIS_SCTRL;
		ready = AUTRI_SIS_SCTRL_CODEC_READY;
		break;
	case AUTRI_DEVICE_ID_ALI_M5451:
		/* warm reset AC97 codec */
		autri_reg_set_4(sc, AUTRI_ALI_SCTRL, 1);
		delay(100);
		/* release reset (warm & cold) */
		autri_reg_clear_4(sc, AUTRI_ALI_SCTRL, 3);
		delay(100);

		addr = AUTRI_ALI_SCTRL;
		ready = AUTRI_ALI_SCTRL_CODEC_READY;
		break;
	}

	/* wait for 'Codec Ready' */
	while (count--) {
		reg = TREAD4(sc, addr);
		if (reg & ready)
			break;
		delay(1000);
	}

	if (count == 0)
		printf("%s: Codec timeout. AC97 is not ready for operation.\n",
		    sc->sc_dev.dv_xname);
}

enum ac97_host_flags
autri_flags_codec(void *v)
{
	struct autri_codec_softc *sc = v;

	return (sc->flags);
}

/*
 *
 */
const struct pci_matchid autri_devices[] = {
	{ PCI_VENDOR_TRIDENT, PCI_PRODUCT_TRIDENT_4DWAVE_NX },
	{ PCI_VENDOR_SIS, PCI_PRODUCT_SIS_7018 },
	{ PCI_VENDOR_ALI, PCI_PRODUCT_ALI_M5451 }
};

int
autri_match(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args *pa = aux;

	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_TRIDENT &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_TRIDENT_4DWAVE_DX) {
		/*
		 * IBM makes a pcn network card and improperly
		 * sets the vendor and product ID's.  Avoid matching.
		 */
		if (PCI_CLASS(pa->pa_class) == PCI_CLASS_NETWORK)
			return (0);
		else
			return (1);
	}

	return (pci_matchbyid((struct pci_attach_args *)aux, autri_devices,
	    nitems(autri_devices)));
}

void
autri_attach(struct device *parent, struct device *self, void *aux)
{
	struct autri_softc *sc = (struct autri_softc *)self;
	struct pci_attach_args *pa = (struct pci_attach_args *)aux;
	pci_chipset_tag_t pc = pa->pa_pc;
	struct autri_codec_softc *codec;
	bus_size_t iosize;
	pci_intr_handle_t ih;
	char const *intrstr;
	mixer_ctrl_t ctl;
	int i, r;

	sc->sc_devid = pa->pa_id;
	sc->sc_class = pa->pa_class;
	sc->sc_revision = PCI_REVISION(pa->pa_class);

	/* map register to memory */
	if (pci_mapreg_map(pa, AUTRI_PCI_MEMORY_BASE,
	    PCI_MAPREG_TYPE_MEM, 0, &sc->memt, &sc->memh, NULL, &iosize, 0)) {
		printf("%s: can't map mem space\n", sc->sc_dev.dv_xname);
		return;
	}

	/* map and establish the interrupt */
	if (pci_intr_map(pa, &ih)) {
		printf("%s: couldn't map interrupt\n", sc->sc_dev.dv_xname);
		bus_space_unmap(sc->memt, sc->memh, iosize);
		return;
	}
	intrstr = pci_intr_string(pc, ih);
	sc->sc_ih = pci_intr_establish(pc, ih, IPL_AUDIO | IPL_MPSAFE,
	    autri_intr, sc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf("%s: couldn't establish interrupt",
		    sc->sc_dev.dv_xname);
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		bus_space_unmap(sc->memt, sc->memh, iosize);
		return;
	}
	printf(": %s\n", intrstr);

	sc->sc_dmatag = pa->pa_dmat;
	sc->sc_pc = pc;
	sc->sc_pt = pa->pa_tag;

	/* initialize the device */
	autri_init(sc);

	/* attach AC97 codec */
	codec = &sc->sc_codec;
	memcpy(&codec->sc_dev, &sc->sc_dev, sizeof(codec->sc_dev));
	codec->sc = sc;

	codec->host_if.arg = codec;
	codec->host_if.attach = autri_attach_codec;
	codec->host_if.reset = autri_reset_codec;
	codec->host_if.read = autri_read_codec;
	codec->host_if.write = autri_write_codec;
	codec->host_if.flags = autri_flags_codec;
	codec->flags = AC97_HOST_DONT_READ | AC97_HOST_SWAPPED_CHANNELS;
	if (sc->sc_dev.dv_cfdata->cf_flags & 0x0001)
		codec->flags &= ~AC97_HOST_SWAPPED_CHANNELS;

	if ((r = ac97_attach(&codec->host_if)) != 0) {
		printf("%s: can't attach codec (error 0x%X)\n",
		    sc->sc_dev.dv_xname, r);
		pci_intr_disestablish(pc, sc->sc_ih);
		bus_space_unmap(sc->memt, sc->memh, iosize);
		return;
	}

	/* disable mutes */
	for (i = 0; i < 4; i++) {
		static struct {
			char *class, *device;
		} d[] = {
			{ AudioCoutputs, AudioNmaster},
			{ AudioCinputs, AudioNdac},
			{ AudioCinputs, AudioNcd},
			{ AudioCrecord, AudioNvolume},
		};

		ctl.type = AUDIO_MIXER_ENUM;
		ctl.un.ord = 0;

#if 0
		ctl.dev = sc->sc_codec.codec_if->vtbl->get_portnum_by_name(sc->sc_codec.codec_if,
		    d[i].class, d[i].device, AudioNmute);
#endif
		ctl.dev = autri_get_portnum_by_name(sc,d[i].class,
						   d[i].device, AudioNmute);
		autri_mixer_set_port(sc, &ctl);
	}

	/* set a reasonable default volume */
	ctl.type = AUDIO_MIXER_VALUE;
	ctl.un.value.num_channels = 2;
	ctl.un.value.level[AUDIO_MIXER_LEVEL_LEFT] =
	ctl.un.value.level[AUDIO_MIXER_LEVEL_RIGHT] = 127;

	ctl.dev = autri_get_portnum_by_name(sc,AudioCoutputs,AudioNmaster,NULL);
	autri_mixer_set_port(sc, &ctl);

	audio_attach_mi(&autri_hw_if, sc, NULL, &sc->sc_dev);

#if NMIDI > 0
	midi_attach_mi(&autri_midi_hw_if, sc, &sc->sc_dev);
#endif
}

int
autri_activate(struct device *self, int act)
{
	struct autri_softc *sc = (struct autri_softc *)self;

	if (act == DVACT_RESUME) {
		autri_init(sc);
		ac97_resume(&sc->sc_codec.host_if, sc->sc_codec.codec_if);
	}
	return (config_activate_children(self, act));
}

int
autri_init(void *sc_)
{
	struct autri_softc *sc = sc_;
	pcireg_t reg;

	pci_chipset_tag_t pc = sc->sc_pc;
	pcitag_t pt = sc->sc_pt;

	DPRINTF(("in autri_init()\n"));
	DPRINTFN(5,("pci_conf_read(0x40) : 0x%X\n",pci_conf_read(pc,pt,0x40)));
	DPRINTFN(5,("pci_conf_read(0x44) : 0x%X\n",pci_conf_read(pc,pt,0x44)));

	switch (sc->sc_devid) {
	case AUTRI_DEVICE_ID_4DWAVE_DX:
		/* disable Legacy Control */
		pci_conf_write(pc, pt, AUTRI_PCI_DDMA_CFG,0);
		reg = pci_conf_read(pc, pt, AUTRI_PCI_LEGACY_IOBASE);
		pci_conf_write(pc, pt, AUTRI_PCI_LEGACY_IOBASE, reg & 0xffff0000);
		delay(100);
		/* audio engine reset */
		reg = pci_conf_read(pc, pt, AUTRI_PCI_LEGACY_IOBASE);
		pci_conf_write(pc, pt, AUTRI_PCI_LEGACY_IOBASE, reg | 0x00040000);
		delay(100);
		/* release reset */
		reg = pci_conf_read(pc, pt, AUTRI_PCI_LEGACY_IOBASE);
		pci_conf_write(pc, pt, AUTRI_PCI_LEGACY_IOBASE, reg & ~0x00040000);
		delay(100);
		/* DAC on */
		autri_reg_set_4(sc,AUTRI_DX_ACR2,0x02);
		break;
	case AUTRI_DEVICE_ID_4DWAVE_NX:
		/* disable Legacy Control */
		pci_conf_write(pc, pt, AUTRI_PCI_DDMA_CFG,0);
		reg = pci_conf_read(pc, pt, AUTRI_PCI_LEGACY_IOBASE);
		pci_conf_write(pc, pt, AUTRI_PCI_LEGACY_IOBASE, reg & 0xffff0000);
		delay(100);
		/* audio engine reset */
		reg = pci_conf_read(pc, pt, AUTRI_PCI_LEGACY_IOBASE);
		pci_conf_write(pc, pt, AUTRI_PCI_LEGACY_IOBASE, reg | 0x00010000);
		delay(100);
		/* release reset */
		reg = pci_conf_read(pc, pt, AUTRI_PCI_LEGACY_IOBASE);
		pci_conf_write(pc, pt, AUTRI_PCI_LEGACY_IOBASE, reg & ~0x00010000);
		delay(100);
		/* DAC on */
		autri_reg_set_4(sc,AUTRI_NX_ACR0,0x02);
		break;
	case AUTRI_DEVICE_ID_SIS_7018:
		/* disable Legacy Control */
		pci_conf_write(pc, pt, AUTRI_PCI_DDMA_CFG,0);
		reg = pci_conf_read(pc, pt, AUTRI_PCI_LEGACY_IOBASE);
		pci_conf_write(pc, pt, AUTRI_PCI_LEGACY_IOBASE, reg & 0xffff0000);
		delay(100);
		/* reset Digital Controller */
		reg = pci_conf_read(pc, pt, AUTRI_PCI_LEGACY_IOBASE);
		pci_conf_write(pc, pt, AUTRI_PCI_LEGACY_IOBASE, reg | 0x000c0000);
		delay(100);
		/* release reset */
		reg = pci_conf_read(pc, pt, AUTRI_PCI_LEGACY_IOBASE);
		pci_conf_write(pc, pt, AUTRI_PCI_LEGACY_IOBASE, reg & ~0x00040000);
		delay(100);
		/* disable AC97 GPIO interrupt */
		TWRITE1(sc, AUTRI_SIS_ACGPIO, 0);
		/* enable 64 channel mode */
		autri_reg_set_4(sc, AUTRI_LFO_GC_CIR, BANK_B_EN);
		break;
	case AUTRI_DEVICE_ID_ALI_M5451:
		/* disable Legacy Control */
		pci_conf_write(pc, pt, AUTRI_PCI_DDMA_CFG,0);
		reg = pci_conf_read(pc, pt, AUTRI_PCI_LEGACY_IOBASE);
		pci_conf_write(pc, pt, AUTRI_PCI_LEGACY_IOBASE, reg & 0xffff0000);
		delay(100);
		/* reset Digital Controller */
		reg = pci_conf_read(pc, pt, AUTRI_PCI_LEGACY_IOBASE);
		pci_conf_write(pc, pt, AUTRI_PCI_LEGACY_IOBASE, reg | 0x000c0000);
		delay(100);
		/* release reset */
		reg = pci_conf_read(pc, pt, AUTRI_PCI_LEGACY_IOBASE);
		pci_conf_write(pc, pt, AUTRI_PCI_LEGACY_IOBASE, reg & ~0x00040000);
		delay(100);
		/* enable PCM input */
		autri_reg_set_4(sc, AUTRI_ALI_GCONTROL, AUTRI_ALI_GCONTROL_PCM_IN);
		break;
	}

	if (sc->sc_devid == AUTRI_DEVICE_ID_ALI_M5451) {
		sc->sc_play.ch      = 0;
		sc->sc_play.ch_intr = 1;
		sc->sc_rec.ch       = 2;
		sc->sc_rec.ch_intr  = 3;
	} else {
		sc->sc_play.ch      = 0x20;
		sc->sc_play.ch_intr = 0x21;
		sc->sc_rec.ch       = 0x22;
		sc->sc_rec.ch_intr  = 0x23;
	}

	/* clear channel status */
	TWRITE4(sc, AUTRI_STOP_A, 0xffffffff);
	TWRITE4(sc, AUTRI_STOP_B, 0xffffffff);

	/* disable channel interrupt */
	TWRITE4(sc, AUTRI_AINTEN_A, 0);
	TWRITE4(sc, AUTRI_AINTEN_B, 0);

#if 0
	/* TLB */
	if (sc->sc_devid == AUTRI_DEVICE_ID_4DWAVE_NX) {
		TWRITE4(sc,AUTRI_NX_TLBC,0);
	}
#endif

	autri_enable_loop_interrupt(sc);

	DPRINTF(("out autri_init()\n"));
	return 0;
}

void
autri_enable_loop_interrupt(void *sc_)
{
	struct autri_softc *sc = sc_;
	u_int32_t reg;

	/*reg = (ENDLP_IE | MIDLP_IE);*/
	reg = ENDLP_IE;
#if 0
	if (sc->sc_devid == AUTRI_DEVICE_ID_SIS_7018)
		reg |= BANK_B_EN;
#endif
	autri_reg_set_4(sc,AUTRI_LFO_GC_CIR,reg);
}

#if 0
void
autri_disable_loop_interrupt(void *sc_)
{
	struct autri_softc *sc = sc_;
	u_int32_t reg;

	reg = (ENDLP_IE | MIDLP_IE);
	autri_reg_clear_4(sc,AUTRI_LFO_GC_CIR,reg);
}
#endif

int
autri_intr(void *p)
{
	struct autri_softc *sc = p;
	u_int32_t intsrc;
	u_int32_t mask, active[2];
	int ch, endch;
/*
	u_int32_t reg;
	u_int32_t cso,eso;
*/

	mtx_enter(&audio_lock);
	intsrc = TREAD4(sc,AUTRI_MISCINT);
	if ((intsrc & (ADDRESS_IRQ|MPU401_IRQ)) == 0) {
		mtx_leave(&audio_lock);
		return 0;
	}

	if (intsrc & ADDRESS_IRQ) {

		active[0] = TREAD4(sc,AUTRI_AIN_A);
		active[1] = TREAD4(sc,AUTRI_AIN_B);

		if (sc->sc_devid == AUTRI_DEVICE_ID_ALI_M5451) {
			endch = 32;
		} else {
			endch = 64;
		}

		for (ch=0; ch<endch; ch++) {
			mask = 1 << (ch & 0x1f);
			if (active[(ch & 0x20) ? 1 : 0] & mask) {

				/* clear interrupt */
				TWRITE4(sc, (ch & 0x20) ? AUTRI_AIN_B : AUTRI_AIN_A, mask);
				/* disable interrupt */
				autri_reg_clear_4(sc,(ch & 0x20) ? AUTRI_AINTEN_B : AUTRI_AINTEN_A, mask);
#if 0
				reg = TREAD4(sc,AUTRI_LFO_GC_CIR) & ~0x0000003f;
				TWRITE4(sc,AUTRI_LFO_GC_CIR, reg | ch);

				if (sc->sc_devid == AUTRI_DEVICE_ID_4DWAVE_NX) {
				  cso = TREAD4(sc, 0xe0) & 0x00ffffff;
				  eso = TREAD4(sc, 0xe8) & 0x00ffffff;
				} else {
				  cso = (TREAD4(sc, 0xe0) >> 16) & 0x0000ffff;
				  eso = (TREAD4(sc, 0xe8) >> 16) & 0x0000ffff;
				}
				/*printf("cso=%d, eso=%d\n",cso,eso);*/
#endif
				if (ch == sc->sc_play.ch_intr) {
					if (sc->sc_play.intr)
						sc->sc_play.intr(sc->sc_play.intr_arg);
				}

				if (ch == sc->sc_rec.ch_intr) {
					if (sc->sc_rec.intr)
						sc->sc_rec.intr(sc->sc_rec.intr_arg);
				}

				/* enable interrupt */
				autri_reg_set_4(sc, (ch & 0x20) ? AUTRI_AINTEN_B : AUTRI_AINTEN_A, mask);
			}
		}
	}

	if (intsrc & MPU401_IRQ) {
		/* XXX */
	}

	autri_reg_set_4(sc,AUTRI_MISCINT,
		ST_TARGET_REACHED | MIXER_OVERFLOW | MIXER_UNDERFLOW);
	mtx_leave(&audio_lock);
	return 1;
}

/*
 *
 */

int
autri_allocmem(struct autri_softc *sc, size_t size, size_t align,
    struct autri_dma *p)
{
	int error;

	p->size = size;
	error = bus_dmamem_alloc(sc->sc_dmatag, p->size, align, 0,
	    p->segs, nitems(p->segs), &p->nsegs, BUS_DMA_NOWAIT);
	if (error)
		return (error);

	error = bus_dmamem_map(sc->sc_dmatag, p->segs, p->nsegs, p->size,
	    &p->addr, BUS_DMA_NOWAIT|BUS_DMA_COHERENT);
	if (error)
		goto free;

	error = bus_dmamap_create(sc->sc_dmatag, p->size, 1, p->size,
	    0, BUS_DMA_NOWAIT, &p->map);
	if (error)
		goto unmap;

	error = bus_dmamap_load(sc->sc_dmatag, p->map, p->addr, p->size, NULL,
	    BUS_DMA_NOWAIT);
	if (error)
		goto destroy;
	return (0);

destroy:
	bus_dmamap_destroy(sc->sc_dmatag, p->map);
unmap:
	bus_dmamem_unmap(sc->sc_dmatag, p->addr, p->size);
free:
	bus_dmamem_free(sc->sc_dmatag, p->segs, p->nsegs);
	return (error);
}

int
autri_freemem(struct autri_softc *sc, struct autri_dma *p)
{
	bus_dmamap_unload(sc->sc_dmatag, p->map);
	bus_dmamap_destroy(sc->sc_dmatag, p->map);
	bus_dmamem_unmap(sc->sc_dmatag, p->addr, p->size);
	bus_dmamem_free(sc->sc_dmatag, p->segs, p->nsegs);
	return 0;
}

int
autri_open(void *addr, int flags)
{
	DPRINTF(("autri_open()\n"));
	DPRINTFN(5,("MISCINT    : 0x%08X\n",
	    TREAD4((struct autri_softc *)addr, AUTRI_MISCINT)));
	DPRINTFN(5,("LFO_GC_CIR : 0x%08X\n",
	    TREAD4((struct autri_softc *)addr, AUTRI_LFO_GC_CIR)));
	return 0;
}

void
autri_close(void *addr)
{
	DPRINTF(("autri_close()\n"));
}

int
autri_set_params(void *addr, int setmode, int usemode, struct audio_params *play,
    struct audio_params *rec)
{
	struct audio_params *p;
	int mode;

	for (mode = AUMODE_RECORD; mode != -1;
	    mode = mode == AUMODE_RECORD ? AUMODE_PLAY : -1) {
		if ((setmode & mode) == 0)
			continue;
		p = mode == AUMODE_PLAY ? play : rec;
		p->sample_rate = 48000;
		if (p->precision != 8)
			p->precision = 16;
		if (p->channels != 1)
			p->channels = 2;
		p->encoding = p->precision == 16 ?
		    AUDIO_ENCODING_SLINEAR_LE : AUDIO_ENCODING_ULINEAR_LE;
		p->bps = AUDIO_BPS(p->precision);
		p->msb = 1;
	}

	return 0;
}

int
autri_round_blocksize(void *addr, int block)
{
	return ((block + 3) & -4);
}

int
autri_halt_output(void *addr)
{
	struct autri_softc *sc = addr;

	DPRINTF(("autri_halt_output()\n"));
	mtx_enter(&audio_lock);
	sc->sc_play.intr = NULL;
	autri_stopch(sc, sc->sc_play.ch, sc->sc_play.ch_intr);
	autri_disable_interrupt(sc, sc->sc_play.ch_intr);
	mtx_leave(&audio_lock);
	return 0;
}

int
autri_halt_input(void *addr)
{
	struct autri_softc *sc = addr;

	DPRINTF(("autri_halt_input()\n"));
	mtx_enter(&audio_lock);
	sc->sc_rec.intr = NULL;
	autri_stopch(sc, sc->sc_rec.ch, sc->sc_rec.ch_intr);
	autri_disable_interrupt(sc, sc->sc_rec.ch_intr);
	mtx_leave(&audio_lock);
	return 0;
}

int
autri_mixer_set_port(void *addr, mixer_ctrl_t *cp)
{
	struct autri_softc *sc = addr;

	return (sc->sc_codec.codec_if->vtbl->mixer_set_port(
	    sc->sc_codec.codec_if, cp));
}

int
autri_mixer_get_port(void *addr, mixer_ctrl_t *cp)
{
	struct autri_softc *sc = addr;

	return (sc->sc_codec.codec_if->vtbl->mixer_get_port(
	    sc->sc_codec.codec_if, cp));
}

int
autri_query_devinfo(void *addr, mixer_devinfo_t *dip)
{
	struct autri_softc *sc = addr;

	return (sc->sc_codec.codec_if->vtbl->query_devinfo(
	    sc->sc_codec.codec_if, dip));
}

int
autri_get_portnum_by_name(struct autri_softc *sc, char *class, char *device,
    char *qualifier)
{
	return (sc->sc_codec.codec_if->vtbl->get_portnum_by_name(
	    sc->sc_codec.codec_if, class, device, qualifier));
}

void *
autri_malloc(void *addr, int direction, size_t size, int pool, int flags)
{
	struct autri_softc *sc = addr;
	struct autri_dma *p;
	int error;

	p = malloc(sizeof(*p), pool, flags);
	if (!p)
		return NULL;

#if 0
	error = autri_allocmem(sc, size, 16, p);
#endif
	error = autri_allocmem(sc, size, 0x10000, p);
	if (error) {
		free(p, pool, sizeof(*p));
		return NULL;
	}

	p->next = sc->sc_dmas;
	sc->sc_dmas = p;
	return KERNADDR(p);
}

void
autri_free(void *addr, void *ptr, int pool)
{
	struct autri_softc *sc = addr;
	struct autri_dma **pp, *p;

	for (pp = &sc->sc_dmas; (p = *pp) != NULL; pp = &p->next) {
		if (KERNADDR(p) == ptr) {
			autri_freemem(sc, p);
			*pp = p->next;
			free(p, pool, sizeof(*p));
			return;
		}
	}
}

struct autri_dma *
autri_find_dma(struct autri_softc *sc, void *addr)
{
	struct autri_dma *p;

	for (p = sc->sc_dmas; p && KERNADDR(p) != addr; p = p->next)
		;

	return p;
}

void
autri_setup_channel(struct autri_softc *sc, int mode, struct audio_params *param)
{
	int i, ch, channel;
	u_int32_t reg, cr[5];
	u_int32_t cso, eso;
	u_int32_t delta, dch[2], ctrl;
	u_int32_t alpha_fms, fm_vol, attribute;

	u_int32_t dmaaddr, dmalen;
	int factor, rvol, cvol;
	struct autri_chstatus *chst;

	ctrl = AUTRI_CTRL_LOOPMODE;
	switch (param->encoding) {
	case AUDIO_ENCODING_SLINEAR_BE:
	case AUDIO_ENCODING_SLINEAR_LE:
		ctrl |= AUTRI_CTRL_SIGNED;
		break;
	}

	factor = 0;
	if (param->precision == 16) {
		ctrl |= AUTRI_CTRL_16BIT;
		factor++;
	}

	if (param->channels == 2) {
		ctrl |= AUTRI_CTRL_STEREO;
		factor++;
	}

	delta = (u_int32_t)param->sample_rate;
	if (delta < 4000)
		delta = 4000;
	if (delta > 48000)
		delta = 48000;

	attribute = 0;

	dch[1] = ((delta << 12) / 48000) & 0x0000ffff;
	if (mode == AUMODE_PLAY) {
		chst = &sc->sc_play;
		dch[0] = ((delta << 12) / 48000) & 0x0000ffff;
		ctrl |= AUTRI_CTRL_WAVEVOL;
/*
		if (sc->sc_devid == AUTRI_DEVICE_ID_ALI_M5451)
			ctrl |= 0x80000000;
*/
	} else {
		chst = &sc->sc_rec;
		dch[0] = ((48000 << 12) / delta) & 0x0000ffff;
		if (sc->sc_devid == AUTRI_DEVICE_ID_SIS_7018) {
			ctrl |= AUTRI_CTRL_MUTE_SIS;
			attribute = AUTRI_ATTR_PCMREC_SIS;
			if (delta != 48000)
				attribute |= AUTRI_ATTR_ENASRC_SIS;
		}
		ctrl |= AUTRI_CTRL_MUTE;
	}

	dmaaddr = DMAADDR(chst->dma);
	cso = alpha_fms = 0;
	rvol = cvol = 0x7f;
	fm_vol = 0x0 | ((rvol & 0x7f) << 7) | (cvol & 0x7f);

	for (ch=0; ch<2; ch++) {

		if (ch == 0)
			dmalen = (chst->length >> factor);
		else {
			/* channel for interrupt */
			dmalen = (chst->blksize >> factor);
			if (sc->sc_devid == AUTRI_DEVICE_ID_SIS_7018)
				ctrl |= AUTRI_CTRL_MUTE_SIS;
			else
				ctrl |= AUTRI_CTRL_MUTE;
			attribute = 0;
		}

		eso = dmalen - 1;

		switch (sc->sc_devid) {
		case AUTRI_DEVICE_ID_4DWAVE_DX:
			cr[0] = (cso << 16) | (alpha_fms & 0x0000ffff);
			cr[1] = dmaaddr;
			cr[2] = (eso << 16) | (dch[ch] & 0x0000ffff);
			cr[3] = fm_vol;
			cr[4] = ctrl;
			break;
		case AUTRI_DEVICE_ID_4DWAVE_NX:
			cr[0] = (dch[ch] << 24) | (cso & 0x00ffffff);
			cr[1] = dmaaddr;
			cr[2] = ((dch[ch] << 16) & 0xff000000) | (eso & 0x00ffffff);
			cr[3] = (alpha_fms << 16) | (fm_vol & 0x0000ffff);
			cr[4] = ctrl;
			break;
		case AUTRI_DEVICE_ID_SIS_7018:
			cr[0] = (cso << 16) | (alpha_fms & 0x0000ffff);
			cr[1] = dmaaddr;
			cr[2] = (eso << 16) | (dch[ch] & 0x0000ffff);
			cr[3] = attribute;
			cr[4] = ctrl;
			break;
		case AUTRI_DEVICE_ID_ALI_M5451:
			cr[0] = (cso << 16) | (alpha_fms & 0x0000ffff);
			cr[1] = dmaaddr;
			cr[2] = (eso << 16) | (dch[ch] & 0x0000ffff);
			cr[3] = 0;
			cr[4] = ctrl;
			break;
		}

		/* write channel data */
		channel = (ch == 0) ? chst->ch : chst->ch_intr;

		reg = TREAD4(sc,AUTRI_LFO_GC_CIR) & ~0x0000003f;
		TWRITE4(sc,AUTRI_LFO_GC_CIR, reg | channel);

		for (i=0; i<5; i++) {
			TWRITE4(sc, AUTRI_ARAM_CR + i*sizeof(cr[0]), cr[i]);
			DPRINTFN(5,("cr[%d] : 0x%08X\n", i, cr[i]));
		}

		/* Bank A only */
		if (channel < 0x20) {
			TWRITE4(sc, AUTRI_EBUF1, AUTRI_EMOD_STILL);
			TWRITE4(sc, AUTRI_EBUF2, AUTRI_EMOD_STILL);
		}
	}

}

int
autri_trigger_output(void *addr, void *start, void *end, int blksize,
    void (*intr)(void *), void *arg, struct audio_params *param)
{
	struct autri_softc *sc = addr;
	struct autri_dma *p;

	DPRINTFN(5,("autri_trigger_output: sc=%p start=%p end=%p "
	    "blksize=%d intr=%p(%p)\n", addr, start, end, blksize, intr, arg));

	sc->sc_play.intr = intr;
	sc->sc_play.intr_arg = arg;
	sc->sc_play.offset = 0;
	sc->sc_play.blksize = blksize;
	sc->sc_play.length = (char *)end - (char *)start;

	p = autri_find_dma(sc, start);
	if (!p) {
		printf("autri_trigger_output: bad addr %p\n", start);
		return (EINVAL);
	}

	sc->sc_play.dma = p;

	/* */
	mtx_enter(&audio_lock);
	autri_setup_channel(sc, AUMODE_PLAY, param);

	/* volume set to no attenuation */
	TWRITE4(sc, AUTRI_MUSICVOL_WAVEVOL, 0);

	/* enable interrupt */
	autri_enable_interrupt(sc, sc->sc_play.ch_intr);

	/* start channel */
	autri_startch(sc, sc->sc_play.ch, sc->sc_play.ch_intr);
	mtx_leave(&audio_lock);
	return 0;
}

int
autri_trigger_input(void *addr, void *start, void *end, int blksize, void (*intr)(void *), void *arg, struct audio_params *param)
{
	struct autri_softc *sc = addr;
	struct autri_dma *p;

	DPRINTFN(5,("autri_trigger_input: sc=%p start=%p end=%p "
	    "blksize=%d intr=%p(%p)\n", addr, start, end, blksize, intr, arg));

	sc->sc_rec.intr = intr;
	sc->sc_rec.intr_arg = arg;
	sc->sc_rec.offset = 0;
	sc->sc_rec.blksize = blksize;
	sc->sc_rec.length = (char *)end - (char *)start;

	/* */
	p = autri_find_dma(sc, start);
	if (!p) {
		printf("autri_trigger_input: bad addr %p\n", start);
		return (EINVAL);
	}

	sc->sc_rec.dma = p;
	mtx_enter(&audio_lock);

	/* */
	if (sc->sc_devid == AUTRI_DEVICE_ID_4DWAVE_NX) {
		autri_reg_set_4(sc, AUTRI_NX_ACR0, AUTRI_NX_ACR0_PSB_CAPTURE);
		TWRITE1(sc, AUTRI_NX_RCI3, AUTRI_NX_RCI3_ENABLE | sc->sc_rec.ch);
	}

#if 0
	/* 4DWAVE only allows capturing at a 48KHz rate */
	if (sc->sc_devid == AUTRI_DEVICE_ID_4DWAVE_DX ||
	    sc->sc_devid == AUTRI_DEVICE_ID_4DWAVE_NX)
		param->sample_rate = 48000;
#endif

	autri_setup_channel(sc, AUMODE_RECORD, param);

	/* enable interrupt */
	autri_enable_interrupt(sc, sc->sc_rec.ch_intr);

	/* start channel */
	autri_startch(sc, sc->sc_rec.ch, sc->sc_rec.ch_intr);
	mtx_leave(&audio_lock);
	return 0;
}

#if 0
int
autri_halt(struct autri_softc *sc)
{
	DPRINTF(("autri_halt().\n"));
	/*autri_stopch(sc);*/
	autri_disable_interrupt(sc, sc->sc_play.channel);
	autri_disable_interrupt(sc, sc->sc_rec.channel);
	return 0;
}
#endif

void
autri_enable_interrupt(struct autri_softc *sc, int ch)
{
	int reg;

	reg = (ch & 0x20) ? AUTRI_AINTEN_B : AUTRI_AINTEN_A;
	ch &= 0x1f;

	autri_reg_set_4(sc, reg, 1 << ch);
}

void
autri_disable_interrupt(struct autri_softc *sc, int ch)
{
	int reg;

	reg = (ch & 0x20) ? AUTRI_AINTEN_B : AUTRI_AINTEN_A;
	ch &= 0x1f;

	autri_reg_clear_4(sc, reg, 1 << ch);
}

void
autri_startch(struct autri_softc *sc, int ch, int ch_intr)
{
	int reg;
	u_int32_t chmask;

	reg = (ch & 0x20) ? AUTRI_START_B : AUTRI_START_A;
	ch &= 0x1f;
	chmask = (1 << ch) | (1 << ch_intr);

	autri_reg_set_4(sc, reg, chmask);
}

void
autri_stopch(struct autri_softc *sc, int ch, int ch_intr)
{
	int reg;
	u_int32_t chmask;

	reg = (ch & 0x20) ? AUTRI_STOP_B : AUTRI_STOP_A;
	ch &= 0x1f;
	chmask = (1 << ch) | (1 << ch_intr);

	autri_reg_set_4(sc, reg, chmask);
}

#if NMIDI > 0
int
autri_midi_open(void *addr, int flags, void (*iintr)(void *, int),
    void (*ointr)(void *), void *arg)
{
	struct autri_softc *sc = addr;

	DPRINTF(("autri_midi_open()\n"));

	DPRINTFN(5,("MPUR1 : 0x%02X\n",TREAD1(sc,AUTRI_MPUR1)));
	DPRINTFN(5,("MPUR2 : 0x%02X\n",TREAD1(sc,AUTRI_MPUR2)));

	sc->sc_iintr = iintr;
	sc->sc_ointr = ointr;
	sc->sc_arg = arg;

	if (flags & FREAD)
		autri_reg_clear_1(sc, AUTRI_MPUR2, AUTRI_MIDIIN_ENABLE_INTR);

	if (flags & FWRITE)
		autri_reg_set_1(sc, AUTRI_MPUR2, AUTRI_MIDIOUT_CONNECT);

	return (0);
}

void
autri_midi_close(void *addr)
{
	struct autri_softc *sc = addr;

	DPRINTF(("autri_midi_close()\n"));

	/* give uart a chance to drain */
	tsleep_nsec(sc, PWAIT, "autri", MSEC_TO_NSEC(100));

	sc->sc_iintr = NULL;
	sc->sc_ointr = NULL;
}

int
autri_midi_output(void *addr, int d)
{
	struct autri_softc *sc = addr;

	if ((TREAD1(sc, AUTRI_MPUR1) & AUTRI_MIDIOUT_READY) != 0) {
		TWRITE1(sc, AUTRI_MPUR0, d);
		return 0;
	}
	return 1;
}

void
autri_midi_getinfo(void *addr, struct midi_info *mi)
{
	mi->name = "4DWAVE MIDI UART";
	mi->props = MIDI_PROP_CAN_INPUT;
}

#endif
