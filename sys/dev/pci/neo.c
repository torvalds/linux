/*      $OpenBSD: neo.c,v 1.43 2025/07/15 13:40:02 jsg Exp $       */

/*
 * Copyright (c) 1999 Cameron Grant <gandalf@vilnya.demon.co.uk>
 * All rights reserved.
 *
 * Derived from the public domain Linux driver
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/dev/sound/pci/neomagic.c,v 1.8 2000/03/20 15:30:50 cg Exp $
 */



#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <dev/pci/pcidevs.h>
#include <dev/pci/pcivar.h>

#include <sys/audioio.h>
#include <dev/audio_if.h>
#include <dev/ic/ac97.h>

#include <dev/pci/neoreg.h>

/* -------------------------------------------------------------------- */
/*
 * As of 04/13/00, public documentation on the Neomagic 256 is not available.
 * These comments were gleaned by looking at the driver carefully.
 *
 * The Neomagic 256 AV/ZX chips provide both video and audio capabilities
 * on one chip. About 2-6 megabytes of memory are associated with
 * the chip. Most of this goes to video frame buffers, but some is used for
 * audio buffering.
 *
 * Unlike most PCI audio chips, the Neomagic chip does not rely on DMA.
 * Instead, the chip allows you to carve two ring buffers out of its
 * memory. How you carve this and how much you can carve seems to be
 * voodoo. The algorithm is in nm_init.
 *
 * Most Neomagic audio chips use the AC-97 codec interface. However, there
 * seem to be a select few chips 256AV chips that do not support AC-97.
 * This driver does not support them but there are rumors that it
 * might work with wss isa drivers. This might require some playing around
 * with your BIOS.
 *
 * The Neomagic 256 AV/ZX have 2 PCI I/O region descriptors. Both of
 * them describe a memory region. The frame buffer is the first region
 * and the register set is the second region.
 *
 * The register manipulation logic is taken from the Linux driver,
 * which is in the public domain.
 *
 * The Neomagic is even nice enough to map the AC-97 codec registers into
 * the register space to allow direct manipulation. Watch out, accessing
 * AC-97 registers on the Neomagic requires great delicateness, otherwise
 * the thing will hang the PCI bus, rendering your system frozen.
 *
 * For one, it seems the Neomagic status register that reports AC-97
 * readiness should NOT be polled more often than once each 1ms.
 *
 * Also, writes to the AC-97 register space may take over 40us to
 * complete.
 *
 * Unlike many sound engines, the Neomagic does not support (as fas as
 * we know :) ) the notion of interrupting every n bytes transferred,
 * unlike many DMA engines.  Instead, it allows you to specify one
 * location in each ring buffer (called the watermark). When the chip
 * passes that location while playing, it signals an interrupt.
 *
 * The ring buffer size is currently 16k. That is about 100ms of audio
 * at 44.1khz/stereo/16 bit. However, to keep the buffer full, interrupts
 * are generated more often than that, so 20-40 interrupts per second
 * should not be unexpected. Increasing BUFFSIZE should help minimize
 * the glitches due to drivers that spend too much time looping at high
 * privilege levels as well as the impact of badly written audio
 * interface clients.
 *
 * TO-DO list:
 *    neo_malloc/neo_free are still seriously broken.
 *
 *    Figure out interaction with video stuff (look at Xfree86 driver?)
 *
 *    Power management (neoactivate)
 *
 *    Fix detection of Neo devices that don't work this driver (see neo_attach)
 *
 *    Figure out how to shrink that huge table neo-coeff.h
 */

#define	NM_BUFFSIZE	16384

#define NM256AV_PCI_ID  0x800510c8
#define NM256ZX_PCI_ID  0x800610c8

/* device private data */
struct neo_softc {
	struct          device dev;

	bus_space_tag_t bufiot;
	bus_space_handle_t  bufioh;

	bus_space_tag_t regiot;
	bus_space_handle_t  regioh;

	u_int32_t	type;
	void            *ih;

