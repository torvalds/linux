/*      $OpenBSD: auglx.c,v 1.25 2024/05/13 01:15:51 jsg Exp $	*/

/*
 * Copyright (c) 2008 Marc Balmer <mbalmer@openbsd.org>
 * All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * AMD CS5536 series AC'97 audio driver.
 *
 * The following datasheets were helpful in the development of this
 * driver:
 * 
 * AMD Geode LX Processors Data Book
 * http://www.amd.com/files/connectivitysolutions/geode/geode_lx/\
 *     33234F_LX_databook.pdf
 *
 * AMD Geode CS5536 Companion Device Data Book
 * http://www.amd.com/files/connectivitysolutions/geode/geode_lx/\
 *     33238G_cs5536_db.pdf
 *
 * Realtek ALC203 Two-Channel AC'97 2.3 Audio Codec
 * ftp://202.65.194.211/pc/audio/ALC203_DataSheet_1.6.pdf
 *
 * This driver is inspired by the auich(4) and auixp(4) drivers, some
 * of the hardware-independent functionality has been derived from them
 * (e.g. memory allocation for the upper level, parameter setting).
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/audioio.h>

#include <machine/bus.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>
#include <dev/audio_if.h>

#include <dev/ic/ac97.h>

#define AUGLX_ACC_BAR		0x10

/* ACC Native Registers */
#define ACC_GPIO_STATUS		0x00
#define ACC_GPIO_CNTL		0x04
#define ACC_CODEC_STATUS	0x08
#define ACC_CODEC_CNTL		0x0c
#define ACC_IRQ_STATUS		0x12
#define ACC_ENGINE_CNTL		0x14
#define ACC_BM0_CMD		0x20		/* Bus Master 0 Command */
#define ACC_BM0_STATUS		0x21		/* Bus Master 0 IRQ Status */
#define ACC_BM0_PRD		0x24		/* BM0 PRD Table Address */
#define ACC_BM1_CMD		0x28		/* Bus Master 1 Command */
#define ACC_BM1_STATUS		0x29		/* Bus Master 1 IRQ Status */
#define ACC_BM1_PRD		0x2c		/* BM1 PRD Table Address */
#define ACC_BM2_CMD		0x30		/* Bus Master 2 Command */
#define ACC_BM2_STATUS		0x31		/* Bus Master 2 IRQ Status */
#define ACC_BM2_PRD		0x34		/* BM2 PRD Table Address */
#define ACC_BM3_CMD		0x38		/* Bus Master 3 Command */
#define ACC_BM3_STATUS		0x39		/* Bus Master 3 IRQ Status */
#define ACC_BM3_PRD		0x3c		/* BM3 PRD Table Address */
#define ACC_BM4_CMD		0x40		/* Bus Master 4 Command */
#define ACC_BM4_STATUS		0x41		/* Bus Master 4 IRQ Status */
#define ACC_BM4_PRD		0x44		/* BM4 PRD Table Address */
#define ACC_BM5_CMD		0x48		/* Bus Master 5 Command */
#define ACC_BM5_STATUS		0x49		/* Bus Master 5 IRQ Status */
#define ACC_BM5_PRD		0x4c		/* BM5 PRD Table Address */
#define ACC_BM6_CMD		0x50		/* Bus Master 6 Command */
#define ACC_BM6_STATUS		0x51		/* Bus Master 6 IRQ Status */
#define ACC_BM6_PRD		0x54		/* BM6 PRD Table Address */
#define ACC_BM7_CMD		0x58		/* Bus Master 7 Command */
#define ACC_BM7_STATUS		0x59		/* Bus Master 7 IRQ Status */
#define ACC_BM7_PRD		0x5c		/* BM7 PRD Table Address */
#define ACC_BM0_PNTR		0x60		/* Bus Master 0 DMA Pointer */
#define ACC_BM1_PNTR		0x64		/* Bus Master 1 DMA Pointer */
#define ACC_BM2_PNTR		0x68		/* Bus Master 2 DMA Pointer */
#define ACC_BM3_PNTR		0x6c		/* Bus Master 3 DMA Pointer */
#define ACC_BM4_PNTR		0x70		/* Bus Master 4 DMA Pointer */
#define ACC_BM5_PNTR		0x74		/* Bus Master 5 DMA Pointer */
#define ACC_BM6_PNTR		0x78		/* Bus Master 6 DMA Pointer */
#define ACC_BM7_PNTR		0x7c		/* Bus Master 7 DMA Pointer */

