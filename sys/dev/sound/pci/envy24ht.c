/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 Konstantin Dimitrov <kosio.dimitrov@gmail.com>
 * Copyright (c) 2001 Katsurajima Naoto <raven@katsurajima.seya.yokohama.jp>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHERIN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

/*
 * Konstantin Dimitrov's thanks list:
 *
 * A huge thanks goes to Spas Filipov for his friendship, support and his
 * generous gift - an 'Audiotrak Prodigy HD2' audio card! I also want to
 * thank Keiichi Iwasaki and his parents, because they helped Spas to get
 * the card from Japan! Having hardware sample of Prodigy HD2 made adding
 * support for that great card very easy and real fun and pleasure.
 *
 */

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_snd.h"
#endif

#include <dev/sound/pcm/sound.h>
#include <dev/sound/pcm/ac97.h>
#include <dev/sound/pci/spicds.h>
#include <dev/sound/pci/envy24ht.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include "mixer_if.h"

SND_DECLARE_FILE("$FreeBSD$");

static MALLOC_DEFINE(M_ENVY24HT, "envy24ht", "envy24ht audio");

/* -------------------------------------------------------------------- */

struct sc_info;

#define ENVY24HT_PLAY_CHNUM 8
#define ENVY24HT_REC_CHNUM 2
#define ENVY24HT_PLAY_BUFUNIT (4 /* byte/sample */ * 8 /* channel */)
#define ENVY24HT_REC_BUFUNIT  (4 /* byte/sample */ * 2 /* channel */)
#define ENVY24HT_SAMPLE_NUM   4096

#define ENVY24HT_TIMEOUT 1000

#define ENVY24HT_DEFAULT_FORMAT	SND_FORMAT(AFMT_S16_LE, 2, 0)

#define ENVY24HT_NAMELEN 32

struct envy24ht_sample {
        volatile u_int32_t buffer;
};

typedef struct envy24ht_sample sample32_t;

/* channel registers */
struct sc_chinfo {
	struct snd_dbuf		*buffer;
	struct pcm_channel	*channel;
	struct sc_info		*parent;
	int			dir;
	unsigned		num; /* hw channel number */

	/* channel information */
	u_int32_t		format;
	u_int32_t		speed;
	u_int32_t		blk; /* hw block size(dword) */

	/* format conversion structure */
	u_int8_t		*data;
	unsigned int		size; /* data buffer size(byte) */
	int			unit; /* sample size(byte) */
	unsigned int		offset; /* samples number offset */
	void			(*emldma)(struct sc_chinfo *);

	/* flags */
	int			run;
};

/* codec interface entrys */
struct codec_entry {
	void *(*create)(device_t dev, void *devinfo, int dir, int num);
	void (*destroy)(void *codec);
	void (*init)(void *codec);
	void (*reinit)(void *codec);
	void (*setvolume)(void *codec, int dir, unsigned int left, unsigned int right);
	void (*setrate)(void *codec, int which, int rate);
};

/* system configuration information */
struct cfg_info {
	char *name;
	u_int16_t subvendor, subdevice;
	u_int8_t scfg, acl, i2s, spdif;
	u_int32_t gpiomask, gpiostate, gpiodir;
	u_int32_t cdti, cclk, cs;
	u_int8_t cif, type, free;
	struct codec_entry *codec;
};

/* device private data */
struct sc_info {
	device_t	dev;
	struct mtx	*lock;

	/* Control/Status registor */
	struct resource *cs;
	int		csid;
	bus_space_tag_t cst;
	bus_space_handle_t csh;
	/* MultiTrack registor */
	struct resource *mt;
	int		mtid;
	bus_space_tag_t mtt;
	bus_space_handle_t mth;
	/* DMA tag */
	bus_dma_tag_t dmat;
	/* IRQ resource */
	struct resource *irq;
	int		irqid;
	void		*ih;

	/* system configuration data */
	struct cfg_info *cfg;

	/* ADC/DAC number and info */
	int		adcn, dacn;
	void		*adc[4], *dac[4];

	/* mixer control data */
	u_int32_t	src;
	u_int8_t	left[ENVY24HT_CHAN_NUM];
	u_int8_t	right[ENVY24HT_CHAN_NUM];

	/* Play/Record DMA fifo */
	sample32_t	*pbuf;
	sample32_t	*rbuf;
	u_int32_t	psize, rsize; /* DMA buffer size(byte) */
	u_int16_t	blk[2]; /* transfer check blocksize(dword) */
	bus_dmamap_t	pmap, rmap;
	bus_addr_t	paddr, raddr;

	/* current status */
	u_int32_t	speed;
	int		run[2];
	u_int16_t	intr[2];
	struct pcmchan_caps	caps[2];

	/* channel info table */
	unsigned	chnum;
	struct sc_chinfo chan[11];
};

/* -------------------------------------------------------------------- */

/*
 * prototypes
 */

/* DMA emulator */
static void envy24ht_p8u(struct sc_chinfo *);
static void envy24ht_p16sl(struct sc_chinfo *);
static void envy24ht_p32sl(struct sc_chinfo *);
static void envy24ht_r16sl(struct sc_chinfo *);
static void envy24ht_r32sl(struct sc_chinfo *);

/* channel interface */
static void *envy24htchan_init(kobj_t, void *, struct snd_dbuf *, struct pcm_channel *, int);
static int envy24htchan_setformat(kobj_t, void *, u_int32_t);
static u_int32_t envy24htchan_setspeed(kobj_t, void *, u_int32_t);
static u_int32_t envy24htchan_setblocksize(kobj_t, void *, u_int32_t);
static int envy24htchan_trigger(kobj_t, void *, int);
static u_int32_t envy24htchan_getptr(kobj_t, void *);
static struct pcmchan_caps *envy24htchan_getcaps(kobj_t, void *);

/* mixer interface */
static int envy24htmixer_init(struct snd_mixer *);
static int envy24htmixer_reinit(struct snd_mixer *);
static int envy24htmixer_uninit(struct snd_mixer *);
static int envy24htmixer_set(struct snd_mixer *, unsigned, unsigned, unsigned);
static u_int32_t envy24htmixer_setrecsrc(struct snd_mixer *, u_int32_t);

/* SPI codec access interface */
static void *envy24ht_spi_create(device_t, void *, int, int);
static void envy24ht_spi_destroy(void *);
static void envy24ht_spi_init(void *);
static void envy24ht_spi_reinit(void *);
static void envy24ht_spi_setvolume(void *, int, unsigned int, unsigned int);

/* -------------------------------------------------------------------- */

/*
  system constant tables
*/

/* API -> hardware channel map */
static unsigned envy24ht_chanmap[ENVY24HT_CHAN_NUM] = {
	ENVY24HT_CHAN_PLAY_DAC1,  /* 1 */
	ENVY24HT_CHAN_PLAY_DAC2,  /* 2 */
	ENVY24HT_CHAN_PLAY_DAC3,  /* 3 */
	ENVY24HT_CHAN_PLAY_DAC4,  /* 4 */
	ENVY24HT_CHAN_PLAY_SPDIF, /* 0 */
	ENVY24HT_CHAN_REC_MIX,    /* 5 */
	ENVY24HT_CHAN_REC_SPDIF,  /* 6 */
	ENVY24HT_CHAN_REC_ADC1,   /* 7 */
	ENVY24HT_CHAN_REC_ADC2,   /* 8 */
	ENVY24HT_CHAN_REC_ADC3,   /* 9 */
	ENVY24HT_CHAN_REC_ADC4,   /* 10 */
};

/* mixer -> API channel map. see above */
static int envy24ht_mixmap[] = {
	-1, /* Master output level. It is depend on codec support */
	-1, /* Treble level of all output channels */
	-1, /* Bass level of all output channels */
	-1, /* Volume of synthesier input */
	0,  /* Output level for the audio device */
	-1, /* Output level for the PC speaker */
	7,  /* line in jack */
	-1, /* microphone jack */
	-1, /* CD audio input */
	-1, /* Recording monitor */
	1,  /* alternative codec */
	-1, /* global recording level */
	-1, /* Input gain */
	-1, /* Output gain */
	8,  /* Input source 1 */
	9,  /* Input source 2 */
	10, /* Input source 3 */
	6,  /* Digital (input) 1 */
	-1, /* Digital (input) 2 */
	-1, /* Digital (input) 3 */
	-1, /* Phone input */
	-1, /* Phone output */
	-1, /* Video/TV (audio) in */
	-1, /* Radio in */
	-1, /* Monitor volume */
};

/* variable rate audio */
static u_int32_t envy24ht_speed[] = {
    192000, 176400, 96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000,
    12000, 11025, 9600, 8000, 0
};

/* known boards configuration */
static struct codec_entry spi_codec = {
	envy24ht_spi_create,
	envy24ht_spi_destroy,
	envy24ht_spi_init,
	envy24ht_spi_reinit,
	envy24ht_spi_setvolume,
	NULL, /* setrate */
};

static struct cfg_info cfg_table[] = {
	{
		"Envy24HT audio (Terratec Aureon 7.1 Space)",
		0x153b, 0x1145,
		0x0b, 0x80, 0xfc, 0xc3,
		0x21efff, 0x7fffff, 0x5e1000,
		0x40000, 0x80000, 0x1000, 0x00, 0x02,
		0,
		&spi_codec,
	},
        {
                "Envy24HT audio (Terratec Aureon 5.1 Sky)",
                0x153b, 0x1147,
                0x0a, 0x80, 0xfc, 0xc3,
                0x21efff, 0x7fffff, 0x5e1000,
                0x40000, 0x80000, 0x1000, 0x00, 0x02,
                0,
                &spi_codec,
        },
	        {
                "Envy24HT audio (Terratec Aureon 7.1 Universe)",
                0x153b, 0x1153,
                0x0b, 0x80, 0xfc, 0xc3,
                0x21efff, 0x7fffff, 0x5e1000,
                0x40000, 0x80000, 0x1000, 0x00, 0x02,
                0,
                &spi_codec,
        },
        {
                "Envy24HT audio (AudioTrak Prodigy 7.1)",
                0x4933, 0x4553,
                0x0b, 0x80, 0xfc, 0xc3,
                0x21efff, 0x7fffff, 0x5e1000,
                0x40000, 0x80000, 0x1000, 0x00, 0x02,
                0,
                &spi_codec,
        },
        {
                "Envy24HT audio (Terratec PHASE 28)",
                0x153b, 0x1149,
                0x0b, 0x80, 0xfc, 0xc3,
                0x21efff, 0x7fffff, 0x5e1000,
                0x40000, 0x80000, 0x1000, 0x00, 0x02,
                0,
                &spi_codec,
        },
        {
                "Envy24HT-S audio (Terratec PHASE 22)",
                0x153b, 0x1150,
                0x10, 0x80, 0xf0, 0xc3,
                0x7ffbc7, 0x7fffff, 0x438,
                0x10, 0x20, 0x400, 0x01, 0x00,
                0,
                &spi_codec,
        },
        {
                "Envy24HT audio (AudioTrak Prodigy 7.1 LT)",
                0x3132, 0x4154,   
                0x4b, 0x80, 0xfc, 0xc3,
                0x7ff8ff, 0x7fffff, 0x700,
                0x400, 0x200, 0x100, 0x00, 0x02,
                0,
                &spi_codec, 
        },
        {
                "Envy24HT audio (AudioTrak Prodigy 7.1 XT)",
                0x3136, 0x4154,  
                0x4b, 0x80, 0xfc, 0xc3,
                0x7ff8ff, 0x7fffff, 0x700,
                0x400, 0x200, 0x100, 0x00, 0x02,
                0,
                &spi_codec,
        },
        {
                "Envy24HT audio (M-Audio Revolution 7.1)",
                0x1412, 0x3630,
                0x43, 0x80, 0xf8, 0xc1,
                0x3fff85, 0x400072, 0x4000fa,
                0x08, 0x02, 0x20, 0x00, 0x04,
                0,
                &spi_codec,
        },
        {
                "Envy24GT audio (M-Audio Revolution 5.1)",
                0x1412, 0x3631,
                0x42, 0x80, 0xf8, 0xc1,
                0x3fff05, 0x4000f0, 0x4000fa,
                0x08, 0x02, 0x10, 0x00, 0x03,
                0,
                &spi_codec,
        },
        {
                "Envy24HT audio (M-Audio Audiophile 192)",
                0x1412, 0x3632,
                0x68, 0x80, 0xf8, 0xc3,
                0x45, 0x4000b5, 0x7fffba,
                0x08, 0x02, 0x10, 0x00, 0x03,
                0,
                &spi_codec,
        },
        {
                "Envy24HT audio (AudioTrak Prodigy HD2)",
                0x3137, 0x4154,
                0x68, 0x80, 0x78, 0xc3,
                0xfff8ff, 0x200700, 0xdfffff,
                0x400, 0x200, 0x100, 0x00, 0x05,
                0,
                &spi_codec,
        },
        {
                "Envy24HT audio (ESI Juli@)",
                0x3031, 0x4553,
                0x20, 0x80, 0xf8, 0xc3,
                0x7fff9f, 0x8016, 0x7fff9f,
                0x08, 0x02, 0x10, 0x00, 0x03,
                0,
                &spi_codec,
        },
	{
                "Envy24HT-S audio (Terrasoniq TS22PCI)",
                0x153b, 0x117b,
                0x10, 0x80, 0xf0, 0xc3,
                0x7ffbc7, 0x7fffff, 0x438,
                0x10, 0x20, 0x400, 0x01, 0x00,
                0,
                &spi_codec,
	},
	{
		"Envy24HT audio (Generic)",
		0, 0,
		0x0b, 0x80, 0xfc, 0xc3,
		0x21efff, 0x7fffff, 0x5e1000,
                0x40000, 0x80000, 0x1000, 0x00, 0x02,
		0,
		&spi_codec, /* default codec routines */
	}
};