	void	(*pintr)(void *);	/* dma completion intr handler */
	void	*parg;		/* arg for intr() */

	void	(*rintr)(void *);	/* dma completion intr handler */
	void	*rarg;		/* arg for intr() */

	u_int32_t	ac97_base, ac97_status, ac97_busy;
	u_int32_t	buftop, pbuf, rbuf, cbuf, acbuf;
	u_int32_t	playint, recint, misc1int, misc2int;
	u_int32_t	irsz, badintr;

        u_int32_t       pbufsize;
        u_int32_t       rbufsize;

	u_int32_t       pblksize;
	u_int32_t       rblksize;

        u_int32_t       pwmark;
        u_int32_t       rwmark;

	struct ac97_codec_if *codec_if;
	struct ac97_host_if host_if;
};

static struct neo_firmware *nf;

/* -------------------------------------------------------------------- */

/*
 * prototypes
 */

static int	 nm_waitcd(struct neo_softc *sc);
static int	 nm_loadcoeff(struct neo_softc *sc, int dir, int num);
static int       nm_init(struct neo_softc *);

int    nmchan_getptr(struct neo_softc *, int);
/* talk to the card */
static u_int32_t nm_rd(struct neo_softc *, int, int);
static void	 nm_wr(struct neo_softc *, int, u_int32_t, int);
static u_int32_t nm_rdbuf(struct neo_softc *, int, int);
static void	 nm_wrbuf(struct neo_softc *, int, u_int32_t, int);

int	neo_match(struct device *, void *, void *);
void	neo_attach(struct device *, struct device *, void *);
int	neo_activate(struct device *, int);
int	neo_intr(void *);

int	neo_open(void *, int);
void	neo_close(void *);
int	neo_set_params(void *, int, int, struct audio_params *, struct audio_params *);
int	neo_round_blocksize(void *, int);
int	neo_trigger_output(void *, void *, void *, int, void (*)(void *),
	    void *, struct audio_params *);
int	neo_trigger_input(void *, void *, void *, int, void (*)(void *),
	    void *, struct audio_params *);
int	neo_halt_output(void *);
int	neo_halt_input(void *);
int	neo_mixer_set_port(void *, mixer_ctrl_t *);
int	neo_mixer_get_port(void *, mixer_ctrl_t *);
int     neo_attach_codec(void *sc, struct ac97_codec_if *);
int	neo_read_codec(void *sc, u_int8_t a, u_int16_t *d);
int	neo_write_codec(void *sc, u_int8_t a, u_int16_t d);
void    neo_reset_codec(void *sc);
enum ac97_host_flags neo_flags_codec(void *sc);
int	neo_query_devinfo(void *, mixer_devinfo_t *);
void   *neo_malloc(void *, int, size_t, int, int);
void	neo_free(void *, void *, int);
size_t	neo_round_buffersize(void *, int, size_t);

struct cfdriver neo_cd = {
	NULL, "neo", DV_DULL
};


const struct cfattach neo_ca = {
	sizeof(struct neo_softc), neo_match, neo_attach, NULL,
	neo_activate
};


#if 0
static u_int32_t badcards[] = {
	0x0007103c,
	0x008f1028,
};
#endif

#define NUM_BADCARDS (sizeof(badcards) / sizeof(u_int32_t))

/* The actual rates supported by the card. */
static int samplerates[9] = {
	8000,
	11025,
	16000,
	22050,
	24000,
	32000,
	44100,
	48000,
	99999999
};

/* -------------------------------------------------------------------- */

const struct audio_hw_if neo_hw_if = {
	.open = neo_open,
	.close = neo_close,
	.set_params = neo_set_params,
	.round_blocksize = neo_round_blocksize,
	.halt_output = neo_halt_output,
	.halt_input = neo_halt_input,
	.set_port = neo_mixer_set_port,
	.get_port = neo_mixer_get_port,
	.query_devinfo = neo_query_devinfo,
	.allocm = neo_malloc,
	.freem = neo_free,
	.round_buffersize = neo_round_buffersize,
	.trigger_output = neo_trigger_output,
	.trigger_input = neo_trigger_input,

};