/* ACC_IRQ_STATUS Bit Definitions */
#define BM7_IRQ_STS	0x0200	/* Audio Bus Master 7 IRQ Status */
#define BM6_IRQ_STS	0x0100	/* Audio Bus Master 6 IRQ Status */
#define BM5_IRQ_STS	0x0080	/* Audio Bus Master 5 IRQ Status */
#define BM4_IRQ_STS	0x0040	/* Audio Bus Master 4 IRQ Status */
#define BM3_IRQ_STS	0x0020	/* Audio Bus Master 3 IRQ Status */
#define BM2_IRQ_STS	0x0010	/* Audio Bus Master 2 IRQ Status */
#define BM1_IRQ_STS	0x0008	/* Audio Bus Master 1 IRQ Status */
#define BM0_IRQ_STS	0x0004	/* Audio Bus Master 0 IRQ Status */
#define WU_IRQ_STS	0x0002	/* Codec GPIO Wakeup IRQ Status */
#define IRQ_STS		0x0001	/* Codec GPIO IRQ Status */

/* ACC_ENGINE_CNTL Bit Definitions */
#define SSND_MODE	0x00000001	/* Surround Sound (5.1) Sync. Mode */

/* ACC_BM[x]_CMD Bit Descriptions */
#define BMx_CMD_RW		0x08	/* 0: Mem to codec, 1: codec to mem */
#define BMx_CMD_BYTE_ORD	0x04	/* 0: Little Endian, 1: Big Endian */
#define BMx_CMD_BM_CTL_DIS	0x00	/* Disable bus master */
#define BMx_CMD_BM_CTL_EN	0x01	/* Enable bus master */
#define BMx_CMD_BM_CTL_PAUSE	0x03	/* Pause bus master */

/* ACC_BM[x]_STATUS Bit Definitions */
#define BMx_BM_EOP_ERR		0x02	/* Bus master error */
#define BMx_BM_EOP		0x01	/* End of page */

/* ACC_CODEC_CNTL Bit Definitions */
#define RW_CMD			0x80000000
#define PD_PRIM			0x00200000
#define PD_SEC			0x00100000
#define LNK_SHTDOWN		0x00040000
#define LNK_WRM_RST		0x00020000
#define CMD_NEW			0x00010000

/* ACC_CODEC_STATUS Bit Definitions */
#define PRM_RDY_STS		0x00800000
#define SEC_RDY_STS		0x00400000
#define SDATAIN2_EN		0x00200000
#define BM5_SEL			0x00100000
#define BM4_SEL			0x00080000
#define STS_NEW			0x00020000

#define AUGLX_TOUT		1000	/* uSec */

#define	AUGLX_DMALIST_MAX	1
#define	AUGLX_DMASEG_MAX	65536

struct auglx_prd {
	u_int32_t	base;
	u_int32_t	size;
#define AUGLX_PRD_EOT	0x80000000
#define AUGLX_PRD_EOP	0x40000000
#define AUGLX_PRD_JMP	0x20000000
};

#define	AUGLX_FIXED_RATE 48000

struct auglx_dma {
	bus_dmamap_t		 map;
	caddr_t			 addr;
	bus_dma_segment_t	 segs[AUGLX_DMALIST_MAX];
	int			 nsegs;
	size_t			 size;
	struct auglx_dma	*next;
};

struct auglx_softc {
	struct device		 sc_dev;
	void			*sc_ih;

	bus_space_tag_t		 sc_iot;
	bus_space_handle_t	 sc_ioh;
	bus_dma_tag_t		 sc_dmat;

	/*
	 * The CS5536 ACC has eight bus masters to support 5.1 audio.
	 * This driver, however, only supports main playback and recording
	 * since I only have a Realtek ALC203 codec available for testing.
	 */
	struct auglx_ring {
		bus_dmamap_t		 sc_prd;
		struct auglx_prd	*sc_vprd;
		int			 sc_nprd;

		size_t			 sc_size;
		int			 nsegs;
		bus_dma_segment_t	 seg;

		void			(*intr)(void *);
		void			*arg;
	} bm0, bm1;	/* bm0: output, bm1: input */

	struct auglx_dma	*sc_dmas;