static u_int32_t envy24ht_recfmt[] = {
	SND_FORMAT(AFMT_S16_LE, 2, 0),
	SND_FORMAT(AFMT_S32_LE, 2, 0),
	0
};
static struct pcmchan_caps envy24ht_reccaps = {8000, 96000, envy24ht_recfmt, 0};

static u_int32_t envy24ht_playfmt[] = {
	SND_FORMAT(AFMT_U8, 2, 0),
	SND_FORMAT(AFMT_S16_LE, 2, 0),
	SND_FORMAT(AFMT_S32_LE, 2, 0),
	0
};

static struct pcmchan_caps envy24ht_playcaps = {8000, 192000, envy24ht_playfmt, 0};

struct envy24ht_emldma {
	u_int32_t	format;
	void		(*emldma)(struct sc_chinfo *);
	int		unit;
};

static struct envy24ht_emldma envy24ht_pemltab[] = {
	{SND_FORMAT(AFMT_U8, 2, 0), envy24ht_p8u, 2},
	{SND_FORMAT(AFMT_S16_LE, 2, 0), envy24ht_p16sl, 4},
	{SND_FORMAT(AFMT_S32_LE, 2, 0), envy24ht_p32sl, 8},
	{0, NULL, 0}
};

static struct envy24ht_emldma envy24ht_remltab[] = {
	{SND_FORMAT(AFMT_S16_LE, 2, 0), envy24ht_r16sl, 4},
	{SND_FORMAT(AFMT_S32_LE, 2, 0), envy24ht_r32sl, 8},
	{0, NULL, 0}
};

/* -------------------------------------------------------------------- */

/* common routines */
static u_int32_t
envy24ht_rdcs(struct sc_info *sc, int regno, int size)
{
	switch (size) {
	case 1:
		return bus_space_read_1(sc->cst, sc->csh, regno);
	case 2:
		return bus_space_read_2(sc->cst, sc->csh, regno);
	case 4:
		return bus_space_read_4(sc->cst, sc->csh, regno);
	default:
		return 0xffffffff;
	}
}

static void
envy24ht_wrcs(struct sc_info *sc, int regno, u_int32_t data, int size)
{
	switch (size) {
	case 1:
		bus_space_write_1(sc->cst, sc->csh, regno, data);
		break;
	case 2:
		bus_space_write_2(sc->cst, sc->csh, regno, data);
		break;
	case 4:
		bus_space_write_4(sc->cst, sc->csh, regno, data);
		break;
	}
}

static u_int32_t
envy24ht_rdmt(struct sc_info *sc, int regno, int size)
{
	switch (size) {
	case 1:
		return bus_space_read_1(sc->mtt, sc->mth, regno);
	case 2:
		return bus_space_read_2(sc->mtt, sc->mth, regno);
	case 4:
		return bus_space_read_4(sc->mtt, sc->mth, regno);
	default:
		return 0xffffffff;
	}
}

static void
envy24ht_wrmt(struct sc_info *sc, int regno, u_int32_t data, int size)
{
	switch (size) {
	case 1:
		bus_space_write_1(sc->mtt, sc->mth, regno, data);
		break;
	case 2:
		bus_space_write_2(sc->mtt, sc->mth, regno, data);
		break;
	case 4:
		bus_space_write_4(sc->mtt, sc->mth, regno, data);
		break;
	}
}

/* -------------------------------------------------------------------- */

/* I2C port/E2PROM access routines */

static int
envy24ht_rdi2c(struct sc_info *sc, u_int32_t dev, u_int32_t addr)
{
	u_int32_t data;
	int i;

#if(0)
	device_printf(sc->dev, "envy24ht_rdi2c(sc, 0x%02x, 0x%02x)\n", dev, addr);
#endif
	for (i = 0; i < ENVY24HT_TIMEOUT; i++) {
		data = envy24ht_rdcs(sc, ENVY24HT_CCS_I2CSTAT, 1);
		if ((data & ENVY24HT_CCS_I2CSTAT_BSY) == 0)
			break;
		DELAY(32); /* 31.25kHz */
	}
	if (i == ENVY24HT_TIMEOUT) {
		return -1;
	}
	envy24ht_wrcs(sc, ENVY24HT_CCS_I2CADDR, addr, 1);
	envy24ht_wrcs(sc, ENVY24HT_CCS_I2CDEV,
	    (dev & ENVY24HT_CCS_I2CDEV_ADDR) | ENVY24HT_CCS_I2CDEV_RD, 1);
	for (i = 0; i < ENVY24HT_TIMEOUT; i++) {
		data = envy24ht_rdcs(sc, ENVY24HT_CCS_I2CSTAT, 1);
		if ((data & ENVY24HT_CCS_I2CSTAT_BSY) == 0)
			break;
		DELAY(32); /* 31.25kHz */
	}
	if (i == ENVY24HT_TIMEOUT) {
		return -1;
	}
	data = envy24ht_rdcs(sc, ENVY24HT_CCS_I2CDATA, 1);

#if(0)
	device_printf(sc->dev, "envy24ht_rdi2c(): return 0x%x\n", data);
#endif
	return (int)data;
}

static int
envy24ht_wri2c(struct sc_info *sc, u_int32_t dev, u_int32_t addr, u_int32_t data)
{
	u_int32_t tmp;
	int i;

#if(0)
	device_printf(sc->dev, "envy24ht_rdi2c(sc, 0x%02x, 0x%02x)\n", dev, addr);
#endif
	for (i = 0; i < ENVY24HT_TIMEOUT; i++) {
		tmp = envy24ht_rdcs(sc, ENVY24HT_CCS_I2CSTAT, 1);
		if ((tmp & ENVY24HT_CCS_I2CSTAT_BSY) == 0)
			break;
		DELAY(32); /* 31.25kHz */
	}
	if (i == ENVY24HT_TIMEOUT) {
		return -1;
	}
	envy24ht_wrcs(sc, ENVY24HT_CCS_I2CADDR, addr, 1);
	envy24ht_wrcs(sc, ENVY24HT_CCS_I2CDATA, data, 1);
	envy24ht_wrcs(sc, ENVY24HT_CCS_I2CDEV,
	    (dev & ENVY24HT_CCS_I2CDEV_ADDR) | ENVY24HT_CCS_I2CDEV_WR, 1);
	for (i = 0; i < ENVY24HT_TIMEOUT; i++) {
		data = envy24ht_rdcs(sc, ENVY24HT_CCS_I2CSTAT, 1);
		if ((data & ENVY24HT_CCS_I2CSTAT_BSY) == 0)
			break;
		DELAY(32); /* 31.25kHz */
	}
	if (i == ENVY24HT_TIMEOUT) {
		return -1;
	}

	return 0;
}

static int
envy24ht_rdrom(struct sc_info *sc, u_int32_t addr)
{
	u_int32_t data;

#if(0)
	device_printf(sc->dev, "envy24ht_rdrom(sc, 0x%02x)\n", addr);
#endif
	data = envy24ht_rdcs(sc, ENVY24HT_CCS_I2CSTAT, 1);
	if ((data & ENVY24HT_CCS_I2CSTAT_ROM) == 0) {
#if(0)
		device_printf(sc->dev, "envy24ht_rdrom(): E2PROM not presented\n");
#endif
		return -1;
	}

	return envy24ht_rdi2c(sc, ENVY24HT_CCS_I2CDEV_ROM, addr);
}

static struct cfg_info *
envy24ht_rom2cfg(struct sc_info *sc)
{
	struct cfg_info *buff;
	int size;
	int i;

#if(0)
	device_printf(sc->dev, "envy24ht_rom2cfg(sc)\n");
#endif
	size = envy24ht_rdrom(sc, ENVY24HT_E2PROM_SIZE);
	if ((size < ENVY24HT_E2PROM_GPIOSTATE + 3) || (size == 0x78)) {
#if(0)
		device_printf(sc->dev, "envy24ht_rom2cfg(): ENVY24HT_E2PROM_SIZE-->%d\n", size);
#endif
        buff = malloc(sizeof(*buff), M_ENVY24HT, M_NOWAIT);
        if (buff == NULL) {
#if(0)
                device_printf(sc->dev, "envy24ht_rom2cfg(): malloc()\n");
#endif
                return NULL;
        }
        buff->free = 1;

	/* no valid e2prom, using default values */
        buff->subvendor = envy24ht_rdrom(sc, ENVY24HT_E2PROM_SUBVENDOR) << 8;
        buff->subvendor += envy24ht_rdrom(sc, ENVY24HT_E2PROM_SUBVENDOR + 1);
        buff->subdevice = envy24ht_rdrom(sc, ENVY24HT_E2PROM_SUBDEVICE) << 8;
        buff->subdevice += envy24ht_rdrom(sc, ENVY24HT_E2PROM_SUBDEVICE + 1);
        buff->scfg = 0x0b;
        buff->acl = 0x80;
        buff->i2s = 0xfc;
        buff->spdif = 0xc3;
        buff->gpiomask = 0x21efff;
        buff->gpiostate = 0x7fffff;
        buff->gpiodir = 0x5e1000;
	buff->cdti = 0x40000;
	buff->cclk = 0x80000;
	buff->cs = 0x1000;
	buff->cif = 0x00;
	buff->type = 0x02;

        for (i = 0; cfg_table[i].subvendor != 0 || cfg_table[i].subdevice != 0;
i++)
                if (cfg_table[i].subvendor == buff->subvendor &&
                    cfg_table[i].subdevice == buff->subdevice)
                        break;
        buff->name = cfg_table[i].name;
        buff->codec = cfg_table[i].codec;

		return buff;
#if 0
		return NULL;
#endif
	}
	buff = malloc(sizeof(*buff), M_ENVY24HT, M_NOWAIT);
	if (buff == NULL) {
#if(0)
		device_printf(sc->dev, "envy24ht_rom2cfg(): malloc()\n");
#endif
		return NULL;
	}
	buff->free = 1;

	buff->subvendor = envy24ht_rdrom(sc, ENVY24HT_E2PROM_SUBVENDOR) << 8;
	buff->subvendor += envy24ht_rdrom(sc, ENVY24HT_E2PROM_SUBVENDOR + 1);
	buff->subdevice = envy24ht_rdrom(sc, ENVY24HT_E2PROM_SUBDEVICE) << 8;
	buff->subdevice += envy24ht_rdrom(sc, ENVY24HT_E2PROM_SUBDEVICE + 1);
	buff->scfg = envy24ht_rdrom(sc, ENVY24HT_E2PROM_SCFG);
	buff->acl = envy24ht_rdrom(sc, ENVY24HT_E2PROM_ACL);
	buff->i2s = envy24ht_rdrom(sc, ENVY24HT_E2PROM_I2S);
	buff->spdif = envy24ht_rdrom(sc, ENVY24HT_E2PROM_SPDIF);
	buff->gpiomask = envy24ht_rdrom(sc, ENVY24HT_E2PROM_GPIOMASK) | \
	envy24ht_rdrom(sc, ENVY24HT_E2PROM_GPIOMASK + 1) << 8 | \
	envy24ht_rdrom(sc, ENVY24HT_E2PROM_GPIOMASK + 2) << 16;
	buff->gpiostate = envy24ht_rdrom(sc, ENVY24HT_E2PROM_GPIOSTATE) | \
	envy24ht_rdrom(sc, ENVY24HT_E2PROM_GPIOSTATE + 1) << 8 | \
	envy24ht_rdrom(sc, ENVY24HT_E2PROM_GPIOSTATE + 2) << 16;
	buff->gpiodir = envy24ht_rdrom(sc, ENVY24HT_E2PROM_GPIODIR) | \
	envy24ht_rdrom(sc, ENVY24HT_E2PROM_GPIODIR + 1) << 8 | \
	envy24ht_rdrom(sc, ENVY24HT_E2PROM_GPIODIR + 2) << 16;

	for (i = 0; cfg_table[i].subvendor != 0 || cfg_table[i].subdevice != 0; i++)
		if (cfg_table[i].subvendor == buff->subvendor &&
		    cfg_table[i].subdevice == buff->subdevice)
			break;
	buff->name = cfg_table[i].name;
	buff->codec = cfg_table[i].codec;

	return buff;
}