/* -------------------------------------------------------------------- */

/* Hardware */
static u_int32_t
nm_rd(struct neo_softc *sc, int regno, int size)
{
	bus_space_tag_t st = sc->regiot;
	bus_space_handle_t sh = sc->regioh;

	switch (size) {
	case 1:
		return bus_space_read_1(st, sh, regno);
	case 2:
		return bus_space_read_2(st, sh, regno);
	case 4:
		return bus_space_read_4(st, sh, regno);
	default:
		return (0xffffffff);
	}
}

static void
nm_wr(struct neo_softc *sc, int regno, u_int32_t data, int size)
{
	bus_space_tag_t st = sc->regiot;
	bus_space_handle_t sh = sc->regioh;

	switch (size) {
	case 1:
		bus_space_write_1(st, sh, regno, data);
		break;
	case 2:
		bus_space_write_2(st, sh, regno, data);
		break;
	case 4:
		bus_space_write_4(st, sh, regno, data);
		break;
	}
}

static u_int32_t
nm_rdbuf(struct neo_softc *sc, int regno, int size)
{
	bus_space_tag_t st = sc->bufiot;
	bus_space_handle_t sh = sc->bufioh;

	switch (size) {
	case 1:
		return bus_space_read_1(st, sh, regno);
	case 2:
		return bus_space_read_2(st, sh, regno);
	case 4:
		return bus_space_read_4(st, sh, regno);
	default:
		return (0xffffffff);
	}
}

static void
nm_wrbuf(struct neo_softc *sc, int regno, u_int32_t data, int size)
{
	bus_space_tag_t st = sc->bufiot;
	bus_space_handle_t sh = sc->bufioh;

	switch (size) {
	case 1:
		bus_space_write_1(st, sh, regno, data);
		break;
	case 2:
		bus_space_write_2(st, sh, regno, data);
		break;
	case 4:
		bus_space_write_4(st, sh, regno, data);
		break;
	}
}

/* ac97 codec */
static int
nm_waitcd(struct neo_softc *sc)
{
	int cnt = 10;
	int fail = 1;

	while (cnt-- > 0) {
		if (nm_rd(sc, sc->ac97_status, 2) & sc->ac97_busy)
			DELAY(100);
		else {
		        fail = 0;
			break;
		}
	}
	return (fail);
}


static void
nm_ackint(struct neo_softc *sc, u_int32_t num)
{
	if (sc->type == NM256AV_PCI_ID)
		nm_wr(sc, NM_INT_REG, num << 1, 2);
	else if (sc->type == NM256ZX_PCI_ID)
		nm_wr(sc, NM_INT_REG, num, 4);
}

static int
nm_loadcoeff(struct neo_softc *sc, int dir, int num)
{
	int ofs, sz, i;
	u_int32_t addr;

	if (nf == NULL) {
		size_t buflen;
		u_char *buf;
		int error;

		error = loadfirmware("neo-coefficients", &buf, &buflen);
		if (error)
			return (error);
		nf = (struct neo_firmware *)buf;
	}

	addr = (dir == AUMODE_PLAY)? 0x01c : 0x21c;
	if (dir == AUMODE_RECORD)
		num += 8;
	sz = nf->coefficientSizes[num];
	ofs = 0;
	while (num-- > 0)
		ofs+= nf->coefficientSizes[num];
	for (i = 0; i < sz; i++)
		nm_wrbuf(sc, sc->cbuf + i, nf->coefficients[ofs + i], 1);
	nm_wr(sc, addr, sc->cbuf, 4);
	if (dir == AUMODE_PLAY)
		sz--;
	nm_wr(sc, addr + 4, sc->cbuf + sz, 4);
	return (0);
}