	struct ac97_codec_if	*codec_if;
	struct ac97_host_if	 host_if;

	int			 sc_dmamap_flags;
};

#ifdef AUGLX_DEBUG
#define	DPRINTF(l,x)	do { if (auglx_debug & (l)) printf x; } while(0)
int auglx_debug = 0;
#define AUGLX_DBG_ACC	0x0001
#define AUGLX_DBG_DMA	0x0002
#define AUGLX_DBG_IRQ	0x0004
#else
#define	DPRINTF(x,y)	/* nothing */
#endif

struct cfdriver auglx_cd = {
	NULL, "auglx", DV_DULL
};

int auglx_open(void *, int);
void auglx_close(void *);
int auglx_set_params(void *, int, int, struct audio_params *,
    struct audio_params *);
int auglx_round_blocksize(void *, int);
int auglx_halt_output(void *);
int auglx_halt_input(void *);
int auglx_set_port(void *, mixer_ctrl_t *);
int auglx_get_port(void *, mixer_ctrl_t *);
int auglx_query_devinfo(void *, mixer_devinfo_t *);
void *auglx_allocm(void *, int, size_t, int, int);
void auglx_freem(void *, void *, int);
size_t auglx_round_buffersize(void *, int, size_t);
int auglx_trigger_output(void *, void *, void *, int, void (*)(void *),
    void *, struct audio_params *);
int auglx_trigger_input(void *, void *, void *, int, void (*)(void *),
    void *, struct audio_params *);
int auglx_alloc_prd(struct auglx_softc *, size_t, struct auglx_ring *);
void auglx_free_prd(struct auglx_softc *sc, struct auglx_ring *bm);
int auglx_allocmem(struct auglx_softc *, size_t, size_t, struct auglx_dma *);
void auglx_freemem(struct auglx_softc *, struct auglx_dma *);

const struct audio_hw_if auglx_hw_if = {
	.open = auglx_open,
	.close = auglx_close,
	.set_params = auglx_set_params,
	.round_blocksize = auglx_round_blocksize,
	.halt_output = auglx_halt_output,
	.halt_input = auglx_halt_input,
	.set_port = auglx_set_port,
	.get_port = auglx_get_port,
	.query_devinfo = auglx_query_devinfo,
	.allocm = auglx_allocm,
	.freem = auglx_freem,
	.round_buffersize = auglx_round_buffersize,
	.trigger_output = auglx_trigger_output,
	.trigger_input = auglx_trigger_input,
};

int	auglx_match(struct device *, void *, void *);
void	auglx_attach(struct device *, struct device *, void *);
int	auglx_activate(struct device *, int);
int	auglx_intr(void *);

int	auglx_attach_codec(void *, struct ac97_codec_if *);
int	auglx_read_codec(void *, u_int8_t, u_int16_t *);
int	auglx_write_codec(void *, u_int8_t, u_int16_t);
void	auglx_reset_codec(void *);
enum ac97_host_flags	auglx_flags_codec(void *);

const struct cfattach auglx_ca = {
	sizeof(struct auglx_softc), auglx_match, auglx_attach, NULL,
	auglx_activate
};

const struct pci_matchid auglx_devices[] = {
	{ PCI_VENDOR_AMD,	PCI_PRODUCT_AMD_CS5536_AUDIO }
};

int
auglx_match(struct device *parent, void *match, void *aux)
{
	return (pci_matchbyid((struct pci_attach_args *)aux, auglx_devices,
	    sizeof(auglx_devices) / sizeof(auglx_devices[0])));
}