static void
envy24ht_cfgfree(struct cfg_info *cfg) {
	if (cfg == NULL)
		return;
	if (cfg->free)
		free(cfg, M_ENVY24HT);
	return;
}

/* -------------------------------------------------------------------- */

/* AC'97 codec access routines */

#if 0
static int
envy24ht_coldcd(struct sc_info *sc)
{
	u_int32_t data;
	int i;

#if(0)
	device_printf(sc->dev, "envy24ht_coldcd()\n");
#endif
	envy24ht_wrmt(sc, ENVY24HT_MT_AC97CMD, ENVY24HT_MT_AC97CMD_CLD, 1);
	DELAY(10);
	envy24ht_wrmt(sc, ENVY24HT_MT_AC97CMD, 0, 1);
	DELAY(1000);
	for (i = 0; i < ENVY24HT_TIMEOUT; i++) {
		data = envy24ht_rdmt(sc, ENVY24HT_MT_AC97CMD, 1);
		if (data & ENVY24HT_MT_AC97CMD_RDY) {
			return 0;
		}
	}

	return -1;
}

static int
envy24ht_slavecd(struct sc_info *sc)
{
	u_int32_t data;
	int i;

#if(0)
	device_printf(sc->dev, "envy24ht_slavecd()\n");
#endif
	envy24ht_wrmt(sc, ENVY24HT_MT_AC97CMD,
	    ENVY24HT_MT_AC97CMD_CLD | ENVY24HT_MT_AC97CMD_WRM, 1);
	DELAY(10);
	envy24ht_wrmt(sc, ENVY24HT_MT_AC97CMD, 0, 1);
	DELAY(1000);
	for (i = 0; i < ENVY24HT_TIMEOUT; i++) {
		data = envy24ht_rdmt(sc, ENVY24HT_MT_AC97CMD, 1);
		if (data & ENVY24HT_MT_AC97CMD_RDY) {
			return 0;
		}
	}

	return -1;
}

static int
envy24ht_rdcd(kobj_t obj, void *devinfo, int regno)
{
	struct sc_info *sc = (struct sc_info *)devinfo;
	u_int32_t data;
	int i;

#if(0)
	device_printf(sc->dev, "envy24ht_rdcd(obj, sc, 0x%02x)\n", regno);
#endif
	envy24ht_wrmt(sc, ENVY24HT_MT_AC97IDX, (u_int32_t)regno, 1);
	envy24ht_wrmt(sc, ENVY24HT_MT_AC97CMD, ENVY24HT_MT_AC97CMD_RD, 1);
	for (i = 0; i < ENVY24HT_TIMEOUT; i++) {
		data = envy24ht_rdmt(sc, ENVY24HT_MT_AC97CMD, 1);
		if ((data & ENVY24HT_MT_AC97CMD_RD) == 0)
			break;
	}
	data = envy24ht_rdmt(sc, ENVY24HT_MT_AC97DLO, 2);

#if(0)
	device_printf(sc->dev, "envy24ht_rdcd(): return 0x%x\n", data);
#endif
	return (int)data;
}

static int
envy24ht_wrcd(kobj_t obj, void *devinfo, int regno, u_int16_t data)
{
	struct sc_info *sc = (struct sc_info *)devinfo;
	u_int32_t cmd;
	int i;

#if(0)
	device_printf(sc->dev, "envy24ht_wrcd(obj, sc, 0x%02x, 0x%04x)\n", regno, data);
#endif
	envy24ht_wrmt(sc, ENVY24HT_MT_AC97IDX, (u_int32_t)regno, 1);
	envy24ht_wrmt(sc, ENVY24HT_MT_AC97DLO, (u_int32_t)data, 2);
	envy24ht_wrmt(sc, ENVY24HT_MT_AC97CMD, ENVY24HT_MT_AC97CMD_WR, 1);
	for (i = 0; i < ENVY24HT_TIMEOUT; i++) {
		cmd = envy24ht_rdmt(sc, ENVY24HT_MT_AC97CMD, 1);
		if ((cmd & ENVY24HT_MT_AC97CMD_WR) == 0)
			break;
	}

	return 0;
}

static kobj_method_t envy24ht_ac97_methods[] = {
	KOBJMETHOD(ac97_read,	envy24ht_rdcd),
	KOBJMETHOD(ac97_write,	envy24ht_wrcd),
	KOBJMETHOD_END
};
AC97_DECLARE(envy24ht_ac97);
#endif

/* -------------------------------------------------------------------- */

/* GPIO access routines */

static u_int32_t
envy24ht_gpiord(struct sc_info *sc)
{
	if (sc->cfg->subvendor == 0x153b  && sc->cfg->subdevice == 0x1150) 
	return envy24ht_rdcs(sc, ENVY24HT_CCS_GPIO_LDATA, 2);
	else
	return (envy24ht_rdcs(sc, ENVY24HT_CCS_GPIO_HDATA, 1) << 16 | envy24ht_rdcs(sc, ENVY24HT_CCS_GPIO_LDATA, 2));
}

static void
envy24ht_gpiowr(struct sc_info *sc, u_int32_t data)
{
#if(0)
	device_printf(sc->dev, "envy24ht_gpiowr(sc, 0x%02x)\n", data & 0x7FFFFF);
	return;
#endif
	envy24ht_wrcs(sc, ENVY24HT_CCS_GPIO_LDATA, data, 2);
	if (sc->cfg->subdevice != 0x1150)
	envy24ht_wrcs(sc, ENVY24HT_CCS_GPIO_HDATA, data >> 16, 1);
	return;
}

#if 0
static u_int32_t
envy24ht_gpiogetmask(struct sc_info *sc)
{
	return (envy24ht_rdcs(sc, ENVY24HT_CCS_GPIO_HMASK, 1) << 16 | envy24ht_rdcs(sc, ENVY24HT_CCS_GPIO_LMASK, 2));
}
#endif

static void
envy24ht_gpiosetmask(struct sc_info *sc, u_int32_t mask)
{
        envy24ht_wrcs(sc, ENVY24HT_CCS_GPIO_LMASK, mask, 2);
	if (sc->cfg->subdevice != 0x1150)
        envy24ht_wrcs(sc, ENVY24HT_CCS_GPIO_HMASK, mask >> 16, 1);
	return;
}

#if 0
static u_int32_t
envy24ht_gpiogetdir(struct sc_info *sc)
{
	return envy24ht_rdcs(sc, ENVY24HT_CCS_GPIO_CTLDIR, 4);
}
#endif

static void
envy24ht_gpiosetdir(struct sc_info *sc, u_int32_t dir)
{
	if (sc->cfg->subvendor == 0x153b  && sc->cfg->subdevice == 0x1150)
	envy24ht_wrcs(sc, ENVY24HT_CCS_GPIO_CTLDIR, dir, 2);
	else 
	envy24ht_wrcs(sc, ENVY24HT_CCS_GPIO_CTLDIR, dir, 4);
	return;
}

/* -------------------------------------------------------------------- */

/* SPI codec access interface routine */

struct envy24ht_spi_codec {
	struct spicds_info *info;
	struct sc_info *parent;
	int dir;
	int num;
	int cs, cclk, cdti;
};

static void
envy24ht_spi_ctl(void *codec, unsigned int cs, unsigned int cclk, unsigned int cdti)
{
	u_int32_t data = 0;
	struct envy24ht_spi_codec *ptr = codec;

#if(0)
	device_printf(ptr->parent->dev, "--> %d, %d, %d\n", cs, cclk, cdti);
#endif
	data = envy24ht_gpiord(ptr->parent);
	data &= ~(ptr->cs | ptr->cclk | ptr->cdti);
	if (cs) data += ptr->cs;
	if (cclk) data += ptr->cclk;
	if (cdti) data += ptr->cdti;
	envy24ht_gpiowr(ptr->parent, data);
	return;
}

static void *
envy24ht_spi_create(device_t dev, void *info, int dir, int num)
{
	struct sc_info *sc = info;
	struct envy24ht_spi_codec *buff = NULL;

#if(0)
	device_printf(sc->dev, "envy24ht_spi_create(dev, sc, %d, %d)\n", dir, num);
#endif
	
	buff = malloc(sizeof(*buff), M_ENVY24HT, M_NOWAIT);
	if (buff == NULL)
		return NULL;

	if (dir == PCMDIR_REC && sc->adc[num] != NULL)
		buff->info = ((struct envy24ht_spi_codec *)sc->adc[num])->info;
	else if (dir == PCMDIR_PLAY && sc->dac[num] != NULL)
		buff->info = ((struct envy24ht_spi_codec *)sc->dac[num])->info;
	else
		buff->info = spicds_create(dev, buff, num, envy24ht_spi_ctl);
	if (buff->info == NULL) {
		free(buff, M_ENVY24HT);
		return NULL;
	}

	buff->parent = sc;
	buff->dir = dir;
	buff->num = num;

	return (void *)buff;
}

static void
envy24ht_spi_destroy(void *codec)
{
	struct envy24ht_spi_codec *ptr = codec;
	if (ptr == NULL)
		return;
#if(0)
	device_printf(ptr->parent->dev, "envy24ht_spi_destroy()\n");
#endif

	if (ptr->dir == PCMDIR_PLAY) {
		if (ptr->parent->dac[ptr->num] != NULL)
			spicds_destroy(ptr->info);
	}
	else {
		if (ptr->parent->adc[ptr->num] != NULL)
			spicds_destroy(ptr->info);
	}

	free(codec, M_ENVY24HT);
}

static void
envy24ht_spi_init(void *codec)
{
	struct envy24ht_spi_codec *ptr = codec;
	if (ptr == NULL)
		return;
#if(0)
	device_printf(ptr->parent->dev, "envy24ht_spicds_init()\n");
#endif
        ptr->cs = ptr->parent->cfg->cs;
	ptr->cclk = ptr->parent->cfg->cclk;
	ptr->cdti =  ptr->parent->cfg->cdti;
	spicds_settype(ptr->info, ptr->parent->cfg->type);
	spicds_setcif(ptr->info, ptr->parent->cfg->cif);
	if (ptr->parent->cfg->type == SPICDS_TYPE_AK4524 || \
	ptr->parent->cfg->type == SPICDS_TYPE_AK4528) {
	spicds_setformat(ptr->info,
	    AK452X_FORMAT_I2S | AK452X_FORMAT_256FSN | AK452X_FORMAT_1X);
	spicds_setdvc(ptr->info, AK452X_DVC_DEMOFF);
	}

	/* for the time being, init only first codec */
	if (ptr->num == 0)
	spicds_init(ptr->info);
}