int
nmchan_getptr(struct neo_softc *sc, int mode)
{
	if (mode == AUMODE_PLAY)
		return (nm_rd(sc, NM_PBUFFER_CURRP, 4) - sc->pbuf);
	else
		return (nm_rd(sc, NM_RBUFFER_CURRP, 4) - sc->rbuf);
}


/* The interrupt handler */
int
neo_intr(void *p)
{
	struct neo_softc *sc = (struct neo_softc *)p;
	int status, x;
	int rv = 0;

	mtx_enter(&audio_lock);
	status = nm_rd(sc, NM_INT_REG, sc->irsz);

	if (status & sc->playint) {
		status &= ~sc->playint;

		sc->pwmark += sc->pblksize;
		sc->pwmark %= sc->pbufsize;

		nm_wr(sc, NM_PBUFFER_WMARK, sc->pbuf + sc->pwmark, 4);

		nm_ackint(sc, sc->playint);

		if (sc->pintr)
			(*sc->pintr)(sc->parg);

		rv = 1;
	}
	if (status & sc->recint) {
		status &= ~sc->recint;

		sc->rwmark += sc->rblksize;
		sc->rwmark %= sc->rbufsize;

		nm_ackint(sc, sc->recint);
		if (sc->rintr)
			(*sc->rintr)(sc->rarg);

		rv = 1;
	}
	if (status & sc->misc1int) {
		status &= ~sc->misc1int;
		nm_ackint(sc, sc->misc1int);
		x = nm_rd(sc, 0x400, 1);
		nm_wr(sc, 0x400, x | 2, 1);
		printf("%s: misc int 1\n", sc->dev.dv_xname);
		rv = 1;
	}
	if (status & sc->misc2int) {
		status &= ~sc->misc2int;
		nm_ackint(sc, sc->misc2int);
		x = nm_rd(sc, 0x400, 1);
		nm_wr(sc, 0x400, x & ~2, 1);
		printf("%s: misc int 2\n", sc->dev.dv_xname);
		rv = 1;
	}
	if (status) {
		status &= ~sc->misc2int;
		nm_ackint(sc, sc->misc2int);
		printf("%s: unknown int\n", sc->dev.dv_xname);
		rv = 1;
	}
	mtx_leave(&audio_lock);
	return (rv);
}

/* -------------------------------------------------------------------- */

/*
 * Probe and attach the card
 */

static int
nm_init(struct neo_softc *sc)
{
	u_int32_t ofs, i;

	if (sc->type == NM256AV_PCI_ID) {
		sc->ac97_base = NM_MIXER_OFFSET;
		sc->ac97_status = NM_MIXER_STATUS_OFFSET;
		sc->ac97_busy = NM_MIXER_READY_MASK;

		sc->buftop = 2560 * 1024;

		sc->irsz = 2;
		sc->playint = NM_PLAYBACK_INT;
		sc->recint = NM_RECORD_INT;
		sc->misc1int = NM_MISC_INT_1;
		sc->misc2int = NM_MISC_INT_2;
	} else if (sc->type == NM256ZX_PCI_ID) {
		sc->ac97_base = NM_MIXER_OFFSET;
		sc->ac97_status = NM2_MIXER_STATUS_OFFSET;
		sc->ac97_busy = NM2_MIXER_READY_MASK;

		sc->buftop = (nm_rd(sc, 0xa0b, 2)? 6144 : 4096) * 1024;

		sc->irsz = 4;
		sc->playint = NM2_PLAYBACK_INT;
		sc->recint = NM2_RECORD_INT;
		sc->misc1int = NM2_MISC_INT_1;
		sc->misc2int = NM2_MISC_INT_2;
	} else return -1;
	sc->badintr = 0;
	ofs = sc->buftop - 0x0400;
	sc->buftop -= 0x1400;

	if ((nm_rdbuf(sc, ofs, 4) & NM_SIG_MASK) == NM_SIGNATURE) {
		i = nm_rdbuf(sc, ofs + 4, 4);
		if (i != 0 && i != 0xffffffff)
			sc->buftop = i;
	}

	sc->cbuf = sc->buftop - NM_MAX_COEFFICIENT;
	sc->rbuf = sc->cbuf - NM_BUFFSIZE;
	sc->pbuf = sc->rbuf - NM_BUFFSIZE;
	sc->acbuf = sc->pbuf - (NM_TOTAL_COEFF_COUNT * 4);

	nm_wr(sc, 0, 0x11, 1);
	nm_wr(sc, NM_RECORD_ENABLE_REG, 0, 1);
	nm_wr(sc, 0x214, 0, 2);

	return 0;
}