void
auglx_attach(struct device *parent, struct device *self, void *aux)
{
	struct auglx_softc *sc = (struct auglx_softc *)self;
	struct pci_attach_args *pa = aux;
	bus_size_t bar_size;
	pci_intr_handle_t ih;
	const char *intrstr;

	if (pci_mapreg_map(pa, AUGLX_ACC_BAR, PCI_MAPREG_TYPE_IO, 0,
	    &sc->sc_iot, &sc->sc_ioh, NULL, &bar_size, 0)) {
		printf(": can't map ACC I/O space\n");
		return;
	}

	sc->sc_dmat = pa->pa_dmat;
	sc->sc_dmamap_flags = BUS_DMA_COHERENT;

	if (pci_intr_map(pa, &ih)) {
		printf(": can't map interrupt");
		bus_space_unmap(sc->sc_iot, sc->sc_ioh, bar_size);
		return;
	}
	intrstr = pci_intr_string(pa->pa_pc, ih);
	sc->sc_ih = pci_intr_establish(pa->pa_pc, ih, IPL_AUDIO | IPL_MPSAFE,
	    auglx_intr, sc, sc->sc_dev.dv_xname);
	if (!sc->sc_ih) {
		printf(": can't establish interrupt");
		if (intrstr)
			printf(" at %s", intrstr);
		printf("\n");
		bus_space_unmap(sc->sc_iot, sc->sc_ioh, bar_size);
		return;
	}

	printf(": %s, %s\n", intrstr, "CS5536 AC97");

	sc->host_if.arg = sc;
	sc->host_if.attach = auglx_attach_codec;
	sc->host_if.read = auglx_read_codec;
	sc->host_if.write = auglx_write_codec;
	sc->host_if.reset = auglx_reset_codec;
	sc->host_if.flags = auglx_flags_codec;

	if (ac97_attach(&sc->host_if) != 0) {
		bus_space_unmap(sc->sc_iot, sc->sc_ioh, bar_size);
		return;
	}
	audio_attach_mi(&auglx_hw_if, sc, NULL, &sc->sc_dev);
}

/* Functions to communicate with the AC97 Codec via the ACC */
int
auglx_read_codec(void *v, u_int8_t reg, u_int16_t *val)
{
	struct auglx_softc *sc = v;
	u_int32_t codec_cntl, codec_status;
	int i;
	
	codec_cntl = RW_CMD | ((u_int32_t)reg << 24) | CMD_NEW;
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, ACC_CODEC_CNTL, codec_cntl);

	for (i = AUGLX_TOUT; i; i--) {
		codec_cntl = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
		    ACC_CODEC_CNTL);
		if (!(codec_cntl & CMD_NEW))
			break;
		delay(1);
	}
	if (codec_cntl & CMD_NEW) {
		printf("%s: codec read timeout after write\n",
		    sc->sc_dev.dv_xname);
		return -1;
	}

	for (i = AUGLX_TOUT; i; i--) {
		codec_status = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
		    ACC_CODEC_STATUS);
		if ((codec_status & STS_NEW) && (codec_status >> 24 == reg))
			break;
		delay(10);
	}
	if (i == 0) {
		printf("%s: codec status read timeout, 0x%08x\n",
		    sc->sc_dev.dv_xname, codec_status);
		return -1;
	}

	*val = codec_status & 0xffff;
	DPRINTF(AUGLX_DBG_ACC, ("%s: read codec register 0x%02x: 0x%04x\n",
	    sc->sc_dev.dv_xname, reg, *val));
	return 0;
}

int
auglx_write_codec(void *v, u_int8_t reg, u_int16_t val)
{
	struct auglx_softc *sc = v;
	u_int32_t codec_cntl;
	int i;

	DPRINTF(AUGLX_DBG_ACC, ("%s: write codec register 0x%02x: 0x%04x\n",
	    sc->sc_dev.dv_xname, reg, val));

	
	codec_cntl = ((u_int32_t)reg << 24) | CMD_NEW | val;
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, ACC_CODEC_CNTL, codec_cntl);

	for (i = AUGLX_TOUT; i; i--) {
		codec_cntl = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
		    ACC_CODEC_CNTL);
		if (!(codec_cntl & CMD_NEW))
			break;
		delay(1);
	}
	if (codec_cntl & CMD_NEW) {
		printf("%s: codec write timeout\n", sc->sc_dev.dv_xname);
		return -1;
	}

	return 0;
}

int
auglx_attach_codec(void *v, struct ac97_codec_if *cif)
{
	struct auglx_softc *sc = v;

	sc->codec_if = cif;
	return 0;
}

void
auglx_reset_codec(void *v)
{
	struct auglx_softc *sc = v;
	u_int32_t codec_cntl;
	int i;

	codec_cntl = LNK_WRM_RST | CMD_NEW;
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, ACC_CODEC_CNTL, codec_cntl);

	for (i = AUGLX_TOUT; i; i--) {
		codec_cntl = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
		    ACC_CODEC_CNTL);
		if (!(codec_cntl & CMD_NEW))
			continue;
		delay(1);
	}
	if (codec_cntl & CMD_NEW)
		printf("%s: codec reset timeout\n", sc->sc_dev.dv_xname);
}