static void
envy24ht_spi_reinit(void *codec)
{
	struct envy24ht_spi_codec *ptr = codec;
	if (ptr == NULL)
		return;
#if(0)
	device_printf(ptr->parent->dev, "envy24ht_spi_reinit()\n");
#endif

	spicds_reinit(ptr->info);
}

static void
envy24ht_spi_setvolume(void *codec, int dir, unsigned int left, unsigned int right)
{
	struct envy24ht_spi_codec *ptr = codec;
	if (ptr == NULL)
		return;
#if(0)
	device_printf(ptr->parent->dev, "envy24ht_spi_set()\n");
#endif

	spicds_set(ptr->info, dir, left, right);
}

/* -------------------------------------------------------------------- */

/* hardware access routeines */

static struct {
	u_int32_t speed;
	u_int32_t code;
} envy24ht_speedtab[] = {
	{48000, ENVY24HT_MT_RATE_48000},
	{24000, ENVY24HT_MT_RATE_24000},
	{12000, ENVY24HT_MT_RATE_12000},
	{9600, ENVY24HT_MT_RATE_9600},
	{32000, ENVY24HT_MT_RATE_32000},
	{16000, ENVY24HT_MT_RATE_16000},
	{8000, ENVY24HT_MT_RATE_8000},
	{96000, ENVY24HT_MT_RATE_96000},
	{192000, ENVY24HT_MT_RATE_192000},
	{64000, ENVY24HT_MT_RATE_64000},
	{44100, ENVY24HT_MT_RATE_44100},
	{22050, ENVY24HT_MT_RATE_22050},
	{11025, ENVY24HT_MT_RATE_11025},
	{88200, ENVY24HT_MT_RATE_88200},
	{176400, ENVY24HT_MT_RATE_176400},
	{0, 0x10}
};

static u_int32_t
envy24ht_setspeed(struct sc_info *sc, u_int32_t speed) {
	u_int32_t code, i2sfmt;
	int i = 0;

#if(0)
	device_printf(sc->dev, "envy24ht_setspeed(sc, %d)\n", speed);
	if (speed == 0) {
		code = ENVY24HT_MT_RATE_SPDIF; /* external master clock */
		envy24ht_slavecd(sc);
	}
	else {
#endif
		for (i = 0; envy24ht_speedtab[i].speed != 0; i++) {
			if (envy24ht_speedtab[i].speed == speed)
				break;
		}
		code = envy24ht_speedtab[i].code;
#if 0
	}
	device_printf(sc->dev, "envy24ht_setspeed(): speed %d/code 0x%04x\n", envy24ht_speedtab[i].speed, code);
#endif
	if (code < 0x10) {
		envy24ht_wrmt(sc, ENVY24HT_MT_RATE, code, 1);
		if ((((sc->cfg->scfg & ENVY24HT_CCSM_SCFG_XIN2) == 0x00) && (code == ENVY24HT_MT_RATE_192000)) || \
									    (code == ENVY24HT_MT_RATE_176400)) {
			i2sfmt = envy24ht_rdmt(sc, ENVY24HT_MT_I2S, 1);
			i2sfmt |= ENVY24HT_MT_I2S_MLR128;
			envy24ht_wrmt(sc, ENVY24HT_MT_I2S, i2sfmt, 1);
		}
		else {
			i2sfmt = envy24ht_rdmt(sc, ENVY24HT_MT_I2S, 1);
			i2sfmt &= ~ENVY24HT_MT_I2S_MLR128;
			envy24ht_wrmt(sc, ENVY24HT_MT_I2S, i2sfmt, 1);
		}
		code = envy24ht_rdmt(sc, ENVY24HT_MT_RATE, 1);
		code &= ENVY24HT_MT_RATE_MASK;
		for (i = 0; envy24ht_speedtab[i].code < 0x10; i++) {
			if (envy24ht_speedtab[i].code == code)
				break;
		}
		speed = envy24ht_speedtab[i].speed;
	}
	else
		speed = 0;

#if(0)
	device_printf(sc->dev, "envy24ht_setspeed(): return %d\n", speed);
#endif
	return speed;
}

static void
envy24ht_setvolume(struct sc_info *sc, unsigned ch)
{
#if(0)
	device_printf(sc->dev, "envy24ht_setvolume(sc, %d)\n", ch);
	envy24ht_wrmt(sc, ENVY24HT_MT_VOLIDX, ch * 2, 1);
	envy24ht_wrmt(sc, ENVY24HT_MT_VOLUME, 0x7f00 | sc->left[ch], 2);
	envy24ht_wrmt(sc, ENVY24HT_MT_VOLIDX, ch * 2 + 1, 1);
	envy24ht_wrmt(sc, ENVY24HT_MT_VOLUME, (sc->right[ch] << 8) | 0x7f, 2);
#endif
}

static void
envy24ht_mutevolume(struct sc_info *sc, unsigned ch)
{
#if 0
	u_int32_t vol;

	device_printf(sc->dev, "envy24ht_mutevolume(sc, %d)\n", ch);
	vol = ENVY24HT_VOL_MUTE << 8 | ENVY24HT_VOL_MUTE;
	envy24ht_wrmt(sc, ENVY24HT_MT_VOLIDX, ch * 2, 1);
	envy24ht_wrmt(sc, ENVY24HT_MT_VOLUME, vol, 2);
	envy24ht_wrmt(sc, ENVY24HT_MT_VOLIDX, ch * 2 + 1, 1);
	envy24ht_wrmt(sc, ENVY24HT_MT_VOLUME, vol, 2);
#endif
}

static u_int32_t
envy24ht_gethwptr(struct sc_info *sc, int dir)
{
	int unit, regno;
	u_int32_t ptr, rtn;

#if(0)
	device_printf(sc->dev, "envy24ht_gethwptr(sc, %d)\n", dir);
#endif
	if (dir == PCMDIR_PLAY) {
		rtn = sc->psize / 4;
		unit = ENVY24HT_PLAY_BUFUNIT / 4;
		regno = ENVY24HT_MT_PCNT;
	}
	else {
		rtn = sc->rsize / 4;
		unit = ENVY24HT_REC_BUFUNIT / 4;
		regno = ENVY24HT_MT_RCNT;
	}

	ptr = envy24ht_rdmt(sc, regno, 2);
	rtn -= (ptr + 1);
	rtn /= unit;

#if(0)
	device_printf(sc->dev, "envy24ht_gethwptr(): return %d\n", rtn);
#endif
	return rtn;
}

static void
envy24ht_updintr(struct sc_info *sc, int dir)
{
	int regptr, regintr;
	u_int32_t mask, intr;
	u_int32_t ptr, size, cnt;
	u_int16_t blk;

#if(0)
	device_printf(sc->dev, "envy24ht_updintr(sc, %d)\n", dir);
#endif
	if (dir == PCMDIR_PLAY) {
		blk = sc->blk[0];
		size = sc->psize / 4;
		regptr = ENVY24HT_MT_PCNT;
		regintr = ENVY24HT_MT_PTERM;
		mask = ~ENVY24HT_MT_INT_PMASK;
	}
	else {
		blk = sc->blk[1];
		size = sc->rsize / 4;
		regptr = ENVY24HT_MT_RCNT;
		regintr = ENVY24HT_MT_RTERM;
		mask = ~ENVY24HT_MT_INT_RMASK;
	}

	ptr = size - envy24ht_rdmt(sc, regptr, 2) - 1;
	/*
	cnt = blk - ptr % blk - 1;
	if (cnt == 0)
		cnt = blk - 1;
	*/
	cnt = blk - 1;
#if(0)
	device_printf(sc->dev, "envy24ht_updintr():ptr = %d, blk = %d, cnt = %d\n", ptr, blk, cnt);
#endif
	envy24ht_wrmt(sc, regintr, cnt, 2);
	intr = envy24ht_rdmt(sc, ENVY24HT_MT_INT_MASK, 1);
#if(0)
	device_printf(sc->dev, "envy24ht_updintr():intr = 0x%02x, mask = 0x%02x\n", intr, mask);
#endif
	envy24ht_wrmt(sc, ENVY24HT_MT_INT_MASK, intr & mask, 1);
#if(0)
	device_printf(sc->dev, "envy24ht_updintr():INT-->0x%02x\n",
		      envy24ht_rdmt(sc, ENVY24HT_MT_INT_MASK, 1));
#endif

	return;
}

#if 0
static void
envy24ht_maskintr(struct sc_info *sc, int dir)
{
	u_int32_t mask, intr;

#if(0)
	device_printf(sc->dev, "envy24ht_maskintr(sc, %d)\n", dir);
#endif
	if (dir == PCMDIR_PLAY)
		mask = ENVY24HT_MT_INT_PMASK;
	else
		mask = ENVY24HT_MT_INT_RMASK;
	intr = envy24ht_rdmt(sc, ENVY24HT_MT_INT, 1);
	envy24ht_wrmt(sc, ENVY24HT_MT_INT, intr | mask, 1);

	return;
}
#endif

static int
envy24ht_checkintr(struct sc_info *sc, int dir)
{
	u_int32_t mask, stat, intr, rtn;

#if(0)
	device_printf(sc->dev, "envy24ht_checkintr(sc, %d)\n", dir);
#endif
	intr = envy24ht_rdmt(sc, ENVY24HT_MT_INT_STAT, 1);
	if (dir == PCMDIR_PLAY) {
		if ((rtn = intr & ENVY24HT_MT_INT_PSTAT) != 0) {
			mask = ~ENVY24HT_MT_INT_RSTAT;
			envy24ht_wrmt(sc, 0x1a, 0x01, 1);
			envy24ht_wrmt(sc, ENVY24HT_MT_INT_STAT, (intr & mask) | ENVY24HT_MT_INT_PSTAT | 0x08, 1);	
			stat = envy24ht_rdmt(sc, ENVY24HT_MT_INT_MASK, 1);
			envy24ht_wrmt(sc, ENVY24HT_MT_INT_MASK, stat | ENVY24HT_MT_INT_PMASK, 1);
		}
	}
	else {
		if ((rtn = intr & ENVY24HT_MT_INT_RSTAT) != 0) {
			mask = ~ENVY24HT_MT_INT_PSTAT;
#if 0
			stat = ENVY24HT_MT_INT_RSTAT | ENVY24HT_MT_INT_RMASK;
#endif
			envy24ht_wrmt(sc, ENVY24HT_MT_INT_STAT, (intr & mask) | ENVY24HT_MT_INT_RSTAT, 1);
			stat = envy24ht_rdmt(sc, ENVY24HT_MT_INT_MASK, 1);
			envy24ht_wrmt(sc, ENVY24HT_MT_INT_MASK, stat | ENVY24HT_MT_INT_RMASK, 1);
		}
	}

	return rtn;
}

static void
envy24ht_start(struct sc_info *sc, int dir)
{
	u_int32_t stat, sw;

#if(0)
	device_printf(sc->dev, "envy24ht_start(sc, %d)\n", dir);
#endif
	if (dir == PCMDIR_PLAY)
		sw = ENVY24HT_MT_PCTL_PSTART;
	else
		sw = ENVY24HT_MT_PCTL_RSTART;

	stat = envy24ht_rdmt(sc, ENVY24HT_MT_PCTL, 1);
	envy24ht_wrmt(sc, ENVY24HT_MT_PCTL, stat | sw, 1);
#if(0)
	DELAY(100);
	device_printf(sc->dev, "PADDR:0x%08x\n", envy24ht_rdmt(sc, ENVY24HT_MT_PADDR, 4));
	device_printf(sc->dev, "PCNT:%ld\n", envy24ht_rdmt(sc, ENVY24HT_MT_PCNT, 2));
#endif

	return;
}