void
neo_attach(struct device *parent, struct device *self, void *aux)
{
	struct neo_softc *sc = (struct neo_softc *)self;
	struct pci_attach_args *pa = (struct pci_attach_args *)aux;
	pci_chipset_tag_t pc = pa->pa_pc;
	char const *intrstr;
	pci_intr_handle_t ih;
	int error;

	sc->type = pa->pa_id;

	/* Map I/O register */
	if (pci_mapreg_map(pa, PCI_MAPS, PCI_MAPREG_TYPE_MEM, 0,
			   &sc->bufiot, &sc->bufioh, NULL, NULL, 0)) {
		printf("\n%s: can't map i/o space\n", sc->dev.dv_xname);
		return;
	}


	if (pci_mapreg_map(pa, PCI_MAPS + 4, PCI_MAPREG_TYPE_MEM, 0,
			   &sc->regiot, &sc->regioh, NULL, NULL, 0)) {
		printf("\n%s: can't map i/o space\n", sc->dev.dv_xname);
		return;
	}

	/* Map and establish the interrupt. */
	if (pci_intr_map(pa, &ih)) {
		printf("\n%s: couldn't map interrupt\n", sc->dev.dv_xname);
		return;
	}
	intrstr = pci_intr_string(pc, ih);
	sc->ih = pci_intr_establish(pc, ih, IPL_AUDIO | IPL_MPSAFE,
	    neo_intr, sc, sc->dev.dv_xname);

	if (sc->ih == NULL) {
		printf("\n%s: couldn't establish interrupt",
		       sc->dev.dv_xname);
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		return;
	}
	printf(": %s\n", intrstr);

	if ((error = nm_init(sc)) != 0)
		return;

	sc->host_if.arg = sc;

	sc->host_if.attach = neo_attach_codec;
	sc->host_if.read   = neo_read_codec;
	sc->host_if.write  = neo_write_codec;
	sc->host_if.reset  = neo_reset_codec;
	sc->host_if.flags  = neo_flags_codec;

	if ((error = ac97_attach(&sc->host_if)) != 0)
		return;

	audio_attach_mi(&neo_hw_if, sc, NULL, &sc->dev);

	return;
}

int
neo_activate(struct device *self, int act)
{
	struct neo_softc *sc = (struct neo_softc *)self;

	switch (act) {
	case DVACT_SUSPEND:
		break;
	case DVACT_RESUME:
		nm_init(sc);
		break;
	}
	return 0;
}

int
neo_match(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args *pa = (struct pci_attach_args *) aux;
#if 0
	u_int32_t subdev, badcard;
#endif

	if (PCI_VENDOR(pa->pa_id) != PCI_VENDOR_NEOMAGIC)
		return (0);

#if 0
	subdev = (pci_get_subdevice(dev) << 16) | pci_get_subvendor(dev);
#endif
	switch (PCI_PRODUCT(pa->pa_id)) {
	case PCI_PRODUCT_NEOMAGIC_NM256AV:
#if 0
		i = 0;
		while ((i < NUM_BADCARDS) && (badcards[i] != subdev))
			i++;
		if (i == NUM_BADCARDS)
			s = "NeoMagic 256AV";
		DEB(else)
			DEB(device_printf(dev, "this is a non-ac97 NM256AV, not attaching\n"));
		return (1);
#endif
	case PCI_PRODUCT_NEOMAGIC_NM256ZX:
		return (1);
	}

	return (0);
}