enum ac97_host_flags
auglx_flags_codec(void *v)
{
	return 0;
}

/*
 * Audio functions
 */
int
auglx_open(void *v, int flags)
{
	return 0;
}

void
auglx_close(void *v)
{
}

int
auglx_set_params(void *v, int setmode, int usemode, struct audio_params *play,
    struct audio_params *rec)
{
	struct auglx_softc *sc = v;
	int error;
	u_int orate;

	if (setmode & AUMODE_PLAY) {
		play->precision = 16;
		play->channels = 2;
		play->encoding = AUDIO_ENCODING_SLINEAR_LE;
		play->bps = AUDIO_BPS(play->precision);
		play->msb = 1;

		orate = play->sample_rate;

		play->sample_rate = orate;
		error = ac97_set_rate(sc->codec_if,
		    AC97_REG_PCM_LFE_DAC_RATE, &play->sample_rate);
		if (error)
			return error;

		play->sample_rate = orate;
		error = ac97_set_rate(sc->codec_if,
		    AC97_REG_PCM_SURR_DAC_RATE, &play->sample_rate);
		if (error)
			return error;

		play->sample_rate = orate;
		error = ac97_set_rate(sc->codec_if,
		    AC97_REG_PCM_FRONT_DAC_RATE, &play->sample_rate);
		if (error)
			return error;
	}

	if (setmode & AUMODE_RECORD) {
		rec->precision = 16;
		rec->channels = 2;
		rec->encoding = AUDIO_ENCODING_ULINEAR_LE;
		rec->bps = AUDIO_BPS(rec->precision);
		rec->msb = 1;

		error = ac97_set_rate(sc->codec_if, AC97_REG_PCM_LR_ADC_RATE,
		    &rec->sample_rate);
		if (error)
			return error;
	}

	return 0;
}

int
auglx_round_blocksize(void *v, int blk)
{
	return (blk + 0x3f) & ~0x3f;
}

int
auglx_halt_output(void *v)
{
	struct auglx_softc *sc = v;

	DPRINTF(AUGLX_DBG_DMA, ("%s: halt_output\n", sc->sc_dev.dv_xname));

	bus_space_write_1(sc->sc_iot, sc->sc_ioh, ACC_BM0_CMD, 0x00);
	sc->bm0.intr = NULL;
	return 0;
}

int
auglx_halt_input(void *v)
{
	struct auglx_softc *sc = v;

	DPRINTF(AUGLX_DBG_DMA,
	    ("%s: halt_input\n", sc->sc_dev.dv_xname));

	bus_space_write_1(sc->sc_iot, sc->sc_ioh, ACC_BM1_CMD, 0x00);
	sc->bm1.intr = NULL;
	return 0;
}

int
auglx_set_port(void *v, mixer_ctrl_t *cp)
{
	struct auglx_softc *sc = v;
	return sc->codec_if->vtbl->mixer_set_port(sc->codec_if, cp);
}

int
auglx_get_port(void *v, mixer_ctrl_t *cp)
{
	struct auglx_softc *sc = v;
	return sc->codec_if->vtbl->mixer_get_port(sc->codec_if, cp);
}

int
auglx_query_devinfo(void *v, mixer_devinfo_t *dp)
{
	struct auglx_softc *sc = v;
	return sc->codec_if->vtbl->query_devinfo(sc->codec_if, dp);
}

void *
auglx_allocm(void *v, int direction, size_t size, int pool, int flags)
{
	struct auglx_softc *sc = v;
	struct auglx_dma *p;
	int error;

	DPRINTF(AUGLX_DBG_DMA, ("%s: request buffer of size %ld, dir %d\n",
	    sc->sc_dev.dv_xname, size, direction));

	/* can only use 1 segment */
	if (size > AUGLX_DMASEG_MAX) {
		DPRINTF(AUGLX_DBG_DMA,
		    ("%s: requested buffer size too large: %d", \
		    sc->sc_dev.dv_xname, size));
		return NULL;
	}

	p = malloc(sizeof(*p), pool, flags | M_ZERO);
	if (!p)
		return NULL;

	error = auglx_allocmem(sc, size, PAGE_SIZE, p);
	if (error) {
		free(p, pool, sizeof(*p));
		return NULL;
	}

	p->next = sc->sc_dmas;
	sc->sc_dmas = p;

	return p->addr;
}