static void
envy24ht_stop(struct sc_info *sc, int dir)
{
	u_int32_t stat, sw;

#if(0)
	device_printf(sc->dev, "envy24ht_stop(sc, %d)\n", dir);
#endif
	if (dir == PCMDIR_PLAY)
		sw = ~ENVY24HT_MT_PCTL_PSTART;
	else
		sw = ~ENVY24HT_MT_PCTL_RSTART;

	stat = envy24ht_rdmt(sc, ENVY24HT_MT_PCTL, 1);
	envy24ht_wrmt(sc, ENVY24HT_MT_PCTL, stat & sw, 1);

	return;
}

#if 0
static int
envy24ht_route(struct sc_info *sc, int dac, int class, int adc, int rev)
{
	return 0;
}
#endif

/* -------------------------------------------------------------------- */

/* buffer copy routines */
static void
envy24ht_p32sl(struct sc_chinfo *ch)
{
	int length;
	sample32_t *dmabuf;
	u_int32_t *data;
	int src, dst, ssize, dsize, slot;
	int i;

	length = sndbuf_getready(ch->buffer) / 8;
	dmabuf = ch->parent->pbuf;
	data = (u_int32_t *)ch->data;
	src = sndbuf_getreadyptr(ch->buffer) / 4;
	dst = src / 2 + ch->offset;
	ssize = ch->size / 4;
	dsize = ch->size / 8;
	slot = ch->num * 2;

	for (i = 0; i < length; i++) {
		dmabuf[dst * ENVY24HT_PLAY_CHNUM + slot].buffer = data[src];
		dmabuf[dst * ENVY24HT_PLAY_CHNUM + slot + 1].buffer = data[src + 1];
		dst++;
		dst %= dsize;
		src += 2;
		src %= ssize;
	}
	
	return;
}

static void
envy24ht_p16sl(struct sc_chinfo *ch)
{
	int length;
	sample32_t *dmabuf;
	u_int16_t *data;
	int src, dst, ssize, dsize, slot;
	int i;

#if(0)
	device_printf(ch->parent->dev, "envy24ht_p16sl()\n");
#endif
	length = sndbuf_getready(ch->buffer) / 4;
	dmabuf = ch->parent->pbuf;
	data = (u_int16_t *)ch->data;
	src = sndbuf_getreadyptr(ch->buffer) / 2;
	dst = src / 2 + ch->offset;
	ssize = ch->size / 2;
	dsize = ch->size / 4;
	slot = ch->num * 2;
#if(0)
	device_printf(ch->parent->dev, "envy24ht_p16sl():%lu-->%lu(%lu)\n", src, dst, length);
#endif
	
	for (i = 0; i < length; i++) {
		dmabuf[dst * ENVY24HT_PLAY_CHNUM + slot].buffer = (u_int32_t)data[src] << 16;
		dmabuf[dst * ENVY24HT_PLAY_CHNUM + slot + 1].buffer = (u_int32_t)data[src + 1] << 16;
#if(0)
		if (i < 16) {
			printf("%08x", dmabuf[dst * ENVY24HT_PLAY_CHNUM + slot]);
			printf("%08x", dmabuf[dst * ENVY24HT_PLAY_CHNUM + slot + 1]);
		}
#endif
		dst++;
		dst %= dsize;
		src += 2;
		src %= ssize;
	}
#if(0)
	printf("\n");
#endif
	
	return;
}

static void
envy24ht_p8u(struct sc_chinfo *ch)
{
	int length;
	sample32_t *dmabuf;
	u_int8_t *data;
	int src, dst, ssize, dsize, slot;
	int i;

	length = sndbuf_getready(ch->buffer) / 2;
	dmabuf = ch->parent->pbuf;
	data = (u_int8_t *)ch->data;
	src = sndbuf_getreadyptr(ch->buffer);
	dst = src / 2 + ch->offset;
	ssize = ch->size;
	dsize = ch->size / 4;
	slot = ch->num * 2;
	
	for (i = 0; i < length; i++) {
		dmabuf[dst * ENVY24HT_PLAY_CHNUM + slot].buffer = ((u_int32_t)data[src] ^ 0x80) << 24;
		dmabuf[dst * ENVY24HT_PLAY_CHNUM + slot + 1].buffer = ((u_int32_t)data[src + 1] ^ 0x80) << 24;
		dst++;
		dst %= dsize;
		src += 2;
		src %= ssize;
	}
	
	return;
}

static void
envy24ht_r32sl(struct sc_chinfo *ch)
{
	int length;
	sample32_t *dmabuf;
	u_int32_t *data;
	int src, dst, ssize, dsize, slot;
	int i;

	length = sndbuf_getfree(ch->buffer) / 8;
	dmabuf = ch->parent->rbuf;
	data = (u_int32_t *)ch->data;
	dst = sndbuf_getfreeptr(ch->buffer) / 4;
	src = dst / 2 + ch->offset;
	dsize = ch->size / 4;
	ssize = ch->size / 8;
	slot = (ch->num - ENVY24HT_CHAN_REC_ADC1) * 2;

	for (i = 0; i < length; i++) {
		data[dst] = dmabuf[src * ENVY24HT_REC_CHNUM + slot].buffer;
		data[dst + 1] = dmabuf[src * ENVY24HT_REC_CHNUM + slot + 1].buffer;
		dst += 2;
		dst %= dsize;
		src++;
		src %= ssize;
	}
	
	return;
}

static void
envy24ht_r16sl(struct sc_chinfo *ch)
{
	int length;
	sample32_t *dmabuf;
	u_int16_t *data;
	int src, dst, ssize, dsize, slot;
	int i;

	length = sndbuf_getfree(ch->buffer) / 4;
	dmabuf = ch->parent->rbuf;
	data = (u_int16_t *)ch->data;
	dst = sndbuf_getfreeptr(ch->buffer) / 2;
	src = dst / 2 + ch->offset;
	dsize = ch->size / 2;
	ssize = ch->size / 8;
	slot = (ch->num - ENVY24HT_CHAN_REC_ADC1) * 2;

	for (i = 0; i < length; i++) {
		data[dst] = dmabuf[src * ENVY24HT_REC_CHNUM + slot].buffer;
		data[dst + 1] = dmabuf[src * ENVY24HT_REC_CHNUM + slot + 1].buffer;
		dst += 2;
		dst %= dsize;
		src++;
		src %= ssize;
	}
	
	return;
}

/* -------------------------------------------------------------------- */

/* channel interface */
static void *
envy24htchan_init(kobj_t obj, void *devinfo, struct snd_dbuf *b, struct pcm_channel *c, int dir)
{
	struct sc_info	*sc = (struct sc_info *)devinfo;
	struct sc_chinfo *ch;
	unsigned num;

#if(0)
	device_printf(sc->dev, "envy24htchan_init(obj, devinfo, b, c, %d)\n", dir);
#endif
	snd_mtxlock(sc->lock);
#if 0
	if ((sc->chnum > ENVY24HT_CHAN_PLAY_SPDIF && dir != PCMDIR_REC) ||
	    (sc->chnum < ENVY24HT_CHAN_REC_ADC1 && dir != PCMDIR_PLAY)) {
		snd_mtxunlock(sc->lock);
		return NULL;
	}
#endif
	num = sc->chnum;

	ch = &sc->chan[num];
	ch->size = 8 * ENVY24HT_SAMPLE_NUM;
	ch->data = malloc(ch->size, M_ENVY24HT, M_NOWAIT);
	if (ch->data == NULL) {
		ch->size = 0;
		ch = NULL;
	}
	else {
		ch->buffer = b;
		ch->channel = c;
		ch->parent = sc;
		ch->dir = dir;
		/* set channel map */
		ch->num = envy24ht_chanmap[num];
		snd_mtxunlock(sc->lock);
		sndbuf_setup(ch->buffer, ch->data, ch->size);
		snd_mtxlock(sc->lock);
		/* these 2 values are dummy */
		ch->unit = 4;
		ch->blk = 10240;
	}
	snd_mtxunlock(sc->lock);

	return ch;
}

static int
envy24htchan_free(kobj_t obj, void *data)
{
	struct sc_chinfo *ch = data;
	struct sc_info *sc = ch->parent;

#if(0)
	device_printf(sc->dev, "envy24htchan_free()\n");
#endif
	snd_mtxlock(sc->lock);
	if (ch->data != NULL) {
		free(ch->data, M_ENVY24HT);
		ch->data = NULL;
	}
	snd_mtxunlock(sc->lock);

	return 0;
}

static int
envy24htchan_setformat(kobj_t obj, void *data, u_int32_t format)
{
	struct sc_chinfo *ch = data;
	struct sc_info *sc = ch->parent;
	struct envy24ht_emldma *emltab;
	/* unsigned int bcnt, bsize; */
	int i;

#if(0)
	device_printf(sc->dev, "envy24htchan_setformat(obj, data, 0x%08x)\n", format);
#endif
	snd_mtxlock(sc->lock);
	/* check and get format related information */
	if (ch->dir == PCMDIR_PLAY)
		emltab = envy24ht_pemltab;
	else
		emltab = envy24ht_remltab;
	if (emltab == NULL) {
		snd_mtxunlock(sc->lock);
		return -1;
	}
	for (i = 0; emltab[i].format != 0; i++)
		if (emltab[i].format == format)
			break;
	if (emltab[i].format == 0) {
		snd_mtxunlock(sc->lock);
		return -1;
	}

	/* set format information */
	ch->format = format;
	ch->emldma = emltab[i].emldma;
	if (ch->unit > emltab[i].unit)
		ch->blk *= ch->unit / emltab[i].unit;
	else
		ch->blk /= emltab[i].unit / ch->unit;
	ch->unit = emltab[i].unit;

	/* set channel buffer information */
	ch->size = ch->unit * ENVY24HT_SAMPLE_NUM;
#if 0
	if (ch->dir == PCMDIR_PLAY)
		bsize = ch->blk * 4 / ENVY24HT_PLAY_BUFUNIT;
	else
		bsize = ch->blk * 4 / ENVY24HT_REC_BUFUNIT;
	bsize *= ch->unit;
	bcnt = ch->size / bsize;
	sndbuf_resize(ch->buffer, bcnt, bsize);
#endif
	snd_mtxunlock(sc->lock);

#if(0)
	device_printf(sc->dev, "envy24htchan_setformat(): return 0x%08x\n", 0);
#endif
	return 0;
}

/*
  IMPLEMENT NOTICE: In this driver, setspeed function only do setting
  of speed information value. And real hardware speed setting is done
  at start triggered(see envy24htchan_trigger()). So, at this function
  is called, any value that ENVY24 can use is able to set. But, at
  start triggerd, some other channel is running, and that channel's
  speed isn't same with, then trigger function will fail.
*/
static u_int32_t
envy24htchan_setspeed(kobj_t obj, void *data, u_int32_t speed)
{
	struct sc_chinfo *ch = data;
	u_int32_t val, prev;
	int i;

#if(0)
	device_printf(ch->parent->dev, "envy24htchan_setspeed(obj, data, %d)\n", speed);
#endif
	prev = 0x7fffffff;
	for (i = 0; (val = envy24ht_speed[i]) != 0; i++) {
		if (abs(val - speed) < abs(prev - speed))
			prev = val;
		else
			break;
	}
	ch->speed = prev;
	
#if(0)
	device_printf(ch->parent->dev, "envy24htchan_setspeed(): return %d\n", ch->speed);
#endif
	return ch->speed;
}