int
neo_read_codec(void *sc_, u_int8_t a, u_int16_t *d)
{
	struct neo_softc *sc = sc_;

	if (!nm_waitcd(sc)) {
		*d = nm_rd(sc, sc->ac97_base + a, 2);
		DELAY(1000);
		return 0;
	}

	return (ENXIO);
}


int
neo_write_codec(void *sc_, u_int8_t a, u_int16_t d)
{
	struct neo_softc *sc = sc_;
	int cnt = 3;

	if (!nm_waitcd(sc)) {
		while (cnt-- > 0) {
			nm_wr(sc, sc->ac97_base + a, d, 2);
			if (!nm_waitcd(sc)) {
				DELAY(1000);
				return (0);
			}
		}
	}

        return (ENXIO);
}


int
neo_attach_codec(void *sc_, struct ac97_codec_if *codec_if)
{
	struct neo_softc *sc = sc_;

	sc->codec_if = codec_if;
	return (0);
}

void
neo_reset_codec(void *sc)
{
	nm_wr(sc, 0x6c0, 0x01, 1);
	nm_wr(sc, 0x6cc, 0x87, 1);
	nm_wr(sc, 0x6cc, 0x80, 1);
	nm_wr(sc, 0x6cc, 0x00, 1);

	return;
}


enum ac97_host_flags
neo_flags_codec(void *sc)
{
	return (AC97_HOST_DONT_READANY);
}

int
neo_open(void *addr, int flags)
{
	return (0);
}

/*
 * Close function is called at splaudio().
 */
void
neo_close(void *addr)
{
	struct neo_softc *sc = addr;

	neo_halt_output(sc);
	neo_halt_input(sc);

	sc->pintr = 0;
	sc->rintr = 0;
}

/* Todo: don't commit settings to card until we've verified all parameters */
int
neo_set_params(void *addr, int setmode, int usemode,
    struct audio_params *play, struct audio_params *rec)
{
	struct neo_softc *sc = addr;
	u_int32_t base;
	u_int8_t x;
	int mode;
	struct audio_params *p;