void
auglx_freem(void *v, void *ptr, int pool)
{
	struct auglx_softc *sc;
	struct auglx_dma *p, **pp;

	sc = v;
	for (pp = &sc->sc_dmas; (p = *pp) != NULL; pp = &p->next) {
		if (p->addr == ptr) {
			auglx_freemem(sc, p);
			*pp = p->next;
			free(p, pool, sizeof(*p));
			return;
		}
	}
}


size_t
auglx_round_buffersize(void *v, int direction, size_t size)
{
	if (size > AUGLX_DMASEG_MAX)
		size = AUGLX_DMASEG_MAX;

	return size;
}

int
auglx_intr(void *v)
{
	struct auglx_softc *sc = v;
	u_int16_t irq_sts;
	u_int8_t bm_sts;

	mtx_enter(&audio_lock);
	irq_sts = bus_space_read_2(sc->sc_iot, sc->sc_ioh, ACC_IRQ_STATUS);
	if (irq_sts == 0) {
		mtx_leave(&audio_lock);
		return 0;
	}

	if (irq_sts & BM0_IRQ_STS) {
		bm_sts = bus_space_read_1(sc->sc_iot, sc->sc_ioh,
		    ACC_BM0_STATUS);
		if (sc->bm0.intr) {
			sc->bm0.intr(sc->bm0.arg);
			bus_space_write_1(sc->sc_iot, sc->sc_ioh, ACC_BM0_CMD,
			    BMx_CMD_BM_CTL_EN);
		}
	} else if (irq_sts & BM1_IRQ_STS) {
		bm_sts = bus_space_read_1(sc->sc_iot, sc->sc_ioh,
		    ACC_BM1_STATUS);
		if (sc->bm1.intr) {
			sc->bm1.intr(sc->bm1.arg);
			bus_space_write_1(sc->sc_iot, sc->sc_ioh, ACC_BM1_CMD,
			    BMx_CMD_RW | BMx_CMD_BM_CTL_EN);
		}
	} else {
		DPRINTF(AUGLX_DBG_IRQ, ("%s: stray intr, status = 0x%04x\n",
		    sc->sc_dev.dv_xname, irq_sts));
		mtx_leave(&audio_lock);
		return -1;
	}
	mtx_leave(&audio_lock);
	return 1;
}

int
auglx_trigger_output(void *v, void *start, void *end, int blksize,
    void (*intr)(void *), void *arg, struct audio_params *param)
{
	struct auglx_softc *sc = v;
	struct auglx_dma *p;
	size_t size;
	u_int32_t addr;
	int i, nprd;

	size = (size_t)((caddr_t)end - (caddr_t)start);
	DPRINTF(AUGLX_DBG_DMA, ("%s: trigger_output, %p 0x%08x bytes, "
	    "blksize 0x%04x\n", sc->sc_dev.dv_xname, start, size, blksize));

	for (p = sc->sc_dmas; p && p->addr != start; p = p->next)
		;
	if (!p) {
		DPRINTF(AUGLX_DBG_DMA, ("%s dma reg not found\n",
		    sc->sc_dev.dv_xname));
		return -1;
	}

	/* set up the PRDs */
	nprd = size / blksize;
	if (sc->bm0.sc_nprd != nprd + 1) {
		if (sc->bm0.sc_nprd > 0)
			auglx_free_prd(sc, &sc->bm0);
		sc->bm0.sc_nprd = nprd + 1;
		auglx_alloc_prd(sc,
		    sc->bm0.sc_nprd * sizeof(struct auglx_prd), &sc->bm0);
	}
	DPRINTF(AUGLX_DBG_DMA, ("%s: nprd = %d\n", sc->sc_dev.dv_xname,
	    nprd));
	addr = p->segs->ds_addr;
	for (i = 0; i < nprd; i++) {
		sc->bm0.sc_vprd[i].base = addr;
		sc->bm0.sc_vprd[i].size = blksize | AUGLX_PRD_EOP;
		addr += blksize;
	}
	sc->bm0.sc_vprd[i].base = sc->bm0.sc_prd->dm_segs[0].ds_addr;
	sc->bm0.sc_vprd[i].size = AUGLX_PRD_JMP;

#ifdef AUGLX_DEBUG
	for (i = 0; i < sc->bm0.sc_nprd; i++)
		DPRINTF(AUGLX_DBG_DMA, ("%s: PRD[%d].base = %p, size %p\n",
		    sc->sc_dev.dv_xname, i, sc->bm0.sc_vprd[i].base,
		    sc->bm0.sc_vprd[i].size));
#endif
	sc->bm0.intr = intr;
	sc->bm0.arg = arg;

	mtx_enter(&audio_lock);
	/* Program the BM0 PRD register */
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, ACC_BM0_PRD,
	    sc->bm0.sc_prd->dm_segs[0].ds_addr);
	/* Start Audio Bus Master 0 */
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, ACC_BM0_CMD,
	    BMx_CMD_BM_CTL_EN);
	mtx_leave(&audio_lock);
	return 0;
}