static u_int32_t
envy24htchan_setblocksize(kobj_t obj, void *data, u_int32_t blocksize)
{
	struct sc_chinfo *ch = data;
	/* struct sc_info *sc = ch->parent; */
	u_int32_t size, prev;
	unsigned int bcnt, bsize;

#if(0)
	device_printf(sc->dev, "envy24htchan_setblocksize(obj, data, %d)\n", blocksize);
#endif
	prev = 0x7fffffff;
	/* snd_mtxlock(sc->lock); */
	for (size = ch->size / 2; size > 0; size /= 2) {
		if (abs(size - blocksize) < abs(prev - blocksize))
			prev = size;
		else
			break;
	}

	ch->blk = prev / ch->unit;
	if (ch->dir == PCMDIR_PLAY)
		ch->blk *= ENVY24HT_PLAY_BUFUNIT / 4;
	else
		ch->blk *= ENVY24HT_REC_BUFUNIT / 4;
        /* set channel buffer information */
        /* ch->size = ch->unit * ENVY24HT_SAMPLE_NUM; */
        if (ch->dir == PCMDIR_PLAY)
                bsize = ch->blk * 4 / ENVY24HT_PLAY_BUFUNIT;
        else
                bsize = ch->blk * 4 / ENVY24HT_REC_BUFUNIT;
        bsize *= ch->unit;
        bcnt = ch->size / bsize;
        sndbuf_resize(ch->buffer, bcnt, bsize);
	/* snd_mtxunlock(sc->lock); */

#if(0)
	device_printf(sc->dev, "envy24htchan_setblocksize(): return %d\n", prev);
#endif
	return prev;
}

/* semantic note: must start at beginning of buffer */
static int
envy24htchan_trigger(kobj_t obj, void *data, int go)
{
	struct sc_chinfo *ch = data;
	struct sc_info *sc = ch->parent;
	u_int32_t ptr;
	int slot;
	int error = 0;
#if 0
	int i;

	device_printf(sc->dev, "envy24htchan_trigger(obj, data, %d)\n", go);
#endif
	snd_mtxlock(sc->lock);
	if (ch->dir == PCMDIR_PLAY)
		slot = 0;
	else
		slot = 1;
	switch (go) {
	case PCMTRIG_START:
#if(0)
		device_printf(sc->dev, "envy24htchan_trigger(): start\n");
#endif
		/* check or set channel speed */
		if (sc->run[0] == 0 && sc->run[1] == 0) {
			sc->speed = envy24ht_setspeed(sc, ch->speed);
			sc->caps[0].minspeed = sc->caps[0].maxspeed = sc->speed;
			sc->caps[1].minspeed = sc->caps[1].maxspeed = sc->speed;
		}
		else if (ch->speed != 0 && ch->speed != sc->speed) {
			error = -1;
			goto fail;
		}
		if (ch->speed == 0)
			ch->channel->speed = sc->speed;
		/* start or enable channel */
		sc->run[slot]++;
		if (sc->run[slot] == 1) {
			/* first channel */
			ch->offset = 0;
			sc->blk[slot] = ch->blk;
		}
		else {
			ptr = envy24ht_gethwptr(sc, ch->dir);
			ch->offset = ((ptr / ch->blk + 1) * ch->blk %
			    (ch->size / 4)) * 4 / ch->unit;
			if (ch->blk < sc->blk[slot])
				sc->blk[slot] = ch->blk;
		}
		if (ch->dir == PCMDIR_PLAY) {
			ch->emldma(ch);
			envy24ht_setvolume(sc, ch->num);
		}
		envy24ht_updintr(sc, ch->dir);
		if (sc->run[slot] == 1)
			envy24ht_start(sc, ch->dir);
		ch->run = 1;
		break;
	case PCMTRIG_EMLDMAWR:
#if(0)
		device_printf(sc->dev, "envy24htchan_trigger(): emldmawr\n");
#endif
		if (ch->run != 1) {
			error = -1;
			goto fail;
		}
		ch->emldma(ch);
		break;
	case PCMTRIG_EMLDMARD:
#if(0)
		device_printf(sc->dev, "envy24htchan_trigger(): emldmard\n");
#endif
		if (ch->run != 1) {
			error = -1;
			goto fail;
		}
		ch->emldma(ch);
		break;
	case PCMTRIG_ABORT:
		if (ch->run) {
#if(0)
		device_printf(sc->dev, "envy24htchan_trigger(): abort\n");
#endif
		ch->run = 0;
		sc->run[slot]--;
		if (ch->dir == PCMDIR_PLAY)
			envy24ht_mutevolume(sc, ch->num);
		if (sc->run[slot] == 0) {
			envy24ht_stop(sc, ch->dir);
			sc->intr[slot] = 0;
		}
/*		else if (ch->blk == sc->blk[slot]) {
			sc->blk[slot] = ENVY24HT_SAMPLE_NUM / 2;
			for (i = 0; i < ENVY24HT_CHAN_NUM; i++) {
				if (sc->chan[i].dir == ch->dir &&
				    sc->chan[i].run == 1 &&
				    sc->chan[i].blk < sc->blk[slot])
					sc->blk[slot] = sc->chan[i].blk;
			}
			if (ch->blk != sc->blk[slot])
				envy24ht_updintr(sc, ch->dir);
		}*/
		}
		break;
	}
fail:
	snd_mtxunlock(sc->lock);
	return (error);
}

static u_int32_t
envy24htchan_getptr(kobj_t obj, void *data)
{
	struct sc_chinfo *ch = data;
	struct sc_info *sc = ch->parent;
	u_int32_t ptr, rtn;

#if(0)
	device_printf(sc->dev, "envy24htchan_getptr()\n");
#endif
	snd_mtxlock(sc->lock);
	ptr = envy24ht_gethwptr(sc, ch->dir);
	rtn = ptr * ch->unit;
	snd_mtxunlock(sc->lock);

#if(0)
	device_printf(sc->dev, "envy24htchan_getptr(): return %d\n",
	    rtn);
#endif
	return rtn;
}

static struct pcmchan_caps *
envy24htchan_getcaps(kobj_t obj, void *data)
{
	struct sc_chinfo *ch = data;
	struct sc_info *sc = ch->parent;
	struct pcmchan_caps *rtn;

#if(0)
	device_printf(sc->dev, "envy24htchan_getcaps()\n");
#endif
	snd_mtxlock(sc->lock);
	if (ch->dir == PCMDIR_PLAY) {
		if (sc->run[0] == 0)
			rtn = &envy24ht_playcaps;
		else
			rtn = &sc->caps[0];
	}
	else {
		if (sc->run[1] == 0)
			rtn = &envy24ht_reccaps;
		else
			rtn = &sc->caps[1];
	}
	snd_mtxunlock(sc->lock);

	return rtn;
}

static kobj_method_t envy24htchan_methods[] = {
	KOBJMETHOD(channel_init,		envy24htchan_init),
	KOBJMETHOD(channel_free,		envy24htchan_free),
	KOBJMETHOD(channel_setformat,		envy24htchan_setformat),
	KOBJMETHOD(channel_setspeed,		envy24htchan_setspeed),
	KOBJMETHOD(channel_setblocksize,	envy24htchan_setblocksize),
	KOBJMETHOD(channel_trigger,		envy24htchan_trigger),
	KOBJMETHOD(channel_getptr,		envy24htchan_getptr),
	KOBJMETHOD(channel_getcaps,		envy24htchan_getcaps),
	KOBJMETHOD_END
};
CHANNEL_DECLARE(envy24htchan);

/* -------------------------------------------------------------------- */

/* mixer interface */

static int
envy24htmixer_init(struct snd_mixer *m)
{
	struct sc_info *sc = mix_getdevinfo(m);

#if(0)
	device_printf(sc->dev, "envy24htmixer_init()\n");
#endif
	if (sc == NULL)
		return -1;

	/* set volume control rate */
	snd_mtxlock(sc->lock);
#if 0
	envy24ht_wrmt(sc, ENVY24HT_MT_VOLRATE, 0x30, 1); /* 0x30 is default value */
#endif

	pcm_setflags(sc->dev, pcm_getflags(sc->dev) | SD_F_SOFTPCMVOL);

	mix_setdevs(m, ENVY24HT_MIX_MASK);
	mix_setrecdevs(m, ENVY24HT_MIX_REC_MASK);
	
	snd_mtxunlock(sc->lock);

	return 0;
}

static int
envy24htmixer_reinit(struct snd_mixer *m)
{
	struct sc_info *sc = mix_getdevinfo(m);

	if (sc == NULL)
		return -1;
#if(0)
	device_printf(sc->dev, "envy24htmixer_reinit()\n");
#endif

	return 0;
}

static int
envy24htmixer_uninit(struct snd_mixer *m)
{
	struct sc_info *sc = mix_getdevinfo(m);

	if (sc == NULL)
		return -1;
#if(0)
	device_printf(sc->dev, "envy24htmixer_uninit()\n");
#endif

	return 0;
}

static int
envy24htmixer_set(struct snd_mixer *m, unsigned dev, unsigned left, unsigned right)
{
	struct sc_info *sc = mix_getdevinfo(m);
	int ch = envy24ht_mixmap[dev];
	int hwch;
	int i;

	if (sc == NULL)
		return -1;
	if (dev == 0 && sc->cfg->codec->setvolume == NULL)
		return -1;
	if (dev != 0 && ch == -1)
		return -1;
	hwch = envy24ht_chanmap[ch];
#if(0)
	device_printf(sc->dev, "envy24htmixer_set(m, %d, %d, %d)\n",
	    dev, left, right);
#endif

	snd_mtxlock(sc->lock);
	if (dev == 0) {
		for (i = 0; i < sc->dacn; i++) {
			sc->cfg->codec->setvolume(sc->dac[i], PCMDIR_PLAY, left, right);
		}
	}
	else {
		/* set volume value for hardware */
		if ((sc->left[hwch] = 100 - left) > ENVY24HT_VOL_MIN)
			sc->left[hwch] = ENVY24HT_VOL_MUTE;
		if ((sc->right[hwch] = 100 - right) > ENVY24HT_VOL_MIN)
			sc->right[hwch] = ENVY24HT_VOL_MUTE;

		/* set volume for record channel and running play channel */
		if (hwch > ENVY24HT_CHAN_PLAY_SPDIF || sc->chan[ch].run)
			envy24ht_setvolume(sc, hwch);
	}
	snd_mtxunlock(sc->lock);

	return right << 8 | left;
}

static u_int32_t
envy24htmixer_setrecsrc(struct snd_mixer *m, u_int32_t src)
{
	struct sc_info *sc = mix_getdevinfo(m);
	int ch = envy24ht_mixmap[src];
#if(0)
	device_printf(sc->dev, "envy24htmixer_setrecsrc(m, %d)\n", src);
#endif

	if (ch > ENVY24HT_CHAN_PLAY_SPDIF)
		sc->src = ch;
	return src;
}

static kobj_method_t envy24htmixer_methods[] = {
	KOBJMETHOD(mixer_init,		envy24htmixer_init),
	KOBJMETHOD(mixer_reinit,	envy24htmixer_reinit),
	KOBJMETHOD(mixer_uninit,	envy24htmixer_uninit),
	KOBJMETHOD(mixer_set,		envy24htmixer_set),
	KOBJMETHOD(mixer_setrecsrc,	envy24htmixer_setrecsrc),
	KOBJMETHOD_END
};
MIXER_DECLARE(envy24htmixer);

/* -------------------------------------------------------------------- */