	for (mode = AUMODE_RECORD; mode != -1;
	     mode = mode == AUMODE_RECORD ? AUMODE_PLAY : -1) {
		if ((setmode & mode) == 0)
			continue;

		p = mode == AUMODE_PLAY ? play : rec;

		if (p == NULL) continue;

		for (x = 0; x < 8; x++)
			if (p->sample_rate < (samplerates[x] + samplerates[x + 1]) / 2)
				break;

		p->sample_rate = samplerates[x];
		nm_loadcoeff(sc, mode, x);

		x <<= 4;
		x &= NM_RATE_MASK;
		if (p->precision == 16) x |= NM_RATE_BITS_16;
		if (p->channels == 2) x |= NM_RATE_STEREO;

		base = (mode == AUMODE_PLAY) ?
		    NM_PLAYBACK_REG_OFFSET : NM_RECORD_REG_OFFSET;
		nm_wr(sc, base + NM_RATE_REG_OFFSET, x, 1);

		switch (p->encoding) {
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
	}

	return (0);
}

int
neo_round_blocksize(void *addr, int blk)
{
	return (NM_BUFFSIZE / 2);
}

int
neo_trigger_output(void *addr, void *start, void *end, int blksize,
    void (*intr)(void *), void *arg, struct audio_params *param)
{
	struct neo_softc *sc = addr;
	int ssz;

	mtx_enter(&audio_lock);
	sc->pintr = intr;
	sc->parg = arg;

	ssz = (param->precision == 16) ? 2 : 1;
	if (param->channels == 2)
		ssz <<= 1;

	sc->pbufsize = ((char *)end - (char *)start);
	sc->pblksize = blksize;
	sc->pwmark = blksize;
	nm_wr(sc, NM_PBUFFER_START, sc->pbuf, 4);
	nm_wr(sc, NM_PBUFFER_END, sc->pbuf + sc->pbufsize - ssz, 4);
	nm_wr(sc, NM_PBUFFER_CURRP, sc->pbuf, 4);
	nm_wr(sc, NM_PBUFFER_WMARK, sc->pbuf + sc->pwmark, 4);
	nm_wr(sc, NM_PLAYBACK_ENABLE_REG, NM_PLAYBACK_FREERUN |
	    NM_PLAYBACK_ENABLE_FLAG, 1);
	nm_wr(sc, NM_AUDIO_MUTE_REG, 0, 2);
	mtx_leave(&audio_lock);
	return (0);
}



int
neo_trigger_input(void *addr, void *start, void *end, int blksize,
    void (*intr)(void *), void *arg, struct audio_params *param)
{
	struct neo_softc *sc = addr;
	int ssz;

	mtx_enter(&audio_lock);
	sc->rintr = intr;
	sc->rarg = arg;

	ssz = (param->precision == 16) ? 2 : 1;
	if (param->channels == 2)
		ssz <<= 1;

	sc->rbufsize = ((char *)end - (char *)start);
	sc->rblksize = blksize;
	sc->rwmark = blksize;
	nm_wr(sc, NM_RBUFFER_START, sc->rbuf, 4);
	nm_wr(sc, NM_RBUFFER_END, sc->rbuf + sc->rbufsize, 4);
	nm_wr(sc, NM_RBUFFER_CURRP, sc->rbuf, 4);
	nm_wr(sc, NM_RBUFFER_WMARK, sc->rbuf + sc->rwmark, 4);
	nm_wr(sc, NM_RECORD_ENABLE_REG, NM_RECORD_FREERUN |
	    NM_RECORD_ENABLE_FLAG, 1);
	mtx_leave(&audio_lock);
	return (0);
}

int
neo_halt_output(void *addr)
{
	struct neo_softc *sc = (struct neo_softc *)addr;

	mtx_enter(&audio_lock);
	nm_wr(sc, NM_PLAYBACK_ENABLE_REG, 0, 1);
	nm_wr(sc, NM_AUDIO_MUTE_REG, NM_AUDIO_MUTE_BOTH, 2);

	sc->pintr = 0;
	mtx_leave(&audio_lock);
	return (0);
}

int
neo_halt_input(void *addr)
{
	struct neo_softc *sc = (struct neo_softc *)addr;

	mtx_enter(&audio_lock);
	nm_wr(sc, NM_RECORD_ENABLE_REG, 0, 1);

	sc->rintr = 0;
	mtx_leave(&audio_lock);
	return (0);
}

int
neo_mixer_set_port(void *addr, mixer_ctrl_t *cp)
{
	struct neo_softc *sc = addr;

	return ((sc->codec_if->vtbl->mixer_set_port)(sc->codec_if, cp));
}

int
neo_mixer_get_port(void *addr, mixer_ctrl_t *cp)
{
	struct neo_softc *sc = addr;

	return ((sc->codec_if->vtbl->mixer_get_port)(sc->codec_if, cp));
}

int
neo_query_devinfo(void *addr, mixer_devinfo_t *dip)
{
	struct neo_softc *sc = addr;

	return ((sc->codec_if->vtbl->query_devinfo)(sc->codec_if, dip));
}

void *
neo_malloc(void *addr, int direction, size_t size, int pool, int flags)
{
	struct neo_softc *sc = addr;
	void *rv = 0;

	switch (direction) {
	case AUMODE_PLAY:
		rv = (char *)sc->bufioh + sc->pbuf;
		break;
	case AUMODE_RECORD:
		rv = (char *)sc->bufioh + sc->rbuf;
		break;
	default:
		break;
	}

	return (rv);
}

void
neo_free(void *addr, void *ptr, int pool)
{
	return;
}

size_t
neo_round_buffersize(void *addr, int direction, size_t size)
{
	return (NM_BUFFSIZE);
}