int
auglx_trigger_input(void *v, void *start, void *end, int blksize,
    void (*intr)(void *), void * arg, struct audio_params *param)
{
	struct auglx_softc *sc = v;
	struct auglx_dma *p;
	size_t size;
	u_int32_t addr;
	int i, nprd;

	size = (size_t)((caddr_t)end - (caddr_t)start);
	DPRINTF(AUGLX_DBG_DMA, ("%s: trigger_input, %p 0x%08x bytes, "
	    "blksize 0x%04x\n", sc->sc_dev.dv_xname, start, size, blksize));

	for (p = sc->sc_dmas; p && p->addr != start; p = p->next)
		;
	if (!p) {
		DPRINTF(AUGLX_DBG_DMA, ("%s dma reg not found\n",
		    sc->sc_dev.dv_xname));
		return -1;
	}

	/* set up the PRDs */
	nprd = size / blksize;
	if (sc->bm1.sc_nprd != nprd + 1) {
		if (sc->bm1.sc_nprd > 0)
			auglx_free_prd(sc, &sc->bm1);
		sc->bm1.sc_nprd = nprd + 1;
		auglx_alloc_prd(sc,
		    sc->bm1.sc_nprd * sizeof(struct auglx_prd), &sc->bm1);
	}
	DPRINTF(AUGLX_DBG_DMA, ("%s: nprd = %d\n", sc->sc_dev.dv_xname,
	    nprd));
	addr = p->segs->ds_addr;
	for (i = 0; i < nprd; i++) {
		sc->bm1.sc_vprd[i].base = addr;
		sc->bm1.sc_vprd[i].size = blksize | AUGLX_PRD_EOP;
		addr += blksize;
	}
	sc->bm1.sc_vprd[i].base = sc->bm1.sc_prd->dm_segs[0].ds_addr;
	sc->bm1.sc_vprd[i].size = AUGLX_PRD_JMP;

#ifdef AUGLX_DEBUG
	for (i = 0; i < sc->bm1.sc_nprd; i++)
		DPRINTF(AUGLX_DBG_DMA, ("%s: PRD[%d].base = %p, size %p\n",
		    sc->sc_dev.dv_xname, i, sc->bm1.sc_vprd[i].base,
		    sc->bm1.sc_vprd[i].size));
#endif
	sc->bm1.intr = intr;
	sc->bm1.arg = arg;

	mtx_enter(&audio_lock);
	/* Program the BM1 PRD register */	
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, ACC_BM1_PRD,
	    sc->bm1.sc_prd->dm_segs[0].ds_addr);
	/* Start Audio Bus Master 0 */
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, ACC_BM1_CMD,
	    BMx_CMD_RW | BMx_CMD_BM_CTL_EN);
	mtx_leave(&audio_lock);
	return 0;
}

int
auglx_allocmem(struct auglx_softc *sc, size_t size, size_t align,
    struct auglx_dma *p)
{
	int error;

	p->size = size;
	error = bus_dmamem_alloc(sc->sc_dmat, p->size, align, 0, p->segs, 1,
	    &p->nsegs, BUS_DMA_NOWAIT);
	if (error) {
		DPRINTF(AUGLX_DBG_DMA, 
		    ("%s: bus_dmamem_alloc failed: error %d\n",
		    sc->sc_dev.dv_xname, error));
		return error;
	}

	error = bus_dmamem_map(sc->sc_dmat, p->segs, 1, p->size, &p->addr,
	    BUS_DMA_NOWAIT | sc->sc_dmamap_flags);
	if (error) {
		DPRINTF(AUGLX_DBG_DMA, 
		    ("%s: bus_dmamem_map failed: error %d\n",
		    sc->sc_dev.dv_xname, error));
		goto free;
	}