/* The interrupt handler */
static void
envy24ht_intr(void *p)
{
	struct sc_info *sc = (struct sc_info *)p;
	struct sc_chinfo *ch;
	u_int32_t ptr, dsize, feed;
	int i;

#if(0)
	device_printf(sc->dev, "envy24ht_intr()\n");
#endif
	snd_mtxlock(sc->lock);
	if (envy24ht_checkintr(sc, PCMDIR_PLAY)) {
#if(0)
		device_printf(sc->dev, "envy24ht_intr(): play\n");
#endif
		dsize = sc->psize / 4;
		ptr = dsize - envy24ht_rdmt(sc, ENVY24HT_MT_PCNT, 2) - 1;
#if(0)
		device_printf(sc->dev, "envy24ht_intr(): ptr = %d-->", ptr);
#endif
		ptr -= ptr % sc->blk[0];
		feed = (ptr + dsize - sc->intr[0]) % dsize; 
#if(0)
		printf("%d intr = %d feed = %d\n", ptr, sc->intr[0], feed);
#endif
		for (i = ENVY24HT_CHAN_PLAY_DAC1; i <= ENVY24HT_CHAN_PLAY_SPDIF; i++) {
			ch = &sc->chan[i];
#if(0)
			if (ch->run)
				device_printf(sc->dev, "envy24ht_intr(): chan[%d].blk = %d\n", i, ch->blk);
#endif
			if (ch->run && ch->blk <= feed) {
				snd_mtxunlock(sc->lock);
				chn_intr(ch->channel);
				snd_mtxlock(sc->lock);
			}
		}
		sc->intr[0] = ptr;
		envy24ht_updintr(sc, PCMDIR_PLAY);
	}
	if (envy24ht_checkintr(sc, PCMDIR_REC)) {
#if(0)
		device_printf(sc->dev, "envy24ht_intr(): rec\n");
#endif
		dsize = sc->rsize / 4;
		ptr = dsize - envy24ht_rdmt(sc, ENVY24HT_MT_RCNT, 2) - 1;
		ptr -= ptr % sc->blk[1];
		feed = (ptr + dsize - sc->intr[1]) % dsize; 
		for (i = ENVY24HT_CHAN_REC_ADC1; i <= ENVY24HT_CHAN_REC_SPDIF; i++) {
			ch = &sc->chan[i];
			if (ch->run && ch->blk <= feed) {
				snd_mtxunlock(sc->lock);
				chn_intr(ch->channel);
				snd_mtxlock(sc->lock);
			}
		}
		sc->intr[1] = ptr;
		envy24ht_updintr(sc, PCMDIR_REC);
	}
	snd_mtxunlock(sc->lock);

	return;
}

/*
 * Probe and attach the card
 */

static int
envy24ht_pci_probe(device_t dev)
{
	u_int16_t sv, sd;
	int i;

#if(0)
	printf("envy24ht_pci_probe()\n");
#endif
	if (pci_get_device(dev) == PCID_ENVY24HT &&
	    pci_get_vendor(dev) == PCIV_ENVY24) {
		sv = pci_get_subvendor(dev);
		sd = pci_get_subdevice(dev);
		for (i = 0; cfg_table[i].subvendor != 0 || cfg_table[i].subdevice != 0; i++) {
			if (cfg_table[i].subvendor == sv &&
			    cfg_table[i].subdevice == sd) {
				break;
			}
		}
		device_set_desc(dev, cfg_table[i].name);
#if(0)
		printf("envy24ht_pci_probe(): return 0\n");
#endif
		return 0;
	}
	else {
#if(0)
		printf("envy24ht_pci_probe(): return ENXIO\n");
#endif
		return ENXIO;
	}
}

static void
envy24ht_dmapsetmap(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	struct sc_info *sc = arg;

	sc->paddr = segs->ds_addr;
#if(0)
	device_printf(sc->dev, "envy24ht_dmapsetmap()\n");
	if (bootverbose) {
		printf("envy24ht(play): setmap %lx, %lx; ",
		    (unsigned long)segs->ds_addr,
		    (unsigned long)segs->ds_len);
	}
#endif
	envy24ht_wrmt(sc, ENVY24HT_MT_PADDR, (uint32_t)segs->ds_addr, 4);
	envy24ht_wrmt(sc, ENVY24HT_MT_PCNT, (uint32_t)(segs->ds_len / 4 - 1), 2);
}

static void
envy24ht_dmarsetmap(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	struct sc_info *sc = arg;

	sc->raddr = segs->ds_addr;
#if(0)
	device_printf(sc->dev, "envy24ht_dmarsetmap()\n");
	if (bootverbose) {
		printf("envy24ht(record): setmap %lx, %lx; ",
		    (unsigned long)segs->ds_addr,
		    (unsigned long)segs->ds_len);
	}
#endif
	envy24ht_wrmt(sc, ENVY24HT_MT_RADDR, (uint32_t)segs->ds_addr, 4);
	envy24ht_wrmt(sc, ENVY24HT_MT_RCNT, (uint32_t)(segs->ds_len / 4 - 1), 2);
}

static void
envy24ht_dmafree(struct sc_info *sc)
{
#if(0)
	device_printf(sc->dev, "envy24ht_dmafree():");
	printf(" sc->raddr(0x%08x)", (u_int32_t)sc->raddr);
	printf(" sc->paddr(0x%08x)", (u_int32_t)sc->paddr);
	if (sc->rbuf) printf(" sc->rbuf(0x%08x)", (u_int32_t)sc->rbuf);
	else printf(" sc->rbuf(null)");
	if (sc->pbuf) printf(" sc->pbuf(0x%08x)\n", (u_int32_t)sc->pbuf);
	else printf(" sc->pbuf(null)\n");
#endif
#if(0)
	if (sc->raddr)
		bus_dmamap_unload(sc->dmat, sc->rmap);
	if (sc->paddr)
		bus_dmamap_unload(sc->dmat, sc->pmap);
	if (sc->rbuf)
		bus_dmamem_free(sc->dmat, sc->rbuf, sc->rmap);
	if (sc->pbuf)
		bus_dmamem_free(sc->dmat, sc->pbuf, sc->pmap);
#else
	bus_dmamap_unload(sc->dmat, sc->rmap);
	bus_dmamap_unload(sc->dmat, sc->pmap);
	bus_dmamem_free(sc->dmat, sc->rbuf, sc->rmap);
	bus_dmamem_free(sc->dmat, sc->pbuf, sc->pmap);
#endif

	sc->raddr = sc->paddr = 0;
	sc->pbuf = NULL;
	sc->rbuf = NULL;

	return;
}

static int
envy24ht_dmainit(struct sc_info *sc)
{

#if(0)
	device_printf(sc->dev, "envy24ht_dmainit()\n");
#endif
	/* init values */
	sc->psize = ENVY24HT_PLAY_BUFUNIT * ENVY24HT_SAMPLE_NUM;
	sc->rsize = ENVY24HT_REC_BUFUNIT * ENVY24HT_SAMPLE_NUM;
	sc->pbuf = NULL;
	sc->rbuf = NULL;
	sc->paddr = sc->raddr = 0;
	sc->blk[0] = sc->blk[1] = 0;

	/* allocate DMA buffer */
#if(0)
	device_printf(sc->dev, "envy24ht_dmainit(): bus_dmamem_alloc(): sc->pbuf\n");
#endif
	if (bus_dmamem_alloc(sc->dmat, (void **)&sc->pbuf, BUS_DMA_NOWAIT, &sc->pmap))
		goto bad;
#if(0)
	device_printf(sc->dev, "envy24ht_dmainit(): bus_dmamem_alloc(): sc->rbuf\n");
#endif
	if (bus_dmamem_alloc(sc->dmat, (void **)&sc->rbuf, BUS_DMA_NOWAIT, &sc->rmap))
		goto bad;
#if(0)
	device_printf(sc->dev, "envy24ht_dmainit(): bus_dmamem_load(): sc->pmap\n");
#endif
	if (bus_dmamap_load(sc->dmat, sc->pmap, sc->pbuf, sc->psize, envy24ht_dmapsetmap, sc, BUS_DMA_NOWAIT))
		goto bad;
#if(0)
	device_printf(sc->dev, "envy24ht_dmainit(): bus_dmamem_load(): sc->rmap\n");
#endif
	if (bus_dmamap_load(sc->dmat, sc->rmap, sc->rbuf, sc->rsize, envy24ht_dmarsetmap, sc, BUS_DMA_NOWAIT))
		goto bad;
	bzero(sc->pbuf, sc->psize);
	bzero(sc->rbuf, sc->rsize);

	return 0;
 bad:
	envy24ht_dmafree(sc);
	return ENOSPC;
}

static void
envy24ht_putcfg(struct sc_info *sc)
{
	device_printf(sc->dev, "system configuration\n");
	printf("  SubVendorID: 0x%04x, SubDeviceID: 0x%04x\n",
	    sc->cfg->subvendor, sc->cfg->subdevice);
	printf("  XIN2 Clock Source: ");
	switch (sc->cfg->scfg & ENVY24HT_CCSM_SCFG_XIN2) {
	case 0x00:
		printf("24.576MHz(96kHz*256)\n");
		break;
	case 0x40:
		printf("49.152MHz(192kHz*256)\n");
		break;
	case 0x80:
		printf("reserved\n");
		break;
	default:
		printf("illegal system setting\n");
	}
	printf("  MPU-401 UART(s) #: ");
	if (sc->cfg->scfg & ENVY24HT_CCSM_SCFG_MPU)
		printf("1\n");
	else
		printf("not implemented\n");
        switch (sc->adcn) {
        case 0x01:
	case 0x02:
                printf("  ADC #: ");
                printf("%d\n", sc->adcn);
                break;
        case 0x03:
                printf("  ADC #: ");
                printf("%d", 1);
                printf(" and SPDIF receiver connected\n");
                break;
        default:
                printf("  no physical inputs\n");
        }
	printf("  DAC #: ");
	printf("%d\n", sc->dacn);
	printf("  Multi-track converter type: ");
	if ((sc->cfg->acl & ENVY24HT_CCSM_ACL_MTC) == 0) {
		printf("AC'97(SDATA_OUT:");
		if (sc->cfg->acl & ENVY24HT_CCSM_ACL_OMODE)
			printf("packed");
		else
			printf("split");
		printf(")\n");
	}
	else {
		printf("I2S(");
		if (sc->cfg->i2s & ENVY24HT_CCSM_I2S_VOL)
			printf("with volume, ");
                if (sc->cfg->i2s & ENVY24HT_CCSM_I2S_192KHZ)
                        printf("192KHz support, ");
                else
                if (sc->cfg->i2s & ENVY24HT_CCSM_I2S_96KHZ)
                        printf("192KHz support, ");
                else
                        printf("48KHz support, ");
		switch (sc->cfg->i2s & ENVY24HT_CCSM_I2S_RES) {
		case ENVY24HT_CCSM_I2S_16BIT:
			printf("16bit resolution, ");
			break;
		case ENVY24HT_CCSM_I2S_18BIT:
			printf("18bit resolution, ");
			break;
		case ENVY24HT_CCSM_I2S_20BIT:
			printf("20bit resolution, ");
			break;
		case ENVY24HT_CCSM_I2S_24BIT:
			printf("24bit resolution, ");
			break;
		}
		printf("ID#0x%x)\n", sc->cfg->i2s & ENVY24HT_CCSM_I2S_ID);
	}
	printf("  S/PDIF(IN/OUT): ");
	if (sc->cfg->spdif & ENVY24HT_CCSM_SPDIF_IN)
		printf("1/");
	else
		printf("0/");
	if (sc->cfg->spdif & ENVY24HT_CCSM_SPDIF_OUT)
		printf("1 ");
	else
		printf("0 ");
	if (sc->cfg->spdif & (ENVY24HT_CCSM_SPDIF_IN | ENVY24HT_CCSM_SPDIF_OUT))
		printf("ID# 0x%02x\n", (sc->cfg->spdif & ENVY24HT_CCSM_SPDIF_ID) >> 2);
	printf("  GPIO(mask/dir/state): 0x%02x/0x%02x/0x%02x\n",
	    sc->cfg->gpiomask, sc->cfg->gpiodir, sc->cfg->gpiostate);
}