	error = bus_dmamap_create(sc->sc_dmat, p->size, 1, p->size, 0,
	    BUS_DMA_NOWAIT, &p->map);
	if (error) {
		DPRINTF(AUGLX_DBG_DMA, 
		    ("%s: bus_dmamap_create failed: error %d\n",
		    sc->sc_dev.dv_xname, error));
		goto unmap;
	}

	error = bus_dmamap_load(sc->sc_dmat, p->map, p->addr, p->size, NULL,
	    BUS_DMA_NOWAIT);
	if (error) {
		DPRINTF(AUGLX_DBG_DMA,
		    ("%s: bus_dmamap_load failed: error %d\n",
		    sc->sc_dev.dv_xname, error));
		goto destroy;
	}
	return 0;

 destroy:
	bus_dmamap_destroy(sc->sc_dmat, p->map);
 unmap:
	bus_dmamem_unmap(sc->sc_dmat, p->addr, p->size);
 free:
	bus_dmamem_free(sc->sc_dmat, p->segs, p->nsegs);
	return error;
}

void
auglx_freemem(struct auglx_softc *sc, struct auglx_dma *p)
{
	bus_dmamap_unload(sc->sc_dmat, p->map);
	bus_dmamap_destroy(sc->sc_dmat, p->map);
	bus_dmamem_unmap(sc->sc_dmat, p->addr, p->size);
	bus_dmamem_free(sc->sc_dmat, p->segs, p->nsegs);
}

int
auglx_alloc_prd(struct auglx_softc *sc, size_t size, struct auglx_ring *bm)
{
	int error, rseg;

	/*
	 * Allocate PRD table structure, and create and load the
	 * DMA map for it.
	 */
	if ((error = bus_dmamem_alloc(sc->sc_dmat, size,
	    PAGE_SIZE, 0, &bm->seg, 1, &rseg, 0)) != 0) {
		printf("%s: unable to allocate PRD, error = %d\n",
		    sc->sc_dev.dv_xname, error);
		goto fail_0;
	}

	if ((error = bus_dmamem_map(sc->sc_dmat, &bm->seg, rseg,
	    size, (caddr_t *)&bm->sc_vprd,
	    sc->sc_dmamap_flags)) != 0) {
		printf("%s: unable to map PRD, error = %d\n",
		    sc->sc_dev.dv_xname, error);
		goto fail_1;
	}

	if ((error = bus_dmamap_create(sc->sc_dmat, size,
	    1, size, 0, 0, &bm->sc_prd)) != 0) {
		printf("%s: unable to create PRD DMA map, "
		    "error = %d\n", sc->sc_dev.dv_xname, error);
		goto fail_2;
	}

	if ((error = bus_dmamap_load(sc->sc_dmat, bm->sc_prd, bm->sc_vprd,
	    size, NULL, 0)) != 0) {
		printf("%s: unable tp load control data DMA map, "
		    "error = %d\n", sc->sc_dev.dv_xname, error);
		goto fail_3;
	}

	return 0;

 fail_3:
	bus_dmamap_destroy(sc->sc_dmat, bm->sc_prd);
 fail_2:
	bus_dmamem_unmap(sc->sc_dmat, (caddr_t)bm->sc_vprd,
	    sizeof(struct auglx_prd));
 fail_1:
	bus_dmamem_free(sc->sc_dmat, &bm->seg, rseg);
 fail_0:
	return error;
}

void
auglx_free_prd(struct auglx_softc *sc, struct auglx_ring *bm)
{
	bus_dmamap_unload(sc->sc_dmat, bm->sc_prd);
	bus_dmamap_destroy(sc->sc_dmat, bm->sc_prd);
	bus_dmamem_unmap(sc->sc_dmat, (caddr_t)bm->sc_vprd, bm->sc_size);
	bus_dmamem_free(sc->sc_dmat, &bm->seg, bm->nsegs);
}

int
auglx_activate(struct device *self, int act)
{
	struct auglx_softc *sc = (struct auglx_softc *)self;

	if (act == DVACT_RESUME)
		ac97_resume(&sc->host_if, sc->codec_if);
	return (config_activate_children(self, act));
}