static int
envy24ht_init(struct sc_info *sc)
{
	u_int32_t data;
#if(0)
	int rtn;
#endif
	int i;
	u_int32_t sv, sd;


#if(0)
	device_printf(sc->dev, "envy24ht_init()\n");
#endif

	/* reset chip */
#if 0
	envy24ht_wrcs(sc, ENVY24HT_CCS_CTL, ENVY24HT_CCS_CTL_RESET, 1);
	DELAY(200);
	envy24ht_wrcs(sc, ENVY24HT_CCS_CTL, ENVY24HT_CCS_CTL_NATIVE, 1);
	DELAY(200);

	/* legacy hardware disable */
	data = pci_read_config(sc->dev, PCIR_LAC, 2);
	data |= PCIM_LAC_DISABLE;
	pci_write_config(sc->dev, PCIR_LAC, data, 2);
#endif

	/* check system configuration */
	sc->cfg = NULL;
	for (i = 0; cfg_table[i].subvendor != 0 || cfg_table[i].subdevice != 0; i++) {
		/* 1st: search configuration from table */
		sv = pci_get_subvendor(sc->dev);
		sd = pci_get_subdevice(sc->dev);
		if (sv == cfg_table[i].subvendor && sd == cfg_table[i].subdevice) {
#if(0)
			device_printf(sc->dev, "Set configuration from table\n");
#endif
			sc->cfg = &cfg_table[i];
			break;
		}
	}
	if (sc->cfg == NULL) {
		/* 2nd: read configuration from table */
		sc->cfg = envy24ht_rom2cfg(sc);
	}
	sc->adcn = ((sc->cfg->scfg & ENVY24HT_CCSM_SCFG_ADC) >> 2) + 1; /* need to be fixed */
	sc->dacn = (sc->cfg->scfg & ENVY24HT_CCSM_SCFG_DAC) + 1;

	if (1 /* bootverbose */) {
		envy24ht_putcfg(sc);
	}

	/* set system configuration */
	envy24ht_wrcs(sc, ENVY24HT_CCS_SCFG, sc->cfg->scfg, 1);
	envy24ht_wrcs(sc, ENVY24HT_CCS_ACL, sc->cfg->acl, 1);
	envy24ht_wrcs(sc, ENVY24HT_CCS_I2S, sc->cfg->i2s, 1);
	envy24ht_wrcs(sc, ENVY24HT_CCS_SPDIF, sc->cfg->spdif, 1);
	envy24ht_gpiosetmask(sc, sc->cfg->gpiomask);
	envy24ht_gpiosetdir(sc, sc->cfg->gpiodir);
	envy24ht_gpiowr(sc, sc->cfg->gpiostate);

	if ((sc->cfg->subvendor == 0x3031) && (sc->cfg->subdevice == 0x4553)) {
		envy24ht_wri2c(sc, 0x22, 0x00, 0x07);
		envy24ht_wri2c(sc, 0x22, 0x04, 0x5f | 0x80);
		envy24ht_wri2c(sc, 0x22, 0x05, 0x5f | 0x80);
	}
	
	for (i = 0; i < sc->adcn; i++) {
		sc->adc[i] = sc->cfg->codec->create(sc->dev, sc, PCMDIR_REC, i);
		sc->cfg->codec->init(sc->adc[i]);
	}
	for (i = 0; i < sc->dacn; i++) {
		sc->dac[i] = sc->cfg->codec->create(sc->dev, sc, PCMDIR_PLAY, i);
		sc->cfg->codec->init(sc->dac[i]);
	}

	/* initialize DMA buffer */
#if(0)
	device_printf(sc->dev, "envy24ht_init(): initialize DMA buffer\n");
#endif
	if (envy24ht_dmainit(sc))
		return ENOSPC;

	/* initialize status */
	sc->run[0] = sc->run[1] = 0;
	sc->intr[0] = sc->intr[1] = 0;
	sc->speed = 0;
	sc->caps[0].fmtlist = envy24ht_playfmt;
	sc->caps[1].fmtlist = envy24ht_recfmt;

	/* set channel router */
#if 0
	envy24ht_route(sc, ENVY24HT_ROUTE_DAC_1, ENVY24HT_ROUTE_CLASS_MIX, 0, 0);
	envy24ht_route(sc, ENVY24HT_ROUTE_DAC_SPDIF, ENVY24HT_ROUTE_CLASS_DMA, 0, 0);
	envy24ht_route(sc, ENVY24HT_ROUTE_DAC_SPDIF, ENVY24HT_ROUTE_CLASS_MIX, 0, 0);
#endif

	/* set macro interrupt mask */
	data = envy24ht_rdcs(sc, ENVY24HT_CCS_IMASK, 1);
	envy24ht_wrcs(sc, ENVY24HT_CCS_IMASK, data & ~ENVY24HT_CCS_IMASK_PMT, 1);
	data = envy24ht_rdcs(sc, ENVY24HT_CCS_IMASK, 1);
#if(0)
	device_printf(sc->dev, "envy24ht_init(): CCS_IMASK-->0x%02x\n", data);
#endif

	return 0;
}

static int
envy24ht_alloc_resource(struct sc_info *sc)
{
	/* allocate I/O port resource */
	sc->csid = PCIR_CCS;
	sc->cs = bus_alloc_resource_any(sc->dev, SYS_RES_IOPORT,
	    &sc->csid, RF_ACTIVE);
	sc->mtid = ENVY24HT_PCIR_MT;
	sc->mt = bus_alloc_resource_any(sc->dev, SYS_RES_IOPORT,
	    &sc->mtid, RF_ACTIVE);
	if (!sc->cs || !sc->mt) {
		device_printf(sc->dev, "unable to map IO port space\n");
		return ENXIO;
	}
	sc->cst = rman_get_bustag(sc->cs);
	sc->csh = rman_get_bushandle(sc->cs);
	sc->mtt = rman_get_bustag(sc->mt);
	sc->mth = rman_get_bushandle(sc->mt);
#if(0)
	device_printf(sc->dev,
	    "IO port register values\nCCS: 0x%lx\nMT: 0x%lx\n",
	    pci_read_config(sc->dev, PCIR_CCS, 4),
	    pci_read_config(sc->dev, PCIR_MT, 4));
#endif

	/* allocate interrupt resource */
	sc->irqid = 0;
	sc->irq = bus_alloc_resource_any(sc->dev, SYS_RES_IRQ, &sc->irqid,
				 RF_ACTIVE | RF_SHAREABLE);
	if (!sc->irq ||
	    snd_setup_intr(sc->dev, sc->irq, INTR_MPSAFE, envy24ht_intr, sc, &sc->ih)) {
		device_printf(sc->dev, "unable to map interrupt\n");
		return ENXIO;
	}

	/* allocate DMA resource */
	if (bus_dma_tag_create(/*parent*/bus_get_dma_tag(sc->dev),
	    /*alignment*/4,
	    /*boundary*/0,
	    /*lowaddr*/BUS_SPACE_MAXADDR_32BIT,
	    /*highaddr*/BUS_SPACE_MAXADDR,
	    /*filter*/NULL, /*filterarg*/NULL,
	    /*maxsize*/BUS_SPACE_MAXSIZE_ENVY24,
	    /*nsegments*/1, /*maxsegsz*/0x3ffff,
	    /*flags*/0, /*lockfunc*/NULL,
	    /*lockarg*/NULL, &sc->dmat) != 0) {
		device_printf(sc->dev, "unable to create dma tag\n");
		return ENXIO;
	}

	return 0;
}

static int
envy24ht_pci_attach(device_t dev)
{
	struct sc_info 		*sc;
	char 			status[SND_STATUSLEN];
	int			err = 0;
	int			i;

#if(0)
	device_printf(dev, "envy24ht_pci_attach()\n");
#endif
	/* get sc_info data area */
	if ((sc = malloc(sizeof(*sc), M_ENVY24HT, M_NOWAIT)) == NULL) {
		device_printf(dev, "cannot allocate softc\n");
		return ENXIO;
	}

	bzero(sc, sizeof(*sc));
	sc->lock = snd_mtxcreate(device_get_nameunit(dev),
	    "snd_envy24ht softc");
	sc->dev = dev;

	/* initialize PCI interface */
	pci_enable_busmaster(dev);

	/* allocate resources */
	err = envy24ht_alloc_resource(sc);
	if (err) {
		device_printf(dev, "unable to allocate system resources\n");
		goto bad;
	}

	/* initialize card */
	err = envy24ht_init(sc);
	if (err) {
		device_printf(dev, "unable to initialize the card\n");
		goto bad;
	}

	/* set multi track mixer */
	mixer_init(dev, &envy24htmixer_class, sc);

	/* set channel information */
	/* err = pcm_register(dev, sc, 5, 2 + sc->adcn); */
	err = pcm_register(dev, sc, 1, 2 + sc->adcn);
	if (err)
		goto bad;
	sc->chnum = 0;
	/* for (i = 0; i < 5; i++) { */
		pcm_addchan(dev, PCMDIR_PLAY, &envy24htchan_class, sc);
		sc->chnum++;
	/* } */
	for (i = 0; i < 2 + sc->adcn; i++) {
		pcm_addchan(dev, PCMDIR_REC, &envy24htchan_class, sc);
		sc->chnum++;
	}

	/* set status iformation */
	snprintf(status, SND_STATUSLEN,
	    "at io 0x%jx:%jd,0x%jx:%jd irq %jd",
	    rman_get_start(sc->cs),
	    rman_get_end(sc->cs) - rman_get_start(sc->cs) + 1,
	    rman_get_start(sc->mt),
	    rman_get_end(sc->mt) - rman_get_start(sc->mt) + 1,
	    rman_get_start(sc->irq));
	pcm_setstatus(dev, status);

	return 0;

bad:
	if (sc->ih)
		bus_teardown_intr(dev, sc->irq, sc->ih);
	if (sc->irq)
		bus_release_resource(dev, SYS_RES_IRQ, sc->irqid, sc->irq);
	envy24ht_dmafree(sc);
	if (sc->dmat)
		bus_dma_tag_destroy(sc->dmat);
        if (sc->cfg->codec->destroy != NULL) {
                for (i = 0; i < sc->adcn; i++)
                        sc->cfg->codec->destroy(sc->adc[i]);
                for (i = 0; i < sc->dacn; i++)
                        sc->cfg->codec->destroy(sc->dac[i]);
        }
	envy24ht_cfgfree(sc->cfg);
	if (sc->cs)
		bus_release_resource(dev, SYS_RES_IOPORT, sc->csid, sc->cs);
	if (sc->mt)
		bus_release_resource(dev, SYS_RES_IOPORT, sc->mtid, sc->mt);
	if (sc->lock)
		snd_mtxfree(sc->lock);
	free(sc, M_ENVY24HT);
	return err;
}

static int
envy24ht_pci_detach(device_t dev)
{
	struct sc_info *sc;
	int r;
	int i;

#if(0)
	device_printf(dev, "envy24ht_pci_detach()\n");
#endif
	sc = pcm_getdevinfo(dev);
	if (sc == NULL)
		return 0;
	r = pcm_unregister(dev);
	if (r)
		return r;

	envy24ht_dmafree(sc);
	if (sc->cfg->codec->destroy != NULL) {
		for (i = 0; i < sc->adcn; i++)
			sc->cfg->codec->destroy(sc->adc[i]);
		for (i = 0; i < sc->dacn; i++)
			sc->cfg->codec->destroy(sc->dac[i]);
	}
	envy24ht_cfgfree(sc->cfg);
	bus_dma_tag_destroy(sc->dmat);
	bus_teardown_intr(dev, sc->irq, sc->ih);
	bus_release_resource(dev, SYS_RES_IRQ, sc->irqid, sc->irq);
	bus_release_resource(dev, SYS_RES_IOPORT, sc->csid, sc->cs);
	bus_release_resource(dev, SYS_RES_IOPORT, sc->mtid, sc->mt);
	snd_mtxfree(sc->lock);
	free(sc, M_ENVY24HT);
	return 0;
}

static device_method_t envy24ht_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		envy24ht_pci_probe),
	DEVMETHOD(device_attach,	envy24ht_pci_attach),
	DEVMETHOD(device_detach,	envy24ht_pci_detach),
	{ 0, 0 }
};

static driver_t envy24ht_driver = {
	"pcm",
	envy24ht_methods,
	PCM_SOFTC_SIZE,
};

DRIVER_MODULE(snd_envy24ht, pci, envy24ht_driver, pcm_devclass, 0, 0);
MODULE_DEPEND(snd_envy24ht, sound, SOUND_MINVER, SOUND_PREFVER, SOUND_MAXVER);
MODULE_DEPEND(snd_envy24ht, snd_spicds, 1, 1, 1);
MODULE_VERSION(snd_envy24ht, 1);
